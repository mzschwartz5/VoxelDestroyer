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
        const std::array<std::vector<FaceConstraint>, 3>& faceConstraints,
        const std::array<std::vector<uint>, 3>& faceIdxToLongRangeConstraintIndices,
        uint numParticles,
        float particleRadius,
        float voxelRestVolume
    ) : ComputeShader(IDR_SHADER4)
    {
        // This shader has a second "entry point" for updating constraint limits from paint data.
        loadShaderObject(updateFaceConstraintsEntryPoint);
        loadShaderObject(mergeRenderParticlesEntryPoint);
        loadShaderObject(expandRenderParticlesEntryPoint);
        initializeBuffers(faceConstraints, faceIdxToLongRangeConstraintIndices, numParticles, particleRadius, voxelRestVolume);
        numExpandParticlesWorkgroups = Utils::divideRoundUp(numParticles / 8, VGS_THREADS);
    };

    ~FaceConstraintsCompute() {
        for (int i = 0; i < 3; i++) {
            DirectX::notifyMayaOfMemoryUsage(faceConstraintBuffers[i]);
        }
    }

    void dispatch() override
    {
        for (activeConstraintAxis = 0; activeConstraintAxis < 3; activeConstraintAxis++) {
            ComputeShader::dispatch(numWorkgroups[activeConstraintAxis]);
        }
        activeConstraintAxis = 0;
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

    void mergeRenderParticles() {
        ComputeShader::dispatch(numExpandParticlesWorkgroups, expandRenderParticlesEntryPoint);
        for (activeConstraintAxis = 0; activeConstraintAxis < 3; activeConstraintAxis++) {
            ComputeShader::dispatch(numWorkgroups[activeConstraintAxis], mergeRenderParticlesEntryPoint);
        }
        activeConstraintAxis = 0;
    }

    void updateVGSParameters(
        float relaxation,
        float edgeUniformity,
        uint iterCount
    ) {
        vgsConstants.relaxation = relaxation;
        vgsConstants.edgeUniformity = edgeUniformity;
        vgsConstants.iterCount = iterCount;
        DirectX::updateConstantBuffer(vgsConstantBuffer, vgsConstants);
    }

    void setParticlesUAV(const ComPtr<ID3D11UnorderedAccessView>& particlesUAV) {
        this->particlesUAV = particlesUAV;
    }

    void setIsSurfaceUAV(const ComPtr<ID3D11UnorderedAccessView>& isSurfaceUAV) {
        this->isSurfaceUAV = isSurfaceUAV;
    }

    void setRenderParticlesUAV(const ComPtr<ID3D11UnorderedAccessView>& renderParticlesUAV) {
        this->renderParticlesUAV = renderParticlesUAV;
    }

    void setLongRangeConstraintCountersUAV(const ComPtr<ID3D11UnorderedAccessView>& longRangeConstraintCountersUAV) {
        this->longRangeConstraintCountersUAV = longRangeConstraintCountersUAV;
    }

private:
    inline static constexpr int updateFaceConstraintsEntryPoint = IDR_SHADER5;
    inline static constexpr int mergeRenderParticlesEntryPoint = IDR_SHADER16;
    inline static constexpr int expandRenderParticlesEntryPoint = IDR_SHADER17;
    int activeConstraintAxis = 0; // x = 0, y = 1, z = 2
    std::array<int, 3> numWorkgroups = { 0, 0, 0 };
    int numExpandParticlesWorkgroups = 0;
    std::array<FaceConstraintsCB, 3> faceConstraintsCBData;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 3> faceConstraintUAVs;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 3> longRangeConstraintIndicesUAVs;
    std::array<ComPtr<ID3D11Buffer>, 3> faceConstraintsCBs;
    std::array<ComPtr<ID3D11Buffer>, 3> faceConstraintBuffers;
    std::array<ComPtr<ID3D11Buffer>, 3> longRangeConstraintIndicesBuffers;
    VGSConstants vgsConstants;
    ComPtr<ID3D11Buffer> vgsConstantBuffer;
    ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV;
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    ComPtr<ID3D11UnorderedAccessView> paintDeltaUAV;  // Only used during update from paint values
    ComPtr<ID3D11UnorderedAccessView> paintValueUAV;  // Only used during update from paint values
    ComPtr<ID3D11UnorderedAccessView> renderParticlesUAV; // A copy of the particles that can be adjusted (i.e. close particle gaps) for rendering (without affecting simulation).
    ComPtr<ID3D11UnorderedAccessView> longRangeConstraintCountersUAV;

    void bind() override
    {
        ID3D11UnorderedAccessView* uavs[] = { 
            particlesUAV.Get(), faceConstraintUAVs[activeConstraintAxis].Get(), isSurfaceUAV.Get(), 
            paintDeltaUAV.Get(), paintValueUAV.Get(), renderParticlesUAV.Get(), 
            longRangeConstraintIndicesUAVs[activeConstraintAxis].Get(), longRangeConstraintCountersUAV.Get() 
        };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { vgsConstantBuffer.Get(), faceConstraintsCBs[activeConstraintAxis].Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
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
    
    void initializeBuffers(const std::array<std::vector<FaceConstraint>, 3>& faceConstraints, const std::array<std::vector<uint>, 3>& faceIdxToLongRangeConstraintIndices, uint numParticles, float particleRadius, float voxelRestVolume) {

        vgsConstants.relaxation = 0.5f;
        vgsConstants.edgeUniformity = 1.0f;
        vgsConstants.iterCount = 3;
        vgsConstants.numVoxels = numParticles / 8;
        vgsConstants.particleRadius = particleRadius;
        vgsConstants.voxelRestVolume = voxelRestVolume;
        vgsConstantBuffer = DirectX::createConstantBuffer<VGSConstants>(vgsConstants);

        // Order of vertex indices and face IDs corresponds to definitions in cube.h
        faceConstraintsCBData = std::array<FaceConstraintsCB, 3>{
            FaceConstraintsCB{{1, 3, 5, 7}, {0, 2, 4, 6}, static_cast<uint>(faceConstraints[0].size()), 1, 0, 0, 0, 0, 0, 0},
            FaceConstraintsCB{{2, 3, 6, 7}, {0, 1, 4, 5}, static_cast<uint>(faceConstraints[1].size()), 3, 2, 0, 0, 0, 0, 0},
            FaceConstraintsCB{{4, 5, 6, 7}, {0, 1, 2, 3}, static_cast<uint>(faceConstraints[2].size()), 5, 4, 0, 0, 0, 0, 0}
        };    

        for (int i = 0; i < 3; i++) {
            numWorkgroups[i] = Utils::divideRoundUp(faceConstraints[i].size(), VGS_THREADS);
            faceConstraintBuffers[i] = DirectX::createReadWriteBuffer<FaceConstraint>(faceConstraints[i]);
            faceConstraintUAVs[i] = DirectX::createUAV(faceConstraintBuffers[i]);
            faceConstraintsCBs[i] = DirectX::createConstantBuffer<FaceConstraintsCB>(faceConstraintsCBData[i]);
            longRangeConstraintIndicesBuffers[i] = DirectX::createReadWriteBuffer<uint>(faceIdxToLongRangeConstraintIndices[i]);
            longRangeConstraintIndicesUAVs[i] = DirectX::createUAV(longRangeConstraintIndicesBuffers[i]);
        }
    }
};