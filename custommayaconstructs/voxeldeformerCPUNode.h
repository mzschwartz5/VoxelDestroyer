#pragma once

#include <maya/MGlobal.h>
#include <maya/MPxDeformerNode.h>
#include <maya/MDataBlock.h>
#include <maya/MItGeometry.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnPluginData.h>
#include "particledata.h"
#include "deformerdata.h"

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

        // Particle info used by the GPU override to set up GPU buffer resources.
        MFnTypedAttribute tAttr;
        aParticleData = tAttr.create("particledata", "ptd", ParticleData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(false); // NOT storable - just for initialization
        tAttr.setWritable(true);
        tAttr.setReadable(false); 
        status = addAttribute(aParticleData);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // The offset into the vertex buffer for each voxel's vertices
        aDeformerData = tAttr.create("deformerData", "dfd", DeformerData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(true); // YES storable - we want this data to persist with save/load
        tAttr.setWritable(false);
        tAttr.setReadable(false);
        status = addAttribute(aDeformerData);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // This is the output of the PBD sim node, which is just used to trigger evaluation of the deformer.
        MFnNumericAttribute nAttr;
        aTrigger = nAttr.create("trigger", "trg", MFnNumericData::kBoolean, false, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setStorable(false);
        nAttr.setWritable(true);
        nAttr.setReadable(false);
        status = addAttribute(aTrigger);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        attributeAffects(aTrigger, outputGeom);

        // Set the minimum verts needed to run on the GPU to 0, so we never fall back to CPU evaluation.
        MGlobal::executeCommand("deformerEvaluator -limitMinimumVerts false;", false, false);
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

    /**
     * Factory method for creating a deformer node. This method assumes (via arg) that the PBD node has already been created.
     */
    static void createDeformerNode(const MDagPath& meshDagPath, const MObject& pbdNodeObj, std::vector<uint>& vertStartIdx) {
        MStatus status;
        MStringArray deformerNodeNameResult;
        MGlobal::executeCommand("deformer -type " + typeName() + " " + meshDagPath.fullPathName(), deformerNodeNameResult, true, false);
        MString deformerNodeName = deformerNodeNameResult[0];
        MSelectionList selList;
        selList.add(deformerNodeName);
        MObject deformerNodeObj;
        selList.getDependNode(0, deformerNodeObj);
        MFnDependencyNode deformerNode(deformerNodeObj, &status);

        // Set the deformer data attribute on the deformer node.
        MFnPluginData pluginDataFn;
        MObject deformerDataObj = pluginDataFn.create(DeformerData::id, &status);
        DeformerData* deformerData = static_cast<DeformerData*>(pluginDataFn.data(&status));
        deformerData->setVertexStartIdx(std::move(vertStartIdx));
        MPlug deformerDataPlug = deformerNode.findPlug("deformerData", false, &status);
        deformerDataPlug.setValue(deformerDataObj);

        // Connect the PBD node's trigger output to the deformer node's trigger input.
        MFnDependencyNode pbdNode(pbdNodeObj);
        MPlug pbdTriggerPlug = pbdNode.findPlug("trigger", false);
        MGlobal::executeCommandOnIdle("connectAttr " + pbdTriggerPlug.name() + " " + deformerNodeName + ".trigger", false);

        // Connect the PBD node's particle data output to the deformer node's particle data input.
        MPlug pbdParticleDataPlug = pbdNode.findPlug("particledata", false);
        MGlobal::executeCommandOnIdle("connectAttr " + pbdParticleDataPlug.name() + " " + deformerNodeName + ".particledata", false);
    }

    inline static MTypeId id{0x0012F000};
    inline static MObject aTrigger;
    inline static MObject aParticleData;
    inline static MObject aDeformerData;
};