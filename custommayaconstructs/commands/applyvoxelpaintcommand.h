#pragma once
#include <maya/MPxCommand.h>
#include <maya/MUuid.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MSelectionList.h>
#include <vector>
#include "../draw/voxelshape.h"
#include "../../directx/directx.h"
#include <maya/M3dView.h>
#include <d3d11.h>
#include <wrl/client.h>

class ApplyVoxelPaintCommand : public MPxCommand {
public:
    inline static const MString commandName = MString("applyVoxelPaint");
    // Every instance of this command stores a vector of paint delta values to apply on undo/redo.
    // Because this can be large, it's stored on the host rather than as a GPU buffer. This means extra
    // compute time copying to and from the GPU, but reduces memory pressure on the GPU.
    std::vector<uint16_t> paintDelta;
    MUuid voxelShapeId;
    
    static void* creator() {
        return new ApplyVoxelPaintCommand();
    }

    static MSyntax syntax() {
        MSyntax syntax;
        syntax.addFlag("-vid", "-voxelShapeId", MSyntax::kString);
        return syntax;
    }

    bool isUndoable() const override {
        return true;
    }

    MStatus doIt(const MArgList& args) override {
        MArgDatabase argData(syntax(), args);
        MString voxelShapeIdStr;
        argData.getFlagArgument("-vid", 0, voxelShapeIdStr);
        voxelShapeId = MUuid(voxelShapeIdStr);
        VoxelShape* voxelShape = getVoxelShapeById(voxelShapeId);

        const ComPtr<ID3D11Buffer>& paintDeltaBuffer = voxelShape->getPaintDeltaBuffer();
        DirectX::copyBufferToVector<uint16_t>(paintDeltaBuffer, paintDelta);

        return MS::kSuccess;
    }

    MStatus redoIt() override {
        VoxelShape* voxelShape = getVoxelShapeById(voxelShapeId);
        voxelShape->undoRedoPaint(paintDelta, 1);
        M3dView::active3dView().refresh(false, true);
        return MS::kSuccess;
    }

    MStatus undoIt() override {
        VoxelShape* voxelShape = getVoxelShapeById(voxelShapeId);
        voxelShape->undoRedoPaint(paintDelta, -1);
        M3dView::active3dView().refresh(false, true);
        return MS::kSuccess;
    }

    VoxelShape* getVoxelShapeById(const MUuid& uuid) {
        MSelectionList selectionList;
        selectionList.add(uuid);
        MObject depNode;
        selectionList.getDependNode(0, depNode);
        return static_cast<VoxelShape*>(MFnDependencyNode(depNode).userNode());
    }

};