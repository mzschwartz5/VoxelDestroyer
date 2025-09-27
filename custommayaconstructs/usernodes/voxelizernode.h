#pragma once

#include <maya/MPxNode.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MStatus.h>
#include <maya/MSharedPtr.h>

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
        MObject voxelizerNodeObj = Utils::createDGNode(VoxelizerNode::typeName);
        MFnDependencyNode voxelizerNode(voxelizerNodeObj);
        VoxelizerNode* thisVoxelizer = static_cast<VoxelizerNode*>(voxelizerNode.userNode());

        MSharedPtr<Voxels> voxels = MSharedPtr<Voxels>::make(
            thisVoxelizer->voxelizer.voxelizeSelectedMesh(
                voxelizationGrid,
                selectedMeshDagPath,
                voxelizeSurface,
                voxelizeInterior,
                doBoolean,
                clipTriangles
            )
        );
        outDagPath = voxels->voxelizedMeshDagPath;

        Utils::createPluginData<VoxelData>(
            voxelizerNodeObj,
            aVoxelData,
            [&voxels, &voxelizationGrid](VoxelData* voxelData) {
                voxelData->setVoxels(voxels);
                voxelData->setVoxelizationGrid(voxelizationGrid);
            }
        );

        return voxelizerNodeObj;
    }

private:
    Voxelizer voxelizer;

    VoxelizerNode() = default;
    ~VoxelizerNode() override = default;

};