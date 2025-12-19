#pragma once

#include "directx/compute/computeshader.h"

class BuildCollisionParticlesCompute : public ComputeShader
{
public:
    BuildCollisionParticlesCompute() = default;

    BuildCollisionParticlesCompute(
        int numParticles,
        const ComPtr<ID3D11UnorderedAccessView>& collisionCellParticleCountsUAV,
        const ComPtr<ID3D11Buffer>& particleCollisionCB
    ) : ComputeShader(IDR_SHADER9), 
        particleCollisionCB(particleCollisionCB),
        collisionCellParticleCountsUAV(collisionCellParticleCountsUAV)
    {
        if (numParticles <= 0) return;
        initializeBuffers(numParticles);
    }

    ~BuildCollisionParticlesCompute() {
        DirectX::notifyMayaOfMemoryUsage(particlesByCollisionCellBuffer);
    }

    void dispatch() override {
        DirectX::clearUintBuffer(particlesByCollisionCellUAV);
        ComputeShader::dispatch(numWorkgroups);
    }

    const ComPtr<ID3D11ShaderResourceView>& getParticlesByCollisionCellSRV() const { return particlesByCollisionCellSRV; }

    void setParticlePositionsSRV(const ComPtr<ID3D11ShaderResourceView>& particlePositionsSRV) {
        this->particlePositionsSRV = particlePositionsSRV;
    }

    void setIsSurfaceSRV(const ComPtr<ID3D11ShaderResourceView>& isSurfaceSRV) {
        this->isSurfaceSRV = isSurfaceSRV;
    }

private:
    int numWorkgroups = 0;
    // Passed in
    ComPtr<ID3D11ShaderResourceView> particlePositionsSRV;
    ComPtr<ID3D11Buffer> particleCollisionCB;
    ComPtr<ID3D11UnorderedAccessView> collisionCellParticleCountsUAV;
    ComPtr<ID3D11ShaderResourceView> isSurfaceSRV;
    
    // Created internally
    ComPtr<ID3D11Buffer> particlesByCollisionCellBuffer;
    ComPtr<ID3D11UnorderedAccessView> particlesByCollisionCellUAV;
    ComPtr<ID3D11ShaderResourceView> particlesByCollisionCellSRV;
    
    void bind() override {
        ID3D11ShaderResourceView* srvs[] = { particlePositionsSRV.Get(), isSurfaceSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { collisionCellParticleCountsUAV.Get(), particlesByCollisionCellUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { particleCollisionCB.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override {
        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void initializeBuffers(int numParticles) {
        // Each particle can overlap up to 8 cells, so we need to allocate memory accordingly.
        int numBufferElements = 8 * numParticles;
        numWorkgroups = Utils::divideRoundUp(numBufferElements, BUILD_COLLISION_PARTICLE_THREADS);

        std::vector<uint> emptyData(numBufferElements, 0);
        particlesByCollisionCellBuffer = DirectX::createReadWriteBuffer<uint>(emptyData);
        particlesByCollisionCellSRV = DirectX::createSRV(particlesByCollisionCellBuffer);
        particlesByCollisionCellUAV = DirectX::createUAV(particlesByCollisionCellBuffer);
    }
};