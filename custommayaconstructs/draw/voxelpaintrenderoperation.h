#pragma once
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MGlobal.h>

class VoxelPaintRenderOperation : public MUserRenderOperation {
public:
    VoxelPaintRenderOperation(const MString& name) : MUserRenderOperation(name) {
        mOperationType = kUserDefined;
        mInputTargetNames.append(paintDepthRenderTargetName);
        mInputTargetNames.append(paintOutputRenderTargetName);
        mOutputTargetNames.append(paintDepthRenderTargetName);
        mOutputTargetNames.append(paintOutputRenderTargetName);

        MRenderTargetDescription desc;
        desc.setName(paintOutputRenderTargetName);
        desc.setRasterFormat(MHWRender::kR32_UINT);
        renderTargetDescriptions[0] = desc;

        desc.setName(paintDepthRenderTargetName);
        desc.setRasterFormat(MHWRender::kD32_FLOAT);
        renderTargetDescriptions[1] = desc;
    }

    ~VoxelPaintRenderOperation() override = default;

    MStatus execute(const MDrawContext& drawContext) override {
        return MStatus::kSuccess;
    }

    /**
     * Tell Maya to override the standard depth and color targets with our own offscreen depth target.
     * It will allocate and manage them for us.
     */
    bool getInputTargetDescription(const MString& name, MRenderTargetDescription& description) override {
        unsigned int width, height;
        MRenderer::theRenderer()->outputTargetSize(width, height);

        if (name == paintDepthRenderTargetName) {
            description = renderTargetDescriptions[1];
            description.setWidth(width);
            description.setHeight(height);
            return true;
        }

        if (name == paintOutputRenderTargetName) {
            description = renderTargetDescriptions[0];
            description.setWidth(width);
            description.setHeight(height);
            return true;
        }

        return false;
    }

private:
    inline static const MString paintOutputRenderTargetName = "voxelPaintOutputTarget";
    inline static const MString paintDepthRenderTargetName = "voxelPaintDepthTarget";
    MRenderTargetDescription renderTargetDescriptions[2];
};