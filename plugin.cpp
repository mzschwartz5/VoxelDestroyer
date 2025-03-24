#include "plugin.h"
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MArgDatabase.h>
#include <maya/MEventMessage.h>
#include <maya/MAnimControl.h>
#include <maya/MSelectionList.h>
#include <maya/MFnTransform.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MFloatPointArray.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MItSelectionList.h>
#include "pbd.h"
#include <vector>

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)

PBD pbdSimulator;
MCallbackId callbackId;

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
	
	int idx = 1;
	for (auto& particle : particles) {
		MString str = "setAttr ball" + MString(std::to_string(idx).c_str());
		MGlobal::executeCommand(MString(str + ".translateX ") + particle.position.x);
		MGlobal::executeCommand(MString(str + ".translateY ") + particle.position.y);
		MGlobal::executeCommand(MString(str + ".translateZ ") + particle.position.z);
		idx++;
	}
}

MStatus createSphere(glm::vec3 initialPos, std::string name)
{
	MStatus status;

	// Create the sphere using MGlobal with the initial position
	std::string command = "polySphere -name ";
	command += name;
	MGlobal::executeCommand(command.c_str());

	// Translate the sphere to the initial position
	std::string translateCommand = "move -absolute ";
	translateCommand += std::to_string(initialPos.x) + " ";
	translateCommand += std::to_string(initialPos.y) + " ";
	translateCommand += std::to_string(initialPos.z) + " ";
	translateCommand += name;
	MGlobal::executeCommand(translateCommand.c_str());

	return MS::kSuccess;
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

	for (; !iter.isDone(); iter.next()) {
		MDagPath dagPath;
		iter.getDagPath(dagPath);

		// Create an MFnMesh function set to operate on the selected mesh
		MFnMesh meshFn(dagPath, &status);
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
			createSphere({ float(point.x), float(point.y), float(point.z) }, "ball" + std::to_string(i + 1));
		}

		// Initialize the PBD simulator with the particles
		pbdSimulator = PBD(particles);
	}

	return MS::kSuccess;
}


// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MStatus status;

	//std::vector<glm::vec3> myParticles = {{5.0f, 0.0f, 0.0f}, {-5.0f, 0.0f, 0.0f}, {0.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 5.0f}};
	//pbdSimulator = PBD(myParticles);

	createParticlesFromSelectedMesh();

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

	// Create four spheres to simulate (the points of) a tetrahedron
	/*createSphere({5.0f, 0.0f, 0.0f}, "ball1");
	createSphere({-5.0f, 0.0f, 0.0f}, "ball2");
	createSphere({0.0f, 5.0f, 0.0f}, "ball3");
	createSphere({0.0f, 0.0f, 5.0f}, "ball4");*/
	
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

	// Delete the sphere
	int idx = 1;
	for (auto& particle : pbdSimulator.getParticles()) {
		MGlobal::executeCommand("delete ball" + MString(std::to_string(idx).c_str()));
		idx++;
	}

	return status;
}
