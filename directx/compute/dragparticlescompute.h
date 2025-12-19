#pragma once
#include <array>
#include "computeshader.h"
#include "../../custommayaconstructs/tools/voxeldragcontext.h"
#include "../../custommayaconstructs/draw/voxelrendereroverride.h"
#include <maya/MPoint.h>
#include <maya/MFloatPoint.h>
#include <maya/MFloatVector.h>
struct DragValues
{
    MousePosition lastMousePosition;
    MousePosition currentMousePosition;
    float selectRadius{ 0.0f };
    float selectStrength{ 1.0f };
};

// NOTE: currently, the ConstantBuffer is perfectly 16-byte aligned. Adding any values to these structs (that gets copied to the GPU)
// or even changing their order can break this shader. Any extra data must fit into the next 16-byte chunk.
struct ConstantBuffer{
    // Difference in world space between the last and current mouse positions
    // for a hypothetical unit depth.
    std::array<float, 3> dragWorldDiff{ { 0.0f, 0.0f, 0.0f } };
    int lastX{ 0 };
    int lastY{ 0 };
    float dragRadius{ 0.0f };
    float viewportWidth{ 0.0f };
    float viewportHeight{ 0.0f };
    float viewMatrix[4][4];  // Not using std::array here because MMatrix::get() requires a C-style array
    float projMatrix[4][4];
};

class DragParticlesCompute : public ComputeShader
{
public:
    DragParticlesCompute() = default;

    DragParticlesCompute(
        int numVoxels
    ) : ComputeShader(IDR_SHADER7)
    {
        initializeBuffers(numVoxels);
        initSubscriptions();
    };

    ~DragParticlesCompute() override {
        DirectX::notifyMayaOfMemoryUsage(isDraggingBuffer);
        removeSubscriptions();
    }

    void setParticlesUAV(const ComPtr<ID3D11UnorderedAccessView>& particlesUAV)
    {
        this->particlesUAV = particlesUAV;
    }

    void setNumSubsteps(int numSubsteps)
    {
        this->numSubsteps = numSubsteps;
    }

    void initSubscriptions() {
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
    }

    // This class needs a move assignment operator override because the subscriptions it creates capture the `this` pointer.
    // We need to unsubscribe on move and re-subscribe in the moved-to object to avoid dangling pointers.
    // This is used implicitly (via RVO) in global-solver compute initialization.
    DragParticlesCompute& operator=(DragParticlesCompute&& other) noexcept
    {
        if (this != &other)
        {
            // Move base class
            ComputeShader::operator=(std::move(other));

            // Move all members
            depthSRV = std::move(other.depthSRV);
            isDraggingUAV = std::move(other.isDraggingUAV);
            constantBuffer = std::move(other.constantBuffer);
            isDraggingBuffer = std::move(other.isDraggingBuffer);
            cameraMatrices = std::move(other.cameraMatrices);
            dragValues = std::move(other.dragValues);
            numWorkgroups = other.numWorkgroups;
            
            removeSubscriptions();
            initSubscriptions();
        }
        return *this;
    }

    void onDepthTargetChange(void* depthResourceHandle)
    {
        ID3D11DepthStencilView* depthStencilView = static_cast<ID3D11DepthStencilView*>(depthResourceHandle);

        // Get the underlying resource from the depth stencil view, so we can create a shader resource view for it,
        // if it has changed.
        ComPtr<ID3D11Resource> oldResource;
        ComPtr<ID3D11Resource> resource;
        if (depthSRV) depthSRV->GetResource(&oldResource);
        depthStencilView->GetResource(&resource);
        
        // Safest to check the underlying resource pointer for changes
        if (resource.Get() == oldResource.Get()) return;

        // Create a Shader Resource View for the depth buffer
        // Note: the format was determined by querying the DSV's description, but could be fragile without programmatic  
        // checks + mapping from DSV format to SRV format.
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
            dragValues.selectStrength = dragState.selectStrength;
            copyConstantBufferToGPU();
        } else {
            DirectX::clearUintBuffer(isDraggingUAV);
        }
    }

    void onMousePositionChanged(const MousePosition& mousePosition)
    {
        // Don't update the last position until dispatch
        // That way the delta is indepdent of how often the mouse position changes
        dragValues.currentMousePosition = mousePosition;
    }

    void onCameraMatricesChange(const CameraMatrices& cameraMatrices)
    {
        this->cameraMatrices = cameraMatrices;
    }

    const ComPtr<ID3D11Buffer>& getIsDraggingBuffer() const
    {
        return isDraggingBuffer;
    }

    void dispatch() override
    {
        // May happen if the render override has not yet been setup
        if (!depthSRV) {
            return;
        }

        if (dragValues.currentMousePosition.x != dragValues.lastMousePosition.x ||
            dragValues.currentMousePosition.y != dragValues.lastMousePosition.y) {
            copyConstantBufferToGPU();
        }
        
        ComputeShader::dispatch(numWorkgroups);
        dragValues.lastMousePosition = dragValues.currentMousePosition;
    };

private:
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    ComPtr<ID3D11ShaderResourceView> depthSRV;
    ComPtr<ID3D11UnorderedAccessView> isDraggingUAV;
    ComPtr<ID3D11Buffer> constantBuffer;
    ComPtr<ID3D11Buffer> isDraggingBuffer;
    CameraMatrices cameraMatrices;
    DragValues dragValues;
    int numSubsteps; // simultation substeps per frame
    int numWorkgroups;
    EventBase::Unsubscribe unsubscribeFromDragStateChange;
    EventBase::Unsubscribe unsubscribeFromMousePositionChange;
    EventBase::Unsubscribe unsubscribeFromDepthTargetChange;
    EventBase::Unsubscribe unsubscribeFromCameraMatricesChange;

    void copyConstantBufferToGPU()
    {
        ConstantBuffer cb = {};
        cb.dragWorldDiff = calculateDragWorldDiff();
        cb.lastX = dragValues.lastMousePosition.x;
        cb.lastY = dragValues.lastMousePosition.y;
        cb.dragRadius = dragValues.selectRadius;
        cb.viewportWidth = cameraMatrices.viewportWidth;
        cb.viewportHeight = cameraMatrices.viewportHeight;
        cameraMatrices.viewMatrix.get(cb.viewMatrix);
        cameraMatrices.projMatrix.get(cb.projMatrix);

        DirectX::updateConstantBuffer(constantBuffer, cb);
    }

    // Reverse-project the mouse start and end points to world space at a unit depth, and return the difference.
    std::array<float, 3> calculateDragWorldDiff() {
        MFloatPoint mouseStartNDC(
            (dragValues.lastMousePosition.x / cameraMatrices.viewportWidth) * 2.0f - 1.0f,
            (dragValues.lastMousePosition.y / cameraMatrices.viewportHeight) * 2.0f - 1.0f,
            1.0f, 1.0f
        );

        MFloatPoint mouseEndNDC(
            (dragValues.currentMousePosition.x / cameraMatrices.viewportWidth) * 2.0f - 1.0f,
            (dragValues.currentMousePosition.y / cameraMatrices.viewportHeight) * 2.0f - 1.0f,
            1.0f, 1.0f
        );

        MFloatVector diff = MVector(mouseEndNDC - mouseStartNDC) * cameraMatrices.invViewProjMatrix;
        diff /= static_cast<float>(numSubsteps);               // Spread the drag effect over the number of simulation substeps
        diff *= dragValues.selectStrength; // Scale by the user-defined strength
        return { diff.x, diff.y, diff.z };
    }

    void bind() override
    {
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

    void initializeBuffers(int numVoxels)
    {
        numWorkgroups = Utils::divideRoundUp(numVoxels, VGS_THREADS);

        // Create CBV for the drag values (mouse position, drag distance, grab radius)
        ConstantBuffer cb = {};
        constantBuffer = DirectX::createConstantBuffer<ConstantBuffer>(cb);

        // Create isDragging buffer and its SRV/UAV
        std::vector<UINT> emptyData(numVoxels, 0);
        isDraggingBuffer = DirectX::createReadWriteBuffer<UINT>(emptyData);
        isDraggingUAV = DirectX::createUAV(isDraggingBuffer);
    };

    void removeSubscriptions() {
        if (unsubscribeFromDragStateChange) {
            unsubscribeFromDragStateChange();
        }
        if (unsubscribeFromMousePositionChange) {
            unsubscribeFromMousePositionChange();
        }
        if (unsubscribeFromDepthTargetChange) {
            unsubscribeFromDepthTargetChange();
        }
        if (unsubscribeFromCameraMatricesChange) {
            unsubscribeFromCameraMatricesChange();
        }
    }
    
};