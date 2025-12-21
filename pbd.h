#pragma once

#include "voxelizer.h"

#include <vector>
#include <array>
#include "directx/compute/vgscompute.h"
#include "directx/compute/prevgscompute.h"
#include "directx/compute/faceconstraintscompute.h"
#include "custommayaconstructs/data/particledata.h"

#include <maya/MSharedPtr.h>

class PBD
{
public:

    PBD() = default;
    ~PBD() = default;
   
    std::array<std::vector<FaceConstraint>, 3> constructFaceToFaceConstraints(MSharedPtr<Voxels> voxels);

    ParticleDataContainer createParticles(MSharedPtr<Voxels> voxels);

    void createComputeShaders(
        MSharedPtr<Voxels> voxels, 
        const std::array<std::vector<FaceConstraint>, 3>& faceConstraints
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

private:
    // Inverse mass (w) and particle radius stored, packed at half-precision, as 4th component.
    // TODO: particles should be stored on the PBD node as an attribute 
    // (we have to hold onto them in CPU memory because they're used to recreate the one-big-buffer whenever a PBD node is added or deleted,
    //  and they should be stored on the node so that they get saved with the scene)
    std::vector<MFloatPoint> particles;
    uint totalParticles{ 0 };
    bool initialized = false;

    // Shaders
    VGSCompute vgsCompute;
    FaceConstraintsCompute faceConstraintsCompute;
    PreVGSCompute preVGSCompute;
};
