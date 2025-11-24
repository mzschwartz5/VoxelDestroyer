#pragma once
#include "directx/compute/computeshader.h"

struct PreVGSConstantBuffer {
    float gravityStrength;
    float groundCollisionY;
    float timeStep;
    int numParticles;
    float massLow;
    float massHigh;
    int padding0;
    int padding1;
};

class PreVGSCompute : public ComputeShader
{
public:
    PreVGSCompute() = default;

    PreVGSCompute(
        int numParticles,
        const PreVGSConstantBuffer& simConstants
	) : ComputeShader(IDR_SHADER5), simConstants(simConstants)
    {
        // This shader has a second entry point for updating particle weights from paint data.
        loadEntryPoint(updateParticleWeightsEntryPoint);
        initializeBuffers(numParticles);
    };

    void dispatch() override {
        ComputeShader::dispatch(numWorkgroups);
    }

    void updateParticleMassFromPaintValues(
        const ComPtr<ID3D11UnorderedAccessView>& paintDeltaUAV, 
        const ComPtr<ID3D11UnorderedAccessView>& paintValueUAV,
        float massLow, 
        float massHigh
    ) {
        this->paintDeltaUAV = paintDeltaUAV;
        this->paintValueUAV = paintValueUAV;
        
        if (massLow != simConstants.massLow || massHigh != simConstants.massHigh) {
            simConstants.massLow = massLow;
            simConstants.massHigh = massHigh;
            DirectX::updateConstantBuffer(simConstantsBuffer, simConstants);
        }

        ComputeShader::dispatch(numWorkgroups, updateParticleWeightsEntryPoint);
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
    inline static const std::string updateParticleWeightsEntryPoint = "updateParticleMassFromPaint";
    int numWorkgroups;
    PreVGSConstantBuffer simConstants;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11UnorderedAccessView> oldPositionsUAV;
    ComPtr<ID3D11ShaderResourceView> isDraggingSRV;
    ComPtr<ID3D11Buffer> simConstantsBuffer; //gravity on, ground on, ground collision y, padding
    ComPtr<ID3D11UnorderedAccessView> paintDeltaUAV;  // Only used during update from paint values
    ComPtr<ID3D11UnorderedAccessView> paintValueUAV;  // Only used during update from paint values

    void bind() override
    {
        ID3D11ShaderResourceView* srvs[] = { isDraggingSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get(), oldPositionsUAV.Get(), paintDeltaUAV.Get(), paintValueUAV.Get() };
		DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbvs[] = { simConstantsBuffer.Get() };
		DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbvs[] = { nullptr };
		DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(int numParticles) {
        numWorkgroups = Utils::divideRoundUp(numParticles, VGS_THREADS);
        simConstantsBuffer = DirectX::createConstantBuffer<PreVGSConstantBuffer>(simConstants);
    }
};