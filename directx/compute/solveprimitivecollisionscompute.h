#pragma once

#include "directx/compute/computeshader.h"
#include "constants.h"

// Hard-limit number of colliders. This is partly because dynamic-sized arrays
// are not supported by constant buffers. But also, collider primitives aren't optimized for performance.
// If there's ever a use case for more, would need to optimize collision code. Cbuffer can hold more, but could also use structured buffer.
struct ColliderBuffer {
    float worldMatrix[MAX_COLLIDERS][4][4];  // diagonal elements are hijacked to store geometric parameters (e.g. radius, height, etc)
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

    const ComPtr<ID3D11Buffer>& getColliderBuffer() const {
        return colliderBuffer;
    }

private:
    int numWorkgroups = 0;
    int numColliders = 0;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11Buffer> colliderBuffer;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { colliderBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(nullptr, NULL, 0);

        ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

        ID3D11Buffer* nullCBs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(nullCBs), nullCBs);
    }

    void initializeBuffers(const ColliderBuffer& initColliderBuffer) {
        int totalParticles = initColliderBuffer.totalParticles;
        numColliders = initColliderBuffer.numColliders;
        numWorkgroups = Utils::divideRoundUp(totalParticles, VGS_THREADS); // TODO: use own thread group size
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};
		
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.ByteWidth = sizeof(ColliderBuffer);
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = 0;
        initData.pSysMem = &initColliderBuffer;
		CreateBuffer(&bufferDesc, &initData, colliderBuffer.GetAddressOf());
    }
};