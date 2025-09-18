#pragma once

#include "directx/compute/computeshader.h"

// Hard-limit number of colliders to 256. This is partly because dynamic-sized arrays
// are not supported by constant buffers. But also, collider primitives aren't optimized for performance.
// If there's ever a use case for more, would need to optimize collision code. Cbuffer can hold more, but could also use structured buffer.
#define MAX_COLLIDERS 256
struct ColliderBuffer {
    float worldMatrix[MAX_COLLIDERS][4][4];
    int numSpheres = 0;
    int numBoxes = 0;
    int numPlanes = 0;
    int numCylinders = 0;
    int numCapsules = 0;
    int padding[3]; // Padding to ensure 16-byte alignment
    float sphereRadius[MAX_COLLIDERS];
    float boxWidth[MAX_COLLIDERS];
    float boxHeight[MAX_COLLIDERS];
    float boxDepth[MAX_COLLIDERS];
    float planeWidth[MAX_COLLIDERS];
    float planeHeight[MAX_COLLIDERS];
    float cylinderRadius[MAX_COLLIDERS];
    float cylinderHeight[MAX_COLLIDERS];
    float capsuleRadius[MAX_COLLIDERS];
    float capsuleHeight[MAX_COLLIDERS];
};

class SolvePrimitiveCollisionsCompute : public ComputeShader
{
public:
    SolvePrimitiveCollisionsCompute() = default;
    SolvePrimitiveCollisionsCompute(
        int totalParticles,
        const ColliderBuffer& initColliderBuffer
    ) : ComputeShader(IDR_SHADER6)
    {
        initializeBuffers(totalParticles, initColliderBuffer);
    };

    void updateColliderBuffer(const ColliderBuffer& newCB) {
        if (!colliderBuffer) return;
        updateConstantBuffer(colliderBuffer, newCB);
    }

    void dispatch() override {
        ComputeShader::dispatch(numWorkgroups);
    }

    void setParticlePositionsUAV(const ComPtr<ID3D11UnorderedAccessView>& positionsUAV) {
        this->positionsUAV = positionsUAV;
    }

    const ComPtr<ID3D11Buffer>& getColliderBuffer() const {
        return colliderBuffer;
    }

private:
    int numWorkgroups;
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

    void initializeBuffers(int totalParticles, const ColliderBuffer& initColliderBuffer) {
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