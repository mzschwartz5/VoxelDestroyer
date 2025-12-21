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

    void setRadiusAndVolumeFromLength(float edge_length) {
        particleRadius = edge_length * 0.25f;
        voxelRestVolume = 8.0f * particleRadius * particleRadius * particleRadius;
    }

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
    std::vector<MFloatPoint> particles;
    uint totalParticles{ 0 };
    bool initialized = false;

    // Shaders
    VGSCompute vgsCompute;
    FaceConstraintsCompute faceConstraintsCompute;
    PreVGSCompute preVGSCompute;

    float BETA{ 0.0f };
    float particleRadius{ 0.25f };

    float RELAXATION{ 0.5f };
    // This is really the rest volume of the volume between particles, which are offset one particle radius from each corner of the voxel
    // towards the center of the voxel. So with a particle radius = 1/4 voxel edge length, the rest volume is (2 * 1/4 edge length)^3 or 8 * (particle radius^3) 
    float voxelRestVolume{ 1.0f };

    float FTF_BETA{ 0.f };
    float FTF_RELAXATION{ 0.5f };

	float GRAVITY_STRENGTH { -10.f };
	float GROUND_COLLISION_ENABLED{ 1.f };
	float GROUND_COLLISION_Y{ 0.f };
};
