#pragma once

#include "voxelizer.h"

#include <vector>
#include <array>
#include "directx/compute/vgscompute.h"
#include "directx/compute/prevgscompute.h"
#include "directx/compute/faceconstraintscompute.h"
#include "custommayaconstructs/data/particledata.h"
#include "directx/compute/longrangeconstraintscompute.h"

#include <maya/MSharedPtr.h>

class PBD
{
public:

    PBD() = default;
    ~PBD() = default;
   
    std::array<std::vector<FaceConstraint>, 3> constructFaceToFaceConstraints(MSharedPtr<Voxels> voxels, std::array<std::vector<int>, 3>& voxelToFaceConstraintIndices);

    LongRangeConstraints constructLongRangeConstraints(MSharedPtr<Voxels> voxels, const std::array<std::vector<int>, 3>& voxelToFaceConstraintIndices, std::array<uint, 3> faceConstraintsCounts);

    ParticleDataContainer createParticles(MSharedPtr<Voxels> voxels);

    void createComputeShaders(
        MSharedPtr<Voxels> voxels, 
        const std::array<std::vector<FaceConstraint>, 3>& faceConstraints,
        const LongRangeConstraints& longRangeConstraints
    );

    void setGPUResourceHandles(
        ComPtr<ID3D11UnorderedAccessView> particleUAV,
        ComPtr<ID3D11UnorderedAccessView> oldParticlesUAV,
        ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV,
        ComPtr<ID3D11ShaderResourceView> isDraggingSRV
    );

    void setInitialized(bool initialized) {
        this->initialized = initialized;
    }

    uint numParticles() const {
        return totalParticles;
    }

    void updateFaceConstraintsWithPaintValues(
        const ComPtr<ID3D11UnorderedAccessView>& paintDeltaUAV,
        const ComPtr<ID3D11UnorderedAccessView>& paintValueUAV,
        float constraintLow, 
        float constraintHigh
    );

    void updateParticleMassWithPaintValues(
        const ComPtr<ID3D11UnorderedAccessView>& paintDeltaUAV,
        const ComPtr<ID3D11UnorderedAccessView>& paintValueUAV,
        float constraintLow, 
        float constraintHigh
    );

    void mergeRenderParticles();
    
    void simulateSubstep();

    void updateSimulationParameters(
        float vgsRelaxation,
        float vgsEdgeUniformity,
        float ftfRelaxation,
        float ftfEdgeUniformity,
        uint vgsIterations,
        float gravityStrength,
        float secondsPerFrame
    );

    const ComPtr<ID3D11ShaderResourceView>& getRenderParticlesSRV() const {
        return renderParticlesSRV;
    }

private:
    // Inverse mass (w) and particle radius stored, packed at half-precision, as 4th component.
    // TODO: particles do not need to be stored after the node is done initializing. (The global solver maintains them on the GPU, and should save them as a node attribute).
    std::vector<Particle> particles;
    uint totalParticles{ 0 };
    bool initialized = false;
    int totalSubsteps = 0;
    ComPtr<ID3D11Buffer> renderParticlesBuffer;
    ComPtr<ID3D11UnorderedAccessView> renderParticlesUAV;
    ComPtr<ID3D11ShaderResourceView> renderParticlesSRV;

    // Shaders
    VGSCompute vgsCompute;
    FaceConstraintsCompute faceConstraintsCompute;
    PreVGSCompute preVGSCompute;
    LongRangeConstraintsCompute longRangeConstraintsCompute;
};
