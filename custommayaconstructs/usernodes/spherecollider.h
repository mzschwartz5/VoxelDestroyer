#pragma once

#include "colliderlocator.h"
#include <maya/MFnNumericAttribute.h>
#include <maya/MStatus.h>

class SphereCollider : public ColliderLocator {

public:
    inline static const MTypeId id = MTypeId(0x810F3);
    inline static const MString typeName = MString("SphereCollider");

    inline static MObject aRadius;
    inline static MObject aColliderData;
    inline static MObject aWorldMatrix;

    static void* creator() { return new SphereCollider(); }
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

        attributeAffects(aRadius, aColliderData);

        return MS::kSuccess;
    }

    void prepareForDraw() override
    {
        MObject thisNode = thisMObject();
        MPlug(thisNode, aRadius).getValue(cachedRadius);
    }

    void draw(MUIDrawManager& drawManager) override
    {
        drawManager.sphere(MPoint::origin, cachedRadius, 20, 20, false);
    }

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override
    {
        return ColliderLocator::compute(plug, dataBlock);
    }

private:
    float cachedRadius = 1.0f;

    SphereCollider() : ColliderLocator() {}
    ~SphereCollider() override {}
};