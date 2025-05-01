#pragma once

#include "directx/compute/computeshader.h"
#include "glm.hpp"

class VGSCompute : public ComputeShader
{
public:
    VGSCompute(
        int numPositions,
		const ComPtr<ID3D11UnorderedAccessView>& positionsUAV
	) : ComputeShader(IDR_SHADER3), positionsUAV(positionsUAV)
    {
        initializeBuffers(numPositions);
    };

    void updatePositionBuffer(const std::vector<glm::vec4>& positions) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX::getContext()->Map(positionsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        memcpy(mappedResource.pData, positions.data(), positions.size() * sizeof(glm::vec4));
        DirectX::getContext()->Unmap(positionsBuffer.Get(), 0);
    };

    void updateWeightsBuffer(const std::vector<float>& weights) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX::getContext()->Map(weightsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        memcpy(mappedResource.pData, weights.data(), weights.size() * sizeof(float));
        DirectX::getContext()->Unmap(weightsBuffer.Get(), 0);
    };

    void dispatch(int numWorkgroups) override
    {
        bind();
        DirectX::getContext()->Dispatch(numWorkgroups, 1, 1);
        unbind();
    };

    const ComPtr<ID3D11ShaderResourceView>& getWeightsSRV() const { return weightsSRV; }

    /*void copyTransformedPositionsToCPU(glm::vec4& updatedPositions, int numPositions) {
        DirectX::getContext()->CopyResource(stagingPositionsBuffer.Get(), positionsBuffer.Get());

        D3D11_MAPPED_SUBRESOURCE mappedPositions;
        HRESULT hr = DirectX::getContext()->Map(stagingPositionsBuffer.Get(), 0, D3D11_MAP_READ, 0, &mappedPositions);

        if (SUCCEEDED(hr)) {
            updatedPositions = *reinterpret_cast<glm::vec4*>(mappedPositions.pData);
            DirectX::getContext()->Unmap(positionsBuffer.Get(), 0);
        }
        else {
            MGlobal::displayError("Failed to map positionsBuffer for CPU readback.");
        }
    }*/

private:
    ComPtr<ID3D11Buffer> positionsBuffer;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;

    ComPtr<ID3D11Buffer> weightsBuffer;
    ComPtr<ID3D11ShaderResourceView> weightsSRV;
    
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

    void initializeBuffers(int numParticles) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

        // Initialize weights buffer and its SRV
        bufferDesc.ByteWidth = numParticles * sizeof(float); // Float for weights
        bufferDesc.StructureByteStride = sizeof(float);

        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &weightsBuffer);

        srvDesc.Buffer.NumElements = numParticles;

        DirectX::getDevice()->CreateShaderResourceView(weightsBuffer.Get(), &srvDesc, &weightsSRV);
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
        positionsBuffer.Reset();
        weightsBuffer.Reset();
        weightsSRV.Reset();
    };

};