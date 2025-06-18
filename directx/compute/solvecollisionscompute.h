#pragma once
#include "directx/compute/computeshader.h"
#include "constants.h"

struct SolveCollisionConstantBuffer
{
    float voxelSize;
    float particleRadius;
    int numCollisionGridCells;
    float padding = 0.0f;
};

class SolveCollisionsCompute : public ComputeShader
{
public:
    SolveCollisionsCompute(
        float voxelSize,
        float particleRadius,
        int numCollisionGridCells,
        ComPtr<ID3D11UnorderedAccessView> particlesUAV,
        ComPtr<ID3D11ShaderResourceView> collisionVoxelCountsSRV,
        ComPtr<ID3D11ShaderResourceView> collisionVoxelIndicesSRV
    ) : ComputeShader(IDR_SHADER9), particlesUAV(particlesUAV), collisionVoxelCountsSRV(collisionVoxelCountsSRV), collisionVoxelIndicesSRV(collisionVoxelIndicesSRV)
    {
        initializeBuffers(voxelSize, particleRadius, numCollisionGridCells);
    };

private:
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    ComPtr<ID3D11ShaderResourceView> collisionVoxelCountsSRV;
    ComPtr<ID3D11ShaderResourceView> collisionVoxelIndicesSRV;
    ComPtr<ID3D11Buffer> constantBuffer;

    void bind() override {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { collisionVoxelCountsSRV.Get(), collisionVoxelIndicesSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { constantBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override {
        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(float voxelSize, float particleRadius, int numCollisionGridCells)
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};

        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(SolveCollisionConstantBuffer);
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = 0; // No need to write to this buffer, stays constant throughout sim.
        bufferDesc.MiscFlags = 0;

        SolveCollisionConstantBuffer cbData;
        cbData.voxelSize = voxelSize;
        cbData.particleRadius = particleRadius;
        cbData.numCollisionGridCells = numCollisionGridCells;

        initData.pSysMem = &cbData;

        HRESULT hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &constantBuffer);
    }

};