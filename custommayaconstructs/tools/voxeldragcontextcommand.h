#pragma once
#include <maya/MPxContextCommand.h>
#include <maya/MSyntax.h>
#include <maya/MArgParser.h>
#include "voxeldragcontext.h"

class VoxelDragContextCommand : public MPxContextCommand {
public:
    static void* creator() { return new VoxelDragContextCommand(); }

    MPxContext* makeObj() override {
        fCtx = new VoxelDragContext();
        return fCtx;
    }

    MStatus appendSyntax() override {
        MSyntax syn = syntax();
        syn.addFlag("-r", "-radius", MSyntax::kDouble);
        syn.addFlag("-s", "-strength",  MSyntax::kDouble);
        syn.addFlag("-cb", "-cameraBased", MSyntax::kLong);
        return MS::kSuccess;
    }

    MStatus doEditFlags() override {
        if (!fCtx) return MS::kFailure;
        MArgParser ap = parser();
        if (ap.isFlagSet("-r")) {
            double v; ap.getFlagArgument("-r", 0, v);
            fCtx->setSelectRadius(v);
        }
        if (ap.isFlagSet("-s")) {
            double v; ap.getFlagArgument("-s", 0, v);
            fCtx->setSelectStrength(v);
        }
        if (ap.isFlagSet("-cb")) {
            int v; ap.getFlagArgument("-cb", 0, v);
            fCtx->setCameraBased(v != 0);
        }
        return MS::kSuccess;
    }

    MStatus doQueryFlags() override {
        if (!fCtx) return MS::kFailure;
        MArgParser ap = parser();
        if (ap.isFlagSet("-r")) setResult(fCtx->getSelectRadius());
        if (ap.isFlagSet("-s")) setResult(fCtx->getSelectStrength());
        if (ap.isFlagSet("-cb")) setResult(fCtx->isCameraBased() ? 1 : 0);
        return MS::kSuccess;
    }

private:
    VoxelDragContext* fCtx = nullptr;
};