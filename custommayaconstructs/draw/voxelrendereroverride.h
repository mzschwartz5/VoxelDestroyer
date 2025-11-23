#pragma once
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include "../../event.h"
#include "voxelpaintrenderoperation.h"
#include "../commands/changevoxeleditmodecommand.h"
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
    inline static const MString voxelRendererOverrideName = "VoxelRendererOverride";

    VoxelRendererOverride(const MString& name) : MRenderOverride(name), name(name) {
        MRenderer::theRenderer()->getStandardViewportOperations(mOperations);
        // Maya manages the memory / lifetime of the operation passed in
        MClearOperation* clearVoxelPaintOp = new MClearOperation(paintClearOpName);
        clearVoxelPaintOp->setMask(MClearOperation::kClearColor | MClearOperation::kClearDepth);
        clearVoxelPaintOp->renameOutputTarget(MRenderOperation::kColorTargetName, VoxelPaintRenderOperation::paintColorRenderTargetName);
        clearVoxelPaintOp->renameOutputTarget(MRenderOperation::kDepthTargetName, VoxelPaintRenderOperation::paintDepthRenderTargetName);
        
        mOperations.insertBefore(MRenderOperation::kStandardPresentName, new VoxelPaintRenderOperation(paintOpName));
        mOperations.insertBefore(paintOpName, clearVoxelPaintOp);
        paintOpIndex = mOperations.indexOf(paintOpName);
        paintClearOpIndex = mOperations.indexOf(paintClearOpName);

        unsubscribeEditModeChange = ChangeVoxelEditModeCommand::subscribe([this](const EditModeChangedEventArgs& args) {
            this->isPainting = (args.newMode == VoxelEditMode::FacePaint || args.newMode == VoxelEditMode::ParticlePaint);
        });
    }

    ~VoxelRendererOverride() override {
        unsubscribeEditModeChange();
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

        mOperations[paintClearOpIndex]->setEnabled(isPainting);
        mOperations[paintOpIndex]->setEnabled(isPainting);
        return MStatus::kSuccess;
    }

    MString uiName() const override {
        return name;
    }

    DrawAPI supportedDrawAPIs() const override {
        return kDirectX11;
    }

    static VoxelRendererOverride* instance() {
        MRenderer* renderer = MRenderer::theRenderer();
        if (!renderer) return nullptr;

        const VoxelRendererOverride* voxelRendererOverrideConst = static_cast<const VoxelRendererOverride*>(renderer->findRenderOverride(VoxelRendererOverride::voxelRendererOverrideName));
        VoxelRendererOverride* voxelRendererOverride = const_cast<VoxelRendererOverride*>(voxelRendererOverrideConst);
        return voxelRendererOverride;
    }

    void sendVoxelInfoToPaintRenderOp(
        VoxelEditMode paintMode,
        const MMatrixArray& allVoxelMatrices, 
        const std::vector<uint32_t>& visibleVoxelIdToGlobalId,
        PingPongView& voxelPaintViews,
        float particleRadius
    ) {
        VoxelPaintRenderOperation* paintOp = static_cast<VoxelPaintRenderOperation*>(mOperations[paintOpIndex]);
        paintOp->prepareToPaint(paintMode, allVoxelMatrices, visibleVoxelIdToGlobalId, voxelPaintViews, particleRadius);
    }

    // TODO: these do not have to be static - consumers can use MRenderer to get the active render override instance.
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
    inline static MString paintClearOpName = "Voxel Paint Clear Operation";
    EventBase::Unsubscribe unsubscribeEditModeChange;
    bool isPainting{ false };
    MString name;
    int paintOpIndex = 0;
    int paintClearOpIndex = 0;
    MRenderTarget* paintRenderTarget;
};