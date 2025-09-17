#pragma once

#include "colliderlocator.h"
#include <maya/MFnNumericAttribute.h>
#include <maya/MStatus.h>

class CapsuleCollider : public ColliderLocator {

public:
    inline static const MTypeId id = MTypeId(0x810F4);
    inline static const MString typeName = MString("CapsuleCollider");

    inline static MObject aRadius;
    inline static MObject aHeight;
    inline static MObject aColliderData;
    inline static MObject aWorldMatrix;

    static void* creator() { return new CapsuleCollider(); }
    static MStatus initialize() {
        MStatus status = initializeColliderDataAttribute(aColliderData, aWorldMatrix);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MFnNumericAttribute nAttr;
        aRadius = nAttr.create("radius", "rds", MFnNumericData::kFloat, 1.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aRadius);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aHeight = nAttr.create("height", "hgt", MFnNumericData::kFloat, 2.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aHeight);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        attributeAffects(aRadius, aColliderData);
        attributeAffects(aHeight, aColliderData);

        return MS::kSuccess;
    }

    void prepareForDraw() override
    {
        MObject thisNode = thisMObject();
        MPlug(thisNode, aRadius).getValue(cachedRadius);
        MPlug(thisNode, aHeight).getValue(cachedHeight);
    }

    void draw(MUIDrawManager& drawManager) override
    {
        drawManager.capsule(MPoint::origin, MVector::yAxis, cachedRadius, cachedHeight, 20, 10, false);
    }

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override
    {
        if (plug != aColliderData) return MS::kUnknownParameter;

        MDataHandle worldMatrixHandle = dataBlock.inputValue(aWorldMatrix);
        MMatrix worldMat = worldMatrixHandle.asMatrix();

        MDataHandle colliderDataHandle = dataBlock.outputValue(aColliderData);
        ColliderData* colliderData = static_cast<ColliderData*>(colliderDataHandle.asPluginData());

        colliderData->setWorldMatrix(worldMat);
        colliderData->setRadius(cachedRadius);
        colliderData->setHeight(cachedHeight);

        colliderDataHandle.set(colliderData);
        dataBlock.setClean(plug);

        return MS::kSuccess;
    }

private:
    float cachedRadius = 1.0f;
    float cachedHeight = 2.0f;

    CapsuleCollider() : ColliderLocator() {}
    ~CapsuleCollider() override {}
};