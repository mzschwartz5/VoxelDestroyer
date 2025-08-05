#pragma once

#include <maya/MGlobal.h>
#include <maya/MPxDeformerNode.h>
#include <maya/MDataBlock.h>
#include <maya/MItGeometry.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MFnNumericAttribute.h>

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
        MStatus status;

        // This is the output of the PBD sim node, which is just used to trigger evaluation of the deformer.
        MFnNumericAttribute nAttr;
        aTrigger = nAttr.create("trigger", "trg", MFnNumericData::kBoolean, false, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setStorable(false);
        nAttr.setWritable(true);
        status = addAttribute(aTrigger);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        attributeAffects(aTrigger, outputGeom);

        // Set the minimum verts needed to run on the GPU to 0, so we never fall back to CPU evaluation.
        // TODO: this will not work if a new scene is loaded in without the plugin being reloaded. Consider a scene load callback to set this and other similar settings.
        // Or use putenv mel command to change the associated env variable to make it last for the entire maya session.
        MGlobal::executeCommand("deformerEvaluator -limitMinimumVerts false;", true, false);
        return MS::kSuccess; 
    }

    void getCacheSetup(
        const MEvaluationNode& evalNode, 
        MNodeCacheDisablingInfo& disablingInfo, 
        MNodeCacheSetupInfo& cacheSetupInfo, 
        MObjectArray& monitoredAttrs) 
    const {
        disablingInfo.setCacheDisabled(true);
        disablingInfo.setReason("The VoxelDestroyer plugin does not currently support Cached Playback.");   
    }

    static MString typeName() { return "VoxelDeformerCPUNode"; }

    MStatus deform(MDataBlock&, MItGeometry&, const MMatrix&, unsigned int) override {
        // No-op: CPU fallback does nothing. Will run once or twice when the nodes are set up,
        // so it's not suitable to log an error here in the case of a legitimate issue with GPU playback, but aside from the base prerequisites of having the evaluation manager and
        // the deformerEvaluator plugin enabled, a few other reasons CPU fallback could occur are listed here: 
        // https://damassets.autodesk.net/content/dam/autodesk/www/html/using-parallel-maya/2023/UsingParallelMaya.html#custom-evaluators
        return MS::kSuccess;
    }

    static void instantiateAndAttachToMesh(const MDagPath& meshDagPath, const MObject& pbdNodeObj) {
        MStringArray deformerNodeNameResult;
        MGlobal::executeCommand("deformer -type " + typeName() + " " + meshDagPath.fullPathName(), deformerNodeNameResult, true, false);
        MString deformerNodeName = deformerNodeNameResult[0];

        // Connect the PBD node's trigger output to the deformer node's trigger input.
        MFnDependencyNode pbdNode(pbdNodeObj);
        MPlug pbdTriggerPlug = pbdNode.findPlug("trigger", false);
        MGlobal::executeCommandOnIdle("connectAttr " + pbdTriggerPlug.name() + " " + deformerNodeName + ".trigger", false);
    }

    static MTypeId id;
    static MObject aTrigger;
};

MTypeId VoxelDeformerCPUNode::id(0x0012F000);
MObject VoxelDeformerCPUNode::aTrigger;