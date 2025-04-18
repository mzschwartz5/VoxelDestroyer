#pragma once
#include "computeshader.h"
#include "../glm/glm.hpp"

class UpdateVoxelBasesCompute : public ComputeShader
{

public:
    UpdateVoxelBasesCompute() : ComputeShader(IDR_SHADER1){};

    void updateParticleBuffer(const std::vector<glm::vec4>& particles) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX::getContext()->Map(particlesBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        memcpy(mappedResource.pData, particles.data(), particles.size() * sizeof(glm::vec4));
        DirectX::getContext()->Unmap(particlesBuffer.Get(), 0);
    }
    
    void initializeBuffers(int numParticles) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    
        // Initialize particlesBuffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = numParticles * sizeof(glm::vec4); // glm::vec4 for alignment
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(glm::vec4); // Size of each element in the buffer
    
        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &particlesBuffer);
    
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numParticles;
    
        DirectX::getDevice()->CreateShaderResourceView(particlesBuffer.Get(), &srvDesc, &particlesSRV);
    
        // Initialize voxelBasesBuffer and its UAV (Structured Buffer)
        bufferDesc.Usage = D3D11_USAGE_DEFAULT; // Allow GPU write access
        bufferDesc.ByteWidth = (numParticles / 8) * sizeof(glm::vec4) * 3; // 8 particles per voxel, 3 basis vectors for each voxel
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0; 
        bufferDesc.StructureByteStride = sizeof(glm::vec4) * 3; // Size of each element in the buffer (3 basis vectors)
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    
        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &voxelBasesBuffer);
    
        uavDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffer, no format
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = (numParticles / 8);
    
        DirectX::getDevice()->CreateUnorderedAccessView(voxelBasesBuffer.Get(), &uavDesc, &voxelBasesUAV);
    }

private:
    ComPtr<ID3D11Buffer> particlesBuffer;
    ComPtr<ID3D11Buffer> voxelBasesBuffer;
    ComPtr<ID3D11Buffer> debugBasesBuffer;
    ComPtr<ID3D11ShaderResourceView> particlesSRV;
    ComPtr<ID3D11UnorderedAccessView> voxelBasesUAV;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);
        
        ID3D11ShaderResourceView* srvs[] = { particlesSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, 1, srvs);

        ID3D11UnorderedAccessView* uavs[] = { voxelBasesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr); // No need for initialCounts
    };
    

    void tearDown() override
    {
        ComputeShader::tearDown();
        particlesBuffer.Reset();
        voxelBasesBuffer.Reset();
        particlesSRV.Reset();
        voxelBasesUAV.Reset();
    };
};