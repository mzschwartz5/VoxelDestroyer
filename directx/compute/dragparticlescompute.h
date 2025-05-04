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
        initializeBuffers();
    };

private:
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr); // No need for initialCounts
    };

    void unbind() override
    {
        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr); // No need for initialCounts
    };

    void initializeBuffers()
    {
        

    };

    void tearDown() override
    {
        ComputeShader::tearDown();
    };
};