#pragma once
#include <maya/MArgList.h>
#include <maya/MObject.h>
#include <maya/MGlobal.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#include <maya/MPxCommand.h>
#include <maya/MFnPlugin.h>
#include <maya/MEventMessage.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnTransform.h>
#include <maya/MVector.h>
#include <maya/MItDag.h>
#include "utils.h"
#include <vector>
#include <array>
#include "voxelizer.h"
#include "directx/directx.h"
#include "constants.h"
#include <maya/MConditionMessage.h>
#include <unordered_map>
#include <string>
#include "custommayaconstructs/draw/voxelrendereroverride.h"
#include <maya/MViewport2Renderer.h>
#include <maya/MEventMessage.h>
using namespace MHWRender;

struct PluginArgs {
	MPoint position{ 0.0f, 0.0f, 0.0f };
	MVector rotation{ 0.0f, 0.0f, 0.0f };
	double voxelSize{ 1.0 };
	std::array<int, 3> voxelsPerEdge{ 2, 2, 2 };
	MString selectedMeshName;
	bool voxelizeSurface{ false };
	bool voxelizeInterior{ false };
	bool renderAsVoxels{ false };
	bool clipTriangles{ false };
};

// TODO: move this command into the commands folder
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
	
	// Compare the center of an object's bounding box to the center of the voxel grid
	// to determine the closest object to the voxel grid (used as a fallback if nothing selected)
	MDagPath findClosestObjectToVoxelGrid(const MPoint& voxelGridCenter, double voxelGridSize, MString gridDisplayName);
	MDagPath getSelectedObject(const MPoint& voxelGridCenter, double voxelGridSize);
	bool isBoundingBoxOverlappingVoxelGrid(const MBoundingBox& bbox, const MPoint& voxelGridCenter, double voxelGridSize);
	void maybeCreateGroundCollider();

	static MString getActiveModelPanel();
	static VoxelRendererOverride* voxelRendererOverride;
	static MCallbackId toolChangedCallbackId;
	
private:
};