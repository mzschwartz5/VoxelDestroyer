#pragma once
#include <maya/MPxContextCommand.h>
#include "voxelpaintcontext.h"

class VoxelPaintContextCommand : public MPxContextCommand {
public:
    static void* creator() { return new VoxelPaintContextCommand(); }

    MPxContext* makeObj() override {
        return new VoxelPaintContext();
    }

private:
};