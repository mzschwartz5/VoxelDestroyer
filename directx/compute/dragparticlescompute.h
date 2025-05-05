#pragma once
#include "computeshader.h"
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>

class DragParticlesCompute : public ComputeShader
{
public:
    DragParticlesCompute(
        const ComPtr<ID3D11UnorderedAccessView>& particlesUAV
    ) : ComputeShader(IDR_SHADER7), particlesUAV(particlesUAV)
    {
        createDepthSampler();
    };

    void updateDepthBuffer(void* depthResourceHandle)
    {
        if (!depthResourceHandle) {
            MGlobal::displayError("Invalid depth resource handle.");
            return;
        }
        
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

    void dispatch(int threadGroupCount) override
    {
        // May happen if the override render has not yet been setup
        if (!depthSRV) {
            return;
        }

        bind();
        DirectX::getContext()->Dispatch(threadGroupCount, 1, 1); 
        unbind();
    };

private:
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    ComPtr<ID3D11SamplerState> samplerState;
    ComPtr<ID3D11ShaderResourceView> depthSRV;
    ID3D11DepthStencilView* depthStencilView;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11ShaderResourceView* srvs[] = { depthSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs); 

        ID3D11SamplerState* samplers[] = { samplerState.Get() };
        DirectX::getContext()->CSSetSamplers(0, ARRAYSIZE(samplers), samplers);
    };

    void unbind() override
    {
        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr); 

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, 1, srvs);

        ID3D11SamplerState* samplers[] = { nullptr };
        DirectX::getContext()->CSSetSamplers(0, 1, samplers);
    };

    void createDepthSampler()
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; // Linear filtering
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;   // Clamp addressing mode
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS;   // Useful for depth comparisons
        samplerDesc.MinLOD = 0;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    
        HRESULT hr = DirectX::getDevice()->CreateSamplerState(&samplerDesc, &samplerState);
        if (FAILED(hr))
        {
            MGlobal::displayError("Failed to create sampler state.");
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
        
        if (samplerState) {
            samplerState->Release();
            samplerState = nullptr;
        }
    };
};