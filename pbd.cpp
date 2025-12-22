#include "pbd.h"
#include "utils.h"
#include "cube.h"
#include <maya/MFloatMatrix.h>

std::array<std::vector<FaceConstraint>, 3> PBD::constructFaceToFaceConstraints(const MSharedPtr<Voxels> voxels) {
    std::array<std::vector<FaceConstraint>, 3> faceConstraints;

    const std::vector<uint32_t>& mortonCodes = voxels->mortonCodes;
    const std::unordered_map<uint32_t, uint32_t>& mortonCodesToSortedIdx = voxels->mortonCodesToSortedIdx;
    const int numOccupied = voxels->numOccupied;

    for (int i = 0; i < numOccupied; i++) {
        std::array<uint32_t, 3> voxelCoords;
        Utils::fromMortonCode(mortonCodes[i], voxelCoords[0], voxelCoords[1], voxelCoords[2]);

        // Check each neighboring direction (x+, y+, z+) (only need to do half the neighbors to avoid double-counting)
        for (int j = 0; j < 3; j++) {
            std::array<uint32_t, 3> neighborCoords = voxelCoords;
            neighborCoords[j] += 1;
            int neighborMortonCode = static_cast<int>(Utils::toMortonCode(neighborCoords[0], neighborCoords[1], neighborCoords[2]));
            if (mortonCodesToSortedIdx.find(neighborMortonCode) == mortonCodesToSortedIdx.end()) continue;

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = mortonCodesToSortedIdx.at(neighborMortonCode);
            faceConstraints[j].push_back(newConstraint);
        }
    }

    return faceConstraints;
}

ParticleDataContainer PBD::createParticles(const MSharedPtr<Voxels> voxels) {
    const int numOccupied = voxels->numOccupied;
    const MMatrixArray& modelMatrices = voxels->modelMatrices;
    double scaleArr[3] = {1.0, 1.0, 1.0};
    float particleRadius = static_cast<float>(voxels->voxelSize) * 0.25f;

    for (int i = 0; i < numOccupied; i++) {
        MMatrix voxelToWorld = modelMatrices[i];
        MTransformationMatrix voxelTransform(voxelToWorld);
        voxelTransform.getScale(scaleArr, MSpace::kWorld);

        for (int j = 0; j < 8; j++) {
            // Offset the particle towards the center of the voxel by particleRadius along each axis
            MPoint corner = MPoint(cubeCorners[j][0], cubeCorners[j][1], cubeCorners[j][2]);
            corner -= ((particleRadius / scaleArr[0]) * Utils::sign(corner));
            
            corner = corner * voxelToWorld;
            uint32_t packedRadiusAndW = Utils::packTwoFloatsInUint32(particleRadius, 1.0f); // w is initialized to 1.0f but is user-editable via the voxel paint tool
            particles.push_back({static_cast<float>(corner.x), static_cast<float>(corner.y), static_cast<float>(corner.z), packedRadiusAndW});
            totalParticles++;
        }
    }

    return {
        totalParticles,
        &particles,
        &voxels->isSurface,
        particleRadius
    };
}

void PBD::createComputeShaders(
    const MSharedPtr<Voxels> voxels, 
    const std::array<std::vector<FaceConstraint>, 3>& faceConstraints
) {

    float particleRadius = static_cast<float>(voxels->voxelSize) * 0.25f;
    // This is really the rest volume of the cube made from particle centers, which are offset one particle radius from each corner of the voxel
    // towards the center of the voxel. So with a particle radius = 1/4 voxel edge length, the rest volume is (2 * 1/4 edge length)^3 or 8 * (particle radius^3) 
    float voxelRestVolume = 8.0f * particleRadius * particleRadius * particleRadius;

    vgsCompute = VGSCompute(
        numParticles(),
        particleRadius, 
        voxelRestVolume
    );

	faceConstraintsCompute = FaceConstraintsCompute(
		faceConstraints,
        numParticles(),
        particleRadius,
        voxelRestVolume
	);

    preVGSCompute = PreVGSCompute(numParticles());
}

void PBD::setGPUResourceHandles(
    ComPtr<ID3D11UnorderedAccessView> particleUAV,
    ComPtr<ID3D11UnorderedAccessView> oldParticlesUAV,
    ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV,
    ComPtr<ID3D11ShaderResourceView> isDraggingSRV
) {
    vgsCompute.setParticlesUAV(particleUAV);
    faceConstraintsCompute.setPositionsUAV(particleUAV);
    faceConstraintsCompute.setIsSurfaceUAV(isSurfaceUAV);
    preVGSCompute.setPositionsUAV(particleUAV);
    preVGSCompute.setOldPositionsUAV(oldParticlesUAV);
    preVGSCompute.setIsDraggingSRV(isDraggingSRV);
}

void PBD::updateFaceConstraintsWithPaintValues(
    const ComPtr<ID3D11UnorderedAccessView>& paintDeltaUAV, 
    const ComPtr<ID3D11UnorderedAccessView>& paintValueUAV, 
    float constraintLow, 
    float constraintHigh
) {
    faceConstraintsCompute.updateFaceConstraintsFromPaint(paintDeltaUAV, paintValueUAV, constraintLow, constraintHigh);
}

void PBD::updateParticleMassWithPaintValues(
    const ComPtr<ID3D11UnorderedAccessView>& paintDeltaUAV, 
    const ComPtr<ID3D11UnorderedAccessView>& paintValueUAV, 
    float massLow, 
    float massHigh
) {
    preVGSCompute.updateParticleMassFromPaintValues(paintDeltaUAV, paintValueUAV, massLow, massHigh);
}

// Note that FPS changes just make the playback choppier / smoother. A lower FPS means each frame is a bigger simulation timestep,
// but the same time passes overall. To make the sim *run* slower or faster, you need to change the timeslider playback speed factor.
void PBD::updateSimulationParameters(
    float vgsRelaxation,
    float vgsEdgeUniformity,
    float ftfRelaxation,
    float ftfEdgeUniformity,
    uint vgsIterations,
    float gravityStrength,
    float secondsPerFrame
) {
    vgsCompute.updateVGSParameters(vgsRelaxation, vgsEdgeUniformity, static_cast<uint>(vgsIterations));
    faceConstraintsCompute.updateVGSParameters(ftfRelaxation, ftfEdgeUniformity, static_cast<uint>(vgsIterations));
    preVGSCompute.updatePreVgsConstants(secondsPerFrame, gravityStrength);
}

void PBD::simulateSubstep() {
    if (!initialized) return;

    preVGSCompute.dispatch();
    vgsCompute.dispatch();
    faceConstraintsCompute.dispatch();
}