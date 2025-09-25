#pragma once

#include "directx/compute/computeshader.h"

struct ParticleCollisionCB {
    float inverseCellSize;
    unsigned int hashGridSize;
    unsigned int numParticles;
    uint padding = 0;
};

static constexpr int HASH_TABLE_SIZE_TO_PARTICLES = 2;

class BuildCollisionGridCompute : public ComputeShader
{
public:
    BuildCollisionGridCompute() = default;

    BuildCollisionGridCompute(
        int numParticles,
        float particleSize
    ) : ComputeShader(IDR_SHADER8) {
        initializeBuffers(numParticles, particleSize);
    };

    ~BuildCollisionGridCompute() override {
        DirectX::notifyMayaOfMemoryUsage(collisionCellParticleCountsBuffer);
    }

    void dispatch() override {
        DirectX::clearUintBuffer(collisionCellParticleCountsUAV);
        ComputeShader::dispatch(numWorkgroups);
    }

    const ComPtr<ID3D11Buffer>& getParticleCollisionCB() const { return particleCollisionCB; }

    const ComPtr<ID3D11UnorderedAccessView>& getCollisionCellParticleCountsUAV() const { return collisionCellParticleCountsUAV; }

    const ComPtr<ID3D11ShaderResourceView>& getCollisionCellParticleCountsSRV() const { return collisionCellParticleCountsSRV; }

    int getHashGridSize() const {
        return particleCollisionCBData.hashGridSize;
    }

    void setParticlePositionsSRV(const ComPtr<ID3D11ShaderResourceView>& particlePositionsSRV) {
        this->particlePositionsSRV = particlePositionsSRV;
    }

    void setIsSurfaceSRV(const ComPtr<ID3D11ShaderResourceView>& isSurfaceSRV) {
        this->isSurfaceSRV = isSurfaceSRV;
    }

private:
    int numWorkgroups = 0;
    ParticleCollisionCB particleCollisionCBData;
    ComPtr<ID3D11Buffer> particleCollisionCB;
    ComPtr<ID3D11Buffer> collisionCellParticleCountsBuffer;
    ComPtr<ID3D11ShaderResourceView> collisionCellParticleCountsSRV;
    ComPtr<ID3D11UnorderedAccessView> collisionCellParticleCountsUAV;
    ComPtr<ID3D11ShaderResourceView> particlePositionsSRV;
    ComPtr<ID3D11ShaderResourceView> isSurfaceSRV;

    void bind() override {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { particlePositionsSRV.Get(), isSurfaceSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { collisionCellParticleCountsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { particleCollisionCB.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override {
        DirectX::getContext()->CSSetShader(nullptr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void initializeBuffers(int numParticles, float particleSize) {
        numWorkgroups = Utils::divideRoundUp(numParticles, BUILD_COLLISION_GRID_THREADS);

        // Multiplpy by a factor to reduce hash collisions.
        // Add one as a "guard" / so the last cell will contribute to the scan and there will be record of it.
        // Round up to the nearest power of two so it can be prefix scanned.
        int numBufferElements = static_cast<int>(pow(2, Utils::ilogbaseceil(HASH_TABLE_SIZE_TO_PARTICLES * numParticles + 1, 2))); 

        std::vector<uint> emptyData(numBufferElements, 0);
        collisionCellParticleCountsBuffer = DirectX::createReadWriteBuffer<uint>(emptyData);
        collisionCellParticleCountsSRV = DirectX::createSRV(collisionCellParticleCountsBuffer);
        collisionCellParticleCountsUAV = DirectX::createUAV(collisionCellParticleCountsBuffer);

        particleCollisionCBData.inverseCellSize = 1.0f / (2.0f * particleSize);
        particleCollisionCBData.hashGridSize = HASH_TABLE_SIZE_TO_PARTICLES * numParticles;
        particleCollisionCBData.numParticles = numParticles;
        particleCollisionCB = DirectX::createConstantBuffer<ParticleCollisionCB>(particleCollisionCBData);
    }
};