#pragma once
#include "computeshader.h"
#include "../glm/glm.hpp"

class BindVerticesCompute : public ComputeShader
{
public:
    BindVerticesCompute(
        const ComPtr<ID3D11ShaderResourceView>& particlesSRV, 
        const float* vertices, 
        int numVerts,
        const std::vector<uint>& vertStartIds,
        const std::vector<uint>& numVertices
    ) : ComputeShader(IDR_SHADER2), particlesSRV(particlesSRV) 
    {
        initializeBuffers(vertices, numVerts, vertStartIds, numVertices);
    };

    // Maybe we should be providing a UAV instead of a buffer?
    const ComPtr<ID3D11Buffer>& getLocalRestPositionsBuffer() const { return localRestPositionsBuffer; };

private:
    ComPtr<ID3D11Buffer> verticesBuffer;
    ComPtr<ID3D11Buffer> vertStartIdxBuffer;       // for each voxel (workgroup), the start index of the vertices in the vertex buffer
    ComPtr<ID3D11Buffer> numVerticesBuffer;        // for each voxel (workgroup), how many vertices are in it
    ComPtr<ID3D11Buffer> localRestPositionsBuffer; // the local (relative to voxel corner v0) rest positions of each vertex
    ComPtr<ID3D11ShaderResourceView> particlesSRV;
    ComPtr<ID3D11ShaderResourceView> verticesSRV;
    ComPtr<ID3D11ShaderResourceView> vertStartIdxUAV;
    ComPtr<ID3D11ShaderResourceView> numVerticesUAV;
    ComPtr<ID3D11UnorderedAccessView> localRestPositionsUAV;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { particlesSRV.Get(), verticesSRV.Get(), vertStartIdxUAV.Get(), numVerticesUAV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { localRestPositionsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

    };

    void initializeBuffers(const float* vertices, int numVerts, const std::vector<uint>& vertStartIds, const std::vector<uint>& numVertices) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

        // Initialize verticesBuffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE; // since this data is going to be set on buffer creation and never changed
        bufferDesc.ByteWidth = sizeof(float) * 4 * numVerts;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0; // No CPU access needed
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(float) * 4; // Size of each element in the buffer

        initData.pSysMem = vertices;
        DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &verticesBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numVerts;

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
        DirectX::getDevice()->CreateShaderResourceView(vertStartIdxBuffer.Get(), &srvDesc, &vertStartIdxUAV);

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
        DirectX::getDevice()->CreateShaderResourceView(numVerticesBuffer.Get(), &srvDesc, &numVerticesUAV);

        // Initialize localRestPositionsBuffer and its UAV (Structured Buffer)
        bufferDesc.Usage = D3D11_USAGE_DEFAULT; // Allow GPU write access
        bufferDesc.ByteWidth = numVerts * sizeof(float) * 4;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.StructureByteStride = sizeof(float) * 4; // Size of each element in the buffer (4 floats for each vertex)
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &localRestPositionsBuffer);

        uavDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffer, no format
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numVerts;
    }


};