#pragma once

#include "directx/compute/computeshader.h"

struct DeformVerticesConstantBuffer {
    float inverseWorldMatrix[4][4];
    int vertexCount;
    int padding[3]; // Padding to align to 16 bytes
};

class DeformVerticesCompute : public ComputeShader
{
public:
    DeformVerticesCompute() = default;
    DeformVerticesCompute(
        int numParticles,
        int vertexCount,
        const MMatrix& inverseWorldMatrix,
        const MFloatPoint* originalParticlePositions,   // Will be uploaded to GPU
        const std::vector<uint>& vertexVoxelIds,      // Will be uploaded to GPU
        const ComPtr<ID3D11UnorderedAccessView>& positionsUAV,
        const ComPtr<ID3D11UnorderedAccessView>& normalsUAV,
        const ComPtr<ID3D11ShaderResourceView>& originalVertPositionsSRV,
        const ComPtr<ID3D11ShaderResourceView>& originalNormalsSRV,
        const ComPtr<ID3D11ShaderResourceView>& particlePositionsSRV
    ) : ComputeShader(IDR_SHADER1), positionsUAV(positionsUAV), normalsUAV(normalsUAV), originalVertPositionsSRV(originalVertPositionsSRV), originalNormalsSRV(originalNormalsSRV), particlePositionsSRV(particlePositionsSRV)
    {
        initializeBuffers(numParticles, vertexCount, inverseWorldMatrix, originalParticlePositions, vertexVoxelIds);
    }

    void dispatch() override
    {
        ComputeShader::dispatch(numWorkgroups);
    };

    void setParticlePositionsSRV(const ComPtr<ID3D11ShaderResourceView>& srv)
    {
        particlePositionsSRV = srv;
    };

private:
    int numWorkgroups = 0;

    // Inputs
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11UnorderedAccessView> normalsUAV;
    ComPtr<ID3D11ShaderResourceView> originalVertPositionsSRV;
    ComPtr<ID3D11ShaderResourceView> originalNormalsSRV;
    ComPtr<ID3D11ShaderResourceView> particlePositionsSRV;

    // Created and owned by this class
    ComPtr<ID3D11Buffer> originalParticlePositionsBuffer;
    ComPtr<ID3D11ShaderResourceView> originalParticlePositionsSRV;

    ComPtr<ID3D11Buffer> vertexVoxelIdsBuffer;
    ComPtr<ID3D11ShaderResourceView> vertexVoxelIdsSRV;

    ComPtr<ID3D11Buffer> constantsBuffer;
    
    void bind() override
    {
        // In case Maya left either the position or normal vertex buffer bound to the IA stage, unbind them here.
        ID3D11Buffer* nullVBs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        UINT zeroStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        UINT zeroOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        DirectX::getContext()->IASetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, nullVBs, zeroStrides, zeroOffsets);

        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { originalVertPositionsSRV.Get(), originalNormalsSRV.Get(), originalParticlePositionsSRV.Get(), particlePositionsSRV.Get(), vertexVoxelIdsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get(), normalsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* constBuffers[] = { constantsBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(constBuffers), constBuffers);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(nullptr, NULL, 0);

        ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(nullSRVs), nullSRVs);

        ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

        ID3D11Buffer* nullConstBuffers[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(nullConstBuffers), nullConstBuffers);
    };

    void initializeBuffers(int numParticles, int vertexCount, const MMatrix& inverseWorldMatrix, const MFloatPoint* originalParticlePositions, const std::vector<uint>& vertexVoxelIds)
    {
        numWorkgroups = Utils::divideRoundUp(vertexCount, DEFORM_VERTICES_THREADS);
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};

        // We only need one reference particle per voxel, not the whole shebang
        std::vector<MFloatPoint> reducedOriginalParticles;
        reducedOriginalParticles.reserve(numParticles / 8);
        for (int i = 0; i < numParticles; i += 8) {
            reducedOriginalParticles.push_back(originalParticlePositions[i]);
        }

        int reducedNumParticles = static_cast<int>(reducedOriginalParticles.size());

        // Create originalParticlePositions buffer and SRV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = reducedNumParticles * sizeof(MFloatPoint);
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(MFloatPoint);

        initData.pSysMem = reducedOriginalParticles.data();
        CreateBuffer(&bufferDesc, &initData, originalParticlePositionsBuffer.GetAddressOf());

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = reducedNumParticles;
        DirectX::getDevice()->CreateShaderResourceView(originalParticlePositionsBuffer.Get(), &srvDesc, originalParticlePositionsSRV.GetAddressOf());

        // Create vertexVoxelIds buffer and SRV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = vertexVoxelIds.size() * sizeof(uint);
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(uint);

        initData.pSysMem = vertexVoxelIds.data();
        CreateBuffer(&bufferDesc, &initData, vertexVoxelIdsBuffer.GetAddressOf());

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = vertexVoxelIds.size();
        DirectX::getDevice()->CreateShaderResourceView(vertexVoxelIdsBuffer.Get(), &srvDesc, vertexVoxelIdsSRV.GetAddressOf());

        // Make a constants buffer for vertexCount
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        bufferDesc.ByteWidth = static_cast<UINT>(sizeof(DeformVerticesConstantBuffer)); // Must be multiple of 16 bytes
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = 0;
        
        DeformVerticesConstantBuffer constants = {};
        inverseWorldMatrix.get(constants.inverseWorldMatrix);
        constants.vertexCount = vertexCount;
        initData.pSysMem = &constants;

        CreateBuffer(&bufferDesc, &initData, &constantsBuffer);
    }

};