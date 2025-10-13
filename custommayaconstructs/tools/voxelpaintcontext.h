#pragma once
#include <maya/MPxContext.h>
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


};