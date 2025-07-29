#include "pbd.h"
#include <maya/MGlobal.h>
#include <float.h>
#include "utils.h"
#include "constants.h"
#include "custommayaconstructs/voxeldeformerGPUNode.h"

void PBD::initialize(const Voxels& voxels, float voxelSize, const MDagPath& meshDagPath) {
    this->meshDagPath = meshDagPath;
    timeStep = (1.0f / 60.0f) / static_cast<float>(substeps);
    constructFaceToFaceConstraints(voxels, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX);
    setRadiusAndVolumeFromLength(voxelSize);
    createParticles(voxels);

    vgsCompute = std::make_unique<VGSCompute>(
        particles.numParticles,
        particles.w.data(),
        particles.positions,
        VGSConstantBuffer{ RELAXATION, BETA, PARTICLE_RADIUS, VOXEL_REST_VOLUME, 3.0f, FTF_RELAXATION, FTF_BETA, voxels.size() }
    );

    VoxelDeformerGPUNode::initializeExternalKernelArgs(
        voxels.size(),
        vgsCompute->getParticlesBuffer().Get(),
        particles.positions,
        voxels.vertStartIdx
    );

	faceConstraintsCompute = std::make_unique<FaceConstraintsCompute>(
		faceConstraints,
        voxels.isSurface,
		vgsCompute->getParticlesUAV(),
		vgsCompute->getWeightsSRV(),
        vgsCompute->getVoxelSimInfoBuffer()
	);

    buildCollisionGridCompute = std::make_unique<BuildCollisionGridCompute>(
        particles.numParticles,
        PARTICLE_RADIUS,
        vgsCompute->getParticlesSRV(),
        faceConstraintsCompute->getIsSurfaceSRV()
    );

    prefixScanCompute = std::make_unique<PrefixScanCompute>(
        buildCollisionGridCompute->getCollisionCellParticleCountsUAV()
    );

    buildCollisionParticleCompute = std::make_unique<BuildCollisionParticlesCompute>(
        particles.numParticles,
        vgsCompute->getParticlesSRV(),
        buildCollisionGridCompute->getCollisionCellParticleCountsUAV(),
        buildCollisionGridCompute->getParticleCollisionCB(),
        faceConstraintsCompute->getIsSurfaceSRV()
    );

    dragParticlesCompute = std::make_unique<DragParticlesCompute>(
        vgsCompute->getParticlesUAV(),
        voxels.size(),
        substeps
    );

    PreVGSConstantBuffer preVGSConstants{GRAVITY_STRENGTH, GROUND_COLLISION_Y, TIMESTEP, particles.numParticles};
    preVGSCompute = std::make_unique<PreVGSCompute>(
        particles.numParticles,
        particles.oldPositions.data(),
		preVGSConstants,
        vgsCompute->getWeightsSRV(),
        vgsCompute->getParticlesUAV(),
        dragParticlesCompute->getIsDraggingUAV()
    );

    initialized = true;
}

void PBD::constructFaceToFaceConstraints(const Voxels& voxels,
    float xTension, float xCompression,
    float yTension, float yCompression,
    float zTension, float zCompression) {
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
            addFaceConstraint(newConstraint, 0);
        }

        // Checks that the up voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(upVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int upNeighborIndex = voxels.mortonCodesToSortedIdx.at(upVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = upNeighborIndex;
            newConstraint.compressionLimit = yCompression;
            newConstraint.tensionLimit = yTension;
            addFaceConstraint(newConstraint, 1);
        }

        // Checks that the front voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(frontVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int frontNeighborIndex = voxels.mortonCodesToSortedIdx.at(frontVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = frontNeighborIndex;
            newConstraint.compressionLimit = zCompression;
            newConstraint.tensionLimit = zTension;
            addFaceConstraint(newConstraint, 2);
        }
    }
}

void PBD::createParticles(const Voxels& voxels) {
    for (int i = 0; i < voxels.numOccupied; i++) {
        glm::vec3 voxelCenter = 0.5f * (voxels.corners[i].corners[0] + voxels.corners[i].corners[7]);

        for (const auto& corner : voxels.corners[i].corners) {
            // Offset the corner towards the center by the radius of the particle
            const glm::vec3& position = corner - (PARTICLE_RADIUS * glm::sign(corner - voxelCenter));
            particles.positions.push_back(vec4(position, 1.0f));
            particles.oldPositions.push_back(vec4(position, 1.0f));
            particles.w.push_back(1.0f);
            particles.numParticles++;
        }
    }
}

void PBD::simulateStep()
{   
    for (int i = 0; i < substeps; i++)
    {
        simulateSubstep();
    }
}

void PBD::simulateSubstep() {
    int numPreVgsComputeWorkgroups = (particles.numParticles + VGS_THREADS + 1) / (VGS_THREADS);
    preVGSCompute->dispatch(numPreVgsComputeWorkgroups);

    int numVgsWorkgroups = ((particles.numParticles >> 3) + VGS_THREADS + 1) / (VGS_THREADS); 

    if (isDragging) {
        dragParticlesCompute->dispatch(numVgsWorkgroups);
    }

    vgsCompute->dispatch(numVgsWorkgroups);
    
    for (int i = 0; i < faceConstraints.size(); i++) {
        faceConstraintsCompute->updateActiveConstraintAxis(i);
		faceConstraintsCompute->dispatch(static_cast<int>((faceConstraints[i].size() + VGS_THREADS + 1) / VGS_THREADS));
    }

    int numBuildCollisionGridWorkgroups = (particles.numParticles + BUILD_COLLISION_GRID_THREADS + 1) / BUILD_COLLISION_GRID_THREADS;
    buildCollisionGridCompute->dispatch(numBuildCollisionGridWorkgroups);

    prefixScanCompute->dispatch(0); // dummy dispatch #. Shader handles the size internally. (It's a todo to refactor dispatches to all do it internally)
    buildCollisionParticleCompute->dispatch(0); // dummy dispath
}

void PBD::setSimValuesFromUI() {
    MStatus status;

    MFnDagNode dagNode(meshDagPath, &status);

    if (status != MS::kSuccess || !dagNode.hasAttribute("voxelSimulationNode")) {
        MGlobal::displayInfo("Failed to find voxelSimulationNode: " + dagNode.name());

        return;
    }

    MGlobal::displayInfo("Found mesh with voxelSimulationNode: " + dagNode.name());

    // Get the connected VoxelSimulationNode
    MPlug voxelSimNodePlug = dagNode.findPlug("voxelSimulationNode", false, &status);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to find voxelSimulationNode plug.");
        return;
    }

    MPlugArray connectedPlugs;
    voxelSimNodePlug.connectedTo(connectedPlugs, true, false, &status);
    if (status != MS::kSuccess || connectedPlugs.length() == 0) {
        MGlobal::displayError("No VoxelSimulationNode connected to the mesh.");
        return;
    }

    MObject voxelSimNodeObj = connectedPlugs[0].node();
    MFnDependencyNode voxelSimNodeFn(voxelSimNodeObj, &status);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to access the VoxelSimulationNode.");
        return;
    }

    // Retrieve the relaxation and edgeUniformity attributes
    MPlug relaxationPlug = voxelSimNodeFn.findPlug("relaxation", false, &status);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to find relaxation attribute.");
        return;
    }

    MPlug edgeUniformityPlug = voxelSimNodeFn.findPlug("edgeUniformity", false, &status);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to find edgeUniformity attribute.");
        return;
    }

	MPlug gravityStrengthPlug = voxelSimNodeFn.findPlug("gravityStrength", false, &status);
	if (status != MS::kSuccess) {
		MGlobal::displayError("Failed to find gravityStrength attribute.");
		return;
	}

	MPlug faceToFaceRelaxationPlug = voxelSimNodeFn.findPlug("faceToFaceRelaxation", false, &status);
	if (status != MS::kSuccess) {
		MGlobal::displayError("Failed to find faceToFaceRelaxation attribute.");
		return;
	}

	MPlug faceToFaceEdgeUniformityPlug = voxelSimNodeFn.findPlug("faceToFaceEdgeUniformity", false, &status);
	if (status != MS::kSuccess) {
		MGlobal::displayError("Failed to find faceToFaceEdgeUniformity attribute.");
		return;
	}

    float relaxationValue;
    float edgeUniformityValue;
	float gravityStrengthValue;
	float faceToFaceRelaxationValue;
	float faceToFaceEdgeUniformityValue;

    relaxationPlug.getValue(relaxationValue);
    edgeUniformityPlug.getValue(edgeUniformityValue);
	gravityStrengthPlug.getValue(gravityStrengthValue);
	faceToFaceRelaxationPlug.getValue(faceToFaceRelaxationValue);
	faceToFaceEdgeUniformityPlug.getValue(faceToFaceEdgeUniformityValue);

    RELAXATION = relaxationValue;
	BETA = edgeUniformityValue;
	GRAVITY_STRENGTH = gravityStrengthValue;
	FTF_RELAXATION = faceToFaceRelaxationValue;
	FTF_BETA = faceToFaceEdgeUniformityValue;

	// Update the simulation values
    updateVGSInfo();
	updateSimInfo();
}
