#include "plugin.h"
#include "glm/glm.hpp"
#include <maya/MCommandResult.h>
#include "voxelsimulationnode.h"
#include <maya/MFnMessageAttribute.h>
#include <windows.h>

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)

Voxelizer plugin::voxelizer = Voxelizer();
PBD plugin::pbdSimulator{};
MCallbackId plugin::callbackId = 0;
MDagPath plugin::voxelizedMeshDagPath = MDagPath();

// Maya Plugin creator function
void* plugin::creator()
{
	return new plugin;
}

void plugin::simulate(void* clientData) {
	plugin::pbdSimulator.simulateStep();
	plugin::pbdSimulator.updateMeshVertices();
	MGlobal::executeCommand("refresh");
}

MSyntax plugin::syntax()
{
	MSyntax syntax;
	syntax.addFlag("-px", "-positionX", MSyntax::kDouble);
	syntax.addFlag("-py", "-positionY", MSyntax::kDouble);
	syntax.addFlag("-pz", "-positionZ", MSyntax::kDouble);
	syntax.addFlag("-s", "-scale", MSyntax::kDouble);
	syntax.addFlag("-v", "-voxelsPerEdge", MSyntax::kLong);
	syntax.addFlag("-n", "-gridDisplayName", MSyntax::kString);
	return syntax;
}

// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MStatus status;

	PluginArgs pluginArgs = parsePluginArgs(argList);
	MDagPath selectedMeshDagPath = getSelectedObject(pluginArgs.position, pluginArgs.scale);
	// Fall back to finding the closest object to the voxel grid if nothing is selected
	if (selectedMeshDagPath == MDagPath() || status != MS::kSuccess) {
		selectedMeshDagPath = findClosestObjectToVoxelGrid(pluginArgs.position, pluginArgs.scale, pluginArgs.gridDisplayName);
		if (selectedMeshDagPath == MDagPath()) {
			MGlobal::displayError("No mesh found to voxelize.");
			return MS::kFailure;
		}

		MSelectionList selectionList;
		selectionList.add(selectedMeshDagPath);
		MGlobal::setActiveSelectionList(selectionList);
	}

	MGlobal::displayInfo("Selected mesh: " + selectedMeshDagPath.fullPathName());

	float voxelSize = static_cast<float>(pluginArgs.scale / pluginArgs.voxelsPerEdge);
	Voxels voxels = voxelizer.voxelizeSelectedMesh(
		static_cast<float>(pluginArgs.scale),
		voxelSize,
		pluginArgs.position,
		selectedMeshDagPath,
		plugin::voxelizedMeshDagPath,
		status
	);
	
	MGlobal::displayInfo("Mesh voxelized. Dag path: " + plugin::voxelizedMeshDagPath.fullPathName());

	// TODO: With the current set up, this wouldn't allow us to support voxelizing and simulating multiple meshes at once.
	plugin::pbdSimulator.initialize(voxels, voxelSize, plugin::voxelizedMeshDagPath);
	MGlobal::displayInfo("PBD particles initialized.");


	plugin::createVoxelSimulationNode();

	return status;
}

PluginArgs plugin::parsePluginArgs(const MArgList& args) {
	PluginArgs pluginArgs;
	MStatus status;

	MArgDatabase argData(syntax(), args, &status);
	if (status != MS::kSuccess) {
		MGlobal::displayError("Failed to parse arguments: " + status.errorString());
		return pluginArgs;
	}
	
	// Voxel grid center position
	if (argData.isFlagSet("-px")) {
		status = argData.getFlagArgument("-px", 0, pluginArgs.position.x);
		if (status != MS::kSuccess) {
			MGlobal::displayError("Failed to get position X: " + status.errorString());
		}
	}
	if (argData.isFlagSet("-py")) {
		status = argData.getFlagArgument("-py", 0, pluginArgs.position.y);
		if (status != MS::kSuccess) {
			MGlobal::displayError("Failed to get position Y: " + status.errorString());
		}
	}
	if (argData.isFlagSet("-pz")) {
		status = argData.getFlagArgument("-pz", 0, pluginArgs.position.z);
		if (status != MS::kSuccess) {
			MGlobal::displayError("Failed to get position Z: " + status.errorString());
		}
	}

	// Voxel grid edge length (scale)
	if (argData.isFlagSet("-s")) {
		status = argData.getFlagArgument("-s", 0, pluginArgs.scale);
		if (status != MS::kSuccess) {
			MGlobal::displayError("Failed to get scale: " + status.errorString());
		}
	}
	
	// Number of voxels per edge
	if (argData.isFlagSet("-v")) {
		status = argData.getFlagArgument("-v", 0, pluginArgs.voxelsPerEdge);
		if (status != MS::kSuccess) {
			MGlobal::displayError("Failed to get voxels per edge: " + status.errorString());
		}
	}

	if (argData.isFlagSet("-n")) {
		status = argData.getFlagArgument("-n", 0, pluginArgs.gridDisplayName);
		if (status != MS::kSuccess) {
			MGlobal::displayError("Failed to get grid display name: " + status.errorString());
		}
	}

	return pluginArgs;
}

MDagPath plugin::getSelectedObject(const MPoint& voxelGridCenter, double voxelGridSize) {
	MStatus status;
    // Get the current selection
    MSelectionList selection;
    MGlobal::getActiveSelectionList(selection);

    // Check if the selection is empty
    if (selection.isEmpty()) {
        MGlobal::displayWarning("No mesh selected.");
        status = MS::kFailure;
        return MDagPath();
    }

    // Get the first selected item and ensure it's a mesh
    MDagPath activeMeshDagPath;
    status = selection.getDagPath(0, activeMeshDagPath);
    if (status != MS::kSuccess || !activeMeshDagPath.hasFn(MFn::kMesh)) {
        MGlobal::displayError("The selected item is not a mesh.");
        status = MS::kFailure;
        return MDagPath();
    }

	MFnMesh meshFn(activeMeshDagPath);
	MBoundingBox boundingBox = meshFn.boundingBox();
	MPoint boundingBoxCenter = boundingBox.center();
	MMatrix worldMatrix = activeMeshDagPath.inclusiveMatrix();
	boundingBox.transformUsing(worldMatrix);

	if (!isBoundingBoxOverlappingVoxelGrid(boundingBox, voxelGridCenter, voxelGridSize)) {
		MGlobal::displayError("The selected mesh is not within the voxel grid.");
		status = MS::kFailure;
		return MDagPath();
	}

    return activeMeshDagPath;
}

MDagPath plugin::findClosestObjectToVoxelGrid(const MPoint& voxelGridCenter, double voxelGridSize, MString gridDisplayName) {
    MDagPath closestDagPath;
    double closestDistance = std::numeric_limits<double>::max();

    // Iterate through all DAG nodes in the scene
    MItDag dagIterator(MItDag::kDepthFirst, MFn::kTransform);
    for (; !dagIterator.isDone(); dagIterator.next()) {
        MDagPath currentDagPath;
        dagIterator.getPath(currentDagPath);

        if (currentDagPath.node().hasFn(MFn::kTransform)) {
			// Skip the grid display object by comparing the transform node name
			if (currentDagPath.partialPathName() == gridDisplayName) continue;

            // Get the shape node under the transform
            MDagPath shapeDagPath = currentDagPath;
            if (shapeDagPath.extendToShape() != MS::kSuccess || !shapeDagPath.node().hasFn(MFn::kMesh)) continue;

            MFnMesh meshFn(shapeDagPath);
            MBoundingBox boundingBox = meshFn.boundingBox();
            MPoint boundingBoxCenter = boundingBox.center();
			MMatrix worldMatrix = shapeDagPath.inclusiveMatrix();
			boundingBox.transformUsing(worldMatrix);
            double distance = (MVector(boundingBoxCenter) - MVector(voxelGridCenter)).length();

            if (!isBoundingBoxOverlappingVoxelGrid(boundingBox, voxelGridCenter, voxelGridSize)) continue;

            if (distance < closestDistance) {
                closestDistance = distance;
                closestDagPath = currentDagPath; // Keep the transform node path
            }
        }
    }

    if (!closestDagPath.isValid()) {
        MGlobal::displayWarning("No objects with meshes found in the scene.");
    }

    return closestDagPath;
}

bool plugin::isBoundingBoxOverlappingVoxelGrid(const MBoundingBox& objectBoundingBox, const MPoint& voxelGridCenter, double voxelGridSize) {
	return (voxelGridCenter.x - voxelGridSize / 2.0 <= objectBoundingBox.max().x &&
			voxelGridCenter.x + voxelGridSize / 2.0 >= objectBoundingBox.min().x &&
			voxelGridCenter.y - voxelGridSize / 2.0 <= objectBoundingBox.max().y &&
			voxelGridCenter.y + voxelGridSize / 2.0 >= objectBoundingBox.min().y &&
			voxelGridCenter.z - voxelGridSize / 2.0 <= objectBoundingBox.max().z &&
			voxelGridCenter.z + voxelGridSize / 2.0 >= objectBoundingBox.min().z
	);
}

void plugin::createVoxelSimulationNode() {
    MStatus status;

    // Create the VoxelSimulationNode
    MFnDependencyNode depNodeFn;
    MObject voxelSimNode = depNodeFn.create(VoxelSimulationNode::id, &status);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to create VoxelSimulationNode: " + status.errorString());
        return;
    }


    // Add a message attribute to the voxelized mesh's transform node
    MFnDagNode dagNode(plugin::voxelizedMeshDagPath.transform(&status));
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to get transform node: " + status.errorString());
        return;
    }

    MFnMessageAttribute msgAttrFn;
    MObject messageAttr;
    if (!dagNode.hasAttribute("voxelSimulationNode")) {
        messageAttr = msgAttrFn.create("voxelSimulationNode", "vsn", &status);
        if (status != MS::kSuccess) {
            MGlobal::displayError("Failed to create message attribute: " + status.errorString());
            return;
        }
        dagNode.addAttribute(messageAttr);
    } else {
        messageAttr = dagNode.attribute("voxelSimulationNode", &status);
    }

    // Connect the VoxelSimulationNode to the transform node using the message attribute
	MPlug meshPlug = dagNode.findPlug(messageAttr, false, &status);
    MPlug simNodePlug = depNodeFn.findPlug("message", false, &status); // The default "message" attribute of the node
    if (status == MS::kSuccess) {
        MDGModifier dgModifier;
        dgModifier.connect(simNodePlug, meshPlug);
        dgModifier.doIt();
    }

}

void plugin::loadVoxelSimulationNodeEditorTemplate() {
	void* data = nullptr;
	DWORD size = Utils::loadResourceFile(MhInstPlugin, IDR_MEL1, L"MEL", &data);
	if (size == 0) {
		MGlobal::displayError("Failed to load Voxelization editor template resource.");
		return;
	}

    MString melScript(static_cast<char*>(data), size);

    // Execute the MEL script to load the UI for the VoxelSimulationNode
    MStatus status = MGlobal::executeCommand(melScript);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to execute AETEMPLATE MEL script: " + status.errorString());
    }
}

void plugin::loadVoxelizerMenu() {
	void* data = nullptr;
	DWORD size = Utils::loadResourceFile(MhInstPlugin, IDR_MEL2, L"MEL", &data);
	if (size == 0) {
		MGlobal::displayError("Failed to load Voxelizer menu resource.");
		return;
	}

	MString melScript(static_cast<char*>(data), size);

	// Execute the MEL script to load the Voxelizer menu into memory
	MStatus status = MGlobal::executeCommand(melScript);
	if (status != MS::kSuccess) {
		MGlobal::displayError("Failed to execute Voxelizer menu MEL script: " + status.errorString());
	}
}

// Initialize Maya Plugin upon loading
EXPORT MStatus initializePlugin(MObject obj)
{
	MStatus status;
	MFnPlugin plugin(obj, "VoxelDestroyer", "1.0", "Any");
	status = plugin.registerCommand("VoxelDestroyer", plugin::creator);
	if (!status)
		status.perror("registerCommand failed");

	status = plugin::setCallbackId(MEventMessage::addEventCallback("timeChanged", plugin::simulate));
	if (!status) {
		MGlobal::displayError("Failed to register callback");
		return status;
	}

	// Initialize DirectX
	// MhInstPlugin is a global variable defined in the MfnPlugin.h file
	DirectX::initialize(MhInstPlugin);

	plugin::loadVoxelSimulationNodeEditorTemplate();
	plugin::loadVoxelizerMenu();

	MGlobal::executeCommand("VoxelizerMenu_addToShelf");

	// Register the VoxelSimulationNode
	status = plugin.registerNode("VoxelSimulationNode", VoxelSimulationNode::id, VoxelSimulationNode::creator, VoxelSimulationNode::initialize);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelSimulationNode");
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
	MEventMessage::removeCallback(plugin::getCallbackId());

	status = plugin.deregisterNode(VoxelSimulationNode::id);
	if (!status) {
		MGlobal::displayError("Failed to deregister VoxelSimulationNode");
	}

	MGlobal::executeCommand("VoxelizerMenu_removeFromShelf");

	return status;
}
