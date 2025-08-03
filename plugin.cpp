#include "plugin.h"
#include "glm/glm.hpp"
#include <maya/MCommandResult.h>
#include "custommayaconstructs/voxelsimulationnode.h"
#include <maya/MFnMessageAttribute.h>
#include <windows.h>
#include "custommayaconstructs/voxeldragcontextcommand.h"
#include <maya/MAnimControl.h>
#include <maya/MProgressWindow.h>

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)
extern std::thread::id g_mainThreadId = std::this_thread::get_id();

Voxelizer plugin::voxelizer = Voxelizer();
PBD plugin::pbdSimulator{};
std::unordered_map<std::string, MCallbackId> plugin::callbacks;
MDagPath plugin::voxelizedMeshDagPath = MDagPath();
VoxelRendererOverride* plugin::voxelRendererOverride = nullptr;

// Maya Plugin creator function
void* plugin::creator()
{
	return new plugin;
}

void plugin::simulate(void* clientData) {
	if (!plugin::pbdSimulator.isInitialized()) return;

	plugin::pbdSimulator.simulateStep();
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
	syntax.addFlag("-t", "-type", MSyntax::kLong);
	return syntax;
}

// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MProgressWindow::reserve();
    // MProgressWindow::setInterruptable(true); // TODO: 
	MProgressWindow::setTitle("Mesh Preparation Progress");
	MProgressWindow::startProgress();

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

	// Progress window message updates done within the voxelizer (for finer-grained control)
	float voxelSize = static_cast<float>(pluginArgs.scale / pluginArgs.voxelsPerEdge);
	Voxels voxels = voxelizer.voxelizeSelectedMesh(
		static_cast<float>(pluginArgs.scale),
		voxelSize,
		pluginArgs.position,
		selectedMeshDagPath,
		plugin::voxelizedMeshDagPath,
		pluginArgs.voxelizeSurface,
		pluginArgs.voxelizeInterior,
		!pluginArgs.renderAsVoxels,
		pluginArgs.clipTriangles,
		status
	);
	
	// TODO: With the current set up, this wouldn't allow us to support voxelizing and simulating multiple meshes at once.
	MProgressWindow::setProgressStatus("Creating PBD particles and face constraints..."); MProgressWindow::setProgressRange(0, 100); MProgressWindow::setProgress(0);
	plugin::pbdSimulator.initialize(voxels, voxelSize, plugin::voxelizedMeshDagPath);
	MProgressWindow::setProgress(100);

	plugin::createVoxelSimulationNode();
		
	VoxelRendererOverride::setPBD(&plugin::pbdSimulator);
	VoxelDeformerCPUNode::instantiateAndAttachToMesh(plugin::voxelizedMeshDagPath);

	MProgressWindow::endProgress();
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

	if (argData.isFlagSet("-t")) {
		int type;
		status = argData.getFlagArgument("-t", 0, type);
		if (status != MS::kSuccess) {
			MGlobal::displayError("Failed to get type: " + status.errorString());
		}

		pluginArgs.voxelizeSurface = (type & 0x1) != 0;
		pluginArgs.voxelizeInterior = (type & 0x2) != 0;
		pluginArgs.renderAsVoxels = (type & 0x4) != 0;
		pluginArgs.clipTriangles = (type & 0x8) != 0;
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

// TODO: this will need work to support multiple meshes. Perhaps move to the VoxelSimulationNode class.
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

MString plugin::getActiveModelPanel() {
	MString result;
	MGlobal::executeCommand("playblast -ae", result);

	// Parse the result to get the active model panel name (result is in form MainPane|viewPanes|modelPanel4|modelPanel4|modelPanel4)
	MStringArray parts;
	result.split('|', parts);
	return parts[parts.length() - 1];
}

// Initialize Maya Plugin upon loading
EXPORT MStatus initializePlugin(MObject obj)
{
	g_mainThreadId = std::this_thread::get_id();

	// Initialize DirectX
	// MhInstPlugin is a global variable defined in the MfnPlugin.h file
	DirectX::initialize(MhInstPlugin);

	MStatus status;
	MFnPlugin plugin(obj, "VoxelDestroyer", "1.0", "Any");
	status = plugin.registerCommand("VoxelDestroyer", plugin::creator);
	if (!status)
		status.perror("registerCommand failed");

	// Register custom maya constructs (nodes, contexts, render overrides, etc.)
	// Voxel Simulation Node
	status = plugin.registerNode("VoxelSimulationNode", VoxelSimulationNode::id, VoxelSimulationNode::creator, VoxelSimulationNode::initialize);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelSimulationNode");
		return status;
	}

	// Drag Context command
	status = plugin.registerContextCommand("voxelDragContextCommand", VoxelDragContextCommand::creator);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelDragContextCommand");
		return status;
	}

	// Renderer Override
	plugin::voxelRendererOverride = new VoxelRendererOverride("VoxelRendererOverride");
	status = MRenderer::theRenderer()->registerOverride(plugin::voxelRendererOverride);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelRendererOverride: " + status.errorString());
		return status;
	}

	// CPU Deformer Node
	status = plugin.registerNode(VoxelDeformerCPUNode::typeName(), VoxelDeformerCPUNode::id, VoxelDeformerCPUNode::creator, VoxelDeformerCPUNode::initialize, MPxNode::kDeformerNode);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelDeformerCPUNode");
		return status;
	}

	// GPU Deformer override
	status = MGPUDeformerRegistry::registerGPUDeformerCreator(VoxelDeformerCPUNode::typeName(), "VoxelDestroyer", VoxelDeformerGPUNode::getGPUDeformerInfo());
	if (!status) {
		MGlobal::displayError("Failed to register VoxelDeformerGPUNode: " + status.errorString());
		return status;
	}
	VoxelDeformerGPUNode::compileKernel();

	// TODO: potentially make this more robust / only allow in perspective panel?
	MString activeModelPanel = plugin::getActiveModelPanel();
	MGlobal::executeCommand(MString("setRendererAndOverrideInModelPanel $gViewport2 VoxelRendererOverride " + activeModelPanel));

	MCallbackId timeChangedCallbackId = MEventMessage::addEventCallback("timeChanged", plugin::simulate, NULL, &status);
	plugin::setCallbackId("timeChanged", timeChangedCallbackId);

	plugin::loadVoxelSimulationNodeEditorTemplate();
	plugin::loadVoxelizerMenu();

	MGlobal::executeCommand("VoxelizerMenu_addToShelf");

	return status;
}

// Cleanup Plugin upon unloading
EXPORT MStatus uninitializePlugin(MObject obj)
{
	MGlobal::executeCommand("VoxelizerMenu_removeFromShelf");

	// Deregister the callbacks
	MCallbackId timeChangedCallbackId = plugin::getCallbackId("timeChanged");
	MEventMessage::removeCallback(timeChangedCallbackId);

    MStatus status;
    MFnPlugin plugin(obj);
    status = plugin.deregisterCommand("VoxelDestroyer");
    if (!status)
        MGlobal::displayError("deregisterCommand failed on VoxelDestroyer: " + status.errorString());

    // Deregister the custom maya constructs (nodes, contexts, render overrides, etc.)
    // Voxel Drag Context command
    status = plugin.deregisterContextCommand("voxelDragContextCommand");
    if (!status)
        MGlobal::displayError("deregisterContextCommand failed on VoxelDragContextCommand: " + status.errorString());

    // Voxel Simulation Node
    status = plugin.deregisterNode(VoxelSimulationNode::id);
    if (!status)
        MGlobal::displayError("deregisterNode failed on VoxelSimulationNode: " + status.errorString());

    // Voxel Renderer Override
    MRenderer::theRenderer()->deregisterOverride(plugin::voxelRendererOverride);
    delete plugin::voxelRendererOverride;
    plugin::voxelRendererOverride = nullptr;
	
    // Voxel Deformer GPU override
	VoxelDeformerGPUNode::tearDown();
    status = MGPUDeformerRegistry::deregisterGPUDeformerCreator(VoxelDeformerCPUNode::typeName(), "VoxelDestroyer");
    if (!status)
		MGlobal::displayError("deregisterGPUDeformerCreator failed on VoxelDeformerCPUNode: " + status.errorString());

	// Voxel Deformer CPU Node
    status = plugin.deregisterNode(VoxelDeformerCPUNode::id);
    if (!status)
        MGlobal::displayError("deregisterNode failed on VoxelDeformerCPUNode: " + status.errorString());

	return status;
}
