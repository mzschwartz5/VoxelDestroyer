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
    createParticles(voxels);
    setRadiusAndVolumeFromLength(voxelSize);

    vgsCompute = std::make_unique<VGSCompute>(
        particles.numParticles,
        particles.w.data(),
        particles.positions,
        VGSConstantBuffer{ RELAXATION, BETA, PARTICLE_RADIUS, VOXEL_REST_VOLUME, 3.0f, 0.0f, FTF_RELAXATION, FTF_BETA }
    );

    VoxelDeformerGPUNode::initializeExternalKernelArgs(
        voxels.size(),
        vgsCompute->getParticlesBuffer().Get(),
        particles.positions,
        voxels.vertStartIdx
    );

    // Hard coded for now. Later set up via UI.
    CollisionVolume collisionVolume{
        { -3.0f, 0.0f, -3.0f }, // gridMin
        { 6, 6, 6 }, // gridDims
        { 1.0f, 1.0f, 1.0f } // gridInvCellDims
    };

    buildCollisionGridCompute = std::make_unique<BuildCollisionGridCompute>(
        collisionVolume,
        voxelSize,
        vgsCompute->getParticlesSRV(),
        voxels.isSurface
    );

	faceConstraintsCompute = std::make_unique<FaceConstraintsCompute>(
		faceConstraints,
		vgsCompute->getParticlesUAV(),
		vgsCompute->getWeightsSRV(),
        vgsCompute->getVoxelSimInfoBuffer(),
        buildCollisionGridCompute->getIsSurfaceUAV()
	);

	simInfo = glm::vec4(GRAVITY_STRENGTH, GROUND_COLLISION_ENABLED, GROUND_COLLISION_Y, TIMESTEP);

    dragParticlesCompute = std::make_unique<DragParticlesCompute>(
        vgsCompute->getParticlesUAV(),
        voxels.size(),
        substeps
    );

    preVGSCompute = std::make_unique<PreVGSCompute>(
        particles.numParticles,
        particles.oldPositions.data(),
		&simInfo,
        vgsCompute->getWeightsSRV(),
        vgsCompute->getParticlesUAV(),
        dragParticlesCompute->getIsDraggingUAV()
    );

    solveCollisionsCompute = std::make_unique<SolveCollisionsCompute>(
        voxelSize,
        PARTICLE_RADIUS,
        216, // num collision grid cells (hardcoded for now)
        vgsCompute->getParticlesUAV(),
        buildCollisionGridCompute->getCollisionVoxelCountsSRV(),
        buildCollisionGridCompute->getCollisionVoxelIndicesSRV()
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
        for (const auto& position : voxels.corners[i].corners) {
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

    // Way to do this once per step instead of once per substep?
    int numCollisionGridCells = 216; // hardcoded 6x6x6 for now
    int numCollisionGridWorkgroups = (numCollisionGridCells + BUILD_COLLISION_THREADS - 1) / BUILD_COLLISION_THREADS;
    buildCollisionGridCompute->dispatch(numCollisionGridWorkgroups);

    int numVgsWorkgroups = ((particles.numParticles >> 3) + VGS_THREADS + 1) / (VGS_THREADS); 

    if (isDragging) {
        dragParticlesCompute->dispatch(numVgsWorkgroups);
    }

    vgsCompute->dispatch(numVgsWorkgroups);
    
    for (int i = 0; i < faceConstraints.size(); i++) {
        faceConstraintsCompute->updateActiveConstraintAxis(i);
		faceConstraintsCompute->dispatch(static_cast<int>((faceConstraints[i].size() + VGS_THREADS + 1) / VGS_THREADS));
    }

    int numCollisionSolveWorkgroups = (numCollisionGridCells + SOLVE_COLLISION_THREADS - 1) / SOLVE_COLLISION_THREADS;
    // solveCollisionsCompute->dispatch(numCollisionSolveWorkgroups);
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
