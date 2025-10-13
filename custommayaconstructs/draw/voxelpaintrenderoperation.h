#pragma once
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MGlobal.h>

class VoxelPaintRenderOperation : public MUserRenderOperation {
public:
    VoxelPaintRenderOperation(const MString& name) : MUserRenderOperation(name) {
        mOperationType = MRenderOperation::kUserDefined;
    }

    ~VoxelPaintRenderOperation() override = default;

    MStatus execute(const MDrawContext& drawContext) override {
        return MStatus::kSuccess;
    }

    MRenderTarget* const* targetOverrideList(unsigned int &listSize) override {
        unsigned int viewportWidth, viewportHeight;
        MRenderer::theRenderer()->outputTargetSize(viewportWidth, viewportHeight);
        createOrUpdateRenderTargets(viewportWidth, viewportHeight);

        listSize = 1;
        return &paintOutputRenderTarget;
    }

    void createOrUpdateRenderTargets(unsigned int targetWidth, unsigned int targetHeight) {
        MRenderer* renderer = MRenderer::theRenderer();
        if (!renderer) return;

        const MRenderTargetManager* targetManager = renderer->getRenderTargetManager();
        if (!targetManager) return;
        if (targetWidth == currentOutputWidth && targetHeight == currentOutputHeight) return;

        currentOutputWidth = targetWidth;
        currentOutputHeight = targetHeight;

        MRenderTargetDescription desc(
            paintOutputRenderTargetName,
            currentOutputWidth, currentOutputHeight,
            1, // number of samples
            MHWRender::kR32_UINT, // format
            1, // array slices
            false // is cube map
        );

        if (paintOutputRenderTarget) {
            paintOutputRenderTarget->updateDescription(desc);
            return;
        }

        paintOutputRenderTarget = targetManager->acquireRenderTarget(desc);
    }

    void clearRenderTargets() {
        if (paintOutputRenderTarget) {
            MRenderer::theRenderer()->getRenderTargetManager()->releaseRenderTarget(paintOutputRenderTarget);
            paintOutputRenderTarget = nullptr;
        }
    }

private:
    inline static const MString paintOutputRenderTargetName = "voxelPaintOutputTarget";
    MRenderTarget* paintOutputRenderTarget = nullptr;
    unsigned int currentOutputWidth = 0;
    unsigned int currentOutputHeight = 0;
};