#pragma once

#include "directx/compute/computeshader.h"
#include "glm.hpp"

class VGSCompute : public ComputeShader
{
public:
    VGSCompute(
        int numPositions,
        const float* weights,
		const ComPtr<ID3D11UnorderedAccessView>& positionsUAV
	) : ComputeShader(IDR_SHADER3), positionsUAV(positionsUAV)
    {
        initializeBuffers(numPositions, weights);
    };

    void copyTransformedPositionsToCPU(std::vector<glm::vec4>& updatedPositions, const ComPtr<ID3D11Buffer>& positionsBuffer, int numPositions) {
        DirectX::getContext()->CopyResource(stagingPositionsBuffer.Get(), positionsBuffer.Get());

        D3D11_MAPPED_SUBRESOURCE mappedPositions;
        HRESULT hr = DirectX::getContext()->Map(stagingPositionsBuffer.Get(), 0, D3D11_MAP_READ, 0, &mappedPositions);

        if (SUCCEEDED(hr)) {
            glm::vec4* positionsData = reinterpret_cast<glm::vec4*>(mappedPositions.pData);
            updatedPositions.assign(positionsData, positionsData + numPositions);
            DirectX::getContext()->Unmap(stagingPositionsBuffer.Get(), 0);
        }
        else {
            MGlobal::displayError("Failed to map positionsBuffer for CPU readback.");
        }
    }

    void dispatch(int numWorkgroups) override
    {
        bind();
        DirectX::getContext()->Dispatch(numWorkgroups, 1, 1);
        unbind();
    };

    const ComPtr<ID3D11ShaderResourceView>& getWeightsSRV() const { return weightsSRV; }

private:
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;

    ComPtr<ID3D11Buffer> weightsBuffer;
    ComPtr<ID3D11ShaderResourceView> weightsSRV;

    ComPtr<ID3D11Buffer> stagingPositionsBuffer;
    
    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = {  weightsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get() };
		DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    };

    void initializeBuffers(int numParticles, const float* weights) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

        // Initialize weights buffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        bufferDesc.ByteWidth = numParticles * sizeof(float); // Float for weights
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(float);

        initData.pSysMem = weights;
        DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &weightsBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numParticles;

        DirectX::getDevice()->CreateShaderResourceView(weightsBuffer.Get(), &srvDesc, &weightsSRV);

        // Initalize staging buffer
        bufferDesc.Usage = D3D11_USAGE_STAGING;
        bufferDesc.ByteWidth = numParticles * sizeof(float) * 4; // Vec4 for positions
        bufferDesc.BindFlags = 0;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        bufferDesc.MiscFlags = 0;
        bufferDesc.StructureByteStride = sizeof(float) * 4; // Vec4 for positions

        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &stagingPositionsBuffer);
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
        weightsBuffer.Reset();
        weightsSRV.Reset();
    };

};