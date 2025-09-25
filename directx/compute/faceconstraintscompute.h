#pragma once

#include "directx/compute/computeshader.h"
#include <array>

struct FaceConstraintsCB {
	std::array<uint, 4> faceOneIndices;
	std::array<uint, 4> faceTwoIndices;
    uint numContraints;
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
    FaceConstraintsCompute() = default;

    FaceConstraintsCompute(
        const std::array<std::vector<FaceConstraint>, 3>& constraints,
        const ComPtr<ID3D11Buffer>& voxelSimInfoBuffer
    ) : ComputeShader(IDR_SHADER4), voxelSimInfoBuffer(voxelSimInfoBuffer)
    {
        initializeBuffers(constraints);
    };

    ~FaceConstraintsCompute() {
        for (int i = 0; i < 3; i++) {
            DirectX::notifyMayaOfMemoryUsage(constraintBuffers[i]);
        }
    }

    void dispatch() override
    {
        ComputeShader::dispatch(numWorkgroups[activeConstraintAxis]);
    };

	void updateActiveConstraintAxis(int axis) {
		activeConstraintAxis = axis;
	}

    void setPositionsUAV(const ComPtr<ID3D11UnorderedAccessView>& positionsUAV) {
        this->positionsUAV = positionsUAV;
    }

    void setIsSurfaceUAV(const ComPtr<ID3D11UnorderedAccessView>& isSurfaceUAV) {
        this->isSurfaceUAV = isSurfaceUAV;
    }

private:
    int activeConstraintAxis = 0; // x = 0, y = 1, z = 2
    std::array<int, 3> numWorkgroups = { 0, 0, 0 };
    std::array<ComPtr<ID3D11UnorderedAccessView>, 3> faceConstraintUAVs;
    std::array<ComPtr<ID3D11Buffer>, 3> faceConstraintsCBs;
    std::array<ComPtr<ID3D11Buffer>, 3> constraintBuffers;
    ComPtr<ID3D11Buffer> voxelSimInfoBuffer;
    ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;

    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get(), faceConstraintUAVs[activeConstraintAxis].Get(), isSurfaceUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { voxelSimInfoBuffer.Get(), faceConstraintsCBs[activeConstraintAxis].Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(nullptr, NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr, nullptr };
		DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };
    
    void initializeBuffers(const std::array<std::vector<FaceConstraint>, 3>& constraints) {

        std::array<FaceConstraintsCB, 3> faceConstraintsCBData = {
            FaceConstraintsCB{{1, 3, 5, 7}, {0, 2, 4, 6}, static_cast<uint>(constraints[0].size()), 0, 0, 0},
            FaceConstraintsCB{{2, 3, 6, 7}, {0, 1, 4, 5}, static_cast<uint>(constraints[1].size()), 0, 0, 0},
            FaceConstraintsCB{{4, 5, 6, 7}, {0, 1, 2, 3}, static_cast<uint>(constraints[2].size()), 0, 0, 0}
        };        

        for (int i = 0; i < 3; i++) {
            numWorkgroups[i] = Utils::divideRoundUp(constraints[i].size(), VGS_THREADS);
            constraintBuffers[i] = DirectX::createReadWriteBuffer<FaceConstraint>(constraints[i]);
            faceConstraintUAVs[i] = DirectX::createUAV(constraintBuffers[i]);
            faceConstraintsCBs[i] = DirectX::createConstantBuffer<FaceConstraintsCB>(faceConstraintsCBData[i]);
        }
    }
};