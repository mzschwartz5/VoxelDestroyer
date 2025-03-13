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

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)

MCallbackId callbackId;
MFnTransform sphereTransform;

// Ball properties
double ballPosition = 50.0; // Initial height
double ballVelocity = 0.0;
const double gravity = -9.8;
const double timeStep = 1.0 / 60.0; // Assuming 60 FPS
const double groundLevel = 0.0;
const double restitution = 0.8; // Bounciness factor

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

void testCallback(void* clientData) {
	// This is very basic, assumes 60 FPS / constant frame rate.
	
    // Update ball velocity and position
    ballVelocity += gravity * timeStep;
	ballPosition += ballVelocity * timeStep;

    // Check for collision with the ground
    if (ballPosition <= groundLevel) {
        ballPosition = groundLevel;
        ballVelocity = -ballVelocity * restitution;
    }

    // Update the sphere's position
	MPoint translation = sphereTransform.getTranslation(MSpace::kWorld);
	translation.y = ballPosition;
	sphereTransform.setTranslation(translation, MSpace::kWorld);
}
// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MStatus status;
	MGlobal::displayInfo("Hello World!");

	// Define the argument flags
	const char* nameFlag = "-n";
	const char* idFlag = "-i";

	// Parse the arguments
	MArgDatabase argData(syntax(), argList, &status);
	if (!status) {
		MGlobal::displayError("Failed to parse arguments: " + status.errorString());
		return status;
	}

	// Extract arguments "name" and "id"
	MString name;
	int id = 0;
	if (argData.isFlagSet(nameFlag)) {
		status = argData.getFlagArgument(nameFlag, 0, name);
		if (!status) {
			MGlobal::displayError("Failed to parse 'name' argument");
			return status;
		}
	}
	if (argData.isFlagSet(idFlag)) {
		status = argData.getFlagArgument(idFlag, 0, id);
		if (!status) {
			MGlobal::displayError("Failed to parse 'id' argument");
			return status;
		}
	}

	// Create the dialog box command
	MString dialogCmd = "confirmDialog -title \"Hello Maya\" -message \"Name: " + name + ", ID: " + id + "\" -button \"OK\"";
	MGlobal::executeCommand(dialogCmd);

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
	callbackId = MEventMessage::addEventCallback("timeChanged", testCallback);
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
