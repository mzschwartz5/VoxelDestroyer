#pragma once
#include "voxelcontextbase.h"
#include <maya/MEvent.h>

enum class BrushMode {
    ADD,
    SUBTRACT,
    SET
};

class VoxelPaintContext : public VoxelContextBase<VoxelPaintContext> {

public:
    VoxelPaintContext() : VoxelContextBase() {
        setTitleString("Voxel Paint Tool");
    }
    
    ~VoxelPaintContext() override {}

    void toolOnSetup(MEvent &event) override {
        VoxelContextBase::toolOnSetup(event);

        setImage("VoxelPaint.png", MPxContext::kImage1);
        MGlobal::executeCommand(
            "if (`exists VoxelPaintContextProperties`) VoxelPaintContextProperties();",
            false
        );
    }

    void toolOffCleanup() override {
        VoxelContextBase::toolOffCleanup();
    }

    MStatus doPtrMoved(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        // Maya doesn't automatically refresh the viewport when the mouse moves. Without a manual refresh, there can be draw artifacts from the tool UI layer.
        MGlobal::executeCommand("refresh");
        return VoxelContextBase::doRelease(event, drawMgr, context);
    }

    void getClassName(MString& name) const override {
        name.set("VoxelPaintContext");
    }

    void setSelectRadius(float radius) override {
        VoxelContextBase::setSelectRadius(radius);
        // Update the tool settings UI
        MGlobal::executeCommandOnIdle("if (`exists VoxelPaintContextValues`) VoxelPaintContextValues();", false);
    }

    float getSelectRadius() const override {
        return VoxelContextBase::getSelectRadius();
    }

    void setBrushMode(BrushMode mode) {
        brushMode = mode;
        // Update the tool settings UI
        MGlobal::executeCommandOnIdle("if (`exists VoxelPaintContextValues`) VoxelPaintContextValues();", false);
    }

    BrushMode getBrushMode() const {
        return brushMode;
    }

private:
    BrushMode brushMode = BrushMode::SET;

};