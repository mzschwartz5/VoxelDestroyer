#include "plugin.h"
#include <maya/MCommandResult.h>
#include <maya/MFnMessageAttribute.h>
#include <windows.h>
#include "custommayaconstructs/tools/voxeldragcontextcommand.h"
#include "custommayaconstructs/tools/voxelpaintcontextcommand.h"
#include <maya/MAnimControl.h>
#include <maya/MProgressWindow.h>
#include "custommayaconstructs/data/voxeldata.h"
#include "custommayaconstructs/data/particledata.h"
#include "custommayaconstructs/data/functionaldata.h"
#include "custommayaconstructs/data/d3d11data.h"
#include "custommayaconstructs/data/colliderdata.h"
#include "custommayaconstructs/draw/voxelshape.h"
#include "custommayaconstructs/draw/voxelsubsceneoverride.h"
#include "custommayaconstructs/draw/colliderdrawoverride.h"
#include "custommayaconstructs/usernodes/pbdnode.h"
#include "custommayaconstructs/usernodes/voxelizernode.h"
#include "custommayaconstructs/usernodes/boxcollider.h"
#include "custommayaconstructs/usernodes/spherecollider.h"
#include "custommayaconstructs/usernodes/capsulecollider.h"
#include "custommayaconstructs/usernodes/cylindercollider.h"
#include "custommayaconstructs/usernodes/planecollider.h"
#include "custommayaconstructs/commands/createcollidercommand.h"
#include "custommayaconstructs/commands/changevoxeleditmodecommand.h"
#include "custommayaconstructs/commands/applyvoxelpaintcommand.h"
#include <maya/MDrawRegistry.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MTimer.h>
#include "directx/compute/computeshader.h"
#include "globalsolver.h"

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)

VoxelRendererOverride* plugin::voxelRendererOverride = nullptr;
MCallbackId plugin::toolChangedCallbackId;

// Maya Plugin creator function
void* plugin::creator()
{
	return new plugin;
}

MSyntax plugin::syntax()
{
	MSyntax syntax;
	syntax.addFlag("-n", "-selectedMeshName", MSyntax::kString);
	syntax.addFlag("-px", "-positionX", MSyntax::kDouble);
	syntax.addFlag("-py", "-positionY", MSyntax::kDouble);
	syntax.addFlag("-pz", "-positionZ", MSyntax::kDouble);
	syntax.addFlag("-rx", "-rotationX", MSyntax::kDouble);
	syntax.addFlag("-ry", "-rotationY", MSyntax::kDouble);
	syntax.addFlag("-rz", "-rotationZ", MSyntax::kDouble);
	syntax.addFlag("-vsz", "-voxelSize", MSyntax::kDouble);
	syntax.addFlag("-vx", "-numVoxelsX", MSyntax::kLong);
	syntax.addFlag("-vy", "-numVoxelsY", MSyntax::kLong);
	syntax.addFlag("-vz", "-numVoxelsZ", MSyntax::kLong);
	syntax.addFlag("-t", "-type", MSyntax::kLong);
	return syntax;
}

MStatus plugin::doIt(const MArgList& argList)
{
	MGlobal::executeCommand("undoInfo -openChunk", false, false); // make everything from here to the end of the function undoable in one command
	MProgressWindow::reserve();
	MProgressWindow::setTitle("Mesh Preparation Progress");
	MProgressWindow::startProgress();

	PluginArgs pluginArgs = parsePluginArgs(argList);
	MSelectionList selectedMesh;
	selectedMesh.add(pluginArgs.selectedMeshName);
	MGlobal::setActiveSelectionList(selectedMesh);
	MDagPath selectedMeshDagPath;
	selectedMesh.getDagPath(0, selectedMeshDagPath);

	// Progress window message updates done within the voxelizer (for finer-grained control)
	const VoxelizationGrid voxelizationGrid {
		pluginArgs.voxelSize * 1.02, // To avoid precision / cut off issues, scale up the voxelization grid very slightly.
		pluginArgs.voxelsPerEdge,
		pluginArgs.position,
		pluginArgs.rotation
	};

	MDagPath voxelizedMeshDagPath;
	MObject voxelizerNodeObj = VoxelizerNode::createVoxelizerNode(
		voxelizationGrid,
		selectedMeshDagPath,
		pluginArgs.voxelizeSurface,
		pluginArgs.voxelizeInterior,
		!pluginArgs.renderAsVoxels,
		pluginArgs.clipTriangles,
		voxelizedMeshDagPath
	);
	
	MProgressWindow::setProgressStatus("Creating PBD particles and face constraints..."); MProgressWindow::setProgressRange(0, 100); MProgressWindow::setProgress(0);
	MObject pbdNodeObj = PBDNode::createPBDNode(voxelizerNodeObj, voxelizedMeshDagPath);
	MObject voxelShapeObj = VoxelShape::createVoxelShapeNode(pbdNodeObj, voxelizedMeshDagPath);
	MProgressWindow::setProgress(100);

	maybeCreateGroundCollider();
	MTime::setUIUnit(MTime::k60FPS);

	MProgressWindow::endProgress();
	MGlobal::executeCommand("undoInfo -closeChunk", false, false); // close the undo chunk	
	return MS::kSuccess;
}

PluginArgs plugin::parsePluginArgs(const MArgList& args) {
	PluginArgs pluginArgs;
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (status != MS::kSuccess) {
		MGlobal::displayError("Failed to parse arguments: " + status.errorString());
		return pluginArgs;
	}

	if (argData.isFlagSet("-n")) {
		status = argData.getFlagArgument("-n", 0, pluginArgs.selectedMeshName);
	}
	
	// Voxel grid center position
	if (argData.isFlagSet("-px")) {
		status = argData.getFlagArgument("-px", 0, pluginArgs.position.x);
	}

	if (argData.isFlagSet("-py")) {
		status = argData.getFlagArgument("-py", 0, pluginArgs.position.y);
	}

	if (argData.isFlagSet("-pz")) {
		status = argData.getFlagArgument("-pz", 0, pluginArgs.position.z);
	}

	if (argData.isFlagSet("-rx")) {
		status = argData.getFlagArgument("-rx", 0, pluginArgs.rotation.x);
	}

	if (argData.isFlagSet("-ry")) {
		status = argData.getFlagArgument("-ry", 0, pluginArgs.rotation.y);
	}

	if (argData.isFlagSet("-rz")) {
		status = argData.getFlagArgument("-rz", 0, pluginArgs.rotation.z);
	}

	if (argData.isFlagSet("-vsz")) {
		status = argData.getFlagArgument("-vsz", 0, pluginArgs.voxelSize);
	}
	
	if (argData.isFlagSet("-vx")) {
		status = argData.getFlagArgument("-vx", 0, pluginArgs.voxelsPerEdge[0]);
	}

	if (argData.isFlagSet("-vy")) {
		status = argData.getFlagArgument("-vy", 0, pluginArgs.voxelsPerEdge[1]);
	}

	if (argData.isFlagSet("-vz")) {
		status = argData.getFlagArgument("-vz", 0, pluginArgs.voxelsPerEdge[2]);
	}

	if (argData.isFlagSet("-t")) {
		int type;
		status = argData.getFlagArgument("-t", 0, type);

		pluginArgs.voxelizeSurface = (type & 0x1) != 0;
		pluginArgs.voxelizeInterior = (type & 0x2) != 0;
		pluginArgs.renderAsVoxels = (type & 0x4) != 0;
		pluginArgs.clipTriangles = (type & 0x8) != 0;
	}

	return pluginArgs;
}

MString plugin::getActiveModelPanel() {
	MString result;
	MGlobal::executeCommand("playblast -ae", result);

	// Parse the result to get the active model panel name (result is in form MainPane|viewPanes|modelPanel4|modelPanel4|modelPanel4)
	MStringArray parts;
	result.split('|', parts);
	return parts[parts.length() - 1];
}

// TODO: when gravity becomes directional, make ground plane in the "down" direction.
// Also, the act of searching for existing colliders should be a utility.
// Potentially have this as an opt-out feature as well (checkbox in the voxelizer menu)
void plugin::maybeCreateGroundCollider() {
	// First, see if there are any colliders in the scene already.
    MItDependencyNodes it;
    for (; !it.isDone(); it.next()) {
        MObject node = it.thisNode();
		if (ColliderLocator::isColliderNode(node)) return;
    }

	MObject planeColliderObj = Utils::createDagNode(PlaneCollider::typeName, MObject::kNullObj, "GroundPlaneCollider");
}

// Initialize Maya Plugin upon loading
EXPORT MStatus initializePlugin(MObject obj)
{
	// Initialize DirectX
	// MhInstPlugin is a global variable defined in the MfnPlugin.h file
	DirectX::initialize(MhInstPlugin);
	plugin::toolChangedCallbackId = MEventMessage::addEventCallback("PostToolChanged", ChangeVoxelEditModeCommand::onExternalToolChange, nullptr);

	MStatus status;
	MFnPlugin plugin(obj, "VoxelDestroyer", "1.0", "Any");
	status = plugin.registerCommand("VoxelDestroyer", plugin::creator, plugin::syntax);
	if (!status)
		status.perror("registerCommand failed");

	status = plugin.registerCommand(CreateColliderCommand::commandName, CreateColliderCommand::creator, CreateColliderCommand::syntax);
	if (!status)
		status.perror("registerCommand failed");

	status = plugin.registerCommand(ChangeVoxelEditModeCommand::commandName, ChangeVoxelEditModeCommand::creator, ChangeVoxelEditModeCommand::syntax);
	if (!status)
		status.perror("registerCommand failed");

	status = plugin.registerCommand(ApplyVoxelPaintCommand::commandName, ApplyVoxelPaintCommand::creator, ApplyVoxelPaintCommand::syntax);
	if (!status)
		status.perror("registerCommand failed");

	// Voxel Data (custom node attribute data type that holds voxel info)
	status = plugin.registerData(VoxelData::fullName, VoxelData::id, VoxelData::creator);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelData: " + status.errorString());
		return status;
	}

	// Particle data (custom node attribute data type for passing particle info to GPU deformer for initialization)
	status = plugin.registerData(ParticleData::fullName, ParticleData::id, ParticleData::creator);
	if (!status) {
		MGlobal::displayError("Failed to register ParticleData: " + status.errorString());
		return status;
	}

	// Functional data (for passing function pointers to GlobalSolver)
	status = plugin.registerData(FunctionalData::fullName, FunctionalData::id, FunctionalData::creator);
	if (!status) {
		MGlobal::displayError("Failed to register FunctionalData: " + status.errorString());
		return status;
	}

	status = plugin.registerData(D3D11Data::fullName, D3D11Data::id, D3D11Data::creator);
	if (!status) {
		MGlobal::displayError("Failed to register D3D11Data: " + status.errorString());
		return status;
	}

	status = plugin.registerData(ColliderData::fullName, ColliderData::id, ColliderData::creator);
	if (!status) {
		MGlobal::displayError("Failed to register ColliderData: " + status.errorString());
		return status;
	}

	// PBD Node
	status = plugin.registerNode(PBDNode::pbdNodeName, PBDNode::id, PBDNode::creator, PBDNode::initialize, MPxNode::kDependNode);
	if (!status) {
		MGlobal::displayError("Failed to register PBD node: " + status.errorString());
		return status;
	}

	status = plugin.registerNode(VoxelizerNode::typeName, VoxelizerNode::id, VoxelizerNode::creator, VoxelizerNode::initialize, MPxNode::kDependNode);
	if (!status) {
		MGlobal::displayError("Failed to register Voxelizer node: " + status.errorString());
		return status;
	}

	status = plugin.registerNode(BoxCollider::typeName, BoxCollider::id, BoxCollider::creator, BoxCollider::initialize, MPxNode::kLocatorNode, &ColliderDrawOverride::drawDbClassification);
	if (!status) {
		MGlobal::displayError("Failed to register BoxCollider node: " + status.errorString());
		return status;
	}

	status = plugin.registerNode(SphereCollider::typeName, SphereCollider::id, SphereCollider::creator, SphereCollider::initialize, MPxNode::kLocatorNode, &ColliderDrawOverride::drawDbClassification);
	if (!status) {
		MGlobal::displayError("Failed to register SphereCollider node: " + status.errorString());
		return status;
	}

	status = plugin.registerNode(CapsuleCollider::typeName, CapsuleCollider::id, CapsuleCollider::creator, CapsuleCollider::initialize, MPxNode::kLocatorNode, &ColliderDrawOverride::drawDbClassification);
	if (!status) {
		MGlobal::displayError("Failed to register CapsuleCollider node: " + status.errorString());
		return status;
	}

	status = plugin.registerNode(CylinderCollider::typeName, CylinderCollider::id, CylinderCollider::creator, CylinderCollider::initialize, MPxNode::kLocatorNode, &ColliderDrawOverride::drawDbClassification);
	if (!status) {
		MGlobal::displayError("Failed to register CylinderCollider node: " + status.errorString());
		return status;
	}

	status = plugin.registerNode(PlaneCollider::typeName, PlaneCollider::id, PlaneCollider::creator, PlaneCollider::initialize, MPxNode::kLocatorNode, &ColliderDrawOverride::drawDbClassification);
	if (!status) {
		MGlobal::displayError("Failed to register PlaneCollider node: " + status.errorString());
		return status;
	}

	// Drag Context command
	status = plugin.registerContextCommand("voxelDragContextCommand", VoxelDragContextCommand::creator);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelDragContextCommand");
		return status;
	}

	// Paint Context command
	status = plugin.registerContextCommand("voxelPaintContextCommand", VoxelPaintContextCommand::creator);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelPaintContextCommand");
		return status;
	}

	// Renderer Override
	plugin::voxelRendererOverride = new VoxelRendererOverride(VoxelRendererOverride::voxelRendererOverrideName);
	status = MRenderer::theRenderer()->registerOverride(plugin::voxelRendererOverride);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelRendererOverride: " + status.errorString());
		return status;
	}

	status = MDrawRegistry::registerDrawOverrideCreator(ColliderDrawOverride::drawDbClassification, ColliderDrawOverride::drawRegistrantId, ColliderDrawOverride::creator);
	if (!status) {
		MGlobal::displayError("Failed to register ColliderDrawOverride: " + status.errorString());
		return status;
	}

	// Global solver node
	status = plugin.registerNode(GlobalSolver::globalSolverNodeName, GlobalSolver::id, GlobalSolver::creator, GlobalSolver::initialize, MPxNode::kDependNode);
	if (!status) {
		MGlobal::displayError("Failed to register GlobalSolver node: " + status.errorString());
		return status;
	}

	status = plugin.registerShape(VoxelShape::typeName, VoxelShape::id, VoxelShape::creator, VoxelShape::initialize, &VoxelShape::drawDbClassification);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelShape: " + status.errorString());
		return status;
	}

	// TODO: use a constant for the string "VoxelSelectionItem"?
	status = MDrawRegistry::registerComponentConverter(MString("VoxelSelectionItem"), VoxelSubSceneComponentConverter::creator);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelSubSceneComponentConverter: " + status.errorString());
		return status;
	}

	status = MDrawRegistry::registerSubSceneOverrideCreator(VoxelSubSceneOverride::drawDbClassification, VoxelSubSceneOverride::drawRegistrantId, VoxelSubSceneOverride::creator);
	if (!status) {
		MGlobal::displayError("Failed to register VoxelSubSceneOverride: " + status.errorString());
		return status;
	}

	// TODO: potentially make this more robust / only allow in perspective panel?
	MString activeModelPanel = plugin::getActiveModelPanel();
	MGlobal::executeCommand(MString("setRendererAndOverrideInModelPanel $gViewport2 " + VoxelRendererOverride::voxelRendererOverrideName + " " + activeModelPanel));

	// VoxelShapeMarkingMenu
	Utils::loadMELScriptByResourceID(MhInstPlugin, IDR_MEL1);
	// VoxelizerMenu
	Utils::loadMELScriptByResourceID(MhInstPlugin, IDR_MEL2);
	// AETemplates
	Utils::loadMELScriptByResourceID(MhInstPlugin, IDR_MEL3);
	// deleteShelfTabNoPrompt
	Utils::loadMELScriptByResourceID(MhInstPlugin, IDR_MEL6);

	// Unlike other MEL scripts, these two can't be loaded into memory, but have to be copied (into the user scripts directory)
	// Maya specifically looks for these files by name when setting up a tool's property sheet.
	MString scriptsDir;
	MGlobal::executeCommand("internalVar -usd", scriptsDir);
	Utils::extractResourceToFile(MhInstPlugin, IDR_MEL4, L"MEL", scriptsDir + "VoxelPaintContextProperties.mel");
	Utils::extractResourceToFile(MhInstPlugin, IDR_MEL5, L"MEL", scriptsDir + "VoxelPaintContextValues.mel");

	// Write icon files to user pref's directory
	MString prefsDir;
    MGlobal::executeCommand("internalVar -userPrefDir", prefsDir);
	MString iconsDir = prefsDir + "icons/";
    Utils::extractResourceToFile(MhInstPlugin, IDR_PNG_VOXELIZER, L"PNG", iconsDir + "Voxelize.png");
    Utils::extractResourceToFile(MhInstPlugin, IDR_PNG_VOXELDRAG, L"PNG", iconsDir + "VoxelDrag.png");
    Utils::extractResourceToFile(MhInstPlugin, IDR_PNG_VOXELCOLLIDER, L"PNG", iconsDir + "VoxelCollider.png");
    Utils::extractResourceToFile(MhInstPlugin, IDR_PNG_VOXELPAINT, L"PNG", iconsDir + "VoxelPaint.png");

	MGlobal::executeCommand("VoxelizerMenu_addToShelf");
	return status;
}

// Cleanup Plugin upon unloading
EXPORT MStatus uninitializePlugin(MObject obj)
{
	MGlobal::executeCommand("VoxelizerMenu_removeFromShelf");

    MStatus status;
    MFnPlugin plugin(obj);
    status = plugin.deregisterCommand("VoxelDestroyer");
    if (!status)
        MGlobal::displayError("deregisterCommand failed on VoxelDestroyer: " + status.errorString());

	status = plugin.deregisterCommand(CreateColliderCommand::commandName);
	if (!status)
		MGlobal::displayError("deregisterCommand failed on createCollider: " + status.errorString());

	status = plugin.deregisterCommand(ChangeVoxelEditModeCommand::commandName);
	if (!status)
		MGlobal::displayError("deregisterCommand failed on changeVoxelEditMode: " + status.errorString());

	status = plugin.deregisterCommand(ApplyVoxelPaintCommand::commandName);
	if (!status)
		MGlobal::displayError("deregisterCommand failed on applyVoxelPaint: " + status.errorString());

    // Deregister the custom maya constructs (nodes, contexts, render overrides, etc.)
    // Voxel Drag Context command
    status = plugin.deregisterContextCommand("voxelDragContextCommand");
    if (!status)
        MGlobal::displayError("deregisterContextCommand failed on VoxelDragContextCommand: " + status.errorString());

	// Voxel Paint Context command
	 status = plugin.deregisterContextCommand("voxelPaintContextCommand");
	 if (!status)
		 MGlobal::displayError("deregisterContextCommand failed on VoxelPaintContextCommand: " + status.errorString());

	status = plugin.deregisterData(VoxelData::id);
	if (!status)
		MGlobal::displayError("deregisterData failed on VoxelData: " + status.errorString());

	status = plugin.deregisterData(ParticleData::id);
	if (!status)
		MGlobal::displayError("deregisterData failed on ParticleData: " + status.errorString());

	status = plugin.deregisterData(FunctionalData::id);
	if (!status)
		MGlobal::displayError("deregisterData failed on FunctionalData: " + status.errorString());

	status = plugin.deregisterData(D3D11Data::id);
	if (!status)
		MGlobal::displayError("deregisterData failed on D3D11Data: " + status.errorString());

	status = plugin.deregisterData(ColliderData::id);
	if (!status)
		MGlobal::displayError("deregisterData failed on ColliderData: " + status.errorString());

	// PBD Node
	status = plugin.deregisterNode(PBDNode::id);
	if (!status)
		MGlobal::displayError("deregisterNode failed on PBD: " + status.errorString());

	// Voxelizer Node
	status = plugin.deregisterNode(VoxelizerNode::id);
	if (!status)
		MGlobal::displayError("deregisterNode failed on Voxelizer: " + status.errorString());

	status = plugin.deregisterNode(BoxCollider::id);
	if (!status)
		MGlobal::displayError("deregisterNode failed on BoxCollider: " + status.errorString());

	status = plugin.deregisterNode(SphereCollider::id);
	if (!status)
		MGlobal::displayError("deregisterNode failed on SphereCollider: " + status.errorString());

	status = plugin.deregisterNode(CapsuleCollider::id);
	if (!status)
		MGlobal::displayError("deregisterNode failed on CapsuleCollider: " + status.errorString());

	status = plugin.deregisterNode(CylinderCollider::id);
	if (!status)
		MGlobal::displayError("deregisterNode failed on CylinderCollider: " + status.errorString());

	status = plugin.deregisterNode(PlaneCollider::id);
	if (!status)
		MGlobal::displayError("deregisterNode failed on PlaneCollider: " + status.errorString());

    // Voxel Renderer Override
    MRenderer::theRenderer()->deregisterOverride(plugin::voxelRendererOverride);
    delete plugin::voxelRendererOverride;
    plugin::voxelRendererOverride = nullptr;

	status = MDrawRegistry::deregisterDrawOverrideCreator(ColliderDrawOverride::drawDbClassification, ColliderDrawOverride::drawRegistrantId);
	if (!status)
		MGlobal::displayError("deregisterDrawOverrideCreator failed on ColliderDrawOverride: " + status.errorString());

	// Global Solver Node
	GlobalSolver::tearDown();
	status = plugin.deregisterNode(GlobalSolver::id);
	if (!status)
		MGlobal::displayError("deregisterNode failed on GlobalSolver: " + status.errorString());

	// Voxel Shape Node
	status = plugin.deregisterNode(VoxelShape::id);
	if (!status)
		MGlobal::displayError("deregisterNode failed on VoxelShape: " + status.errorString());

	status = MDrawRegistry::deregisterComponentConverter("VoxelSelectionItem");
	if (!status)
		MGlobal::displayError("deregisterComponentConverter failed on VoxelSubSceneComponentConverter: " + status.errorString());

	// Voxel SubScene Override
	status = MDrawRegistry::deregisterSubSceneOverrideCreator(VoxelSubSceneOverride::drawDbClassification, VoxelSubSceneOverride::drawRegistrantId);
	if (!status)
		MGlobal::displayError("deregisterSubSceneOverrideCreator failed on VoxelSubSceneOverride: " + status.errorString());

	// Any loaded shaders should be cleared to free resources
	ComputeShader::clearShaderCache();

	MEventMessage::removeCallback(plugin::toolChangedCallbackId);

	return status;
}
