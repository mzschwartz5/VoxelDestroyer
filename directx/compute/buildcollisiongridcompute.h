#pragma once

#include "directx/compute/computeshader.h"

struct ParticleCollisionCB {
    float inverseCellSize;
    int hashGridSize;
    int numParticles;
    int padding1 = 0;
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

    void dispatch(int numWorkgroups) override {
        bind();
        DirectX::getContext()->Dispatch(numWorkgroups, 1, 1);
        unbind();
    }

    void clearCollisionCellParticleCounts() {
        // See docs: 4 values are required even though only the first will be used, in our case.
        UINT clearValues[4] = { 0, 0, 0, 0 };
        DirectX::getContext()->ClearUnorderedAccessViewUint(collisionCellParticleCountsUAV.Get(), clearValues);
    }

    const ComPtr<ID3D11Buffer>& getParticleCollisionCB() const { return particleCollisionCB; }

    void updateParticleCollisionCB(int numParticles, float particleSize) {
        ParticleCollisionCB cb;
        cb.inverseCellSize = 1.0f / particleSize;
        // Note that the hash grid size is the number of particles, even though the grid buffer size is numParticles + 1
        // The +1 is just a guard, but we don't want to include it when modulo'ing the particle index.
        cb.hashGridSize = numParticles;
        cb.numParticles = numParticles;
        ComputeShader::updateConstantBuffer(particleCollisionCB, cb);
    }

private:
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

        // Create the collision cell particle counts buffer (typed, not structured or raw, so it can be used with ID3DX11Scan)
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(uint) * (numParticles + 1); // +1 as guard for the last cell
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &collisionCellParticleCountsBuffer);

        // Create the SRV for the collision cell particle counts buffer
        srvDesc.Format = DXGI_FORMAT_R32_UINT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numParticles + 1;
        DirectX::getDevice()->CreateShaderResourceView(collisionCellParticleCountsBuffer.Get(), &srvDesc, &collisionCellParticleCountsSRV);

        // Create the UAV for the collision cell particle counts buffer
        uavDesc.Format = DXGI_FORMAT_R32_UINT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numParticles + 1;
        uavDesc.Buffer.Flags = 0;
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