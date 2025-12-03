#include "pbd.h"
#include "utils.h"
#include "cube.h"

std::array<std::vector<FaceConstraint>, 3> PBD::constructFaceToFaceConstraints(const MSharedPtr<Voxels> voxels) {
    std::array<std::vector<FaceConstraint>, 3> faceConstraints;

    const std::vector<uint32_t>& mortonCodes = voxels->mortonCodes;
    const std::unordered_map<uint32_t, uint32_t>& mortonCodesToSortedIdx = voxels->mortonCodesToSortedIdx;
    const int numOccupied = voxels->numOccupied;

    for (int i = 0; i < numOccupied; i++) {
        uint32_t x, y, z;
        Utils::fromMortonCode(mortonCodes[i], x, y, z);

        int rightVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x + 1, y, z));
        int upVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x, y + 1, z));
        int frontVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x, y, z + 1));

        // Checks that the right voxel is in the grid and is occupied
        if (mortonCodesToSortedIdx.find(rightVoxelMortonCode) != mortonCodesToSortedIdx.end()) {
            int rightNeighborIndex = mortonCodesToSortedIdx.at(rightVoxelMortonCode);
            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = rightNeighborIndex;
            faceConstraints[0].push_back(newConstraint);
        }

        // Checks that the up voxel is in the grid and is occupied
        if (mortonCodesToSortedIdx.find(upVoxelMortonCode) != mortonCodesToSortedIdx.end()) {
            int upNeighborIndex = mortonCodesToSortedIdx.at(upVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = upNeighborIndex;
            faceConstraints[1].push_back(newConstraint);
        }

        // Checks that the front voxel is in the grid and is occupied
        if (mortonCodesToSortedIdx.find(frontVoxelMortonCode) != mortonCodesToSortedIdx.end()) {
            int frontNeighborIndex = mortonCodesToSortedIdx.at(frontVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = frontNeighborIndex;
            faceConstraints[2].push_back(newConstraint);
        }
    }

    return faceConstraints;
}

ParticleDataContainer PBD::createParticles(const MSharedPtr<Voxels> voxels) {
    const int numOccupied = voxels->numOccupied;
    const MMatrixArray& modelMatrices = voxels->modelMatrices;
    double scaleArr[3] = {1.0, 1.0, 1.0};

    for (int i = 0; i < numOccupied; i++) {
        MTransformationMatrix tmat(modelMatrices[i]);
        MFloatPoint voxelCenter = tmat.getTranslation(MSpace::kWorld);
        tmat.getScale(scaleArr, MSpace::kWorld);
        const float edgeLength = static_cast<float>(scaleArr[0]);

        for (int j = 0; j < 8; j++) {
            MFloatPoint corner = MFloatPoint(
                voxelCenter.x + (cubeCorners[j][0] * edgeLength),
                voxelCenter.y + (cubeCorners[j][1] * edgeLength),
                voxelCenter.z + (cubeCorners[j][2] * edgeLength)
            );

            // Offset the corner towards the center by the radius of the particle
            const MFloatPoint position = corner - (particleRadius * Utils::sign(corner - voxelCenter));
            float packedRadiusAndW = Utils::packTwoFloatsAsHalfs(particleRadius, 1.0f); // for now, w is hardcoded to 1.0f
            particles.push_back(MFloatPoint(position.x, position.y, position.z, packedRadiusAndW));
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
    vgsCompute = VGSCompute(
        numParticles(),
        VGSConstantBuffer{ RELAXATION, BETA, particleRadius, VOXEL_REST_VOLUME, 3.0f, FTF_RELAXATION, FTF_BETA, voxels->size() }
    );

	faceConstraintsCompute = FaceConstraintsCompute(
		faceConstraints,
        vgsCompute.getVoxelSimInfoBuffer()
	);

    // Hardcode initial timestep of 1/60 (assumes 60 FPS and 10 substeps).
    PreVGSConstantBuffer preVGSConstants{GRAVITY_STRENGTH, GROUND_COLLISION_Y, 1.0f / 600.0f, numParticles()};
    preVGSCompute = PreVGSCompute(
        numParticles(),
		preVGSConstants
    );
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
void PBD::updateTimestep(float secondsPerFrame) {
    preVGSCompute.updateTimeStep(secondsPerFrame);
}

void PBD::simulateSubstep() {
    if (!initialized) return;

    preVGSCompute.dispatch();
    vgsCompute.dispatch();
    faceConstraintsCompute.dispatch();
}