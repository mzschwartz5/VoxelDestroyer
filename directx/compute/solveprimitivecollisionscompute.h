#pragma once

#include "directx/compute/computeshader.h"

// Hard-limit number of colliders. This is partly because dynamic-sized arrays
// are not supported by constant buffers. But also, collider primitives aren't optimized for performance.
// If there's ever a use case for more, would need to optimize collision code. Cbuffer can hold more, but could also use structured buffer.
struct ColliderBuffer {
    float worldMatrix[MAX_COLLIDERS][4][4];  // bottom row hijacked to store geometric parameters (e.g. radius, height, etc)
    float inverseWorldMatrix[MAX_COLLIDERS][4][4];
    int totalParticles = 0;
    int numColliders = 0;
    int padding[2];                          // Padding to ensure 16-byte alignment
};

class SolvePrimitiveCollisionsCompute : public ComputeShader
{
public:
    SolvePrimitiveCollisionsCompute() = default;
    SolvePrimitiveCollisionsCompute(
        const ColliderBuffer& initColliderBuffer
    ) : ComputeShader(IDR_SHADER13)
    {
        initializeBuffers(initColliderBuffer);
    };

    void updateColliderBuffer(const ColliderBuffer& newCB) {
        if (colliderBuffer.Get() == nullptr) return;
        numColliders = newCB.numColliders;
        DirectX::updateConstantBuffer(colliderBuffer, newCB);
    }

    void dispatch() override {
        if (numColliders <= 0) return;
        ComputeShader::dispatch(numWorkgroups);
    }

    void setParticlesUAV(const ComPtr<ID3D11UnorderedAccessView>& particlesUAV) {
        this->particlesUAV = particlesUAV;
    }

    void setOldParticlesSRV(const ComPtr<ID3D11ShaderResourceView>& oldParticlesSRV) {
        this->oldParticlesSRV = oldParticlesSRV;
    }

    const ComPtr<ID3D11Buffer>& getColliderBuffer() const {
        return colliderBuffer;
    }

private:
    int numWorkgroups = 0;
    int numColliders = 0;
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    ComPtr<ID3D11ShaderResourceView> oldParticlesSRV;
    ComPtr<ID3D11Buffer> colliderBuffer;

    void bind() override
    {
        ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11ShaderResourceView* srvs[] = { oldParticlesSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11Buffer* cbvs[] = { colliderBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override
    {
        ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

        ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);

        ID3D11Buffer* nullCBs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
    }

    void initializeBuffers(const ColliderBuffer& initColliderBuffer) {
        int totalParticles = initColliderBuffer.totalParticles;
        numColliders = initColliderBuffer.numColliders;
        numWorkgroups = Utils::divideRoundUp(totalParticles, VGS_THREADS); // TODO: use own thread group size
        colliderBuffer = DirectX::createConstantBuffer(initColliderBuffer);
    }
};