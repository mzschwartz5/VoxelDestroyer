#pragma once
#include <maya/MGlobal.h>
#include <maya/MpxContext.h>

class VoxelPaintContext : public MpxContext {

public:
    VoxelPaintContext() : MpxContext() {
        setTitleString("Voxel Paint Tool");
    }
    
    ~VoxelPaintContext() override {}

    void toolOnSetup(MEvent &event) override {
        MpxContext::toolOnSetup(event);

        setImage("cMuscle_skin_paint.png", MPxContext::kImage1);
    }

    void toolOffCleanup() override {
        MpxContext::toolOffCleanup();
    }

private:

};