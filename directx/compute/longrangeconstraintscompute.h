#pragma once

#include "directx/compute/computeshader.h"

struct LongRangeConstraints {
    // Each group of 8 consecutive indices corresponds to one voxel's long-range constraint particles
    std::vector<uint> particleIndices;
    // Inverse mapping: each entry gives the constraint index for a given particle
    // The lower 4 bits are used as a counter for the number of broken face constraints within voxels involved
    // in this long-range constraint. When a number of face constraints have broken, the long range constraint is also broken.
    std::vector<uint> constraintIndicesAndCounters;
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

    ~LongRangeConstraintsCompute() {
        DirectX::notifyMayaOfMemoryUsage(longRangeParticleIndicesBuffer, false);
        DirectX::notifyMayaOfMemoryUsage(longRangeConstraintIndicesAndCountersBuffer, false);
        DirectX::notifyMayaOfMemoryUsage(longRangeConstraintsCB, false);
    }

    void dispatch() override {
        ComputeShader::dispatch(numWorkgroups);
    }

    const ComPtr<ID3D11UnorderedAccessView>& getConstraintIndicesAndCountersUAV() const {
        return longRangeConstraintIndicesAndCountersUAV;
    }

    void setParticlesUAV(const ComPtr<ID3D11UnorderedAccessView>& uav) {
        particlesUAV = uav;
    }

private:
    int numWorkgroups = 0;
    VGSConstants vgsConstants;
    // Owned resources
    ComPtr<ID3D11Buffer> longRangeParticleIndicesBuffer;
    ComPtr<ID3D11Buffer> longRangeConstraintIndicesAndCountersBuffer;
    ComPtr<ID3D11Buffer> longRangeConstraintsCB;
    ComPtr<ID3D11Buffer> vgsConstantsCB;
    ComPtr<ID3D11ShaderResourceView> longRangeParticleIndicesSRV;
    ComPtr<ID3D11UnorderedAccessView> longRangeConstraintIndicesAndCountersUAV;
    ComPtr<ID3D11ShaderResourceView> longRangeConstraintIndicesAndCountersSRV;
    // Passed-in resources
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;

    void bind() override
    {
        ID3D11ShaderResourceView* srvs[] = { longRangeParticleIndicesSRV.Get(), longRangeConstraintIndicesAndCountersSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { longRangeConstraintsCB.Get(), vgsConstantsCB.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        ID3D11ShaderResourceView* srvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(uint numParticles, float particleRadius, float voxelRestVolume, const LongRangeConstraints& constraints) {
        uint numConstraints = static_cast<uint>(constraints.particleIndices.size() / 8);
        numWorkgroups = Utils::divideRoundUp(numConstraints, VGS_THREADS);

        longRangeParticleIndicesBuffer = DirectX::createReadOnlyBuffer(constraints.particleIndices);
        longRangeParticleIndicesSRV = DirectX::createSRV(longRangeParticleIndicesBuffer);
        longRangeConstraintIndicesAndCountersBuffer = DirectX::createReadWriteBuffer(constraints.constraintIndicesAndCounters);
        longRangeConstraintIndicesAndCountersUAV = DirectX::createUAV(longRangeConstraintIndicesAndCountersBuffer);
        longRangeConstraintIndicesAndCountersSRV = DirectX::createSRV(longRangeConstraintIndicesAndCountersBuffer);
        
        longRangeConstraintsCB = DirectX::createConstantBuffer<LongRangeConstraintsCB>({ numConstraints, 0, 0, 0 });

        // For now, at least, hardcode relaxation and edge uniformity
        vgsConstants.relaxation = 0.5f;
        vgsConstants.edgeUniformity = 1.0f;
        vgsConstants.iterCount = 3;
        vgsConstants.numVoxels = numParticles / 8;

        // These two values get fudged a bit. Long range constraints treat 2x2x2 groups of voxels as a one voxel.
        // So the effective particle radius is **tripled** (not doubled - draw it out :)), and the rest volume is adjusted accordingly.
        vgsConstants.particleRadius = particleRadius * 3.0f;
        vgsConstants.voxelRestVolume = voxelRestVolume * (216.0f / 8.0f);
        vgsConstantsCB = DirectX::createConstantBuffer<VGSConstants>(vgsConstants);
    };
};