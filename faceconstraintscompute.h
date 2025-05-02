#pragma once

#include "directx/compute/computeshader.h"
#include "pbd.h"
#include "glm.hpp"

class FaceConstraintsCompute : public ComputeShader
{
public:
    FaceConstraintsCompute(
        const std::vector<FaceConstraint>& constraints,
        const ComPtr<ID3D11UnorderedAccessView>& positionsUAV,
		const ComPtr<ID3D11ShaderResourceView>& weightsSRV
    ) : ComputeShader(IDR_SHADER4), positionsUAV(positionsUAV), weightsSRV(weightsSRV)
    {
        initializeBuffers(constraints);
    };

    void dispatch(int numWorkgroups) override
    {
        bind();
        DirectX::getContext()->Dispatch(numWorkgroups, 1, 1);
        unbind();
    };

    const ComPtr<ID3D11ShaderResourceView>& getWeightsSRV() const { return weightsSRV; }

    void updateAxis(int newAxis) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DirectX::getContext()->Map(constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr))
        {
            // Copy the flag to the constant buffer
            memcpy(mappedResource.pData, &newAxis, sizeof(int));
            DirectX::getContext()->Unmap(constantBuffer.Get(), 0);
        }
        else
        {
            MGlobal::displayError("Failed to map constant buffer.");
        }
    }

private:
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11ShaderResourceView> weightsSRV;
    ComPtr<ID3D11ShaderResourceView> constraintsSRV;
	ComPtr<ID3D11Buffer> constraintsBuffer;
    ComPtr<ID3D11Buffer> constantBuffer;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { weightsSRV.Get(), constraintsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { constantBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
		DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(const std::vector<FaceConstraint>& constraints) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};

        bufferDesc.Usage = D3D11_USAGE_DYNAMIC; // Dynamic for CPU updates
        bufferDesc.ByteWidth = sizeof(int);    // Size of the constant buffer
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; // Bind as a constant buffer
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // Allow CPU writes
        bufferDesc.MiscFlags = 0;

        HRESULT hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &constantBuffer);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create constant buffer.");
        }

		// Initialize constraints buffer and its SRV
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = sizeof(FaceConstraint) * constraints.size();
		bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDesc.StructureByteStride = sizeof(FaceConstraint);
		initData.pSysMem = constraints.data();
		hr = DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, &constraintsBuffer);
		if (FAILED(hr)) {
			MGlobal::displayError("Failed to create constraints buffer.");
		}

		// Create the SRV for the constraints buffer
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = constraints.size();
		srvDesc.Buffer.ElementWidth = sizeof(FaceConstraint);
		srvDesc.Buffer.ElementOffset = 0;
		hr = DirectX::getDevice()->CreateShaderResourceView(constraintsBuffer.Get(), &srvDesc, &constraintsSRV);
		if (FAILED(hr)) {
			MGlobal::displayError("Failed to create constraints SRV.");
		}
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
    };

};