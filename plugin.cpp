#include "plugin.h"
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MArgDatabase.h>

// define EXPORT for exporting dll functions
#define EXPORT _declspec(dllexport)

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

// Initialize Maya Plugin upon loading
EXPORT MStatus initializePlugin(MObject obj)
{
	MStatus status;
	MFnPlugin plugin(obj, "CIS660", "1.0", "Any");
	status = plugin.registerCommand("plugin", plugin::creator, plugin::syntax);
	if (!status)
		status.perror("registerCommand failed");
	return status;
}

// Cleanup Plugin upon unloading
EXPORT MStatus uninitializePlugin(MObject obj)
{
	MStatus status;
	MFnPlugin plugin(obj);
	status = plugin.deregisterCommand("plugin");
	if (!status)
		status.perror("deregisterCommand failed");
	return status;
}
