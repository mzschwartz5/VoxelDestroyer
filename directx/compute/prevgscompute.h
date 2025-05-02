#pragma once

#include "directx/compute/computeshader.h"

class PreVGSCompute : public ComputeShader
{
public:
    PreVGSCompute(
        int numParticles,
        const glm::vec4* initialOldPositions,
        const glm::vec4* initialVelocities,
        const ComPtr<ID3D11ShaderResourceView>& weightsSRV,
		const ComPtr<ID3D11UnorderedAccessView>& positionsUAV
	) : ComputeShader(IDR_SHADER5), positionsUAV(positionsUAV), weightsSRV(weightsSRV)
    {
        initializeBuffers(numParticles, initialOldPositions, initialVelocities);
    };

    const ComPtr<ID3D11ShaderResourceView>& getOldPositionsSRV() const { return oldPositionsSRV; }
    const ComPtr<ID3D11UnorderedAccessView>& getVelocitiesUAV() const { return velocitiesUAV; }

private:
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11Buffer> oldPositionsBuffer;
    ComPtr<ID3D11Buffer> velocitiesBuffer;
    ComPtr<ID3D11ShaderResourceView> weightsSRV;
    ComPtr<ID3D11ShaderResourceView> oldPositionsSRV;
    ComPtr<ID3D11UnorderedAccessView> oldPositionsUAV;
    ComPtr<ID3D11UnorderedAccessView> velocitiesUAV;


    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { weightsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get(), oldPositionsUAV.Get(), velocitiesUAV.Get() };
		DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    };

    void initializeBuffers(int numParticles, const glm::vec4* initialOldPositions, const glm::vec4* initialVelocities) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};

        // Create oldPositions buffer and its SRV and UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = numParticles * sizeof(glm::vec4);
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(glm::vec4); // Size of each element in the buffer

        initData.pSysMem = initialOldPositions; 
        DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &oldPositionsBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffer, no format
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numParticles;
        DirectX::getDevice()->CreateShaderResourceView(oldPositionsBuffer.Get(), &srvDesc, &oldPositionsSRV);

        uavDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffer, no format
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numParticles;

        DirectX::getDevice()->CreateUnorderedAccessView(oldPositionsBuffer.Get(), &uavDesc, &oldPositionsUAV);

        // Create velocities buffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = numParticles * sizeof(glm::vec4);
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(glm::vec4); // Size of each element in the buffer

        initData.pSysMem = initialVelocities;
        DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &velocitiesBuffer);

        uavDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffer, no format
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numParticles;
        DirectX::getDevice()->CreateUnorderedAccessView(velocitiesBuffer.Get(), &uavDesc, &velocitiesUAV);
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
        oldPositionsBuffer->Release();
        oldPositionsSRV->Release();
        velocitiesBuffer->Release();
        velocitiesUAV->Release();
    };

};