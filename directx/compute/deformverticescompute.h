#pragma once

#include "directx/compute/computeshader.h"

class DeformVerticesCompute : public ComputeShader
{
public:
    DeformVerticesCompute() = default;
    DeformVerticesCompute(
        int numParticles,
        int vertexCount,
        const glm::vec4* originalParticlePositions,   // Will be uploaded to GPU
        const std::vector<uint>& vertexVoxelIds,      // Will be uploaded to GPU
        const ComPtr<ID3D11UnorderedAccessView>& positionsUAV,
        const ComPtr<ID3D11UnorderedAccessView>& normalsUAV,
        const ComPtr<ID3D11ShaderResourceView>& originalPositionsSRV,
        const ComPtr<ID3D11ShaderResourceView>& originalNormalsSRV,
        const ComPtr<ID3D11ShaderResourceView>& particleSRV
    ) : ComputeShader(IDR_SHADER1), positionsUAV(positionsUAV), normalsUAV(normalsUAV), originalPositionsSRV(originalPositionsSRV), originalNormalsSRV(originalNormalsSRV), particleSRV(particleSRV)
    {
        initializeBuffers(numParticles, vertexCount, originalParticlePositions);
    }

    void dispatch() override
    {
        // ComputeShader::dispatch(numWorkgroups);
    };

    void setParticleSRV(const ComPtr<ID3D11ShaderResourceView>& srv)
    {
        particleSRV = srv;
    };

private:
    int numWorkgroups = 0;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11UnorderedAccessView> normalsUAV;
    ComPtr<ID3D11ShaderResourceView> originalPositionsSRV;
    ComPtr<ID3D11ShaderResourceView> originalNormalsSRV;
    ComPtr<ID3D11ShaderResourceView> particleSRV;
    
    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(nullptr, NULL, 0);
    };

    void initializeBuffers(int numParticles, int vertexCount, const glm::vec4* originalParticlePositions)
    {
        
    }

};