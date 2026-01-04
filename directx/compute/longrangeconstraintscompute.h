#pragma once

#include "directx/compute/computeshader.h"

struct LongRangeConstraints {
    // Each group of 8 consecutive indices corresponds to one voxel's long-range constraint particles
    std::vector<uint> particleIndices;
    std::array<std::vector<uint>, 3> faceIdxToLRConstraintIndices;
};

struct LongRangeConstraintsCB {
    uint numConstraints{0};
    uint padding0{0};
    uint padding1{0};
    uint padding2{0};
};

class LongRangeConstraintsCompute : public ComputeShader
{
public:
    LongRangeConstraintsCompute() = default;

    LongRangeConstraintsCompute(
        uint numParticles,
        float particleRadius,
        float voxelRestVolume,
        const LongRangeConstraints& constraints
    ) : ComputeShader(IDR_SHADER19)
    {
        initializeBuffers(numParticles, particleRadius, voxelRestVolume, constraints);
    }

    LongRangeConstraintsCompute(const LongRangeConstraintsCompute&) = delete;
    LongRangeConstraintsCompute& operator=(const LongRangeConstraintsCompute&) = delete;
    LongRangeConstraintsCompute(LongRangeConstraintsCompute&&) noexcept = default;
    LongRangeConstraintsCompute& operator=(LongRangeConstraintsCompute&&) noexcept = default;

    ~LongRangeConstraintsCompute() {
        DirectX::notifyMayaOfMemoryUsage(longRangeParticleIndicesBuffer, false);
        DirectX::notifyMayaOfMemoryUsage(longRangeConstraintsCB, false);
    }

    void dispatch() override {
        ComputeShader::dispatch(numWorkgroups);
    }

    // This is hijacked by the FaceConstraintsCompute shader as a counter for number of broken face constraints
    // within each long-range constraint. The lower 4 bits of the first numLRConstraint entries are used for this.
    const ComPtr<ID3D11UnorderedAccessView>& getLongRangeParticleIndicesUAV() const {
        return longRangeParticleIndicesUAV;
    }

    void setParticlesUAV(const ComPtr<ID3D11UnorderedAccessView>& uav) {
        particlesUAV = uav;
    }

    void updateVGSParameters(
        float vgsRelaxation,
        float vgsEdgeUniformity,
        uint vgsIterations,
        float compliance
    ) {
        vgsConstants.relaxation = vgsRelaxation;
        vgsConstants.edgeUniformity = vgsEdgeUniformity;
        vgsConstants.iterCount = vgsIterations;
        vgsConstants.compliance = compliance;
        DirectX::updateConstantBuffer(vgsConstantsCB, vgsConstants);
    }

private:
    int numWorkgroups = 0;
    VGSConstants vgsConstants;
    // Owned resources
    ComPtr<ID3D11Buffer> longRangeParticleIndicesBuffer;
    ComPtr<ID3D11Buffer> longRangeConstraintsCB;
    ComPtr<ID3D11Buffer> vgsConstantsCB;
    ComPtr<ID3D11ShaderResourceView> longRangeParticleIndicesSRV;
    ComPtr<ID3D11UnorderedAccessView> longRangeParticleIndicesUAV;
    // Passed-in resources
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;

    void bind() override
    {
        ID3D11ShaderResourceView* srvs[] = { longRangeParticleIndicesSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { longRangeConstraintsCB.Get(), vgsConstantsCB.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(uint numParticles, float particleRadius, float voxelRestVolume, const LongRangeConstraints& constraints) {
        uint numConstraints = static_cast<uint>(constraints.particleIndices.size() / 8);
        numWorkgroups = Utils::divideRoundUp(numConstraints, VGS_THREADS);

        longRangeParticleIndicesBuffer = DirectX::createReadWriteBuffer(constraints.particleIndices);
        longRangeParticleIndicesSRV = DirectX::createSRV(longRangeParticleIndicesBuffer);
        longRangeParticleIndicesUAV = DirectX::createUAV(longRangeParticleIndicesBuffer);
        
        longRangeConstraintsCB = DirectX::createConstantBuffer<LongRangeConstraintsCB>({ numConstraints, 0, 0, 0 });

        // Defaults
        vgsConstants.relaxation = 0.5f;
        vgsConstants.edgeUniformity = 1.0f;
        vgsConstants.iterCount = 3;
        vgsConstants.numVoxels = numParticles / 8;
        vgsConstants.compliance = 0;

        // These two values get fudged a bit. Long range constraints treat 2x2x2 groups of voxels as a one voxel.
        // So the effective particle radius is **tripled** (not doubled - draw it out :)), and the rest volume is adjusted accordingly.
        vgsConstants.particleRadius = particleRadius * 3.0f;
        vgsConstants.voxelRestVolume = voxelRestVolume * (216.0f / 8.0f);
        vgsConstantsCB = DirectX::createConstantBuffer<VGSConstants>(vgsConstants);
    };
};