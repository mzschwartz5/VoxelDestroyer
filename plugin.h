#pragma once
#include <maya/MArgList.h>
#include <maya/MObject.h>
#include <maya/MGlobal.h>
#include <maya/MSyntax.h>
#include <maya/MArgList.h>
#include <maya/MArgDatabase.h>
#include <maya/MPxCommand.h>
#include <maya/MFnPlugin.h>
#include <maya/MEventMessage.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MDagPath.h>
#include <memory>
#include <maya/MFnDagNode.h>
#include <maya/MFnTransform.h>
#include <maya/MVector.h>
#include <maya/MItDag.h>
#include "utils.h"
#include "pbd.h"
#include <vector>
#include "pbd.h"
#include "voxelizer.h"
#include "directx/directx.h"
#include "constants.h"
#include "directx/compute/computeshader.h"
#include "directx/compute/transformverticescompute.h"
#include "directx/compute/bindverticescompute.h"

struct PluginArgs {
	MPoint position{ 0.0f, 0.0f, 0.0f };
	double scale{ 1.0f };
	int voxelsPerEdge{ 10 };
	MString gridDisplayName{ "VoxelGridDisplay" };
};

// Making most functions and members static so we can bind methods to the timeChanged event
// and access data during the special standalone intialize and uninitialize functions
class plugin : public MPxCommand
{
public:
	plugin() {};
	// Called when the command ("VoxelDestroyer") is executed in Maya
	virtual MStatus doIt(const MArgList& args);
	PluginArgs parsePluginArgs(const MArgList& args);
	// Called when the command is registered in Maya
	static void* creator();
	static MSyntax syntax();
	// A callback bound to the timeChanged event (e.g. moving the animation slider)
	static void simulate(void* clientData);
	
	// Compare the center of an object's bounding box to the center of the voxel grid
	// to determine the closest object to the voxel grid (used as a fallback if nothing selected)
	MDagPath findClosestObjectToVoxelGrid(const MPoint& voxelGridCenter, double voxelGridSize, MString gridDisplayName);
	MDagPath getSelectedObject(const MPoint& voxelGridCenter, double voxelGridSize);
	bool isBoundingBoxOverlappingVoxelGrid(const MBoundingBox& bbox, const MPoint& voxelGridCenter, double voxelGridSize);

	static void createVoxelSimulationNode();
	static void loadVoxelSimulationNodeEditorTemplate();
	static void loadVoxelizerMenu();

	static MCallbackId getCallbackId() { return plugin::callbackId; }
	static MStatus setCallbackId(MCallbackId id) { 
		if (id == 0) return MStatus::kFailure;
		
		plugin::callbackId = id; 
		return MStatus::kSuccess;
	}

private:
	static MCallbackId callbackId;
	static Voxelizer voxelizer;
	static PBD pbdSimulator;
	static MDagPath voxelizedMeshDagPath;

	// Shaders
	// It seems that they need to be created and managed via unique pointers. Otherwise they dispatch but don't run. Perhaps an issue with copy assignment and DX resources with the non-pointer version.
	static int transformVerticesNumWorkgroups;
	static std::unique_ptr<TransformVerticesCompute> transformVerticesCompute;
	static std::unique_ptr<BindVerticesCompute> bindVerticesCompute;
};