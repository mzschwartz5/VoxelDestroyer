#pragma once

#include "directx/compute/computeshader.h"
#include "constants.h"
#include <array>

struct CollisionVolume
{
    std::array<float, 3> gridMin;    
    std::array<int, 3> gridDims;
    std::array<float, 3> gridInvCellDims;
};

// Must be 16-byte aligned.
struct CollisionGridConstantBuffer
{
    std::array<float, 3> gridMin;
    float voxelSize;
    std::array<int, 3> gridDims;
    float padding0 = 0.0f;
    std::array<float, 3> gridInvCellDims;
    float padding1 = 0.0f;
};

class BuildCollisionGridCompute : public ComputeShader
{
public:
    BuildCollisionGridCompute(
        const CollisionVolume& collisionVolume,
        float voxelSize,
        ComPtr<ID3D11ShaderResourceView> positionsSRV,
        const std::vector<uint>& isSurface
    ) : ComputeShader(IDR_SHADER8), positionsSRV(positionsSRV)
    {
        initializeBuffers(isSurface, collisionVolume, voxelSize);
    };

    void resetCollisionBuffers() {
        // See docs: 4 values are required even though only the first will be used, in our case.
        UINT clearValues[4] = { 0, 0, 0, 0 };
        DirectX::getContext()->ClearUnorderedAccessViewUint(collisionVoxelCountsUAV.Get(), clearValues);
        DirectX::getContext()->ClearUnorderedAccessViewUint(collisionVoxelIndicesUAV.Get(), clearValues);
    }

private:
    ComPtr<ID3D11ShaderResourceView> positionsSRV;
    ComPtr<ID3D11Buffer> isSurfaceBuffer;
    ComPtr<ID3D11Buffer> collisionVoxelCountsBuffer;
    ComPtr<ID3D11Buffer> collisionVoxelIndicesBuffer;
    ComPtr<ID3D11Buffer> constantBuffer;
    ComPtr<ID3D11ShaderResourceView> isSurfaceSRV;
    ComPtr<ID3D11ShaderResourceView> collisionVoxelCountsSRV;
    ComPtr<ID3D11ShaderResourceView> collisionVoxelIndicesSRV;
    ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV;
    ComPtr<ID3D11UnorderedAccessView> collisionVoxelCountsUAV;
    ComPtr<ID3D11UnorderedAccessView> collisionVoxelIndicesUAV;

    void bind() override {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { positionsSRV.Get(), isSurfaceSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { collisionVoxelCountsUAV.Get(), collisionVoxelIndicesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { constantBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };
    
    void unbind() override {
        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };
    
    void initializeBuffers(const std::vector<uint>& isSurface, const CollisionVolume& collisionVolume, float voxelSize) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};
        int numVoxels = static_cast<int>(isSurface.size());
        int numGridCells = collisionVolume.gridDims[0] * collisionVolume.gridDims[1] * collisionVolume.gridDims[2];

        // Create the isSurface buffer and its SRV and UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(uint) * numVoxels;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(uint);

        initData.pSysMem = isSurface.data();

        HRESULT hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &isSurfaceBuffer);
        
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numVoxels;
        DirectX::getDevice()->CreateShaderResourceView(isSurfaceBuffer.Get(), &srvDesc, &isSurfaceSRV);

        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numVoxels;
        DirectX::getDevice()->CreateUnorderedAccessView(isSurfaceBuffer.Get(), &uavDesc, &isSurfaceUAV);

        // Create the collisionVoxelCounts buffer and its SRV and UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(int) * numGridCells;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(int);

        hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &collisionVoxelCountsBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numGridCells;
        DirectX::getDevice()->CreateShaderResourceView(collisionVoxelCountsBuffer.Get(), &srvDesc, &collisionVoxelCountsSRV);

        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numGridCells;
        DirectX::getDevice()->CreateUnorderedAccessView(collisionVoxelCountsBuffer.Get(), &uavDesc, &collisionVoxelCountsUAV);

        // Create the collisionVoxelIndices buffer and its SRV and UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(int) * numGridCells * MAX_VOXELS_PER_CELL;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(int);

        hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &collisionVoxelIndicesBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numGridCells * MAX_VOXELS_PER_CELL;

        DirectX::getDevice()->CreateShaderResourceView(collisionVoxelIndicesBuffer.Get(), &srvDesc, &collisionVoxelIndicesSRV);

        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numGridCells * MAX_VOXELS_PER_CELL;
        DirectX::getDevice()->CreateUnorderedAccessView(collisionVoxelIndicesBuffer.Get(), &uavDesc, &collisionVoxelIndicesUAV);

        // Create CBV for the collision volume and voxel size
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(CollisionGridConstantBuffer);
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = 0; // No need to write to this buffer, stays constant throughout sim.
        bufferDesc.MiscFlags = 0;

        CollisionGridConstantBuffer cbData;
        cbData.gridMin = collisionVolume.gridMin;
        cbData.gridDims = collisionVolume.gridDims;
        cbData.gridInvCellDims = collisionVolume.gridInvCellDims;
        cbData.voxelSize = voxelSize;

        initData.pSysMem = &cbData;

        hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &constantBuffer);

        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create constant buffer for collision grid.");
            return;
        }
    }

    void tearDown() override
    {
        ComputeShader::tearDown();

        isSurfaceBuffer.Reset();
        collisionVoxelCountsBuffer.Reset();
        collisionVoxelIndicesBuffer.Reset();
        constantBuffer.Reset();

        isSurfaceSRV.Reset();
        collisionVoxelCountsSRV.Reset();
        collisionVoxelIndicesSRV.Reset();

        isSurfaceUAV.Reset();
        collisionVoxelCountsUAV.Reset();
        collisionVoxelIndicesUAV.Reset();
    };

};