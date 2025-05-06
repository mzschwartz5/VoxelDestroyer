#pragma once
#include "computeshader.h"
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MViewport2Renderer.h>
#include "glm/glm.hpp"

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
    glm::mat4 viewProjMatrix;
    glm::mat4 invViewProjMatrix;
};

struct ConstantBuffer{
    DragValues dragValues;
    float dragStrength{ 20.0f };
    CameraMatrices cameraMatrices;
};

class DragParticlesCompute : public ComputeShader
{
public:
    DragParticlesCompute(
        const ComPtr<ID3D11UnorderedAccessView>& particlesUAV,
        const ComPtr<ID3D11ShaderResourceView>& oldParticlesSRV
    ) : ComputeShader(IDR_SHADER7), particlesUAV(particlesUAV), oldParticlesSRV(oldParticlesSRV)
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

    void updateDragValues(const DragValues& dragValues)
    {
        this->dragValues = dragValues;

        ConstantBuffer cb{
            dragValues,
            20.0f, // drag strength, hardcoded for now (TODO: wire up to sim constants)
            cameraMatrices
        };

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX::getContext()->Map(constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr))
        {
            memcpy(mappedResource.pData, &cb, sizeof(ConstantBuffer));
            DirectX::getContext()->Unmap(constantBuffer.Get(), 0);
        }
        else
        {
            MGlobal::displayError("Failed to map drag values buffer.");
        }
    }

    void updateCameraMatrices(const CameraMatrices& cameraMatrices)
    {
        this->cameraMatrices = cameraMatrices;
    }

    void dispatch(int threadGroupCount) override
    {
        // May happen if the override render has not yet been setup
        if (!depthSRV) {
            return;
        }

        bind();
        DirectX::getContext()->Dispatch(threadGroupCount, 1, 1); 
        unbind();

        // Reset drag values (because mouse drag event isn't called every frame, only when the mouse is moved)
        if (dragValues.currX != dragValues.lastX || dragValues.currY != dragValues.lastY) {
            dragValues.lastX = dragValues.currX;
            dragValues.lastY = dragValues.currY;
            updateDragValues(dragValues);
        }
    };

private:
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    ComPtr<ID3D11ShaderResourceView> oldParticlesSRV;
    ComPtr<ID3D11ShaderResourceView> depthSRV;
    ComPtr<ID3D11Buffer> constantBuffer;
    ID3D11DepthStencilView* depthStencilView;
    CameraMatrices cameraMatrices;
    DragValues dragValues;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11ShaderResourceView* srvs[] = { oldParticlesSRV.Get(), depthSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs); 

        ID3D11Buffer* cbvs[] = { constantBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr); 

        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);


        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers()
    {
        // Create CBV for the drag values (mouse position, drag distance, grab radius)
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = sizeof(ConstantBuffer);
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = 0;
    
        HRESULT hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &constantBuffer);

        if (FAILED(hr))
        {
            MGlobal::displayError("Failed to create drag values buffer.");
            return;
        }

    };

    void tearDown() override
    {
        ComputeShader::tearDown();
        if (depthSRV) {
            depthSRV->Release();
            depthSRV = nullptr;
        }

        constantBuffer.Reset();
    };
    
};