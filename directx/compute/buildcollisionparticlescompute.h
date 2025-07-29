#pragma once

#include "directx/compute/computeshader.h"

class BuildCollisionParticlesCompute : public ComputeShader
{
public:
    BuildCollisionParticlesCompute(
        const ComPtr<ID3D11ShaderResourceView>& particlePositionsSRV,
        const ComPtr<ID3D11UnorderedAccessView>& collisionCellParticleCountsUAV,
        const ComPtr<ID3D11Buffer>& particleCollisionCB,
        const ComPtr<ID3D11ShaderResourceView>& isSurfaceSRV
    ) : ComputeShader(IDR_SHADER9), 
        particlePositionsSRV(particlePositionsSRV),
        particleCollisionCB(particleCollisionCB),
        collisionCellParticleCountsUAV(collisionCellParticleCountsUAV), 
        isSurfaceSRV(isSurfaceSRV) 
    {
        initializeBuffers();
    }

    void dispatch() override {
        clearUintBuffer(particlesByCollisionCellUAV);
        ComputeShader::dispatch(numWorkgroups);
    }

    const ComPtr<ID3D11ShaderResourceView>& getParticlesByCollisionCellSRV() const { return particlesByCollisionCellSRV; }

private:
    int numWorkgroups = 0;
    D3D11_SHADER_RESOURCE_VIEW_DESC srvQueryDesc;
    // Passed in
    ComPtr<ID3D11ShaderResourceView> particlePositionsSRV;
    ComPtr<ID3D11Buffer> particleCollisionCB;
    ComPtr<ID3D11UnorderedAccessView> collisionCellParticleCountsUAV;
    ComPtr<ID3D11ShaderResourceView> isSurfaceSRV;
    
    // Created internally
    ComPtr<ID3D11Buffer> particlesByCollisionCellBuffer;
    ComPtr<ID3D11UnorderedAccessView> particlesByCollisionCellUAV;
    ComPtr<ID3D11ShaderResourceView> particlesByCollisionCellSRV;
    
    void bind() override {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { particlePositionsSRV.Get(), isSurfaceSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { collisionCellParticleCountsUAV.Get(), particlesByCollisionCellUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { particleCollisionCB.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void initializeBuffers() {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

        // Each particle can overlap up to 8 cells, so we need to allocate memory accordingly.
        particlePositionsSRV->GetDesc(&srvQueryDesc);
        int numParticles = srvQueryDesc.Buffer.NumElements;
        int numBufferElements = 8 * numParticles;
        numWorkgroups = Utils::divideRoundUp(numBufferElements, BUILD_COLLISION_PARTICLE_THREADS);

        // Create the particles by collision cell buffer, and its UAV and SRV.
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = numBufferElements * sizeof(uint);
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(uint);
        CreateBuffer(&bufferDesc, nullptr, &particlesByCollisionCellBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numBufferElements;
        DirectX::getDevice()->CreateShaderResourceView(particlesByCollisionCellBuffer.Get(), &srvDesc, &particlesByCollisionCellSRV);

        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numBufferElements;
        DirectX::getDevice()->CreateUnorderedAccessView(particlesByCollisionCellBuffer.Get(), &uavDesc, &particlesByCollisionCellUAV);
    }
};