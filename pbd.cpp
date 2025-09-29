#include "pbd.h"
#include "utils.h"

std::array<std::vector<FaceConstraint>, 3> PBD::constructFaceToFaceConstraints(
    const MSharedPtr<Voxels> voxels,
    float xTension, float xCompression,
    float yTension, float yCompression,
    float zTension, float zCompression
) {
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
            newConstraint.compressionLimit = xCompression;
            newConstraint.tensionLimit = xTension;
            faceConstraints[0].push_back(newConstraint);
        }

        // Checks that the up voxel is in the grid and is occupied
        if (mortonCodesToSortedIdx.find(upVoxelMortonCode) != mortonCodesToSortedIdx.end()) {
            int upNeighborIndex = mortonCodesToSortedIdx.at(upVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = upNeighborIndex;
            newConstraint.compressionLimit = yCompression;
            newConstraint.tensionLimit = yTension;
            faceConstraints[1].push_back(newConstraint);
        }

        // Checks that the front voxel is in the grid and is occupied
        if (mortonCodesToSortedIdx.find(frontVoxelMortonCode) != mortonCodesToSortedIdx.end()) {
            int frontNeighborIndex = mortonCodesToSortedIdx.at(frontVoxelMortonCode);

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
std::array<std::array<float, 3>, 8> cornerOffsets = {{
    {{-0.5f, -0.5f, -0.5f}},
    {{ 0.5f, -0.5f, -0.5f}},
    {{-0.5f,  0.5f, -0.5f}},
    {{ 0.5f,  0.5f, -0.5f}},
    {{-0.5f, -0.5f,  0.5f}},
    {{ 0.5f, -0.5f,  0.5f}},
    {{-0.5f,  0.5f,  0.5f}},
    {{ 0.5f,  0.5f,  0.5f}}
}};

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
                voxelCenter.x + (cornerOffsets[j][0] * edgeLength),
                voxelCenter.y + (cornerOffsets[j][1] * edgeLength),
                voxelCenter.z + (cornerOffsets[j][2] * edgeLength)
            );

            // Offset the corner towards the center by the radius of the particle
            const MFloatPoint& position = corner - (PARTICLE_RADIUS * Utils::sign(corner - voxelCenter));
            float packedRadiusAndW = Utils::packTwoFloatsAsHalfs(PARTICLE_RADIUS, 1.0f); // for now, w is hardcoded to 1.0f
            particles.push_back(MFloatPoint(position.x, position.y, position.z, packedRadiusAndW));
            totalParticles++;
        }
    }

    return {
        totalParticles,
        &particles,
        &voxels->isSurface,
        PARTICLE_RADIUS
    };
}

void PBD::createComputeShaders(
    const MSharedPtr<Voxels> voxels, 
    const std::array<std::vector<FaceConstraint>, 3>& faceConstraints
) {
    vgsCompute = VGSCompute(
        numParticles(),
        VGSConstantBuffer{ RELAXATION, BETA, PARTICLE_RADIUS, VOXEL_REST_VOLUME, 3.0f, FTF_RELAXATION, FTF_BETA, voxels->size() }
    );

	faceConstraintsCompute = FaceConstraintsCompute(
		faceConstraints,
        vgsCompute.getVoxelSimInfoBuffer()
	);

    PreVGSConstantBuffer preVGSConstants{GRAVITY_STRENGTH, GROUND_COLLISION_Y, TIMESTEP, numParticles()};
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


void PBD::simulateSubstep() {
    if (!initialized) return;

    preVGSCompute.dispatch();
    vgsCompute.dispatch();
    
    for (int i = 0; i < 3; i++) {
        faceConstraintsCompute.updateActiveConstraintAxis(i);
		faceConstraintsCompute.dispatch();
    }
}