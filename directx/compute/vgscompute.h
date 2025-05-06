#pragma once

#include "directx/compute/computeshader.h"
#include "glm.hpp"

class VGSCompute : public ComputeShader
{
public:
    VGSCompute(
        int numPositions,
        const float* weights,
        std::array<glm::vec4, 2> voxelSimInfo,
		const ComPtr<ID3D11UnorderedAccessView>& positionsUAV
	) : ComputeShader(IDR_SHADER3), positionsUAV(positionsUAV)
    {
        // TODO: weights should just be the unused 4th float component of the particle positions
        initializeBuffers(numPositions, weights, voxelSimInfo);
    };

    void dispatch(int numWorkgroups) override
    {
        bind();
        DirectX::getContext()->Dispatch(numWorkgroups, 1, 1);
        unbind();
    };

    void updateVoxelSimInfo(std::array<glm::vec4, 2> newInfo) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX::getContext()->Map(voxelSimInfoBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr))
        {
            // Copy the flag to the constant buffer
            memcpy(mappedResource.pData, &newInfo, sizeof(std::array<glm::vec4, 2>));
            DirectX::getContext()->Unmap(voxelSimInfoBuffer.Get(), 0);
        }
        else
        {
            MGlobal::displayError("Failed to map constant buffer.");
        }
    }

    const ComPtr<ID3D11ShaderResourceView>& getWeightsSRV() const { return weightsSRV; }
    const ComPtr<ID3D11Buffer>& getVoxelSimInfoBuffer() const { return voxelSimInfoBuffer; }

private:
    ComPtr<ID3D11Buffer> voxelSimInfoBuffer;
    ComPtr<ID3D11Buffer> weightsBuffer;
    ComPtr<ID3D11ShaderResourceView> weightsSRV;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    
    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = {  weightsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get() };
		DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { voxelSimInfoBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(int numParticles, const float* weights, std::array<glm::vec4, 2> voxelSimInfo) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

        // Initialize weights buffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        bufferDesc.ByteWidth = numParticles * sizeof(float); // Float for weights
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(float);

        initData.pSysMem = weights;
        DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &weightsBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numParticles;

        DirectX::getDevice()->CreateShaderResourceView(weightsBuffer.Get(), &srvDesc, &weightsSRV);

        //Initialize constant buffer
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC; // Dynamic for CPU updates
        bufferDesc.ByteWidth = sizeof(std::array<glm::vec4, 2>);  // Size of the constant buffer
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; // Bind as a constant buffer
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // Allow CPU writes
        bufferDesc.MiscFlags = 0;
        initData.pSysMem = voxelSimInfo.data();
        HRESULT hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &voxelSimInfoBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create constant buffer.");
        }
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
        weightsBuffer.Reset();
        weightsSRV.Reset();
        voxelSimInfoBuffer.Reset();
    };

};