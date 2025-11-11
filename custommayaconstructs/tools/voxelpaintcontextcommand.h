#pragma once
#include <maya/MPxContextCommand.h>
#include <maya/MSyntax.h>
#include <maya/MArgParser.h>
#include "voxelpaintcontext.h"
#include "../commands/changevoxeleditmodecommand.h"

class VoxelPaintContextCommand : public MPxContextCommand {
public:
    static void* creator() { return new VoxelPaintContextCommand(); }

    VoxelPaintContextCommand() {
        unsubscribeEditModeChange = ChangeVoxelEditModeCommand::subscribe(
            [this](const EditModeChangedEventArgs& args) {
                bool isPaintEditMode = (args.newMode == VoxelEditMode::FacePaint || args.newMode == VoxelEditMode::VertexPaint);
                if (isPaintEditMode) {
                    MGlobal::executeCommandOnIdle("string $ctx = `voxelPaintContextCommand`; setToolTo $ctx; toolPropertyWindow;");
                }
            }
        );
    }

    ~VoxelPaintContextCommand() override {
        unsubscribeEditModeChange();
    }

    MPxContext* makeObj() override {
        fCtx = new VoxelPaintContext();
        return fCtx;
    }

    MStatus appendSyntax() override {
        MSyntax syn = syntax();
        syn.addFlag("-r", "-radius", MSyntax::kDouble);
        syn.addFlag("-m", "-mode",   MSyntax::kLong);
        return MS::kSuccess;
    }

    MStatus doEditFlags() override {
        if (!fCtx) return MS::kFailure;
        MArgParser ap = parser();
        if (ap.isFlagSet("-r")) {
            double v; ap.getFlagArgument("-r", 0, v);
            fCtx->setSelectRadius(v);
        }
        if (ap.isFlagSet("-m")) {
            int v; ap.getFlagArgument("-m", 0, v);
            fCtx->setBrushMode(static_cast<BrushMode>(v));
        }
        return MS::kSuccess;
    }

    MStatus doQueryFlags() override {
        if (!fCtx) return MS::kFailure;
        MArgParser ap = parser();
        if (ap.isFlagSet("-r")) setResult(fCtx->getSelectRadius());
        if (ap.isFlagSet("-m")) setResult((int)fCtx->getBrushMode());
        return MS::kSuccess;
    }

private:
    VoxelPaintContext* fCtx = nullptr;
    EventBase::Unsubscribe unsubscribeEditModeChange;
};