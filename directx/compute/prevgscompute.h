#pragma once
#include "directx/compute/computeshader.h"
#include "shaders/constants.hlsli"

class PreVGSCompute : public ComputeShader
{
public:
    PreVGSCompute() = default;

    PreVGSCompute(
        uint numParticles
	) : ComputeShader(IDR_SHADER6)
    {
        // This shader has a second "entry point" for updating particle weights from paint data.
        loadShaderObject(updateParticleWeightsEntryPoint);
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
        
        preVgsConstants.massLow = massLow;
        preVgsConstants.massHigh = massHigh;
        DirectX::updateConstantBuffer(preVgsConstantsBuffer, preVgsConstants);

        ComputeShader::dispatch(numWorkgroups, updateParticleWeightsEntryPoint);
    }

    void updatePreVgsConstants(float timeStep, float gravityStrength) {
        preVgsConstants.timeStep = timeStep;
        preVgsConstants.gravityStrength = gravityStrength;
        DirectX::updateConstantBuffer(preVgsConstantsBuffer, preVgsConstants);
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
    inline static constexpr int updateParticleWeightsEntryPoint = IDR_SHADER7;
    int numWorkgroups;
    PreVGSConstants preVgsConstants;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11UnorderedAccessView> oldPositionsUAV;
    ComPtr<ID3D11ShaderResourceView> isDraggingSRV;
    ComPtr<ID3D11Buffer> preVgsConstantsBuffer;
    ComPtr<ID3D11UnorderedAccessView> paintDeltaUAV;  // Only used during update from paint values
    ComPtr<ID3D11UnorderedAccessView> paintValueUAV;  // Only used during update from paint values

    void bind() override
    {
        ID3D11ShaderResourceView* srvs[] = { isDraggingSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get(), oldPositionsUAV.Get(), paintDeltaUAV.Get(), paintValueUAV.Get() };
		DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11Buffer* cbvs[] = { preVgsConstantsBuffer.Get() };
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

    void initializeBuffers(uint numParticles) {
        numWorkgroups = Utils::divideRoundUp(numParticles, VGS_THREADS);

        preVgsConstants.numParticles = numParticles;
        preVgsConstants.gravityStrength = -9.81f; // Default gravity strength
        preVgsConstants.timeStep = 1.0f / 600.0f; // Default timestep (60 FPS and 10 substeps)
        preVgsConstantsBuffer = DirectX::createConstantBuffer(preVgsConstants);
    }
};