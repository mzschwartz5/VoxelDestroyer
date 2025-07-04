#pragma once

#include "directx/compute/computeshader.h"

class PostVGSCompute : public ComputeShader
{
public:
    PostVGSCompute(
        const ComPtr<ID3D11ShaderResourceView>& weightsSRV,
        const ComPtr<ID3D11ShaderResourceView>& positionsSRV,
        const ComPtr<ID3D11ShaderResourceView>& oldPositionsSRV,
        const ComPtr<ID3D11UnorderedAccessView>& velocitiesUAV,
        const ComPtr<ID3D11UnorderedAccessView>& isDraggingUAV
	) : ComputeShader(IDR_SHADER6), weightsSRV(weightsSRV), positionsSRV(positionsSRV), oldPositionsSRV(oldPositionsSRV), velocitiesUAV(velocitiesUAV), isDraggingUAV(isDraggingUAV)
    {};

private:

    ComPtr<ID3D11ShaderResourceView> weightsSRV;
    ComPtr<ID3D11ShaderResourceView> positionsSRV;
    ComPtr<ID3D11ShaderResourceView> oldPositionsSRV;
    ComPtr<ID3D11UnorderedAccessView> velocitiesUAV;
    ComPtr<ID3D11UnorderedAccessView> isDraggingUAV;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { weightsSRV.Get(), positionsSRV.Get(), oldPositionsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { velocitiesUAV.Get(), isDraggingUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    };

    void tearDown() override
    {
        ComputeShader::tearDown();
    };

};