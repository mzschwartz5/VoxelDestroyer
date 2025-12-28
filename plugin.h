#pragma once
#include <maya/MArgList.h>
#include <maya/MSyntax.h>
#include <maya/MPxCommand.h>
#include <maya/MVector.h>
#include <array>
#include "custommayaconstructs/draw/voxelrendereroverride.h"
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

	static VoxelRendererOverride* voxelRendererOverride;
	static MCallbackId toolChangedCallbackId;
	
private:
};