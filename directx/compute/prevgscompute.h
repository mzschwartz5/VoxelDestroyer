#pragma once
#include "directx/compute/computeshader.h"

struct PreVGSConstantBuffer {
    float gravityStrength;
    float groundCollisionY;
    float timeStep;
    int numParticles;
};

class PreVGSCompute : public ComputeShader
{
public:
    PreVGSCompute() = default;

    PreVGSCompute(
        int numParticles,
        const glm::vec4* initialOldPositions,
        const PreVGSConstantBuffer& simConstants,
        const ComPtr<ID3D11ShaderResourceView>& weightsSRV,
        const ComPtr<ID3D11UnorderedAccessView>& isDraggingUAV
	) : ComputeShader(IDR_SHADER5), weightsSRV(weightsSRV), isDraggingUAV(isDraggingUAV)
    {
        initializeBuffers(numParticles, initialOldPositions, simConstants);
    };

    void updateSimConstants(const PreVGSConstantBuffer& newCB) {
        updateConstantBuffer(simConstantsBuffer, newCB);
    }

    const ComPtr<ID3D11ShaderResourceView>& getOldPositionsSRV() const { return oldPositionsSRV; }

    void dispatch() override {
        ComputeShader::dispatch(numWorkgroups);
    }

    void setPositionsUAV(const ComPtr<ID3D11UnorderedAccessView>& positionsUAV) {
        this->positionsUAV = positionsUAV;
    }

private:
    int numWorkgroups;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11Buffer> oldPositionsBuffer;
    ComPtr<ID3D11ShaderResourceView> weightsSRV;
    ComPtr<ID3D11ShaderResourceView> oldPositionsSRV;
    ComPtr<ID3D11UnorderedAccessView> oldPositionsUAV;
    ComPtr<ID3D11UnorderedAccessView> isDraggingUAV;
    ComPtr<ID3D11Buffer> simConstantsBuffer; //gravity on, ground on, ground collision y, padding

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { weightsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get(), oldPositionsUAV.Get(), isDraggingUAV.Get() };
		DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbvs[] = { simConstantsBuffer.Get() };
		DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbvs[] = { nullptr };
		DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(int numParticles, const glm::vec4* initialOldPositions, const PreVGSConstantBuffer& simConstants) {
        numWorkgroups = Utils::divideRoundUp(numParticles, VGS_THREADS);
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
        CreateBuffer(&bufferDesc, &initData, &oldPositionsBuffer);

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

		// Create simConstants buffer
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.ByteWidth = sizeof(glm::vec4);
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = 0;
		initData.pSysMem = &simConstants;
		CreateBuffer(&bufferDesc, &initData, &simConstantsBuffer);

    }
};