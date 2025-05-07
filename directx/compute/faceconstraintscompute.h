#pragma once

#include "directx/compute/computeshader.h"
#include "glm.hpp"

struct FaceConstraints {
    std::vector<int> voxelOneIdx;
    std::vector<int> voxelTwoIdx;
    std::vector<float> tensionLimit;
    std::vector<float> compressionLimit;
};

class FaceConstraintsCompute : public ComputeShader
{
public:
    FaceConstraintsCompute(
        const std::array<FaceConstraints, 3>& constraints,
        const ComPtr<ID3D11UnorderedAccessView>& positionsUAV,
        const ComPtr<ID3D11ShaderResourceView>& weightsSRV,
        const ComPtr<ID3D11Buffer>& voxelSimInfoBuffer
    ) : ComputeShader(IDR_SHADER4), positionsUAV(positionsUAV), weightsSRV(weightsSRV), voxelSimInfoBuffer(voxelSimInfoBuffer)
    {
        initializeBuffers(constraints);
    };

    void dispatch(int numWorkgroups) override
    {
        bind();
        DirectX::getContext()->Dispatch(numWorkgroups, 1, 1);
        unbind();
    };

    const ComPtr<ID3D11ShaderResourceView>& getWeightsSRV() const { return weightsSRV; }

    // Update tension and compression limits for a specific axis
    void updateLimits(int axisIndex, const std::vector<float>& tensionLimit, const std::vector<float>& compressionLimit) {
        ComPtr<ID3D11Buffer> tensionBuffer;
        ComPtr<ID3D11Buffer> compressionBuffer;

        // Select the appropriate buffer based on the axis index
        switch (axisIndex) {
        case 0: // X axis
            tensionBuffer = xTensionLimitBuffer;
            compressionBuffer = xCompressionLimitBuffer;
            break;
        case 1: // Y axis
            tensionBuffer = yTensionLimitBuffer;
            compressionBuffer = yCompressionLimitBuffer;
            break;
        case 2: // Z axis
            tensionBuffer = zTensionLimitBuffer;
            compressionBuffer = zCompressionLimitBuffer;
            break;
        default:
            MGlobal::displayError("Invalid axis index for updating constraints.");
            return;
        }

        // Update the buffer contents
        updateBuffer(tensionBuffer, tensionLimit);
        updateBuffer(compressionBuffer, compressionLimit);
    }

private:
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11ShaderResourceView> weightsSRV;

    ComPtr<ID3D11UnorderedAccessView> xVoxelOneIdxUAV;
    ComPtr<ID3D11Buffer> xVoxelOneIdxBuffer;
    ComPtr<ID3D11UnorderedAccessView> xVoxelTwoIdxUAV;
    ComPtr<ID3D11Buffer> xVoxelTwoIdxBuffer;
    ComPtr<ID3D11ShaderResourceView> xTensionLimitSRV;
    ComPtr<ID3D11Buffer> xTensionLimitBuffer;
    ComPtr<ID3D11ShaderResourceView> xCompressionLimitSRV;
    ComPtr<ID3D11Buffer> xCompressionLimitBuffer;

    ComPtr<ID3D11UnorderedAccessView> yVoxelOneIdxUAV;
    ComPtr<ID3D11Buffer> yVoxelOneIdxBuffer;
    ComPtr<ID3D11UnorderedAccessView> yVoxelTwoIdxUAV;
    ComPtr<ID3D11Buffer> yVoxelTwoIdxBuffer;
    ComPtr<ID3D11ShaderResourceView> yTensionLimitSRV;
    ComPtr<ID3D11Buffer> yTensionLimitBuffer;
    ComPtr<ID3D11ShaderResourceView> yCompressionLimitSRV;
    ComPtr<ID3D11Buffer> yCompressionLimitBuffer;

    ComPtr<ID3D11UnorderedAccessView> zVoxelOneIdxUAV;
    ComPtr<ID3D11Buffer> zVoxelOneIdxBuffer;
    ComPtr<ID3D11UnorderedAccessView> zVoxelTwoIdxUAV;
    ComPtr<ID3D11Buffer> zVoxelTwoIdxBuffer;
    ComPtr<ID3D11ShaderResourceView> zTensionLimitSRV;
    ComPtr<ID3D11Buffer> zTensionLimitBuffer;
    ComPtr<ID3D11ShaderResourceView> zCompressionLimitSRV;
    ComPtr<ID3D11Buffer> zCompressionLimitBuffer;

    ComPtr<ID3D11Buffer> voxelSimInfoBuffer;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = {
            weightsSRV.Get(),
            xTensionLimitSRV.Get(), xCompressionLimitSRV.Get(),
            yTensionLimitSRV.Get(), yCompressionLimitSRV.Get(),
            zTensionLimitSRV.Get(), zCompressionLimitSRV.Get()
        };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = {
            positionsUAV.Get(),
            xVoxelOneIdxUAV.Get(), xVoxelTwoIdxUAV.Get(),
            yVoxelOneIdxUAV.Get(), yVoxelTwoIdxUAV.Get(),
            zVoxelOneIdxUAV.Get(), zVoxelTwoIdxUAV.Get()
        };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { voxelSimInfoBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(const std::array<FaceConstraints, 3>& constraints) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};

        // Initialize X voxel one idx buffer and its UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = UINT(sizeof(int) * constraints[0].voxelOneIdx.size());
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(int);
        initData.pSysMem = constraints[0].voxelOneIdx.data();
        HRESULT hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &xVoxelOneIdxBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create constraints buffer.");
        }

        // Create the UAV for the X voxel one idx buffer
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = UINT(constraints[0].voxelOneIdx.size());
        hr = DirectX::getDevice()->CreateUnorderedAccessView(xVoxelOneIdxBuffer.Get(), &uavDesc, &xVoxelOneIdxUAV);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create X voxel one idx UAV.");
        }

        // Initialize X voxel two idx buffer and its UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = UINT(sizeof(int) * constraints[0].voxelTwoIdx.size());
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(int);
        initData.pSysMem = constraints[0].voxelTwoIdx.data();
        hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &xVoxelTwoIdxBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create constraints buffer.");
        }

        // Create the UAV for the X voxel two idx buffer
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = UINT(constraints[0].voxelTwoIdx.size());
        hr = DirectX::getDevice()->CreateUnorderedAccessView(xVoxelTwoIdxBuffer.Get(), &uavDesc, &xVoxelTwoIdxUAV);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create X voxel two idx UAV.");
        }

        // Create tension and compression SRVs for X axis
        createTensionCompressionBuffers(
            constraints[0].tensionLimit,
            constraints[0].compressionLimit,
            xTensionLimitBuffer,
            xTensionLimitSRV,
            xCompressionLimitBuffer,
            xCompressionLimitSRV
        );

        // Initialize Y voxel one idx buffer and its UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = UINT(sizeof(int) * constraints[1].voxelOneIdx.size());
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(int);
        initData.pSysMem = constraints[1].voxelOneIdx.data();
        hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &yVoxelOneIdxBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create constraints buffer.");
        }

        // Create the UAV for the Y voxel one idx buffer
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = UINT(constraints[1].voxelOneIdx.size());
        hr = DirectX::getDevice()->CreateUnorderedAccessView(yVoxelOneIdxBuffer.Get(), &uavDesc, &yVoxelOneIdxUAV);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create Y voxel one idx UAV.");
        }

        // Initialize Y voxel two idx buffer and its UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = UINT(sizeof(int) * constraints[1].voxelTwoIdx.size());
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(int);
        initData.pSysMem = constraints[1].voxelTwoIdx.data();
        hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &yVoxelTwoIdxBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create constraints buffer.");
        }

        // Create the UAV for the Y voxel two idx buffer
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = UINT(constraints[1].voxelTwoIdx.size());
        hr = DirectX::getDevice()->CreateUnorderedAccessView(yVoxelTwoIdxBuffer.Get(), &uavDesc, &yVoxelTwoIdxUAV);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create Y voxel two idx UAV.");
        }

        // Create tension and compression SRVs for Y axis
        createTensionCompressionBuffers(
            constraints[1].tensionLimit,
            constraints[1].compressionLimit,
            yTensionLimitBuffer,
            yTensionLimitSRV,
            yCompressionLimitBuffer,
            yCompressionLimitSRV
        );

        // Initialize Z voxel one idx buffer and its UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = UINT(sizeof(int) * constraints[2].voxelOneIdx.size());
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(int);
        initData.pSysMem = constraints[2].voxelOneIdx.data();
        hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &zVoxelOneIdxBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create constraints buffer.");
        }

        // Create the UAV for the Z voxel one idx buffer
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = UINT(constraints[2].voxelOneIdx.size());
        hr = DirectX::getDevice()->CreateUnorderedAccessView(zVoxelOneIdxBuffer.Get(), &uavDesc, &zVoxelOneIdxUAV);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create Z voxel one idx UAV.");
        }

        // Initialize Z voxel two idx buffer and its UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = UINT(sizeof(int) * constraints[2].voxelTwoIdx.size());
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(int);
        initData.pSysMem = constraints[2].voxelTwoIdx.data();
        hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &zVoxelTwoIdxBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create constraints buffer.");
        }

        // Create the UAV for the Z voxel two idx buffer
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = UINT(constraints[2].voxelTwoIdx.size());
        hr = DirectX::getDevice()->CreateUnorderedAccessView(zVoxelTwoIdxBuffer.Get(), &uavDesc, &zVoxelTwoIdxUAV);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create Z voxel two idx UAV.");
        }

        // Create tension and compression SRVs for Z axis
        createTensionCompressionBuffers(
            constraints[2].tensionLimit,
            constraints[2].compressionLimit,
            zTensionLimitBuffer,
            zTensionLimitSRV,
            zCompressionLimitBuffer,
            zCompressionLimitSRV
        );
    }

    void createTensionCompressionBuffers(
        const std::vector<float>& tensionLimit,
        const std::vector<float>& compressionLimit,
        ComPtr<ID3D11Buffer>& tensionBuffer,
        ComPtr<ID3D11ShaderResourceView>& tensionSRV,
        ComPtr<ID3D11Buffer>& compressionBuffer,
        ComPtr<ID3D11ShaderResourceView>& compressionSRV
    ) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};
        HRESULT hr;

        // Initialize tension limit buffer
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = UINT(sizeof(float) * tensionLimit.size());
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(float);
        initData.pSysMem = tensionLimit.data();
        hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &tensionBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create tension limit buffer.");
        }

        // Create the SRV for the tension limit buffer
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = UINT(tensionLimit.size());
        hr = DirectX::getDevice()->CreateShaderResourceView(tensionBuffer.Get(), &srvDesc, &tensionSRV);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create tension limit SRV.");
        }

        // Initialize compression limit buffer
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = UINT(sizeof(float) * compressionLimit.size());
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(float);
        initData.pSysMem = compressionLimit.data();
        hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &compressionBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create compression limit buffer.");
        }

        // Create the SRV for the compression limit buffer
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = UINT(compressionLimit.size());
        hr = DirectX::getDevice()->CreateShaderResourceView(compressionBuffer.Get(), &srvDesc, &compressionSRV);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create compression limit SRV.");
        }
    }


    void updateBuffer(ComPtr<ID3D11Buffer>& buffer, const std::vector<float>& data) {
        D3D11_BUFFER_DESC desc = {};
        buffer->GetDesc(&desc);

        // Create a staging buffer
        D3D11_BUFFER_DESC stagingDesc = {};
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.ByteWidth = desc.ByteWidth;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        stagingDesc.StructureByteStride = sizeof(float);
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Buffer> stagingBuffer;
        HRESULT hr = DirectX::getDevice()->CreateBuffer(&stagingDesc, nullptr, &stagingBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create staging buffer for updating constraints.");
            return;
        }

        // Map the staging buffer and copy the new data
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = DirectX::getContext()->Map(stagingBuffer.Get(), 0, D3D11_MAP_WRITE, 0, &mappedResource);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to map staging buffer for updating constraints.");
            return;
        }

        memcpy(mappedResource.pData, data.data(), sizeof(float) * data.size());
        DirectX::getContext()->Unmap(stagingBuffer.Get(), 0);

        // Copy from staging buffer to the actual buffer
        DirectX::getContext()->CopyResource(buffer.Get(), stagingBuffer.Get());
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
        xCompressionLimitBuffer.Reset();
        xCompressionLimitSRV.Reset();
        xTensionLimitBuffer.Reset();
        xTensionLimitSRV.Reset();
        xVoxelOneIdxBuffer.Reset();
        xVoxelOneIdxUAV.Reset();
        xVoxelTwoIdxBuffer.Reset();
        xVoxelTwoIdxUAV.Reset();
        yCompressionLimitBuffer.Reset();
        yCompressionLimitSRV.Reset();
        yTensionLimitBuffer.Reset();
        yTensionLimitSRV.Reset();
        yVoxelOneIdxBuffer.Reset();
        yVoxelOneIdxUAV.Reset();
        yVoxelTwoIdxBuffer.Reset();
        yVoxelTwoIdxUAV.Reset();
        zCompressionLimitBuffer.Reset();
        zCompressionLimitSRV.Reset();
        zTensionLimitBuffer.Reset();
        zTensionLimitSRV.Reset();
        zVoxelOneIdxBuffer.Reset();
        zVoxelOneIdxUAV.Reset();
        zVoxelTwoIdxBuffer.Reset();
        zVoxelTwoIdxUAV.Reset();
    };
};