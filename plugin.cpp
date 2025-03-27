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
MDagPath selectedMeshDagPath;
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

	MFnMesh meshFn(selectedMeshDagPath);
	MPointArray vertexArray;
	meshFn.getPoints(vertexArray, MSpace::kWorld);

	int idx = 0;
	for (auto& particle : particles) {
		vertexArray[idx] = MPoint(particle.position.x, particle.position.y, particle.position.z);
		idx++;
	}

	meshFn.setPoints(vertexArray, MSpace::kWorld);
	meshFn.updateSurface();

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

	std::vector<glm::vec3> particles;
	std::vector<std::array<int, 4>> tetIndices;
	std::vector<std::array<int, 2>> edgeIndices;

    for (; !iter.isDone(); iter.next()) {
        iter.getDagPath(selectedMeshDagPath);

        // Create an MFnMesh function set to operate on the selected mesh
        MFnMesh meshFn(selectedMeshDagPath, &status);
        if (status != MS::kSuccess) {
            MGlobal::displayError("Failed to create MFnMesh.");
            return status;
        }

		// Get the vertex positions
		MPointArray vertexArray;
		meshFn.getPoints(vertexArray, MSpace::kWorld);

		// Create particles at each vertex position
		std::vector<glm::vec3> particles;
		for (unsigned int i = 0; i < vertexArray.length(); ++i) {
			MPoint point = vertexArray[i];
			particles.push_back(glm::vec3(point.x, point.y, point.z));
		}

		// Iterate over each face of the mesh
		MItMeshPolygon faceIter(selectedMeshDagPath, MObject::kNullObj, &status);

		for (; !faceIter.isDone(); faceIter.next()) {
			MIntArray vertexIndices;
			faceIter.getVertices(vertexIndices);

			// Create tetrahedra from each face
			std::array<int, 4> tet;
			for (unsigned int i = 0; i < 4; ++i) {
				tet[i] = vertexIndices[i];
			}

			tetIndices.push_back(tet);
		}

		// Iterate over each edge of the mesh
		MItMeshEdge edgeIter(selectedMeshDagPath, MObject::kNullObj, &status);

		for (; !edgeIter.isDone(); edgeIter.next()) {
			MIntArray vertexIndices;
			
			std::array<int, 2> edge;
			edge[0] = edgeIter.index(0);
			edge[1] = edgeIter.index(1);

			edgeIndices.push_back(edge);
		}

        // Initialize the PBD simulator with the particles
        pbdSimulator = PBD(particles, tetIndices, edgeIndices);
    }

    return MS::kSuccess;
}

// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MStatus status;

	createParticlesFromSelectedMesh();
	MGlobal::displayInfo("Particles created.");

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
	dx = DirectX();
	
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
