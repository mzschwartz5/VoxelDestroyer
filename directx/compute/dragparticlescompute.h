#pragma once
#include "computeshader.h"
#include "glm/glm.hpp"
#include "../custommayaconstructs/voxeldragcontext.h"
#include "../custommayaconstructs/voxelrendereroverride.h"

// NOTE: currently, the ConstantBuffer is perfectly 16-byte aligned. Adding any values to these structs
// or even changing their order can break this shader. Any extra data must fit into the next 16-byte chunk.
struct DragValues
{
    MousePosition lastMousePosition;
    MousePosition currentMousePosition;
    float selectRadius{ 0.0f };
};

struct ConstantBuffer{
    // Difference in world space between the last and current mouse positions
    // for a hypothetical unit depth.
    glm::vec3 dragWorldDiff{ 0.0f, 0.0f, 0.0f }; 
    int lastX{ 0 };
    int lastY{ 0 };
    float dragRadius{ 0.0f };
    float viewportWidth{ 0.0f };
    float viewportHeight{ 0.0f };
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
};

class DragParticlesCompute : public ComputeShader
{
public:
    DragParticlesCompute(
        const ComPtr<ID3D11UnorderedAccessView>& particlesUAV,
        int numSubsteps
    ) : ComputeShader(IDR_SHADER7), particlesUAV(particlesUAV), numSubsteps(numSubsteps)
    {
        initializeBuffers();

        unsubscribeFromDragStateChange = VoxelDragContext::subscribeToDragStateChange([this](const DragState& dragState) {
            onDragStateChange(dragState);
        });

        unsubscribeFromMousePositionChange = VoxelDragContext::subscribeToMousePositionChange([this](const MousePosition& mousePosition) {
            onMousePositionChanged(mousePosition);
        });

        unsubscribeFromDepthTargetChange = VoxelRendererOverride::subscribeToDepthTargetChange([this](void* depthResourceHandle) {
            onDepthTargetChange(depthResourceHandle);
        });
        
        unsubscribeFromCameraMatricesChange = VoxelRendererOverride::subscribeToCameraInfoChange([this](const CameraMatrices& cameraMatrices) {
            onCameraMatricesChange(cameraMatrices);
        });
    };

    void onDepthTargetChange(void* depthResourceHandle)
    {
        ID3D11DepthStencilView* depthStencilView = static_cast<ID3D11DepthStencilView*>(depthResourceHandle);

        // Get the underlying resource from the depth stencil view, so we can create a shader resource view for it.
        ComPtr<ID3D11Resource> resource;
        depthStencilView->GetResource(&resource);
        if (!resource) {
            MGlobal::displayError("Failed to get resource from depth stencil view.");
            return;
        }

        // Create a Shader Resource View for the depth buffer
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        HRESULT hr = DirectX::getDevice()->CreateShaderResourceView(resource.Get(), &srvDesc, depthSRV.GetAddressOf());
        if (FAILED(hr)) {
            MGlobal::displayError(MString("Failed to create shader resource view for depth buffer.") + std::to_string(hr).c_str());
            return;
        }
    };

    void onDragStateChange(const DragState& dragState) {
        if (dragState.isDragging) {
            dragValues.currentMousePosition = dragState.mousePosition;
            dragValues.lastMousePosition = dragState.mousePosition;
            dragValues.selectRadius = dragState.selectRadius;
            copyConstantBufferToGPU();
        } else {
            clearUintBuffer(isDraggingUAV);
        }
    }

    void onMousePositionChanged(const MousePosition& mousePosition)
    {
        this->dragValues.lastMousePosition = this->dragValues.currentMousePosition;
        this->dragValues.currentMousePosition = mousePosition;
    }

    void onCameraMatricesChange(const CameraMatrices& cameraMatrices)
    {
        this->cameraMatrices = cameraMatrices;
    }

    const ComPtr<ID3D11UnorderedAccessView>& getIsDraggingUAV() const
    {
        return isDraggingUAV;
    }

    void dispatch() override
    {
        // May happen if the override render has not yet been setup
        if (!depthSRV) {
            return;
        }

        // *Could* do this in onMousePositionChanged, but no reason to copy the buffer on
        // every mouse event, which can fire more frequently than dispatches. Cheaper to do it on dispatch as needed.
        if (dragValues.currentMousePosition.x != dragValues.lastMousePosition.x ||
            dragValues.currentMousePosition.y != dragValues.lastMousePosition.y) {
            copyConstantBufferToGPU();
        }
        
        ComputeShader::dispatch(numWorkgroups);
    };

private:
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    ComPtr<ID3D11ShaderResourceView> depthSRV;
    ComPtr<ID3D11UnorderedAccessView> isDraggingUAV;
    ComPtr<ID3D11Buffer> constantBuffer;
    ComPtr<ID3D11Buffer> isDraggingBuffer;
    CameraMatrices cameraMatrices;
    DragValues dragValues;
    int numWorkgroups;
    float numSubsteps;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavQueryDesc;
    EventBase::Unsubscribe unsubscribeFromDragStateChange;
    EventBase::Unsubscribe unsubscribeFromMousePositionChange;
    EventBase::Unsubscribe unsubscribeFromDepthTargetChange;
    EventBase::Unsubscribe unsubscribeFromCameraMatricesChange;

    void copyConstantBufferToGPU()
    {
        ConstantBuffer cb{
            calculateDragWorldDiff(),
            dragValues.lastMousePosition.x,
            dragValues.lastMousePosition.y,
            dragValues.selectRadius,
            cameraMatrices.viewportWidth,
            cameraMatrices.viewportHeight,
            cameraMatrices.viewMatrix,
            cameraMatrices.projMatrix,
        };

        updateConstantBuffer(constantBuffer, cb);
    }

    // Reverse-project the mouse start and end points to world space at a unit depth.
    // Get the difference and amortize it across the number of substeps in one simulation step.
    glm::vec3 calculateDragWorldDiff() {
        glm::vec2 mouseStartNDC = glm::vec2((dragValues.lastMousePosition.x / cameraMatrices.viewportWidth) * 2.0f - 1.0f,
                                            (dragValues.lastMousePosition.y / cameraMatrices.viewportHeight) * 2.0f - 1.0f);
        glm::vec4 mouseStartWorld =  glm::vec4(mouseStartNDC, 1.0f, 1.0f) * cameraMatrices.invViewProjMatrix;

        glm::vec2 mouseEndNDC = glm::vec2((dragValues.currentMousePosition.x / cameraMatrices.viewportWidth) * 2.0f - 1.0f,
                                          (dragValues.currentMousePosition.y / cameraMatrices.viewportHeight) * 2.0f - 1.0f);
        glm::vec4 mouseEndWorld = glm::vec4(mouseEndNDC, 1.0f, 1.0f) * cameraMatrices.invViewProjMatrix;

        return (mouseEndWorld - mouseStartWorld) / numSubsteps;
    }

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get(), isDraggingUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11ShaderResourceView* srvs[] = { depthSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs); 

        ID3D11Buffer* cbvs[] = { constantBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr); 

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);


        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers()
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        particlesUAV->GetDesc(&uavQueryDesc);
        int numVoxels = uavQueryDesc.Buffer.NumElements / 8;
        numWorkgroups = Utils::divideRoundUp(numVoxels, VGS_THREADS);

        // Create CBV for the drag values (mouse position, drag distance, grab radius)
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = sizeof(ConstantBuffer);
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = 0;
    
        CreateBuffer(&bufferDesc, nullptr, &constantBuffer);

        // Create isDragging buffer and its SRV/UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(UINT) * numVoxels;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(UINT);

        CreateBuffer(&bufferDesc, nullptr, &isDraggingBuffer);

        // Create the UAV for the isDragging buffer
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numVoxels;

        DirectX::getDevice()->CreateUnorderedAccessView(isDraggingBuffer.Get(), &uavDesc, &isDraggingUAV);
        clearUintBuffer(isDraggingUAV);
    };

    void tearDown() override
    {
        ComputeShader::tearDown();
        if (depthSRV) {
            depthSRV->Release();
            depthSRV = nullptr;
        }

        constantBuffer.Reset();

        isDraggingBuffer.Reset();
        isDraggingUAV.Reset();

        unsubscribeFromDragStateChange();
        unsubscribeFromMousePositionChange();
        unsubscribeFromDepthTargetChange();
        unsubscribeFromCameraMatricesChange();
    };
    
};