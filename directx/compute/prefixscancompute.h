#pragma once
#include "directx/compute/computeshader.h"
#include "./prefixscancollectcompute.h"
#include <memory>

/**
 * Class to perform an exclusive parallel prefix scan via GPU compute (GPU Gems algorithm).
 * While this could easily be generalized to any buffer, for this project it's tied to the collision grid cell buffer.
 */
class PrefixScanCompute : public ComputeShader
{
public:
    PrefixScanCompute(
        const ComPtr<ID3D11UnorderedAccessView>& collisionCellParticleCountsUAV
    ) : ComputeShader(IDR_SHADER2), collisionCellParticleCountsUAV(collisionCellParticleCountsUAV) {
        collectComputePass = std::make_unique<PrefixScanCollectCompute>(collisionCellParticleCountsUAV);
    }

    void dispatch(int numWorkgroups) override {
        bind();
        DirectX::getContext()->Dispatch(numWorkgroups, 1, 1);
        unbind();

        collectComputePass->dispatch(2 * numWorkgroups); // Collection requires twice the workgroups as scan because each thread only processes one element instead of two.
    }
    
private:
    ComPtr<ID3D11UnorderedAccessView> collisionCellParticleCountsUAV;
    std::unique_ptr<PrefixScanCollectCompute> collectComputePass;

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