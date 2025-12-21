#pragma once

#include "directx/compute/computeshader.h"
#include "shaders/constants.hlsli"

class VGSCompute : public ComputeShader
{
public:
    VGSCompute() = default;

    VGSCompute(
        uint numParticles,
        float particleRadius,
        float voxelRestVolume
	) : ComputeShader(IDR_SHADER3)
    {
        numWorkgroups = Utils::divideRoundUp(numParticles / 8, VGS_THREADS);
        initializeBuffers(numParticles, particleRadius, voxelRestVolume);
    };

    void dispatch() override
    {
        ComputeShader::dispatch(numWorkgroups);
    };

    void updateVGSParameters(
        float relaxation,
        float edgeUniformity,
        uint iterCount
    ) {
        vgsConstants.relaxation = relaxation;
        vgsConstants.edgeUniformity = edgeUniformity;
        vgsConstants.iterCount = iterCount;
        DirectX::updateConstantBuffer(vgsConstantBuffer, vgsConstants);
    }

    void setParticlesUAV(const ComPtr<ID3D11UnorderedAccessView>& particlesUAV) {
        this->particlesUAV = particlesUAV;
    }

private:
    int numWorkgroups = 0;
    ComPtr<ID3D11Buffer> vgsConstantBuffer;
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    VGSConstants vgsConstants;
    
    void bind() override
    {
		ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
		DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { vgsConstantBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(
        uint numParticles,
        float particleRadius,
        float voxelRestVolume
    ) {

        vgsConstants.relaxation = 0.5f; // default values
        vgsConstants.edgeUniformity = 0.0f;
        vgsConstants.iterCount = 3;
        vgsConstants.numVoxels = numParticles / 8;
        vgsConstants.particleRadius = particleRadius;
        vgsConstants.voxelRestVolume = voxelRestVolume;
    
        vgsConstantBuffer = DirectX::createConstantBuffer(vgsConstants);
    }
};