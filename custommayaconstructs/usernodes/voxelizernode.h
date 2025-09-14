#pragma once

#include <maya/MPxNode.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MStatus.h>
#include <maya/MFnPluginData.h>

#include "../../voxelizer.h"
#include "../data/voxeldata.h"

// TODO: create an input mesh plug to store the selected mesh on file save, etc.
class VoxelizerNode : public MPxNode {
public:
    inline static MTypeId id{0x0013A7C0};
    inline static MString typeName{"voxelizerNode"};
    inline static MObject aVoxelData;

    static void* creator() {
        return new VoxelizerNode();
    }

    static MStatus initialize() {
        MStatus status;
        MFnTypedAttribute tVoxelDataAttr;
        aVoxelData = tVoxelDataAttr.create("voxelData", "vxd", VoxelData::id);
        tVoxelDataAttr.setStorable(true);
        tVoxelDataAttr.setWritable(false);
        tVoxelDataAttr.setReadable(true);
        status = addAttribute(aVoxelData);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        return MS::kSuccess;
    }

    static MObject createVoxelizerNode(
        const VoxelizationGrid& voxelizationGrid,
        const MDagPath& selectedMeshDagPath,
        bool voxelizeSurface,
        bool voxelizeInterior,
        bool doBoolean,
        bool clipTriangles,
        MDagPath& outDagPath
    ) {
        MStatus status;
        MDGModifier dgMod;
        MObject voxelizerNodeObj = dgMod.createNode(VoxelizerNode::typeName, &status);
        dgMod.doIt();
        MFnDependencyNode voxelizerNode(voxelizerNodeObj);
        VoxelizerNode* thisVoxelizer = static_cast<VoxelizerNode*>(voxelizerNode.userNode());

        Voxels voxels = thisVoxelizer->voxelizer.voxelizeSelectedMesh(
            voxelizationGrid,
            selectedMeshDagPath,
            voxelizeSurface,
            voxelizeInterior,
            doBoolean,
            clipTriangles
        );
        outDagPath = voxels.voxelizedMeshDagPath;

        MFnPluginData pluginDataFn;
        MObject pluginDataObj = pluginDataFn.create( VoxelData::id, &status );
        VoxelData* voxelData = static_cast<VoxelData*>(pluginDataFn.data(&status));
        voxelData->setVoxels(voxels);
        voxelData->setVoxelizationGrid(voxelizationGrid);

        MPlug voxelDataInPlug = voxelizerNode.findPlug(aVoxelData, false, &status);
        voxelDataInPlug.setValue(pluginDataObj);

        return voxelizerNodeObj;
    }

private:
    Voxelizer voxelizer;

    VoxelizerNode() = default;
    ~VoxelizerNode() override = default;

};