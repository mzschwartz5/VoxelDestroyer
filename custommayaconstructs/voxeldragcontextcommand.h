#pragma once
#include <maya/MPxContextCommand.h>
#include "voxeldragcontext.h"
#include "pbd.h"

class VoxelDragContextCommand : public MPxContextCommand {
public:
    static void* creator() { return new VoxelDragContextCommand(); }

    MPxContext* makeObj() override {
        return new VoxelDragContext(pbdSimulator);
    }
    static void setPBD(PBD* pbd) {
        pbdSimulator = pbd;
    }

private:
    inline static PBD* pbdSimulator = nullptr;
};