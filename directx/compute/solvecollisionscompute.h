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
        const ComPtr<ID3D11UnorderedAccessView>& particlePositionsUAV,
        const ComPtr<ID3D11ShaderResourceView>& particlesByCollisionCellSRV,
        const ComPtr<ID3D11ShaderResourceView>& collisionCellParticleCountsSRV,
        const ComPtr<ID3D11Buffer>& particleCollisionCB
    ) : ComputeShader(IDR_SHADER12),
        particlePositionsUAV(particlePositionsUAV),
        particlesByCollisionCellSRV(particlesByCollisionCellSRV),
        collisionCellParticleCountsSRV(collisionCellParticleCountsSRV),
        particleCollisionCB(particleCollisionCB)
    {
        numWorkgroups = Utils::divideRoundUp(hashGridSize, SOLVE_COLLISION_THREADS);
    }

    void dispatch() override {
        ComputeShader::dispatch(numWorkgroups);
    }


private:
    int numWorkgroups = 0;
    ComPtr<ID3D11UnorderedAccessView> particlePositionsUAV;
    ComPtr<ID3D11ShaderResourceView> particlesByCollisionCellSRV;
    ComPtr<ID3D11ShaderResourceView> collisionCellParticleCountsSRV;
    ComPtr<ID3D11Buffer> particleCollisionCB;

    void bind() override {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);
        
        ID3D11ShaderResourceView* srvs[] = { particlesByCollisionCellSRV.Get(), collisionCellParticleCountsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { particlePositionsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { particleCollisionCB.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);
        
        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

};