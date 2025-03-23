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
#include "pbd.h"
#include <vector>

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)

PBD pbdSimulator;
MCallbackId callbackId;
MFnTransform sphereTransform;
double ballPosition = 50.0; // Initial height

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
	MPoint translation = sphereTransform.getTranslation(MSpace::kWorld);

	translation.y = particles[0].newPosition.y;
	sphereTransform.setTranslation(translation, MSpace::kWorld);
}

// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MStatus status;

	std::vector<glm::vec3> myParticles = {{0.0f, 50.0f, 0.0f}};
	pbdSimulator = PBD(myParticles);

	return status;
}

MStatus createSphere()
{
    MStatus status;

    // Create the sphere using MGlobal
    MGlobal::executeCommand("polySphere -name \"bouncingBall\"");

    // Get the sphere object by name
    MSelectionList selectionList;
    status = MGlobal::getSelectionListByName("bouncingBall", selectionList);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to find the sphere by name.");
        return status;
    }

    // Get the DAG path for the sphere (the first item in the selection list)
    MDagPath dagPath;
    status = selectionList.getDagPath(0, dagPath);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to get the DAG path for the sphere.");
        return status;
    }

    // Ensure the DAG path is a transform node
    if (!dagPath.hasFn(MFn::kTransform)) {
        MGlobal::displayError("The sphere object is not a valid transform node.");
        return MStatus::kFailure;
    }

	sphereTransform.setObject(dagPath);

    // Set the translation
    MPoint translation = sphereTransform.getTranslation(MSpace::kWorld);  // Get current translation
    translation.y += ballPosition;

    // Apply the translation to the sphere
    status = sphereTransform.setTranslation(translation, MSpace::kWorld);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to set the translation for the sphere: " + status.errorString());
        return status;
    }

    return MS::kSuccess;
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

	// Create the sphere
	status = createSphere();
	if (status != MS::kSuccess) {
		MGlobal::displayError("Failed to create the sphere.");
		return status;
	}

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
	MGlobal::executeCommand("delete bouncingBall");

	return status;
}
