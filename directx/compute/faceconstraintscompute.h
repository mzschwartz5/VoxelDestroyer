#pragma once

#include "directx/compute/computeshader.h"
#include "glm.hpp"

struct FaceContraintsCB {
	glm::uvec4 faceOneIndices;
	glm::uvec4 faceTwoIndices;
    int numContraints;
    int padding0;
    int padding1;
    int padding2;
};

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

	void updateActiveConstraintAxis(int axis) {
		activeConstraintAxis = axis;
	}

private:
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
	int activeConstraintAxis = 0; // x = 0, y = 1, z = 2
	std::array<ComPtr<ID3D11UnorderedAccessView>, 3> faceConstraintUAVs;
	std::array<ComPtr<ID3D11Buffer>, 3> faceContraintsCBs;
	std::array<ComPtr<ID3D11Buffer>, 3> constraintBuffers;
    ComPtr<ID3D11Buffer> voxelSimInfoBuffer;
    ComPtr<ID3D11ShaderResourceView> weightsSRV;
	ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { weightsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get(), faceConstraintUAVs[activeConstraintAxis].Get(), isSurfaceUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { voxelSimInfoBuffer.Get(), faceContraintsCBs[activeConstraintAxis].Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr, nullptr };
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
		CreateBuffer(&bufferDesc, &initData, &constraintBuffers[0]);

		// Create the UAV for the X constraints buffer
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = UINT(constraints[0].size());
		DirectX::getDevice()->CreateUnorderedAccessView(constraintBuffers[0].Get(), &uavDesc, &faceConstraintUAVs[0]);

		// Initialize Y constraints buffer and its UAV
		bufferDesc.ByteWidth = UINT(sizeof(FaceConstraint) * constraints[1].size());
		bufferDesc.StructureByteStride = sizeof(FaceConstraint);
		initData.pSysMem = constraints[1].data();
		CreateBuffer(&bufferDesc, &initData, &constraintBuffers[1]);

		// Create the UAV for the Y constraints buffer
		uavDesc.Buffer.NumElements = static_cast<uint>((constraints[1].size()));
		DirectX::getDevice()->CreateUnorderedAccessView(constraintBuffers[1].Get(), &uavDesc, &faceConstraintUAVs[1]);

		// Initialize Z constraints buffer and its UAV
		bufferDesc.ByteWidth = static_cast<uint>(sizeof(FaceConstraint) * constraints[2].size());
		bufferDesc.StructureByteStride = sizeof(FaceConstraint);
		initData.pSysMem = constraints[2].data();
		CreateBuffer(&bufferDesc, &initData, &constraintBuffers[2]);

		// Create the UAV for the Z constraints buffer
		uavDesc.Buffer.NumElements = static_cast<uint>((constraints[2].size()));
		DirectX::getDevice()->CreateUnorderedAccessView(constraintBuffers[2].Get(), &uavDesc, &faceConstraintUAVs[2]);

		// Initialize constant buffer for X-drection face indices
		bufferDesc.Usage = D3D11_USAGE_DYNAMIC; // Dynamic for CPU updates
        bufferDesc.ByteWidth = sizeof(FaceContraintsCB);  // Size of the constant buffer
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; // Bind as a constant buffer
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // Allow CPU writes
        bufferDesc.MiscFlags = 0;
        FaceContraintsCB xFaceIndices = {{1, 3, 5, 7}, {0, 2, 4, 6}, static_cast<int>(constraints[0].size()), 0, 0, 0};
        initData.pSysMem = &xFaceIndices;
        CreateBuffer(&bufferDesc, &initData, &faceContraintsCBs[0]);

        // Initialize constant buffer for Y-direction face indices
        FaceContraintsCB yFaceIndices = {{2, 3, 6, 7}, {0, 1, 4, 5}, static_cast<int>(constraints[1].size()), 0, 0, 0};
        initData.pSysMem = &yFaceIndices;
        CreateBuffer(&bufferDesc, &initData, &faceContraintsCBs[1]);

        // Initialize constant buffer for Z-direction face indices
        FaceContraintsCB zFaceIndices = {{4, 5, 6, 7}, {0, 1, 2, 3}, static_cast<int>(constraints[2].size()), 0, 0, 0};
        initData.pSysMem = &zFaceIndices;
        CreateBuffer(&bufferDesc, &initData, &faceContraintsCBs[2]);
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
		for (auto& buffer : constraintBuffers) {
			buffer.Reset();
		}

		for (auto& uav : faceConstraintUAVs) {
			uav.Reset();
		}

		for (auto& buffer : faceContraintsCBs) {
			buffer.Reset();
		}
    };

};