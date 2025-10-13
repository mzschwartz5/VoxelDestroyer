#pragma once
#include "voxelcontextbase.h"
#include <maya/MEvent.h>

class VoxelPaintContext : public VoxelContextBase<VoxelPaintContext> {

public:
    VoxelPaintContext() : VoxelContextBase() {
        setTitleString("Voxel Paint Tool");
    }
    
    ~VoxelPaintContext() override {}

    void toolOnSetup(MEvent &event) override {
        VoxelContextBase::toolOnSetup(event);

        setImage("cMuscle_skin_paint.png", MPxContext::kImage1);
    }

    void toolOffCleanup() override {
        VoxelContextBase::toolOffCleanup();
    }

    MStatus doDrag(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        // Maya doesn't automatically refresh the viewport during a drag operation - force it.
        MGlobal::executeCommand("refresh");
        return VoxelContextBase::doDrag(event, drawMgr, context);
    }

};