#pragma once
#include "computeshader.h"
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MViewport2Renderer.h>
#include "glm/glm.hpp"

// NOTE: currently, the ConstantBuffer is perfectly 16-byte aligned. Adding any values to these structs
// or even changing their order can break this shader. Any extra data must fit into the next 16-byte chunk.
struct DragValues
{   
    int lastX{ 0 };
    int lastY{ 0 };
    int currX{ 0 };
    int currY{ 0 };
    float dragRadius{ 0.0f };
};

struct CameraMatrices
{
    float viewportWidth{ 0.0f };
    float viewportHeight{ 0.0f };
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 invViewProjMatrix;
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
    };

    void updateDepthBuffer(void* depthResourceHandle)
    {
        ID3D11DepthStencilView* newDepthStencilView = static_cast<ID3D11DepthStencilView*>(depthResourceHandle);
        if (newDepthStencilView == depthStencilView) {
            return; 
        }
        depthStencilView = newDepthStencilView;        

        // Get the underlying resource from the depth stencil view, so we can create a shader resource view for it.
        ID3D11Resource* resource = nullptr;
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

        HRESULT hr = DirectX::getDevice()->CreateShaderResourceView(resource, &srvDesc, &depthSRV);
        if (FAILED(hr)) {
            MGlobal::displayError(MString("Failed to create shader resource view for depth buffer.") + std::to_string(hr).c_str());
            return;
        }
    };

    void updateDragValues(const DragValues& dragValues, bool isDragging)
    {
        // Accumulate the drag values (e.g. update current position but not last) 
        // This accounts for mouse events potentially occuring at a higher rate than the simulation.
        this->dragValues.currX = dragValues.currX;
        this->dragValues.currY = dragValues.currY;
        this->dragValues.dragRadius = dragValues.dragRadius;

        // And if we just started dragging, update the last pos as well so we forget the last pos from the previous drag.
        if (!wasDragging && isDragging) {
            this->dragValues.lastX = dragValues.lastX;
            this->dragValues.lastY = dragValues.lastY;
        }

        // If we just stopped dragging, reset the isDraggingBuffer to false for all voxels.
        if (wasDragging && !isDragging) {
            clearUintBuffer(isDraggingUAV);
        }

        wasDragging = isDragging;
        copyConstantBufferToGPU();
    }

    void updateCameraMatrices(const CameraMatrices& cameraMatrices)
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
        
        ComputeShader::dispatch(numWorkgroups);

        // Reset drag values (because mouse drag event isn't called every frame, only when the mouse is moved).
        if (dragValues.currX != dragValues.lastX || dragValues.currY != dragValues.lastY) {
            dragValues.lastX = dragValues.currX;
            dragValues.lastY = dragValues.currY;
        }
    };

private:
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    ComPtr<ID3D11ShaderResourceView> depthSRV;
    ComPtr<ID3D11UnorderedAccessView> isDraggingUAV;
    ComPtr<ID3D11Buffer> constantBuffer;
    ComPtr<ID3D11Buffer> isDraggingBuffer;
    ID3D11DepthStencilView* depthStencilView;
    CameraMatrices cameraMatrices;
    DragValues dragValues;
    int numWorkgroups;
    bool wasDragging = false;
    float numSubsteps;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavQueryDesc;

    void copyConstantBufferToGPU()
    {
        ConstantBuffer cb{
            calculateDragWorldDiff(),
            dragValues.lastX,
            dragValues.lastY,
            dragValues.dragRadius,
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
        glm::vec2 mouseStartNDC = glm::vec2((dragValues.lastX / cameraMatrices.viewportWidth) * 2.0f - 1.0f,
                                            (dragValues.lastY / cameraMatrices.viewportHeight) * 2.0f - 1.0f);
        glm::vec4 mouseStartWorld =  glm::vec4(mouseStartNDC, 1.0f, 1.0f) * cameraMatrices.invViewProjMatrix;

        glm::vec2 mouseEndNDC = glm::vec2((dragValues.currX / cameraMatrices.viewportWidth) * 2.0f - 1.0f,
                                          (dragValues.currY / cameraMatrices.viewportHeight) * 2.0f - 1.0f);
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
    };
    
};