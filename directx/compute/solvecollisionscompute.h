#pragma once

#include "directx/compute/computeshader.h"

/**
 * The workhorse of voxel collisions. Following broadphase presteps (building a dense array of particle indices sorted by grid cell),
 * this shader resolves collisions between particles in the same grid cell in a pairwise fashion.
 */
class SolveCollisionsCompute : public ComputeShader
{
public:
    SolveCollisionsCompute() = default;

    SolveCollisionsCompute(
        int hashGridSize,
        const ComPtr<ID3D11ShaderResourceView>& particlesByCollisionCellSRV,
        const ComPtr<ID3D11ShaderResourceView>& collisionCellParticleCountsSRV,
        const ComPtr<ID3D11Buffer>& particleCollisionCB
    ) : ComputeShader(IDR_SHADER12),
        particlesByCollisionCellSRV(particlesByCollisionCellSRV),
        collisionCellParticleCountsSRV(collisionCellParticleCountsSRV),
        particleCollisionCB(particleCollisionCB)
    {
        if (hashGridSize <= 0) return;
        numWorkgroups = Utils::divideRoundUp(hashGridSize, SOLVE_COLLISION_THREADS);
    }

    void dispatch() override {
        ComputeShader::dispatch(numWorkgroups);
    }

    void setParticlesUAV(const ComPtr<ID3D11UnorderedAccessView>& particlesUAV) {
        this->particlesUAV = particlesUAV;
    }

    void setOldParticlesSRV(const ComPtr<ID3D11ShaderResourceView>& oldParticlesSRV) {
        this->oldParticlesSRV = oldParticlesSRV;
    }

private:
    int numWorkgroups = 0;
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    ComPtr<ID3D11ShaderResourceView> oldParticlesSRV;
    ComPtr<ID3D11ShaderResourceView> particlesByCollisionCellSRV;
    ComPtr<ID3D11ShaderResourceView> collisionCellParticleCountsSRV;
    ComPtr<ID3D11Buffer> particleCollisionCB;

    void bind() override {
        ID3D11ShaderResourceView* srvs[] = { particlesByCollisionCellSRV.Get(), collisionCellParticleCountsSRV.Get(), oldParticlesSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { particleCollisionCB.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override {
        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

};