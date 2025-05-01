#include "plugin.h"
#include "glm/glm.hpp"

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)

Voxelizer plugin::voxelizer = Voxelizer();
PBD plugin::pbdSimulator = PBD();
MCallbackId plugin::callbackId = 0;
MDagPath plugin::voxelizedMeshDagPath = MDagPath();

// Compute shaders
int plugin::transformVerticesNumWorkgroups = 0;
std::unique_ptr<TransformVerticesCompute> plugin::transformVerticesCompute = nullptr;
std::unique_ptr<BindVerticesCompute> plugin::bindVerticesCompute = nullptr;

// Maya Plugin creator function
void* plugin::creator()
{
	return new plugin;
}

void plugin::simulate(void* clientData) {
	const Particles& particles = plugin::pbdSimulator.simulateStep();

	MFnMesh meshFn(plugin::voxelizedMeshDagPath);
	MFloatPointArray vertexArray;

	// For rendering, we need to update each voxel with its new basis, which we'll use to transform all vertices owned by that voxel
	bindVerticesCompute->updateParticleBuffer(particles.positions); // (owns the particles buffer)
	transformVerticesCompute->dispatch(transformVerticesNumWorkgroups);
	transformVerticesCompute->copyTransformedVertsToCPU(vertexArray, meshFn.numVertices());

	meshFn.setPoints(vertexArray, MSpace::kWorld);
	meshFn.updateSurface();

	MGlobal::executeCommand("refresh");
}

// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MGlobal::displayInfo("VoxelDestroyer command executed.");

	MStatus status;
	float gridEdgeLength = 3.0f;
	float numVoxelsPerEdge = 5.0f;
	float voxelSize = gridEdgeLength / numVoxelsPerEdge;
	Voxels voxels = voxelizer.voxelizeSelectedMesh(
		gridEdgeLength, //size of the grid
		voxelSize, // voxel size
		MPoint(0.0f, 3.0f, 0.0f), // grid center
		plugin::voxelizedMeshDagPath,
		status
	);
	MFnMesh voxelMeshFn(plugin::voxelizedMeshDagPath);
	
	MGlobal::displayInfo("Mesh voxelized. Dag path: " + plugin::voxelizedMeshDagPath.fullPathName());

	// TODO: With the current set up, this wouldn't allow us to support voxelizing and simulating multiple meshes at once.
	plugin::pbdSimulator = PBD(voxels, voxelSize, gridEdgeLength, bindVerticesCompute->getParticlesUAV());
	MGlobal::displayInfo("PBD particles initialized.");

	// TODO: handle if doIt is called repeatedly... this will just create new buffers but not free old ones?
	// Also need to make sure that this is called before updating the buffers in the callback...
	// Calculate a local rest position for each vertex in every voxel.
	plugin::bindVerticesCompute = std::make_unique<BindVerticesCompute>(
		static_cast<int>(voxels.size()) * 8, // 8 particles per voxel
		voxelMeshFn.getRawPoints(&status),
		voxelMeshFn.numVertices(&status),
		voxels.vertStartIdx, 
		voxels.numVerts
	);

	bindVerticesCompute->updateParticleBuffer(plugin::pbdSimulator.getParticles().positions);
	bindVerticesCompute->dispatch(voxels.size());

	MGlobal::displayInfo("Bind vertices compute shader dispatched.");

	plugin::transformVerticesCompute = std::make_unique<TransformVerticesCompute>(
		voxelMeshFn.numVertices(&status),
		bindVerticesCompute->getParticlesSRV(), 			
		bindVerticesCompute->getVertStartIdxSRV(), 
		bindVerticesCompute->getNumVerticesSRV(), 
		bindVerticesCompute->getLocalRestPositionsSRV()
	);
	plugin::transformVerticesNumWorkgroups = voxels.size();
	
	// TODO: With the current set up, this wouldn't allow us to support voxelizing and simulating multiple meshes at once.

	MGlobal::displayInfo("Transform vertices compute shader initialized.");

	return status;
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

	return status;
}
