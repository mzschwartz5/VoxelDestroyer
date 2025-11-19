#pragma once
#include "directx/compute/computeshader.h"
#include "directx/pingpongview.h"

struct Constants {
    int numElements;
    int sign;
    int padding0;
    int padding1;
};

/**
 * Used to calculate the delta in paint values after a paint stroke,
 * so we can store that in a command for undo/redo.
 * 
 * The input deltaUAV has the "before" paint values, and the paintViews pingpong buffer
 * has the "after" paint values. The delta is calculated in place, stored in deltaUAV.
 */
class PaintDeltaCompute : public ComputeShader
{
public:
    PaintDeltaCompute() = default;

    PaintDeltaCompute(
        int numElements,
        PingPongView& paintViews,
        const ComPtr<ID3D11UnorderedAccessView>& deltaUAV
    ) : ComputeShader(IDR_SHADER16), paintViews(&paintViews), deltaUAV(deltaUAV), numElements(numElements)
    {
        numWorkgroups = Utils::divideRoundUp(numElements, VGS_THREADS);
        constantBuffer = DirectX::createConstantBuffer<Constants>({ numElements, -1, 0, 0 });
    };

    void dispatch() override {
        ComputeShader::dispatch(numWorkgroups);
    }

    void bind() override {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { deltaUAV.Get(), paintViews->UAV().Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { constantBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    }

    void unbind() override {
        DirectX::getContext()->CSSetShader(nullptr, NULL, 0);

        ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

        ID3D11Buffer* nullCBVs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(nullCBVs), nullCBVs);
    }

    void updateSign(int sign) {
        DirectX::updateConstantBuffer<Constants>(constantBuffer, { numElements, sign, 0, 0 });
    }

private:
    int numElements;
    int numWorkgroups;
    const PingPongView* paintViews = nullptr;
    ComPtr<ID3D11UnorderedAccessView> deltaUAV;
    ComPtr<ID3D11Buffer> constantBuffer;
};