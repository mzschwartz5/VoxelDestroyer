#include "pbd.h"
#include "utils.h"

std::array<std::vector<FaceConstraint>, 3> PBD::constructFaceToFaceConstraints(const Voxels& voxels,
    float xTension, float xCompression,
    float yTension, float yCompression,
    float zTension, float zCompression) {
    std::array<std::vector<FaceConstraint>, 3> faceConstraints;

    for (int i = 0; i < voxels.numOccupied; i++) {
        uint32_t x, y, z;
        Utils::fromMortonCode(voxels.mortonCodes[i], x, y, z);

        int rightVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x + 1, y, z));
        int upVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x, y + 1, z));
        int frontVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x, y, z + 1));

        // Checks that the right voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(rightVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int rightNeighborIndex = voxels.mortonCodesToSortedIdx.at(rightVoxelMortonCode);
            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = rightNeighborIndex;
            newConstraint.compressionLimit = xCompression;
            newConstraint.tensionLimit = xTension;
            faceConstraints[0].push_back(newConstraint);
        }

        // Checks that the up voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(upVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int upNeighborIndex = voxels.mortonCodesToSortedIdx.at(upVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = upNeighborIndex;
            newConstraint.compressionLimit = yCompression;
            newConstraint.tensionLimit = yTension;
            faceConstraints[1].push_back(newConstraint);
        }

        // Checks that the front voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(frontVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int frontNeighborIndex = voxels.mortonCodesToSortedIdx.at(frontVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = frontNeighborIndex;
            newConstraint.compressionLimit = zCompression;
            newConstraint.tensionLimit = zTension;
            faceConstraints[2].push_back(newConstraint);
        }
    }

    return faceConstraints;
}

// Importantly: in Morton order (the order the VGS algorithm expects)
std::array<std::array<int, 3>, 8> cornerOffsets = {{
    {{0, 0, 0}},
    {{1, 0, 0}},
    {{0, 1, 0}},
    {{1, 1, 0}},
    {{0, 0, 1}},
    {{1, 0, 1}},
    {{0, 1, 1}},
    {{1, 1, 1}}
}};

ParticleDataContainer PBD::createParticles(const Voxels& voxels) {
    for (int i = 0; i < voxels.numOccupied; i++) {
        const VoxelDimensions& voxelDims = voxels.dimensions[i];
        const MFloatPoint& voxelMin = voxelDims.min;
        double edgeLength = voxelDims.edgeLength;
        const MFloatPoint voxelCenter = MFloatPoint(
            voxelMin.x + (edgeLength * 0.5),
            voxelMin.y + (edgeLength * 0.5),
            voxelMin.z + (edgeLength * 0.5)
        );

        for (int j = 0; j < 8; j++) {
            MFloatPoint corner = MFloatPoint(
                voxelMin.x + (cornerOffsets[j][0] * edgeLength),
                voxelMin.y + (cornerOffsets[j][1] * edgeLength),
                voxelMin.z + (cornerOffsets[j][2] * edgeLength)
            );

            // Offset the corner towards the center by the radius of the particle
            const MFloatPoint& position = corner - (PARTICLE_RADIUS * Utils::sign(corner - voxelCenter));
            float packedRadiusAndW = Utils::packTwoFloatsAsHalfs(PARTICLE_RADIUS, 1.0f); // for now, w is hardcoded to 1.0f
            particles.positions.push_back(MFloatPoint(position.x, position.y, position.z, packedRadiusAndW));
            particles.oldPositions.push_back(MFloatPoint(position.x, position.y, position.z, packedRadiusAndW));
            particles.numParticles++;
        }
    }

    return {
        particles.numParticles,
        particles.positions.data(),
        PARTICLE_RADIUS,
        voxels.isSurface.data()
    };
}

void PBD::createComputeShaders(
    const Voxels& voxels, 
    const std::array<std::vector<FaceConstraint>, 3>& faceConstraints
) {
    vgsCompute = VGSCompute(
        numParticles(),
        VGSConstantBuffer{ RELAXATION, BETA, PARTICLE_RADIUS, VOXEL_REST_VOLUME, 3.0f, FTF_RELAXATION, FTF_BETA, voxels.size() }
    );

	faceConstraintsCompute = FaceConstraintsCompute(
		faceConstraints,
        vgsCompute.getVoxelSimInfoBuffer()
	);

    PreVGSConstantBuffer preVGSConstants{GRAVITY_STRENGTH, GROUND_COLLISION_Y, TIMESTEP, numParticles()};
    preVGSCompute = PreVGSCompute(
        numParticles(),
        particles.oldPositions.data(),
		preVGSConstants
    );
}

void PBD::setGPUResourceHandles(
    ComPtr<ID3D11UnorderedAccessView> particleUAV,
    ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV,
    ComPtr<ID3D11ShaderResourceView> isDraggingSRV
) {
    vgsCompute.setParticlesUAV(particleUAV);
    faceConstraintsCompute.setPositionsUAV(particleUAV);
    faceConstraintsCompute.setIsSurfaceUAV(isSurfaceUAV);
    preVGSCompute.setPositionsUAV(particleUAV);
    preVGSCompute.setIsDraggingSRV(isDraggingSRV);
}


void PBD::simulateSubstep() {
    if (!initialized) return;

    preVGSCompute.dispatch();
    vgsCompute.dispatch();
    
    for (int i = 0; i < 3; i++) {
        faceConstraintsCompute.updateActiveConstraintAxis(i);
		faceConstraintsCompute.dispatch();
    }
}