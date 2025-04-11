#include "plugin.h"
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MArgDatabase.h>
#include <maya/MEventMessage.h>
#include <maya/MSelectionList.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MItMeshEdge.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MItSelectionList.h>
#include "pbd.h"
#include <vector>
#include "directx.h"

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)

PBD pbdSimulator;
MCallbackId callbackId;
DirectX dx;
std::vector<std::pair<MDagPath, std::pair<size_t, size_t>>> meshParticleRanges; // Mesh and its particle range

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

	for (const auto& entry : meshParticleRanges) {
		auto& meshDagPath = entry.first;
		auto& range = entry.second;
		MFnMesh meshFn(meshDagPath);
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


// Function to create particles at each vertex of the selected mesh

MStatus createParticlesFromSelectedMesh()
{
	MStatus status;

	// Get the current selection
	MSelectionList selection;
	MGlobal::getActiveSelectionList(selection);

	// Iterate through the selection list
	MItSelectionList iter(selection, MFn::kMesh, &status);
	if (status != MS::kSuccess) {
		MGlobal::displayError("No mesh selected.");
		return status;
	}

	std::vector<glm::vec3> allParticles;
	meshParticleRanges.clear(); // Clear any previous mappings

	for (; !iter.isDone(); iter.next()) {
		MDagPath meshDagPath;
		iter.getDagPath(meshDagPath);

		// Create an MFnMesh function set to operate on the selected mesh
		MFnMesh meshFn(meshDagPath, &status);
		if (status != MS::kSuccess) {
			MGlobal::displayError("Failed to create MFnMesh.");
			return status;
		}

		// Get the vertex positions
		MPointArray vertexArray;
		meshFn.getPoints(vertexArray, MSpace::kWorld);

		// Store the starting index of this mesh's particles
		size_t startIdx = allParticles.size();

		// Add particles for each vertex position
		for (unsigned int i = 0; i < vertexArray.length(); ++i) {
			MPoint point = vertexArray[i];
			allParticles.push_back(glm::vec3(point.x, point.y, point.z));
		}

		// Store the ending index of this mesh's particles
		size_t endIdx = allParticles.size();

		// Map the mesh to its particle range
		meshParticleRanges.emplace_back(meshDagPath, std::make_pair(startIdx, endIdx));
	}

	// Initialize the PBD simulator with all particles
	pbdSimulator = PBD(allParticles);

	return MS::kSuccess;
}


// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MStatus status;

	status = createParticlesFromSelectedMesh();
	if (status != MS::kSuccess) {
		MGlobal::displayError("Failed to create particles for selected meshes.");
		return status;
	}

	MGlobal::displayInfo("Particles created for all selected meshes.");

	dx.dispatchComputeShaders();
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
	dx = DirectX(MhInstPlugin);
	
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

	dx.tearDown();

	return status;
}
