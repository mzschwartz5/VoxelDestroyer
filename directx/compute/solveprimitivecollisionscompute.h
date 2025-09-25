#pragma once

#include "directx/compute/computeshader.h"
#include "constants.h"

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
    ) : ComputeShader(IDR_SHADER14)
    {
        initializeBuffers(initColliderBuffer);
    };

    void updateColliderBuffer(const ColliderBuffer& newCB) {
        if (colliderBuffer.Get() == nullptr) return;
        numColliders = newCB.numColliders;
        updateConstantBuffer(colliderBuffer, newCB);
    }

    void dispatch() override {
        if (numColliders <= 0) return;
        ComputeShader::dispatch(numWorkgroups);
    }

    void setParticlePositionsUAV(const ComPtr<ID3D11UnorderedAccessView>& positionsUAV) {
        this->positionsUAV = positionsUAV;
    }

    void setOldParticlePositionsSRV(const ComPtr<ID3D11ShaderResourceView>& oldPositionsSRV) {
        this->oldPositionsSRV = oldPositionsSRV;
    }

    const ComPtr<ID3D11Buffer>& getColliderBuffer() const {
        return colliderBuffer;
    }

private:
    int numWorkgroups = 0;
    int numColliders = 0;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11ShaderResourceView> oldPositionsSRV;
    ComPtr<ID3D11Buffer> colliderBuffer;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11ShaderResourceView* srvs[] = { oldPositionsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11Buffer* cbvs[] = { colliderBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(nullptr, NULL, 0);

        ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

        ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);

        ID3D11Buffer* nullCBs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
    }

    void initializeBuffers(const ColliderBuffer& initColliderBuffer) {
        int totalParticles = initColliderBuffer.totalParticles;
        numWorkgroups = Utils::divideRoundUp(totalParticles, VGS_THREADS); // TODO: use own thread group size
        colliderBuffer = DirectX::createConstantBuffer(initColliderBuffer);
    }
};