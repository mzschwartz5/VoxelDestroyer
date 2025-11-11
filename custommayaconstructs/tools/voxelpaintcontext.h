#pragma once
#include "voxelcontextbase.h"
#include <maya/MEvent.h>
#include <maya/MToolsInfo.h>

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
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    void toolOffCleanup() override {
        VoxelContextBase::toolOffCleanup();
    }

    void getClassName(MString& name) const override {
        name.set("VoxelPaintContext");
    }

    void setSelectRadius(float radius) override {
        VoxelContextBase::setSelectRadius(radius);
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    float getSelectRadius() const override {
        return VoxelContextBase::getSelectRadius();
    }

    void setBrushMode(BrushMode mode) {
        brushMode = mode;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    BrushMode getBrushMode() const {
        return brushMode;
    }

private:
    BrushMode brushMode = BrushMode::SET;

};