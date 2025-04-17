#pragma once
#include "computeshader.h"
#include "../glm/glm.hpp"

class UpdateVoxelBasesCompute : public ComputeShader
{

public:
    UpdateVoxelBasesCompute() : ComputeShader(){};

    UpdateVoxelBasesCompute(int numParticles) : ComputeShader(IDR_SHADER1){
        initializeBuffers(numParticles);
    };

    void updateParticleBuffer(const std::vector<glm::vec4>& particles) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX::getContext()->Map(particlesBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        memcpy(mappedResource.pData, particles.data(), particles.size() * sizeof(glm::vec4));
        DirectX::getContext()->Unmap(particlesBuffer.Get(), 0);
    }

private:
    ComPtr<ID3D11Buffer> particlesBuffer;
    ComPtr<ID3D11Buffer> voxelBasesBuffer;
    ComPtr<ID3D11ShaderResourceView> particlesSRV;
    ComPtr<ID3D11UnorderedAccessView> voxelBasesUAV;

    // Override dispatch, maybe? So we can put a memory barrier after?

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { particlesSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, 1, srvs);

        ID3D11UnorderedAccessView* uavs[] = { voxelBasesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr); // No need for initialCounts
    };

    void initializeBuffers(int numParticles) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

        // Initialize particlesBuffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = numParticles * sizeof(glm::vec4); // glm::vec4 for alignment
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = 0;

        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &particlesBuffer);

        srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numParticles;

        DirectX::getDevice()->CreateShaderResourceView(particlesBuffer.Get(), &srvDesc, &particlesSRV);

        // Initialize voxelBasesBuffer and its UAV
        bufferDesc.ByteWidth = (numParticles / 8) * sizeof(glm::vec4) * 3; // 8 particles per voxel, 3 basis vectors for each voxel
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;

        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &voxelBasesBuffer);

        uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = (numParticles / 8) * 3;

        DirectX::getDevice()->CreateUnorderedAccessView(voxelBasesBuffer.Get(), &uavDesc, &voxelBasesUAV);
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
        particlesBuffer.Reset();
        voxelBasesBuffer.Reset();
        particlesSRV.Reset();
        voxelBasesUAV.Reset();
    };
};