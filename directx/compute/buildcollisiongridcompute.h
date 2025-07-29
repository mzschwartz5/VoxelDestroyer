#pragma once

#include "directx/compute/computeshader.h"

struct ParticleCollisionCB {
    float inverseCellSize;
    int hashGridSize;
    unsigned int numParticles;
    float particleRadius;
};

class BuildCollisionGridCompute : public ComputeShader
{
public:
    BuildCollisionGridCompute(
        int numParticles,
        float particleSize,
        const ComPtr<ID3D11ShaderResourceView>& particlePositionsSRV,
        const ComPtr<ID3D11ShaderResourceView>& isSurfaceSRV
    ) : ComputeShader(IDR_SHADER8), particlePositionsSRV(particlePositionsSRV), isSurfaceSRV(isSurfaceSRV) {
        initializeBuffers(numParticles, particleSize);
    };

    void dispatch() override {
        clearUintBuffer(collisionCellParticleCountsUAV);
        ComputeShader::dispatch(numWorkgroups);
    }

    const ComPtr<ID3D11Buffer>& getParticleCollisionCB() const { return particleCollisionCB; }

    const ComPtr<ID3D11UnorderedAccessView>& getCollisionCellParticleCountsUAV() const { return collisionCellParticleCountsUAV; }

    const ComPtr<ID3D11ShaderResourceView>& getCollisionCellParticleCountsSRV() const { return collisionCellParticleCountsSRV; }

    void updateParticleCollisionCB(int numParticles, float particleSize) {
        ParticleCollisionCB cb;
        cb.inverseCellSize = 1.0f / (2.0f * particleSize);
        cb.hashGridSize = numParticles;
        cb.numParticles = numParticles;
        cb.particleRadius = particleSize;
        ComputeShader::updateConstantBuffer(particleCollisionCB, cb);
    }

private:
    int numWorkgroups = 0;
    ComPtr<ID3D11Buffer> particleCollisionCB;
    ComPtr<ID3D11Buffer> collisionCellParticleCountsBuffer;
    ComPtr<ID3D11ShaderResourceView> collisionCellParticleCountsSRV;
    ComPtr<ID3D11UnorderedAccessView> collisionCellParticleCountsUAV;
    ComPtr<ID3D11ShaderResourceView> particlePositionsSRV;
    ComPtr<ID3D11ShaderResourceView> isSurfaceSRV;

    void bind() override {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { particlePositionsSRV.Get(), isSurfaceSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { collisionCellParticleCountsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { particleCollisionCB.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void initializeBuffers(int numParticles, float particleSize) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        numWorkgroups = Utils::divideRoundUp(numParticles, BUILD_COLLISION_GRID_THREADS);

        // Round up to the nearest power of two so it can be prefix scanned.
        int numBufferElements = pow(2, Utils::ilogbaseceil(numParticles, 2)); 

        // Create the collision cell particle counts buffer
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(uint) * numBufferElements;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(uint);
        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &collisionCellParticleCountsBuffer);

        // Create the SRV for the collision cell particle counts buffer
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numBufferElements;
        DirectX::getDevice()->CreateShaderResourceView(collisionCellParticleCountsBuffer.Get(), &srvDesc, &collisionCellParticleCountsSRV);

        // Create the UAV for the collision cell particle counts buffer
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numBufferElements;
        DirectX::getDevice()->CreateUnorderedAccessView(collisionCellParticleCountsBuffer.Get(), &uavDesc, &collisionCellParticleCountsUAV);

        // Create the particle collision constant buffer
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;              // Dynamic for CPU updates
        bufferDesc.ByteWidth = sizeof(ParticleCollisionCB);  // Size of the constant buffer
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;   // Bind as a constant buffer
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;  // Allow CPU writes
        bufferDesc.MiscFlags = 0;

        CreateBuffer(&bufferDesc, nullptr, &particleCollisionCB);
        updateParticleCollisionCB(numParticles, particleSize);
    }

    void tearDown() override {
        ComputeShader::tearDown();
        collisionCellParticleCountsBuffer.Reset();
        collisionCellParticleCountsSRV.Reset();
        collisionCellParticleCountsUAV.Reset();
    }
};