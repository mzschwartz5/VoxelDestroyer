#pragma once
#include "computeshader.h"
#include "../glm/glm.hpp"

class BindVerticesCompute : public ComputeShader
{
public:
    BindVerticesCompute(
        int numParticles,
        const float* vertices, 
        int numVerts,
        const std::vector<glm::vec4>& particles,
        const std::vector<uint>& vertStartIds,
        const std::vector<uint>& numVertices
    ) : ComputeShader(IDR_SHADER2)
    {
        initializeBuffers(numParticles, particles, vertices, numVerts, vertStartIds, numVertices);
    };

    void updateParticleBuffer(const std::vector<glm::vec4>& particles) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        DirectX::getContext()->Map(particlesStagingBuffer.Get(), 0, D3D11_MAP_WRITE, 0, &mappedResource);
        memcpy(mappedResource.pData, particles.data(), particles.size() * sizeof(glm::vec4));
        DirectX::getContext()->Unmap(particlesStagingBuffer.Get(), 0);
        DirectX::getContext()->CopyResource(particlesBuffer.Get(), particlesStagingBuffer.Get());
    };

    void dispatch(int numWorkgroups) override
    {
        bind();
        DirectX::getContext()->Dispatch(numWorkgroups, 1, 1);
        unbind();

        // Can actually release the verticesBuffer and SRV now: used only for binding, which occurs just once.
        verticesBuffer.Reset();
        verticesSRV.Reset();
    };

    const ComPtr<ID3D11ShaderResourceView>& getParticlesSRV() const { return particlesSRV; }
    const ComPtr<ID3D11ShaderResourceView>& getVertStartIdxSRV() const { return vertStartIdxSRV; }
    const ComPtr<ID3D11ShaderResourceView>& getNumVerticesSRV() const { return numVerticesSRV; };
    const ComPtr<ID3D11ShaderResourceView>& getLocalRestPositionsSRV() const { return localRestPositionsSRV; };
    const ComPtr<ID3D11UnorderedAccessView>& getParticlesUAV() const { return particlesUAV; };
    const ComPtr<ID3D11Buffer>& getParticlesBuffer() const { return particlesBuffer; };

private:
    ComPtr<ID3D11Buffer> particlesBuffer; 
    ComPtr<ID3D11Buffer> particlesStagingBuffer;
    ComPtr<ID3D11Buffer> verticesBuffer;
    ComPtr<ID3D11Buffer> vertStartIdxBuffer;       // for each voxel (workgroup), the start index of the vertices in the vertex buffer
    ComPtr<ID3D11Buffer> numVerticesBuffer;        // for each voxel (workgroup), how many vertices are in it
    ComPtr<ID3D11Buffer> localRestPositionsBuffer; // the local (relative to voxel corner v0) rest positions of each vertex
    ComPtr<ID3D11ShaderResourceView> particlesSRV;
    ComPtr<ID3D11ShaderResourceView> verticesSRV;
    ComPtr<ID3D11ShaderResourceView> vertStartIdxSRV;
    ComPtr<ID3D11ShaderResourceView> numVerticesSRV;
    ComPtr<ID3D11ShaderResourceView> localRestPositionsSRV; // Owned by this class, but used by the transformVertices compute shader
    ComPtr<ID3D11UnorderedAccessView> localRestPositionsUAV;
    ComPtr<ID3D11UnorderedAccessView> particlesUAV; 

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { particlesSRV.Get(), verticesSRV.Get(), vertStartIdxSRV.Get(), numVerticesSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { localRestPositionsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    };

    void initializeBuffers(int numParticles, const std::vector<glm::vec4>& particles, const float* vertices, int numVerts, const std::vector<uint>& vertStartIds, const std::vector<uint>& numVertices) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    
        // Initialize particlesBuffer and its SRV and UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = numParticles * sizeof(glm::vec4); // glm::vec4 for alignment
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(glm::vec4); // Size of each element in the buffer
    
        initData.pSysMem = particles.data();
        DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &particlesBuffer);
    
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numParticles;
    
        DirectX::getDevice()->CreateShaderResourceView(particlesBuffer.Get(), &srvDesc, &particlesSRV);

        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numParticles;

        DirectX::getDevice()->CreateUnorderedAccessView(particlesBuffer.Get(), &uavDesc, &particlesUAV);

        // Create the particles staging buffer
        bufferDesc.Usage = D3D11_USAGE_STAGING;
        bufferDesc.ByteWidth = numParticles * sizeof(glm::vec4);
        bufferDesc.BindFlags = 0;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = 0;

        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &particlesStagingBuffer);

        // Initialize verticesBuffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE; // since this data is going to be set on buffer creation and never changed
        bufferDesc.ByteWidth = sizeof(float) * 3 * numVerts;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0; // No CPU access needed
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(float); // Size of each element in the buffer

        initData.pSysMem = vertices;
        DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &verticesBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numVerts * 3;

        DirectX::getDevice()->CreateShaderResourceView(verticesBuffer.Get(), &srvDesc, &verticesSRV);

        // Initialize vertStartIdxBuffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        bufferDesc.ByteWidth = sizeof(uint) * static_cast<UINT>(vertStartIds.size());
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0; // No CPU access needed
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(uint); // Size of each element in the buffer

        initData.pSysMem = vertStartIds.data();
        DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &vertStartIdxBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(vertStartIds.size());
        DirectX::getDevice()->CreateShaderResourceView(vertStartIdxBuffer.Get(), &srvDesc, &vertStartIdxSRV);

        // Initialize numVerticesBuffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        bufferDesc.ByteWidth = sizeof(uint) * static_cast<UINT>(numVertices.size());
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0; // No CPU access needed
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(uint); // Size of each element in the buffer

        initData.pSysMem = numVertices.data();
        DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &numVerticesBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(numVertices.size());
        DirectX::getDevice()->CreateShaderResourceView(numVerticesBuffer.Get(), &srvDesc, &numVerticesSRV);

        // Initialize localRestPositionsBuffer and its UAV, and an SRV (for the transformVertices compute shader)
        bufferDesc.Usage = D3D11_USAGE_DEFAULT; // Allow GPU write access
        bufferDesc.ByteWidth = numVerts * sizeof(float) * 4;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.StructureByteStride = sizeof(float) * 4; // Size of each element in the buffer (4 floats for each vertex)
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &localRestPositionsBuffer);

        uavDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffer, no format
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numVerts;

        DirectX::getDevice()->CreateUnorderedAccessView(localRestPositionsBuffer.Get(), &uavDesc, &localRestPositionsUAV);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffer, no format
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numVerts;

        DirectX::getDevice()->CreateShaderResourceView(localRestPositionsBuffer.Get(), &srvDesc, &localRestPositionsSRV);
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
        particlesBuffer.Reset();
        vertStartIdxBuffer.Reset();
        numVerticesBuffer.Reset();
        localRestPositionsBuffer.Reset();
        particlesSRV.Reset();
        vertStartIdxSRV.Reset();
        numVerticesSRV.Reset();
        localRestPositionsUAV.Reset();
    };

};