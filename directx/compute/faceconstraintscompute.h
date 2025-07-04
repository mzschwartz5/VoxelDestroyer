#pragma once

#include "directx/compute/computeshader.h"
#include "glm.hpp"

struct FaceConstraint {
	int voxelOneIdx;
	int voxelTwoIdx;
	float tensionLimit;
	float compressionLimit;
};

class FaceConstraintsCompute : public ComputeShader
{
public:
    FaceConstraintsCompute(
        const std::array<std::vector<FaceConstraint>, 3>& constraints,
        const ComPtr<ID3D11UnorderedAccessView>& positionsUAV,
		const ComPtr<ID3D11ShaderResourceView>& weightsSRV,
        const ComPtr<ID3D11Buffer>& voxelSimInfoBuffer,
		const ComPtr<ID3D11UnorderedAccessView>& isSurfaceUAV
    ) : ComputeShader(IDR_SHADER4), positionsUAV(positionsUAV), weightsSRV(weightsSRV), voxelSimInfoBuffer(voxelSimInfoBuffer), isSurfaceUAV(isSurfaceUAV)
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

private:
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11ShaderResourceView> weightsSRV;

    ComPtr<ID3D11UnorderedAccessView> xConstraintsUAV;
	ComPtr<ID3D11Buffer> xConstraintsBuffer;

	ComPtr<ID3D11UnorderedAccessView> yConstraintsUAV;
	ComPtr<ID3D11Buffer> yConstraintsBuffer;

	ComPtr<ID3D11UnorderedAccessView> zConstraintsUAV;
	ComPtr<ID3D11Buffer> zConstraintsBuffer;

    ComPtr<ID3D11Buffer> voxelSimInfoBuffer;

	ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { weightsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get(), xConstraintsUAV.Get(), yConstraintsUAV.Get(), zConstraintsUAV.Get(), isSurfaceUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { voxelSimInfoBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr, nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
		DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(const std::array<std::vector<FaceConstraint>, 3>& constraints) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};

		// Initialize X constraints buffer and its UAV
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = UINT(sizeof(FaceConstraint) * constraints[0].size());
		bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		bufferDesc.CPUAccessFlags = 0;
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDesc.StructureByteStride = sizeof(FaceConstraint);
		initData.pSysMem = constraints[0].data();
		CreateBuffer(&bufferDesc, &initData, &xConstraintsBuffer);

		// Create the UAV for the X constraints buffer
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = UINT(constraints[0].size());
		DirectX::getDevice()->CreateUnorderedAccessView(xConstraintsBuffer.Get(), &uavDesc, &xConstraintsUAV);

		// Initialize Y constraints buffer and its UAV
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth = UINT(sizeof(FaceConstraint) * constraints[1].size());
		bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		bufferDesc.CPUAccessFlags = 0;
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDesc.StructureByteStride = sizeof(FaceConstraint);
		initData.pSysMem = constraints[1].data();
		CreateBuffer(&bufferDesc, &initData, &yConstraintsBuffer);

		// Create the UAV for the Y constraints buffer
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = UINT(constraints[1].size());
		DirectX::getDevice()->CreateUnorderedAccessView(yConstraintsBuffer.Get(), &uavDesc, &yConstraintsUAV);

		// Initialize Z constraints buffer and its UAV
		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth = UINT(sizeof(FaceConstraint) * constraints[2].size());
		bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		bufferDesc.CPUAccessFlags = 0;
		bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bufferDesc.StructureByteStride = sizeof(FaceConstraint);
		initData.pSysMem = constraints[2].data();
		CreateBuffer(&bufferDesc, &initData, &zConstraintsBuffer);

		// Create the UAV for the Z constraints buffer
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = UINT(constraints[2].size());
		DirectX::getDevice()->CreateUnorderedAccessView(zConstraintsBuffer.Get(), &uavDesc, &zConstraintsUAV);

    }

    void tearDown() override
    {
        ComputeShader::tearDown();
		xConstraintsBuffer.Reset();
		xConstraintsUAV.Reset();
		yConstraintsBuffer.Reset();
		yConstraintsUAV.Reset();
		zConstraintsBuffer.Reset();
		zConstraintsUAV.Reset();
    };

};