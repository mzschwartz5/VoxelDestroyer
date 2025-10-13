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

};