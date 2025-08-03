#pragma once
#include <maya/MPxContextCommand.h>
#include "voxeldragcontext.h"

class VoxelDragContextCommand : public MPxContextCommand {
public:
    static void* creator() { return new VoxelDragContextCommand(); }

    MPxContext* makeObj() override {
        return new VoxelDragContext();
    }

private:
};