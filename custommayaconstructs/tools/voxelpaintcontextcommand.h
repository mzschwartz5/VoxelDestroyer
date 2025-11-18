#pragma once
#include <maya/MPxContextCommand.h>
#include <maya/MSyntax.h>
#include <maya/MArgParser.h>
#include "voxelpaintcontext.h"

class VoxelPaintContextCommand : public MPxContextCommand {
public:
    static void* creator() { return new VoxelPaintContextCommand(); }

    VoxelPaintContextCommand() {}

    ~VoxelPaintContextCommand() override {  }

    MPxContext* makeObj() override {
        fCtx = new VoxelPaintContext();
        return fCtx;
    }

    MStatus appendSyntax() override {
        MSyntax syn = syntax();
        syn.addFlag("-r", "-radius", MSyntax::kDouble);
        syn.addFlag("-m", "-mode",   MSyntax::kLong);
        syn.addFlag("-v", "-value",  MSyntax::kDouble);
        syn.addFlag("-cb", "-cameraBased", MSyntax::kLong);
        syn.addFlag("-lc", "-lowColor", MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble);
        syn.addFlag("-hc", "-highColor", MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble, MSyntax::kDouble);
        syn.addFlag("-cm", "-componentMask", MSyntax::kLong);
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
        if (ap.isFlagSet("-v")) {
            double v; ap.getFlagArgument("-v", 0, v);
            fCtx->setBrushValue(v);
        }
        if (ap.isFlagSet("-cb")) {
            int v; ap.getFlagArgument("-cb", 0, v);
            fCtx->setCameraBased(v != 0);
        }
        if (ap.isFlagSet("-lc")) {
            double r, g, b, a;
            ap.getFlagArgument("-lc", 0, r);
            ap.getFlagArgument("-lc", 1, g);
            ap.getFlagArgument("-lc", 2, b);
            ap.getFlagArgument("-lc", 3, a);
            fCtx->setLowColor(MColor(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a)));
        }
        if (ap.isFlagSet("-hc")) {
            double r, g, b, a;
            ap.getFlagArgument("-hc", 0, r);
            ap.getFlagArgument("-hc", 1, g);
            ap.getFlagArgument("-hc", 2, b);
            ap.getFlagArgument("-hc", 3, a);
            fCtx->setHighColor(MColor(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a)));
        }
        if (ap.isFlagSet("-cm")) {
            int v; ap.getFlagArgument("-cm", 0, v);
            fCtx->setComponentMask(static_cast<uint8_t>(v));
        }
        return MS::kSuccess;
    }

    MStatus doQueryFlags() override {
        if (!fCtx) return MS::kFailure;
        MArgParser ap = parser();
        if (ap.isFlagSet("-r")) setResult(fCtx->getSelectRadius());
        if (ap.isFlagSet("-m")) setResult((int)fCtx->getBrushMode());
        if (ap.isFlagSet("-v")) setResult(fCtx->getBrushValue());
        if (ap.isFlagSet("-cb")) setResult(fCtx->isCameraBased() ? 1 : 0);
        if (ap.isFlagSet("-lc")) {
            MColor c = fCtx->getLowColor();
            MString result;
            MStringArray args;
            args.append(MString() + c.r);
            args.append(MString() + c.g);
            args.append(MString() + c.b);
            args.append(MString() + c.a);
            result.format("^1s ^2s ^3s ^4s", args);
            setResult(result);
        }
        if (ap.isFlagSet("-hc")) {
            MColor c = fCtx->getHighColor();
            MString result;
            MStringArray args;
            args.append(MString() + c.r);
            args.append(MString() + c.g);
            args.append(MString() + c.b);
            args.append(MString() + c.a);
            result.format("^1s ^2s ^3s ^4s", args);
            setResult(result);
        }
        if (ap.isFlagSet("-cm")) {
            setResult((int)fCtx->getComponentMask());
        }
        return MS::kSuccess;
    }

private:
    VoxelPaintContext* fCtx = nullptr;
};