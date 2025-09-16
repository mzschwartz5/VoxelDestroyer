#pragma once

#include "colliderlocator.h"
#include <maya/MFnNumericAttribute.h>
#include <maya/MStatus.h>

class CylinderCollider : public ColliderLocator {

public:
    inline static const MTypeId id = MTypeId(0x810F5);
    inline static const MString typeName = MString("CylinderCollider");

    inline static MObject aRadius;
    inline static MObject aHeight;

    static void* creator() { return new CylinderCollider(); }
    static MStatus initialize() {
        MStatus status;
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
        drawManager.cylinder(MPoint::origin, MVector::yAxis, cachedRadius, cachedHeight, 20, false);
    }

private:
    float cachedRadius = 1.0f;
    float cachedHeight = 2.0f;

    CylinderCollider() : ColliderLocator() {}
    ~CylinderCollider() override {}

};