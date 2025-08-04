#pragma once
#include "directx/compute/computeshader.h"

/**
 * Helper class for performing an exclusive parallel prefix scan via GPU compute (GPU Gems algorithm).
 * This compute pass does the collect step between scans, where partial sums are added back to the input buffer.
 */
class PrefixScanCollectCompute : public ComputeShader
{
public:
    PrefixScanCollectCompute() : ComputeShader(IDR_SHADER11) {}

    void collect(
        const ComPtr<ID3D11UnorderedAccessView>& originalBufferUAV,
        const ComPtr<ID3D11ShaderResourceView>& partialSumsSRV,
        int numWorkgroups
    ) {
        this->originalBufferUAV = originalBufferUAV;
        this->partialSumsSRV = partialSumsSRV;
        ComputeShader::dispatch(numWorkgroups);
    }

    
private:
    ComPtr<ID3D11UnorderedAccessView> originalBufferUAV;
    ComPtr<ID3D11ShaderResourceView> partialSumsSRV;

    // No-op. This class needs to be given a numWorkgroups.
    void dispatch() override {}
    
    void bind() override {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { partialSumsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { originalBufferUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    }

    void unbind() override {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    }
};