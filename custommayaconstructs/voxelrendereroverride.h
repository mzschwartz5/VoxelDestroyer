#pragma once
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include "../event.h"
using namespace MHWRender;

struct CameraMatrices
{
    float viewportWidth{ 0.0f };
    float viewportHeight{ 0.0f };
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 invViewProjMatrix;
};

/**
 * Note: to actually activate a render override, you need to register it and THEN select it from the renderer drop down menu in Maya.
 * There *is* a way to programmatically switch to the override via MEL (see plugin.cpp).
 */
class VoxelRendererOverride : public MRenderOverride {
public:
    VoxelRendererOverride(const MString& name) : MRenderOverride(name), name(name) {}

    MStatus setup(const MString& destination) override {
        const MFrameContext* mFrameContext = getFrameContext();
        const MRenderTarget* depthTarget = mFrameContext->getCurrentDepthRenderTarget();

        const MMatrix& viewMatrix = mFrameContext->getMatrix(MFrameContext::kViewMtx);
        const MMatrix& projMatrix = mFrameContext->getMatrix(MFrameContext::kProjectionMtx);
        const MMatrix& invViewProjMatrix = mFrameContext->getMatrix(MFrameContext::kViewProjInverseMtx);

        int viewportWidth, viewportHeight, viewportOriginX, viewportOriginY;
        mFrameContext->getViewportDimensions(viewportOriginX, viewportOriginY, viewportWidth, viewportHeight);

        if (depthTarget != nullptr && depthTarget != currentDepthTarget) {
            currentDepthTarget = depthTarget;
            depthTargetChangedEvent.notify(depthTarget->resourceHandle());
        }

        cameraInfoChangedEvent.notify({ static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), mayaMatrixToGlm(viewMatrix), mayaMatrixToGlm(projMatrix), mayaMatrixToGlm(invViewProjMatrix) });

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
    MString name;
    const MRenderTarget* currentDepthTarget = nullptr;

    inline glm::mat4 mayaMatrixToGlm(const MMatrix& matrix) {
        return glm::mat4(
            matrix(0, 0), matrix(1, 0), matrix(2, 0), matrix(3, 0),
            matrix(0, 1), matrix(1, 1), matrix(2, 1), matrix(3, 1),
            matrix(0, 2), matrix(1, 2), matrix(2, 2), matrix(3, 2),
            matrix(0, 3), matrix(1, 3), matrix(2, 3), matrix(3, 3)
        );
    }
};