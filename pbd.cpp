#include "pbd.h"
#include "utils.h"
#include "cube.h"

std::array<FaceConstraints, 3> PBD::constructFaceToFaceConstraints(const MSharedPtr<Voxels> voxels, std::array<std::vector<int>, 3>& voxelToFaceConstraintIndices) {
    std::array<FaceConstraints, 3> faceConstraints;

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

            faceConstraints[j].voxelIndices.push_back(i);
            faceConstraints[j].voxelIndices.push_back(mortonCodesToSortedIdx.at(neighborMortonCode));

            faceConstraints[j].limits.push_back(0.0f); // Initial constraint limits - can be updated via voxel paint tool
            faceConstraints[j].limits.push_back(0.0f);

            voxelToFaceConstraintIndices[j][i] = static_cast<int>(faceConstraints[j].size() - 1);
        }
    }

    return faceConstraints;
}

LongRangeConstraints PBD::constructLongRangeConstraints(const MSharedPtr<Voxels> voxels, const std::array<std::vector<int>, 3>& voxelToFaceConstraintIndices, std::array<uint, 3> faceConstraintsCounts) {
    LongRangeConstraints longRangeConstraints;
    // Up to 4 long range constraint indices per face constraint index
    // Use 0xFFFFFFF as sentinel for no LR constraint
    longRangeConstraints.faceIdxToLRConstraintIndices[0].resize(4 * faceConstraintsCounts[0], 0xFFFFFFFF);
    longRangeConstraints.faceIdxToLRConstraintIndices[1].resize(4 * faceConstraintsCounts[1], 0xFFFFFFFF);
    longRangeConstraints.faceIdxToLRConstraintIndices[2].resize(4 * faceConstraintsCounts[2], 0xFFFFFFFF);

    const std::vector<uint32_t>& mortonCodes = voxels->mortonCodes;
    const std::unordered_map<uint32_t, uint32_t>& mortonCodesToSortedIdx = voxels->mortonCodesToSortedIdx;
    const int numOccupied = voxels->numOccupied;
    
    std::array<uint, 8> particleIndices;
    std::array<std::vector<uint>, 3> faceConstraintIndices; // 4 internal face constraints per axis per LR constraint
    std::array<std::vector<uint>, 3> faceConstraintVisitedCounts = { std::vector<uint>(faceConstraintsCounts[0], 0), std::vector<uint>(faceConstraintsCounts[1], 0), std::vector<uint>(faceConstraintsCounts[2], 0) };

    for (int i = 0; i < numOccupied; i++) {
        std::array<uint32_t, 3> voxelCoords;
        Utils::fromMortonCode(mortonCodes[i], voxelCoords[0], voxelCoords[1], voxelCoords[2]);
        bool hasAllNeighbors = true;
        faceConstraintIndices[0].clear(); faceConstraintIndices[1].clear(); faceConstraintIndices[2].clear();

        for (uint corner = 0; corner < 8; ++corner) {
            // Note that this can include the voxel itself (intentionally)
            std::array<uint32_t, 3> neighborCoords = {
                voxelCoords[0] + ((corner >> 0) & 1),
                voxelCoords[1] + ((corner >> 1) & 1),
                voxelCoords[2] + ((corner >> 2) & 1)
            }; 

            int neighborMortonCode = static_cast<int>(Utils::toMortonCode(neighborCoords[0], neighborCoords[1], neighborCoords[2]));
            if (mortonCodesToSortedIdx.find(neighborMortonCode) == mortonCodesToSortedIdx.end()) {
                hasAllNeighbors = false;
                break;
            };
            
            // Get the particle involved in the constraint from this neighbor voxel
            // Hijack the lower 4 bits of each entry to store a broken face constraint counter
            uint neighborVoxelIdx = mortonCodesToSortedIdx.at(neighborMortonCode);
            particleIndices[corner] = (neighborVoxelIdx * 8u + corner) << 4; // (28 bits for particle indices is far more than enough)

            // Get the face constraints this voxel contributes to this LR constraint
            for (int axis = 0; axis < 3; ++axis) {
                if (((corner >> axis) & 1) != 0) continue; // only internal faces

                int faceConstraintIdx = voxelToFaceConstraintIndices[axis][neighborVoxelIdx];
                if (faceConstraintIdx == -1) continue;

                faceConstraintIndices[axis].push_back(faceConstraintIdx);
                faceConstraintVisitedCounts[axis][faceConstraintIdx]++;
            }
        }

        if (!hasAllNeighbors) continue;
        
        // Record the particle indices for this long-range constraint
        longRangeConstraints.particleIndices.insert( longRangeConstraints.particleIndices.end(), particleIndices.begin(), particleIndices.end() );
        
        // Record the face constraint indices for this long-range constraint
        uint currentLRConstraintIdx = static_cast<uint>(longRangeConstraints.particleIndices.size() / 8 - 1);
        for (int axis = 0; axis < 3; ++axis) {
            for (uint faceConstraintIdx : faceConstraintIndices[axis]) {
                uint visitedCount = faceConstraintVisitedCounts[axis][faceConstraintIdx];
                uint lrConstraintArrayOffset = faceConstraintIdx * 4 + (visitedCount - 1);
                longRangeConstraints.faceIdxToLRConstraintIndices[axis][lrConstraintArrayOffset] = currentLRConstraintIdx;
            }
        }
    }

    return longRangeConstraints;
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

    renderParticlesBuffer = DirectX::createReadWriteBuffer(particles);
    renderParticlesUAV = DirectX::createUAV(renderParticlesBuffer);
    renderParticlesSRV = DirectX::createSRV(renderParticlesBuffer);

    return {
        totalParticles,
        &particles,
        &voxels->isSurface,
        particleRadius
    };
}

void PBD::createComputeShaders(
    const MSharedPtr<Voxels> voxels, 
    const std::array<FaceConstraints, 3>& faceConstraints,
    const LongRangeConstraints& longRangeConstraints
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

    longRangeConstraintsCompute = LongRangeConstraintsCompute(
        numParticles(),
        particleRadius,
        voxelRestVolume,
        longRangeConstraints
    );

	faceConstraintsCompute = FaceConstraintsCompute(
		faceConstraints,
        longRangeConstraints.faceIdxToLRConstraintIndices,
        numParticles(),
        particleRadius,
        voxelRestVolume
	);
    faceConstraintsCompute.setRenderParticlesUAV(renderParticlesUAV);
    faceConstraintsCompute.setLongRangeConstraintCountersUAV(longRangeConstraintsCompute.getLongRangeParticleIndicesUAV());

    preVGSCompute = PreVGSCompute(numParticles());
}

// See note in PBDNode destructor
// Only need to reset compute shaders that own buffers
void PBD::resetComputeShaders() {
    faceConstraintsCompute.reset();
    longRangeConstraintsCompute.reset();
}

void PBD::setGPUResourceHandles(
    ComPtr<ID3D11UnorderedAccessView> particleUAV,
    ComPtr<ID3D11UnorderedAccessView> oldParticlesUAV,
    ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV,
    ComPtr<ID3D11ShaderResourceView> isDraggingSRV
) {
    vgsCompute.setParticlesUAV(particleUAV);
    faceConstraintsCompute.setParticlesUAV(particleUAV);
    faceConstraintsCompute.setIsSurfaceUAV(isSurfaceUAV);
    preVGSCompute.setParticlesUAV(particleUAV);
    preVGSCompute.setOldParticlesUAV(oldParticlesUAV);
    preVGSCompute.setIsDraggingSRV(isDraggingSRV);
    longRangeConstraintsCompute.setParticlesUAV(particleUAV);
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
void PBD::updateSimulationParameters(const SimulationParameters& simParams) {
    if (simParams == simulationParameters) return;

    const float compliance = simParams.compliance / simParams.secondsPerFrame; // normalize compliance by timestep to keep behavior consistent at different substeps per frame.
    vgsCompute.updateVGSParameters(simParams.vgsRelaxation, simParams.vgsEdgeUniformity, static_cast<uint>(simParams.vgsIterations), compliance);
    faceConstraintsCompute.updateVGSParameters(simParams.vgsRelaxation, simParams.vgsEdgeUniformity, static_cast<uint>(simParams.vgsIterations), compliance);
    longRangeConstraintsCompute.updateVGSParameters(simParams.vgsRelaxation, simParams.vgsEdgeUniformity, static_cast<uint>(simParams.vgsIterations), compliance);
    preVGSCompute.updatePreVgsConstants(simParams.secondsPerFrame, simParams.gravityStrength);
}

void PBD::mergeRenderParticles() {
    faceConstraintsCompute.mergeRenderParticles();
}

void PBD::simulateSubstep() {
    if (!initialized) return;

    preVGSCompute.dispatch();
    vgsCompute.dispatch();
    longRangeConstraintsCompute.dispatch();
    faceConstraintsCompute.dispatch();
}