#pragma once

#include "computeshader.h"
#include "../glm/glm.hpp"

class UpdateVoxelBasesCompute : public ComputeShader
{

public:
    UpdateVoxelBasesCompute(ID3D11Device* device, ID3D11DeviceContext* dxContext, int numParticles) : ComputeShader(IDR_SHADER1, device, dxContext){
        initializeBuffers(numParticles);
    };

    ComputeShaderType getType() const override
    {
        return UpdateVoxelBasis;
    };

private:
    ID3D11DeviceContext* dxContext = nullptr;
    ComPtr<ID3D11Buffer> particlesBuffer;
    ComPtr<ID3D11Buffer> voxelBasesBuffer;
    ComPtr<ID3D11ShaderResourceView> particlesSRV;
    ComPtr<ID3D11UnorderedAccessView> voxelBasesUAV;

    // Override dispatch, maybe? So we can put a memory barrier after?

    void bind(ID3D11DeviceContext* dxContext) override
    {
        dxContext->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { particlesSRV.Get() };
        dxContext->CSSetShaderResources(0, 1, srvs);

        ID3D11UnorderedAccessView* uavs[] = { voxelBasesUAV.Get() };
        dxContext->CSSetUnorderedAccessViews(0, 1, uavs, nullptr); // No need for initialCounts
    };

    void updateParticleBuffer(const std::vector<glm::vec4>& particles) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        dxContext->Map(particlesBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        memcpy(mappedResource.pData, particles.data(), particles.size() * sizeof(glm::vec4));
        dxContext->Unmap(particlesBuffer.Get(), 0);
    }

    void initializeBuffers(int numParticles) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

        // Initialize particlesBuffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = numParticles * sizeof(glm::vec4); // glm::vec4 for alignment
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = 0;

        dxDevice->CreateBuffer(&bufferDesc, nullptr, &particlesBuffer);

        srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numParticles;

        dxDevice->CreateShaderResourceView(particlesBuffer.Get(), &srvDesc, &particlesSRV);

        // Initialize voxelBasesBuffer and its UAV
        bufferDesc.ByteWidth = (numParticles / 8) * sizeof(glm::vec4) * 3; // 8 particles per voxel, 3 basis vectors for each voxel
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;

        dxDevice->CreateBuffer(&bufferDesc, nullptr, &voxelBasesBuffer);

        uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = (numParticles / 8) * 3;

        dxDevice->CreateUnorderedAccessView(voxelBasesBuffer.Get(), &uavDesc, &voxelBasesUAV);
    }

};