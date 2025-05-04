#pragma once
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MGlobal.h>
#include "pbd.h"
using namespace MHWRender;

class VoxelRendererOverride : public MRenderOverride {
public:
    VoxelRendererOverride(const MString& name) : MRenderOverride(name), mWidth(0), mHeight(0), name(name) {}

    static void setPBD(PBD* pbd) {
        pbdSimulator = pbd;
    }

    MStatus setup(const MString& destination) override {
        // Retrieve the frame context
        const MFrameContext* mFrameContext = getFrameContext();
        if (!mFrameContext) {
            MGlobal::displayError("Failed to get frame context during setup.");
            return MStatus::kFailure;
        }

        // Access the depth buffer
        const MRenderTarget* depthTarget = mFrameContext->getCurrentDepthRenderTarget();
        if (!depthTarget) {
            MGlobal::displayError("Failed to get depth render target.");
            return MStatus::kFailure;
        }

        MRenderTargetDescription desc; 
        depthTarget->targetDescription(desc);
        unsigned int width = desc.width();
        unsigned int height = desc.height();

        if (width != mWidth || height != mHeight) {
            mWidth = width;
            mHeight = height;
            pbdSimulator->updateDepthResourceHandle(depthTarget->resourceHandle());
        }

        return MStatus::kSuccess;
    }

    MString uiName() const override {
        return name;
    }

    DrawAPI supportedDrawAPIs() const override {
        return kDirectX11;
    }

private:
    inline static PBD* pbdSimulator = nullptr;
    unsigned int mWidth, mHeight;
    MString name;
};