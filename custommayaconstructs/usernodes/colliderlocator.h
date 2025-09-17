#pragma once

#include <maya/MPxLocatorNode.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <maya/MStatus.h>
#include <maya/MUIDrawManager.h>
#include <maya/MMatrix.h>
#include <maya/MColor.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnDagNode.h>
#include <maya/MDGModifier.h>
#include "../data/colliderdata.h"

/**
 * UI locator node for collision primitives.
 */
class ColliderLocator : public MPxLocatorNode {
public:

    ColliderLocator() {}
    ~ColliderLocator() override {}

    virtual void draw(MUIDrawManager& drawManager) = 0;
    virtual void prepareForDraw() = 0;

    virtual MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override {
        // No-op for now, just pull the data to prove it works.
        MFnDependencyNode thisNode(thisMObject());
        MPlug colliderDataPlug = thisNode.findPlug(colliderDataAttrName, true);
        if (plug == colliderDataPlug) {
            MPlug wmPlug = thisNode.findPlug(worldMatrixAttrName, true);
            MDataHandle wmHandle = dataBlock.inputValue(wmPlug);
            MMatrix worldMat = wmHandle.asMatrix();
            MDataHandle colliderDataHandle = dataBlock.outputValue(colliderDataPlug);
            colliderDataHandle.setClean();
            return MS::kSuccess;
        }
        
        return MS::kUnknownParameter;
    }

protected:
    static MStatus initializeColliderDataAttribute(
        MObject& colliderDataAttr,
        MObject& worldMatrix
    ) {
        MStatus status;
        MFnTypedAttribute tAttr;
        colliderDataAttr = tAttr.create(colliderDataAttrName, "cd", ColliderData::id);
        tAttr.setStorable(false);
        tAttr.setReadable(true);
        tAttr.setWritable(false);

        status = addAttribute(colliderDataAttr);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MFnMatrixAttribute mAttr;
        worldMatrix = mAttr.create(worldMatrixAttrName, "wmi", MFnMatrixAttribute::kDouble);
        mAttr.setStorable(false);
        mAttr.setReadable(false);
        mAttr.setWritable(true);

        status = addAttribute(worldMatrix);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        
        status = attributeAffects(worldMatrix, colliderDataAttr);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        return status;
    }

private:
    inline static const MString colliderDataAttrName = MString("colliderData");
    // Can't use the name "worldMatrix" here because Maya uses that already for an _output_ attribute
    inline static const MString worldMatrixAttrName = MString("worldMatrixIn");
};