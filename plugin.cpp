#include "plugin.h"
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MEventMessage.h>
#include <maya/MSelectionList.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MDagPath.h>
#include <maya/MItSelectionList.h>
#include "pbd.h"
#include <vector>
#include "voxelizer.h"
#include "directx.h"

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)

Voxelizer voxelizer;
PBD pbdSimulator;
MCallbackId callbackId;
MDagPath voxelizedMeshDagPath;
DirectX dx;

// Maya Plugin creator function
void* plugin::creator()
{
	return new plugin;
}

// Define the syntax for the command
MSyntax plugin::syntax()
{
	MSyntax syntax;
	syntax.addFlag("-n", "-name", MSyntax::kString);
	syntax.addFlag("-i", "-identifier", MSyntax::kLong);
	return syntax;
}

void simulatePBDStep(void* clientData) {
	const std::vector<Particle>& particles = pbdSimulator.simulateStep();

	MFnMesh meshFn(voxelizedMeshDagPath);
	MPointArray vertexArray;
	meshFn.getPoints(vertexArray, MSpace::kWorld);

		// Update the vertex positions based on the corresponding particles
		size_t startIdx = range.first;
		size_t endIdx = range.second;
		for (size_t i = startIdx; i < endIdx; ++i) {
			vertexArray[i - startIdx] = MPoint(particles[i].position.x, particles[i].position.y, particles[i].position.z);
		}

		// Apply the updated vertex positions to the mesh
		meshFn.setPoints(vertexArray, MSpace::kWorld);
		meshFn.updateSurface();
	}

	// Refresh the viewport
	MGlobal::executeCommand("refresh");
}

// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MStatus status;

	std::vector<Voxel> voxels = voxelizer.voxelizeSelectedMesh(
		1.76f,
		0.25f, // voxel size
		MPoint(0.0f, 0.875f, 0.0f), // grid center
		voxelizedMeshDagPath,
		status
	);

	MGlobal::displayInfo("Mesh voxelized. Dag path: " + voxelizedMeshDagPath.fullPathName());

	// Iterate over voxels and collect voxels.corners into a single particle list for the PBD simulator
	std::vector<glm::vec3> particlePositions;
	for (const auto& voxel : voxels) {
		if (!voxel.occupied) continue;
		
		for (const auto& corner : voxel.corners) {
			particlePositions.push_back(glm::vec3(corner.x, corner.y, corner.z));
		}
	}

	pbdSimulator = PBD(particlePositions);

	MGlobal::displayInfo("PBD particles initialized.");

	// dx.dispatchComputeShaders();
	MGlobal::displayInfo("Compute shaders dispatched.");

	return status;
}


// Initialize Maya Plugin upon loading
EXPORT MStatus initializePlugin(MObject obj)
{
	MStatus status;
	MFnPlugin plugin(obj, "VoxelDestroyer", "1.0", "Any");
	status = plugin.registerCommand("VoxelDestroyer", plugin::creator, plugin::syntax);
	if (!status)
		status.perror("registerCommand failed");

	// Register a callback
	callbackId = MEventMessage::addEventCallback("timeChanged", simulatePBDStep);
	if (callbackId == 0) {
		MGlobal::displayError("Failed to register callback");
		return MStatus::kFailure;
	}

	// Initialize DirectX
	// MhInstPlugin is a global variable defined in the MfnPlugin.h file
	// dx = DirectX(MhInstPlugin);
	voxelizer = Voxelizer();
	
	return status;
}

// Cleanup Plugin upon unloading
EXPORT MStatus uninitializePlugin(MObject obj)
{
	MStatus status;
	MFnPlugin plugin(obj);
	status = plugin.deregisterCommand("VoxelDestroyer");
	if (!status)
		status.perror("deregisterCommand failed");

	// Deregister the callback
	MEventMessage::removeCallback(callbackId);

	// dx.tearDown();

	return status;
}
