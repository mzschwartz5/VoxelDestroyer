#pragma once
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include "../../event.h"
#include "voxelPaintrenderoperation.h"
#include "../tools/voxelpaintcontext.h"
using namespace MHWRender;

struct CameraMatrices
{
    float viewportWidth{ 0.0f };
    float viewportHeight{ 0.0f };
    MMatrix viewMatrix;
    MMatrix projMatrix;
    MMatrix invViewProjMatrix;
};

/**
 * Pass-through implementation of a render override, simply to get consistent access to the render target's depth buffer and camera matrices.
 * Does not add any render operations; should just replicate default viewport rendering.
 * 
 * Note: to actually activate a render override, you need to register it and THEN select it from the renderer drop down menu in Maya.
 * There *is* a way to programmatically switch to the override via MEL (see plugin.cpp).
 */
class VoxelRendererOverride : public MRenderOverride {
public:
    VoxelRendererOverride(const MString& name) : MRenderOverride(name), name(name) {
        unsubscribeFromPaintMove = VoxelPaintContext::subscribeToMousePositionChange([this](const MousePosition& mousePos) {
        });

        unsubscribeFromPaintStateChange = VoxelPaintContext::subscribeToDragStateChange([this](const DragState& state) {
            isPainting = state.isDragging;
        });

        MRenderer::theRenderer()->getStandardViewportOperations(mOperations);
        // Maya manages the memory / lifetime of the operation passed in
        mOperations.insertBefore(MRenderOperation::kStandardPresentName, new VoxelPaintRenderOperation(paintOpName));
        paintOpIndex = mOperations.indexOf(paintOpName);
    }

    ~VoxelRendererOverride() override {
        unsubscribeFromPaintMove();
        unsubscribeFromPaintStateChange();
    }

    /**
     * Runs at the beginning of every frame.
     */
    MStatus setup(const MString& destination) override {
        const MFrameContext* mFrameContext = getFrameContext();
        const MRenderTarget* depthTarget = mFrameContext->getCurrentDepthRenderTarget();

        const MMatrix& viewMatrix = mFrameContext->getMatrix(MFrameContext::kViewMtx);
        const MMatrix& projMatrix = mFrameContext->getMatrix(MFrameContext::kProjectionMtx);
        const MMatrix& invViewProjMatrix = mFrameContext->getMatrix(MFrameContext::kViewProjInverseMtx);

        unsigned int viewportWidth, viewportHeight;
        MRenderer::theRenderer()->outputTargetSize(viewportWidth, viewportHeight);

        depthTargetChangedEvent.notify(depthTarget->resourceHandle());
        cameraInfoChangedEvent.notify({ static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), viewMatrix, projMatrix, invViewProjMatrix });

        mOperations[paintOpIndex]->setEnabled(isPainting);
        return MStatus::kSuccess;
    }

    MString uiName() const override {
        return name;
    }

    DrawAPI supportedDrawAPIs() const override {
        return kDirectX11;
    }

    static Event<void*>::Unsubscribe subscribeToDepthTargetChange(Event<void*>::Listener listener) {
        return depthTargetChangedEvent.subscribe(listener);
    }

    static Event<CameraMatrices>::Unsubscribe subscribeToCameraInfoChange(Event<CameraMatrices>::Listener listener) {
        return cameraInfoChangedEvent.subscribe(listener);
    }

private:
    inline static Event<void*> depthTargetChangedEvent;
    inline static Event<CameraMatrices> cameraInfoChangedEvent;
    inline static MString paintOpName = "Voxel Paint Operation";
    EventBase::Unsubscribe unsubscribeFromPaintMove;
    EventBase::Unsubscribe unsubscribeFromPaintStateChange;
    bool isPainting{ false };
    MString name;
    int paintOpIndex = 0;
    MRenderTarget* paintRenderTarget;
};