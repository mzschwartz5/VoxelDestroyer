#pragma once
#include <maya/MArgList.h>
#include <maya/MObject.h>
#include <maya/MGlobal.h>
#include <maya/MPxCommand.h>
#include <maya/MFnPlugin.h>
#include <maya/MEventMessage.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MDagPath.h>
#include <memory>
#include <vector>
#include "pbd.h"
#include "voxelizer.h"
#include "directx/directx.h"
#include "constants.h"
#include "directx/compute/computeshader.h"
#include "directx/compute/transformverticescompute.h"
#include "directx/compute/bindverticescompute.h"

// Making most functions and members static so we can bind methods to the timeChanged event
// and access data during the special standalone intialize and uninitialize functions
class plugin : public MPxCommand
{
public:
	plugin() {};
	// Called when the command ("VoxelDestroyer") is executed in Maya
	virtual MStatus doIt(const MArgList& args);
	// Called when the command is registered in Maya
	static void* creator();
	// A callback bound to the timeChanged event (e.g. moving the animation slider)
	static void simulate(void* clientData);

	static void createVoxelGridDisplay();

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
	static MString voxelGridDisplayName;

	// Shaders
	// It seems that they need to be created and managed via unique pointers. Otherwise they dispatch but don't run. Perhaps an issue with copy assignment and DX resources with the non-pointer version.
	static int transformVerticesNumWorkgroups;
	static std::unique_ptr<TransformVerticesCompute> transformVerticesCompute;
	static std::unique_ptr<BindVerticesCompute> bindVerticesCompute;


};