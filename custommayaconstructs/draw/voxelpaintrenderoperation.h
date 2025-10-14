#pragma once
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MGlobal.h>

class VoxelPaintRenderOperation : public MUserRenderOperation {
public:
    VoxelPaintRenderOperation(const MString& name) : MUserRenderOperation(name) {
        mOperationType = kUserDefined;
        // Below, we override this input target to be an offscreen target, separate from the standard scene's depth target.
        mInputTargetNames.append(kDepthTargetName);
    }

    ~VoxelPaintRenderOperation() override = default;

    MStatus execute(const MDrawContext& drawContext) override {
        return MStatus::kSuccess;
    }

    /**
     * Tell Maya to override the standard depth target with our own offscreen depth target.
     */
    bool getInputTargetDescription(const MString& name, MRenderTargetDescription& description) override {
        if (name != kDepthTargetName) return false;
        
        MRenderer::theRenderer()->outputTargetSize(currentOutputWidth, currentOutputHeight);
        description.setName(paintDepthRenderTargetName);
        description.setWidth(currentOutputWidth);
        description.setHeight(currentOutputHeight);
        description.setMultiSampleCount(1);
        description.setRasterFormat(MHWRender::kD32_FLOAT);
        description.setArraySliceCount(1);
        description.setIsCubeMap(false);

        return true;
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
    inline static const MString paintDepthRenderTargetName = "voxelPaintDepthTarget";
    MRenderTarget* paintOutputRenderTarget = nullptr;
    unsigned int currentOutputWidth = 0;
    unsigned int currentOutputHeight = 0;
};