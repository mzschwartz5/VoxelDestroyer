#pragma once

#include <maya/MPxGPUDeformer.h>
#include <maya/MStatus.h>
#include <maya/MGPUDeformerRegistry.h>

class VoxelDeformerGPUNode : public MPxGPUDeformer {
public:
    VoxelDeformerGPUNode() = default;
    ~VoxelDeformerGPUNode() override = default;

    static MGPUDeformerRegistrationInfo* getGPUDeformerInfo();

    DeformerStatus evaluate(
        MDataBlock& block,
        const MEvaluationNode& evaluationNode,
        const MPlug& outputPlug,
        const MPlugArray& inputPlugs,
        const MGPUDeformerData& inputData,
        MGPUDeformerData& outputData 
    ) override {
        return kDeformerSuccess;
    }
};

class VoxelDeformerGPUNodeInfo : public MGPUDeformerRegistrationInfo {
public:
    VoxelDeformerGPUNodeInfo() {};
    ~VoxelDeformerGPUNodeInfo() override = default;

    MPxGPUDeformer* createGPUDeformer() override {
        return new VoxelDeformerGPUNode();
    }

    bool validateNodeInGraph(MDataBlock& block, const MEvaluationNode&, const MPlug& plug, MStringArray* messages) override {
        return true;
    }

    bool validateNodeValues(MDataBlock& block, const MEvaluationNode&, const MPlug& plug, MStringArray* messages) override {
        return true;
    }
};

// Has to be outside of the class because of dependency on the definition of the info class.
inline MGPUDeformerRegistrationInfo* VoxelDeformerGPUNode::getGPUDeformerInfo() {
    static VoxelDeformerGPUNodeInfo info;
    return &info;
}