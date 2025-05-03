#include "pbd.h"
#include <maya/MGlobal.h>
#include <float.h>
#include "utils.h"
#include "constants.h"

void PBD::initialize(const Voxels& voxels, float voxelSize, const MDagPath& meshDagPath) {
    this->meshDagPath = meshDagPath;
    timeStep = (1.0f / 60.0f) / static_cast<float>(substeps);
    constructFaceToFaceConstraints(voxels);
    createParticles(voxels);
    setRadiusAndVolumeFromLength(voxelSize);

    MStatus status;
	MFnMesh voxelMeshFn(meshDagPath);

	// Calculate a local rest position for each vertex in every voxel.
	bindVerticesCompute = std::make_unique<BindVerticesCompute>(
		static_cast<int>(voxels.size()) * 8, // 8 particles per voxel
		voxelMeshFn.getRawPoints(&status),
		voxelMeshFn.numVertices(&status),
        particles.positions,
		voxels.vertStartIdx, 
		voxels.numVerts
	);
	bindVerticesCompute->dispatch(voxels.size());

	MGlobal::displayInfo("Bind vertices compute shader dispatched.");

	transformVerticesCompute = std::make_unique<TransformVerticesCompute>(
		voxelMeshFn.numVertices(&status),
		bindVerticesCompute->getParticlesSRV(), 			
		bindVerticesCompute->getVertStartIdxSRV(), 
		bindVerticesCompute->getNumVerticesSRV(), 
		bindVerticesCompute->getLocalRestPositionsSRV()
	);
	transformVerticesNumWorkgroups = voxels.size();

	MGlobal::displayInfo("Transform vertices compute shader initialized.");

    setSimValuesFromUI(meshDagPath);

    voxelSimInfo[0] = glm::vec4(RELAXATION, BETA, PARTICLE_RADIUS, VOXEL_REST_VOLUME);
    voxelSimInfo[1] = glm::vec4(3.0, 0, 0, 0); //iter count, axis, padding, padding

    vgsCompute = std::make_unique<VGSCompute>(
        particles.numParticles,
        particles.w.data(),
        voxelSimInfo,
        bindVerticesCompute->getParticlesUAV()
    );

	faceConstraintsCompute = std::make_unique<FaceConstraintsCompute>(
		faceConstraints,
		bindVerticesCompute->getParticlesUAV(),
		vgsCompute->getWeightsSRV(),
        vgsCompute->getVoxelSimInfoBuffer()
	);

    preVGSCompute = std::make_unique<PreVGSCompute>(
        particles.numParticles,
        particles.oldPositions.data(),
        particles.velocities.data(),
        vgsCompute->getWeightsSRV(),
        bindVerticesCompute->getParticlesUAV()
    );

    postVGSCompute = std::make_unique<PostVGSCompute>(
        vgsCompute->getWeightsSRV(),
        bindVerticesCompute->getParticlesSRV(),
        preVGSCompute->getOldPositionsSRV(),
        preVGSCompute->getVelocitiesUAV()
    );
}

void PBD::constructFaceToFaceConstraints(const Voxels& voxels) {
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
            newConstraint.compressionLimit = -FLT_MAX;
            newConstraint.tensionLimit = FLT_MAX;
            addFaceConstraint(newConstraint, 0);
        }

        // Checks that the up voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(upVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int upNeighborIndex = voxels.mortonCodesToSortedIdx.at(upVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = upNeighborIndex;
            newConstraint.compressionLimit = -FLT_MAX;
            newConstraint.tensionLimit = FLT_MAX;
            addFaceConstraint(newConstraint, 1);
        }

        // Checks that the front voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(frontVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int frontNeighborIndex = voxels.mortonCodesToSortedIdx.at(frontVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = frontNeighborIndex;
            newConstraint.compressionLimit = -FLT_MAX;
            newConstraint.tensionLimit = FLT_MAX;
            addFaceConstraint(newConstraint, 2);
        }
    }
}

void PBD::createParticles(const Voxels& voxels) {
    for (int i = 0; i < voxels.numOccupied; i++) {
        for (const auto& position : voxels.corners[i].corners) {
            particles.positions.push_back(vec4(position, 0.0f));
            particles.oldPositions.push_back(vec4(position, 0.0f));
            particles.velocities.push_back(vec4(0.0f));
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

void PBD::updateMeshVertices() {
	MFnMesh meshFn(meshDagPath);
	MFloatPointArray vertexArray;

	// For rendering, we need to update each voxel with its new basis, which we'll use to transform all vertices owned by that voxel
	transformVerticesCompute->dispatch(transformVerticesNumWorkgroups);
	transformVerticesCompute->copyTransformedVertsToCPU(vertexArray, meshFn.numVertices());

	meshFn.setPoints(vertexArray, MSpace::kWorld);
	meshFn.updateSurface();
}

void PBD::simulateSubstep() {
    int numPreAndPostVgsComputeWorkgroups = (particles.numParticles + VGS_THREADS + 1) / (VGS_THREADS);
    preVGSCompute->dispatch(numPreAndPostVgsComputeWorkgroups);

    int numVgsWorkgroups = ((particles.numParticles >> 3) + VGS_THREADS + 1) / (VGS_THREADS); 
    vgsCompute->dispatch(numVgsWorkgroups);
    
    for (int i = 0; i < faceConstraints.size(); i++) {
        updateAxis(i);
		faceConstraintsCompute->dispatch(int(((faceConstraints.size()) + VGS_THREADS + 1) / (VGS_THREADS)));
    }

    postVGSCompute->dispatch(numPreAndPostVgsComputeWorkgroups);
}

vec4 PBD::project(vec4 x, vec4 y) {
    return (glm::dot(y, x) / glm::dot(y, y)) * y;
}

void PBD::setSimValuesFromUI(const MDagPath& dagPath) {
    MStatus status;

    MFnDagNode dagNode(dagPath, &status);

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

    float relaxationValue;
    float edgeUniformityValue;

    relaxationPlug.getValue(relaxationValue);
    edgeUniformityPlug.getValue(edgeUniformityValue);

    // Display the values
    RELAXATION = relaxationValue;
    MGlobal::displayInfo("Set relaxation to: " + MString() + relaxationValue + " for " + dagPath.fullPathName());
	BETA = edgeUniformityValue;
    MGlobal::displayInfo("Set edge uniformity to: " + MString() + edgeUniformityValue + " for " + dagPath.fullPathName());
}
