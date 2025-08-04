#include <maya/MPxNode.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MTypeId.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MEventMessage.h>
#include <maya/MNodeMessage.h>
#include <maya/MCallbackIdArray.h>

#include "plugin.h"
#include "pbd.h"

class VoxelSimulationNode : public MPxNode {
public:
    static MTypeId id;
    // Input attributes
    static MObject relaxationAttr;
    static MObject edgeUniformityAttr;
    static MObject gravityStrengthAttr;
    static MObject faceToFaceRelaxationAttr;
    static MObject faceToFaceEdgeUniformityAttr;

    // Output attribute
    static MObject outputAttr;

    // PBD* pbdInstance{};
    MCallbackIdArray callbackIds;

    static void* creator() {
        return new VoxelSimulationNode();
    }

    static MStatus initialize() {
        MStatus status;
        MFnNumericAttribute nAttr;

        // Relaxation attribute (float, range 0 to 1)
        relaxationAttr = nAttr.create("relaxation", "rel", MFnNumericData::kFloat, 0.5, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setMax(1.0f);
        status = addAttribute(relaxationAttr);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Edge Uniformity attribute (float, range 0 to 1)
        edgeUniformityAttr = nAttr.create("edgeUniformity", "eu", MFnNumericData::kFloat, 0.5, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setMax(1.0f);
        status = addAttribute(edgeUniformityAttr);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Face To Face Relaxation attribute (float, range 0 to 1)
        faceToFaceRelaxationAttr = nAttr.create("faceToFaceRelaxation", "ftfr", MFnNumericData::kFloat, 0.5, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setMax(1.0f);
        status = addAttribute(faceToFaceRelaxationAttr);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Face To Face Edge Uniformity attribute (float, range 0 to 1)
        faceToFaceEdgeUniformityAttr = nAttr.create("faceToFaceEdgeUniformity", "fteu", MFnNumericData::kFloat, 0.5, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setMax(1.0f);
        status = addAttribute(faceToFaceEdgeUniformityAttr);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Gravity Strength attribute (float, range -20 to 20)
        gravityStrengthAttr = nAttr.create("gravityStrength", "gs", MFnNumericData::kFloat, -10.0, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setKeyable(true);
        nAttr.setMin(-20.0f);
        nAttr.setMax(20.0f);
        status = addAttribute(gravityStrengthAttr);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Output attribute - this will be used to trigger compute
        outputAttr = nAttr.create("output", "out", MFnNumericData::kFloat, 0.0, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setWritable(false);
        nAttr.setStorable(false);
        status = addAttribute(outputAttr);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Set up the attribute affects relationships
        attributeAffects(relaxationAttr, outputAttr);
        attributeAffects(edgeUniformityAttr, outputAttr);
        attributeAffects(gravityStrengthAttr, outputAttr);
        attributeAffects(faceToFaceRelaxationAttr, outputAttr);
        attributeAffects(faceToFaceEdgeUniformityAttr, outputAttr);

        return MS::kSuccess;
    }

    void postConstructor() override {
        // Get the MObject for this node
        MObject thisNode = thisMObject();

        // Register a callback for attribute changes
        MStatus status;
        MCallbackId id = MNodeMessage::addAttributeChangedCallback(
            thisNode,
            attributeChangedCallback,
            this,
            &status
        );

        if (status) {
            callbackIds.append(id);
            MGlobal::displayInfo("VoxelSimulationNode: Registered attribute changed callback");
        }
        else {
            MGlobal::displayError("VoxelSimulationNode: Failed to register attribute changed callback");
        }

        // Make sure the node can exist without connections
        setExistWithoutOutConnections(true);
        setExistWithoutInConnections(true);
    }

    ~VoxelSimulationNode() override {
        // Remove all callbacks
        MMessage::removeCallbacks(callbackIds);
    }

    // Callback when attributes change
    static void attributeChangedCallback(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
        // Only respond to attribute value changes
        if (!(msg & MNodeMessage::kAttributeSet)) {
            return;
        }

        // VoxelSimulationNode* node = static_cast<VoxelSimulationNode*>(clientData);
        // if (!node || !node->pbdInstance) {
        //     return;
        // }

        // // Handle attribute changes directly in the callback
        // if (plug.attribute() == relaxationAttr) {
        //     float value = plug.asFloat();
        //     node->pbdInstance->setRelaxation(value);
        // }
        // else if (plug.attribute() == edgeUniformityAttr) {
        //     float value = plug.asFloat();
        //     node->pbdInstance->setBeta(value);
        // }
        // else if (plug.attribute() == gravityStrengthAttr) {
        //     float value = plug.asFloat();
        //     node->pbdInstance->setGravityStrength(value);
        // }
        // else if (plug.attribute() == faceToFaceRelaxationAttr) {
        //     float value = plug.asFloat();
        //     node->pbdInstance->setFTFRelaxation(value);
        // }
        // else if (plug.attribute() == faceToFaceEdgeUniformityAttr) {
        //     float value = plug.asFloat();
        //     node->pbdInstance->setFTFBeta(value);
        // }

        // // Update simulation info after any attribute change
        // node->pbdInstance->updateSimInfo();
        // node->pbdInstance->updateVGSInfo();
        // MGlobal::displayInfo("Voxel Destroyer updated with new parameters");
    }

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override {
        if (plug == outputAttr) {
            // Standard compute would go here, but we're handling updates via callbacks
            // Set the output value to something
            MDataHandle outputHandle = dataBlock.outputValue(outputAttr);
            outputHandle.setFloat(1.0f);
            outputHandle.setClean();
            return MS::kSuccess;
        }

        return MPxNode::compute(plug, dataBlock);
    }

    VoxelSimulationNode() {}
};

// Unique ID for the node
MTypeId VoxelSimulationNode::id(0x0007F123);
MObject VoxelSimulationNode::relaxationAttr;
MObject VoxelSimulationNode::edgeUniformityAttr;
MObject VoxelSimulationNode::gravityStrengthAttr;
MObject VoxelSimulationNode::faceToFaceRelaxationAttr;
MObject VoxelSimulationNode::faceToFaceEdgeUniformityAttr;
MObject VoxelSimulationNode::outputAttr;