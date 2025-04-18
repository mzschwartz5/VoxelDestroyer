#include "plugin.h"

// define EXPORT for exporting dll functions
#define EXPORT __declspec(dllexport)

Voxelizer plugin::voxelizer = Voxelizer();
PBD plugin::pbdSimulator = PBD();
MCallbackId plugin::callbackId = 0;
MDagPath plugin::voxelizedMeshDagPath = MDagPath();

// Compute shaders
std::unique_ptr<UpdateVoxelBasesCompute> plugin::updateVoxelBasesCompute = nullptr;
int plugin::updateVoxelBasesNumWorkgroups = 0;

// Maya Plugin creator function
void* plugin::creator()
{
	return new plugin;
}

void plugin::simulate(void* clientData) {
	const Particles& particles = plugin::pbdSimulator.simulateStep();

	MFnMesh meshFn(plugin::voxelizedMeshDagPath);
	MPointArray vertexArray;
	meshFn.getPoints(vertexArray, MSpace::kWorld);

	int idx = 0;
	for (int i = 0; i < particles.numParticles; i++) {
		vertexArray[idx] = MPoint(particles.positions[i].x, particles.positions[i].y, particles.positions[i].z);
		idx++;
	}

	// For rendering, we need to update each voxel with its new basis, which we'll use to transform all vertices owned by that voxel
	updateVoxelBasesCompute->updateParticleBuffer(particles.positions);
	updateVoxelBasesCompute->dispatch(updateVoxelBasesNumWorkgroups);

	meshFn.setPoints(vertexArray, MSpace::kWorld);
	meshFn.updateSurface();

	MGlobal::executeCommand("refresh");
}

// Plugin doIt function
MStatus plugin::doIt(const MArgList& argList)
{
	MStatus status;
	float voxelSize = 0.1f;
	std::vector<Voxel> voxels = voxelizer.voxelizeSelectedMesh(
		1.0f, //size of the grid
		voxelSize, // voxel size
		MPoint(0.0f, 3.0f, 0.0f), // grid center
		plugin::voxelizedMeshDagPath,
		status
	);

	// TODO: handle if doIt is called repeatedly... this will just create new buffers but not free old ones?
	// Also need to make sure that this is called before updating the buffers in the callback...
	plugin::updateVoxelBasesCompute = std::make_unique<UpdateVoxelBasesCompute>(static_cast<int>(voxels.size()) * 8);
	plugin::updateVoxelBasesNumWorkgroups = (static_cast<int>(voxels.size()) + UPDATE_VOXEL_BASES_THEADS - 1) / UPDATE_VOXEL_BASES_THEADS;
	
	MGlobal::displayInfo("Mesh voxelized. Dag path: " + plugin::voxelizedMeshDagPath.fullPathName());

	// Iterate over voxels and collect voxels corners into a single particle list for the PBD simulator
	std::vector<glm::vec3> particlePositions;
	for (const auto& voxel : voxels) {
		if (!voxel.occupied) continue;
		
		for (const auto& corner : voxel.corners) {
			particlePositions.push_back(glm::vec3(corner.x, corner.y, corner.z));
		}
	}

	// TODO: With the current set up, this wouldn't allow us to support voxelizing and simulating multiple meshes at once.
	plugin::pbdSimulator = PBD(particlePositions, voxelSize);

	MGlobal::displayInfo("PBD particles initialized.");
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
