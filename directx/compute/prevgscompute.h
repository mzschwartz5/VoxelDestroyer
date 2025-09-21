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
        const PreVGSConstantBuffer& simConstants
	) : ComputeShader(IDR_SHADER5)
    {
        initializeBuffers(numParticles, simConstants);
    };

    void updateSimConstants(const PreVGSConstantBuffer& newCB) {
        updateConstantBuffer(simConstantsBuffer, newCB);
    }

    void dispatch() override {
        ComputeShader::dispatch(numWorkgroups);
    }

    void setPositionsUAV(const ComPtr<ID3D11UnorderedAccessView>& positionsUAV) {
        this->positionsUAV = positionsUAV;
    }

    void setOldPositionsUAV(const ComPtr<ID3D11UnorderedAccessView>& oldPositionsUAV) {
        this->oldPositionsUAV = oldPositionsUAV;
    }

    void setIsDraggingSRV(const ComPtr<ID3D11ShaderResourceView>& isDraggingSRV) {
        this->isDraggingSRV = isDraggingSRV;
    }

private:
    int numWorkgroups;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11UnorderedAccessView> oldPositionsUAV;
    ComPtr<ID3D11ShaderResourceView> isDraggingSRV;
    ComPtr<ID3D11Buffer> simConstantsBuffer; //gravity on, ground on, ground collision y, padding

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { isDraggingSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get(), oldPositionsUAV.Get() };
		DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbvs[] = { simConstantsBuffer.Get() };
		DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(nullptr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbvs[] = { nullptr };
		DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(int numParticles, const PreVGSConstantBuffer& simConstants) {
        numWorkgroups = Utils::divideRoundUp(numParticles, VGS_THREADS);
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};

		// Create simConstants buffer
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		bufferDesc.ByteWidth = sizeof(PreVGSConstantBuffer);
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = 0;
		initData.pSysMem = &simConstants;
		CreateBuffer(&bufferDesc, &initData, &simConstantsBuffer);
    }
};