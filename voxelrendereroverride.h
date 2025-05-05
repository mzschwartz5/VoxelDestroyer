#pragma once
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MGlobal.h>
#include "pbd.h"
using namespace MHWRender;

/**
 * Note: to actually activate a render override, you need to register it and THEN select it from the renderer drop down menu in Maya.
 * There *is* a way to programmatically switch to the override via MEL (see plugin.cpp).
 */
class VoxelRendererOverride : public MRenderOverride {
public:
    VoxelRendererOverride(const MString& name) : MRenderOverride(name), name(name) {}

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

        if (pbdSimulator) {
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
    MString name;
};