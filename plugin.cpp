#include "plugin.h"
#include <maya/MFnPlugin.h>
#include <maya/MEventMessage.h>
#include <maya/MSelectionList.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MDagPath.h>
#include <maya/MItSelectionList.h>
#include "pbd.h"
#include <vector>
#include "voxelizer.h"
#include "directx/directx.h"
#include "constants.h"
#include <memory>

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)

Voxelizer voxelizer;
PBD pbdSimulator;
MCallbackId callbackId;
MDagPath voxelizedMeshDagPath;

// Compute shaders
std::unique_ptr<UpdateVoxelBasesCompute> updateVoxelBasesCompute;
int updateVoxelBasesNumWorkgroups;

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
	const Particles& particles = pbdSimulator.simulateStep();

	MFnMesh meshFn(voxelizedMeshDagPath);
	MPointArray vertexArray;
	meshFn.getPoints(vertexArray, MSpace::kWorld);

	int idx = 0;
	for (int i = 0; i < particles.numParticles; i++) {
		vertexArray[idx] = MPoint(particles.positions[i].x, particles.positions[i].y, particles.positions[i].z);
		idx++;
	}

	updateVoxelBasesCompute->updateParticleBuffer(particles.positions);
	updateVoxelBasesCompute->dispatch(updateVoxelBasesNumWorkgroups);

	meshFn.setPoints(vertexArray, MSpace::kWorld);
	meshFn.updateSurface();

	MGlobal::executeCommand("refresh");
}

// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MStatus status;
	float voxelSize = 0.1f;
	std::vector<Voxel> voxels = voxelizer.voxelizeSelectedMesh(
		1.0f, //size of the grid
		voxelSize, // voxel size
		MPoint(0.0f, 3.0f, 0.0f), // grid center
		voxelizedMeshDagPath,
		status
	);

	// TODO: handle if doIt is called repeatedly... this will just create new buffers but not free old ones?
	// Also need to make sure that this is called before updating the buffers in the callback...
	updateVoxelBasesCompute = std::make_unique<UpdateVoxelBasesCompute>();
	updateVoxelBasesCompute->initializeBuffers(static_cast<int>(voxels.size()) * 8); // 8 particles per voxel
	updateVoxelBasesNumWorkgroups = (static_cast<int>(voxels.size()) + UPDATE_VOXEL_BASES_THEADS - 1) / UPDATE_VOXEL_BASES_THEADS;
	
	MGlobal::displayInfo("Mesh voxelized. Dag path: " + voxelizedMeshDagPath.fullPathName());

	// Iterate over voxels and collect voxels corners into a single particle list for the PBD simulator
	std::vector<glm::vec3> particlePositions;
	for (const auto& voxel : voxels) {
		if (!voxel.occupied) continue;
		
		for (const auto& corner : voxel.corners) {
			particlePositions.push_back(glm::vec3(corner.x, corner.y, corner.z));
		}
	}

	pbdSimulator = PBD(particlePositions, voxelSize);

	MGlobal::displayInfo("PBD particles initialized.");
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
	DirectX::initialize(MhInstPlugin);
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

	return status;
}
