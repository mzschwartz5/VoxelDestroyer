#pragma once

#include "directx/compute/computeshader.h"
#include <array>

struct FaceConstraintsCB {
	std::array<uint, 4> faceOneIndices;
	std::array<uint, 4> faceTwoIndices;
    uint numContraints;
    int faceOneId;
    int faceTwoId;
    float constraintLow;
    float constraintHigh;
    int padding0;
    int padding1;
    int padding2;
};

struct FaceConstraint {
	int voxelOneIdx;
	int voxelTwoIdx;
	float tensionLimit = 0.0f;
	float compressionLimit = 0.0f;
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
        // This shader has a second entry point for updating constraint limits from paint data.
        loadEntryPoint(updateFaceConstraintsEntryPoint);
        initializeBuffers(constraints);
    };

    ~FaceConstraintsCompute() {
        for (int i = 0; i < 3; i++) {
            DirectX::notifyMayaOfMemoryUsage(constraintBuffers[i]);
        }
    }

    void dispatch() override
    {
        for (activeConstraintAxis = 0; activeConstraintAxis < 3; activeConstraintAxis++) {
            ComputeShader::dispatch(numWorkgroups[activeConstraintAxis]);
        }
    };

    void updateFaceConstraintsFromPaint(
        const ComPtr<ID3D11UnorderedAccessView>& paintDeltaUAV, 
        const ComPtr<ID3D11UnorderedAccessView>& paintValueUAV,
        float constraintLow, 
        float constraintHigh
    ) {
        this->paintDeltaUAV = paintDeltaUAV;
        this->paintValueUAV = paintValueUAV;
        
        if (constraintLow != faceConstraintsCBData[0].constraintLow || constraintHigh != faceConstraintsCBData[0].constraintHigh) {
            updateConstraintLimits(constraintLow,  constraintHigh);
        }

        for (activeConstraintAxis = 0; activeConstraintAxis < 3; activeConstraintAxis++) {
            ComputeShader::dispatch(numWorkgroups[activeConstraintAxis], updateFaceConstraintsEntryPoint);
        }
    }

    void setPositionsUAV(const ComPtr<ID3D11UnorderedAccessView>& positionsUAV) {
        this->positionsUAV = positionsUAV;
    }

    void setIsSurfaceUAV(const ComPtr<ID3D11UnorderedAccessView>& isSurfaceUAV) {
        this->isSurfaceUAV = isSurfaceUAV;
    }

private:
    inline static const std::string updateFaceConstraintsEntryPoint = "updateFaceConstraintsFromPaint";
    int activeConstraintAxis = 0; // x = 0, y = 1, z = 2
    std::array<int, 3> numWorkgroups = { 0, 0, 0 };
    std::array<FaceConstraintsCB, 3> faceConstraintsCBData;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 3> faceConstraintUAVs;
    std::array<ComPtr<ID3D11Buffer>, 3> faceConstraintsCBs;
    std::array<ComPtr<ID3D11Buffer>, 3> constraintBuffers;
    ComPtr<ID3D11Buffer> voxelSimInfoBuffer;
    ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;
    ComPtr<ID3D11UnorderedAccessView> paintDeltaUAV;  // Only used during update from paint values
    ComPtr<ID3D11UnorderedAccessView> paintValueUAV;  // Only used during update from paint values

    void bind() override
    {
        ID3D11UnorderedAccessView* uavs[] = { positionsUAV.Get(), faceConstraintUAVs[activeConstraintAxis].Get(), isSurfaceUAV.Get(), paintDeltaUAV.Get(), paintValueUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { voxelSimInfoBuffer.Get(), faceConstraintsCBs[activeConstraintAxis].Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr, nullptr, nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr, nullptr };
		DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void updateConstraintLimits(float constraintLow, float constraintHigh) {
        for (int i = 0; i < 3; i++) {
            faceConstraintsCBData[i].constraintLow = constraintLow;
            faceConstraintsCBData[i].constraintHigh = constraintHigh;
            DirectX::updateConstantBuffer(faceConstraintsCBs[i], faceConstraintsCBData[i]);
        }
    };
    
    void initializeBuffers(const std::array<std::vector<FaceConstraint>, 3>& constraints) {

        // Order of vertex indices and face IDs corresponds to definitions in cube.h
        faceConstraintsCBData = std::array<FaceConstraintsCB, 3>{
            FaceConstraintsCB{{1, 3, 5, 7}, {0, 2, 4, 6}, static_cast<uint>(constraints[0].size()), 1, 0, 0, 0, 0, 0, 0},
            FaceConstraintsCB{{2, 3, 6, 7}, {0, 1, 4, 5}, static_cast<uint>(constraints[1].size()), 3, 2, 0, 0, 0, 0, 0},
            FaceConstraintsCB{{4, 5, 6, 7}, {0, 1, 2, 3}, static_cast<uint>(constraints[2].size()), 5, 4, 0, 0, 0, 0, 0}
        };    

        for (int i = 0; i < 3; i++) {
            numWorkgroups[i] = Utils::divideRoundUp(constraints[i].size(), VGS_THREADS);
            constraintBuffers[i] = DirectX::createReadWriteBuffer<FaceConstraint>(constraints[i]);
            faceConstraintUAVs[i] = DirectX::createUAV(constraintBuffers[i]);
            faceConstraintsCBs[i] = DirectX::createConstantBuffer<FaceConstraintsCB>(faceConstraintsCBData[i]);
        }
    }
};