#pragma once
#include "directx/compute/computeshader.h"

/**
 * Helper class for performing an exclusive parallel prefix scan via GPU compute (GPU Gems algorithm).
 * This compute pass does the collect step between scans, where partial sums are added back to the input buffer.
 */
class PrefixScanCollectCompute : public ComputeShader
{
public:
    PrefixScanCollectCompute(
        const ComPtr<ID3D11UnorderedAccessView>& collisionCellParticleCountsUAV
    ) : ComputeShader(IDR_SHADER11), collisionCellParticleCountsUAV(collisionCellParticleCountsUAV) {}

    void dispatch(int numWorkgroups) override {
        bind();
        DirectX::getContext()->Dispatch(numWorkgroups, 1, 1);
        unbind();
    }
    
private:
    ComPtr<ID3D11UnorderedAccessView> collisionCellParticleCountsUAV;

    void bind() override {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { collisionCellParticleCountsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    }

    void unbind() override {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    }
};