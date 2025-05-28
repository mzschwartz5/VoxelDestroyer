#pragma once

#include <maya/MGlobal.h>
#include <maya/MPxDeformerNode.h>
#include <maya/MDataBlock.h>
#include <maya/MItGeometry.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MFnUnitAttribute.h>

/**
 * In order to register a GPU deformer node, Maya first requires a CPU deformer node that
 * can be used as a fallback. Then you can register the GPU deformer as an override of the CPU deformer.
 * 
 * Since a CPU implementation of our algorithm isn't really feasible, this class is a no-op.
 */
class VoxelDeformerCPUNode : public MPxDeformerNode {
public:
    VoxelDeformerCPUNode() = default;
    ~VoxelDeformerCPUNode() override = default;

    static void* creator() { return new VoxelDeformerCPUNode(); }
    static MStatus initialize() { 
        // By using time as an input to this node, we ensure the deformer will evaluate every frame.
        // This is just the plug; time1.outTime needs to be connected to this attribute to function.
        MFnUnitAttribute uAttr;
        aTime = uAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0);
        addAttribute(aTime);
        attributeAffects(aTime, outputGeom);
        return MS::kSuccess; 
    }

    MStatus deform(MDataBlock&, MItGeometry&, const MMatrix&, unsigned int) override {
        // No-op: CPU fallback does nothing
        MGlobal::displayWarning("To enable voxel simulation, a GPU must be available and the deformerEvaluator plug-in must be turned on.");
        return MS::kSuccess;
    }

    static MTypeId id;
    static MObject aTime;
};

MTypeId VoxelDeformerCPUNode::id(0x0012F000);
MObject VoxelDeformerCPUNode::aTime;