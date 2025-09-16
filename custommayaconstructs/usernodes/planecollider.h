#pragma once

#include "colliderlocator.h"
#include <maya/MFnNumericAttribute.h>
#include <maya/MStatus.h>
#include <maya/MVector.h>

class PlaneCollider : public ColliderLocator {

public:
    inline static const MTypeId id = MTypeId(0x810F6);
    inline static const MString typeName = MString("PlaneCollider");

    inline static MObject aWidth;
    inline static MObject aHeight;
    inline static MObject aNormal;
    inline static MObject aInfinite;

    static void* creator() { return new PlaneCollider(); }
    static MStatus initialize() {
        MStatus status;
        MFnNumericAttribute nAttr;

        aWidth = nAttr.create("width", "wdt", MFnNumericData::kFloat, 5.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aWidth);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aHeight = nAttr.create("height", "hgt", MFnNumericData::kFloat, 5.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aHeight);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aInfinite = nAttr.create("infinite", "inf", MFnNumericData::kBoolean, true);
        nAttr.setKeyable(true);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aInfinite);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        return MS::kSuccess;
    }

    void prepareForDraw() override
    {
        MObject thisNode = thisMObject();
        MPlug(thisNode, aWidth).getValue(cachedWidth);
        MPlug(thisNode, aHeight).getValue(cachedHeight);
        cachedWidth *= 0.5f;
        cachedHeight *= 0.5f;

        uiNormalLength = std::max(cachedWidth, cachedHeight) * 0.5f;
        uiConeRadius = uiNormalLength * 0.1f;
        uiConeHeight = uiNormalLength * 0.2f;

        MPlug(thisNode, aInfinite).getValue(cachedInfinite);
    }

    void draw(MUIDrawManager& drawManager) override
    {
        drawManager.rect(MPoint::origin, MVector::zAxis, MVector::yAxis, cachedWidth, cachedHeight, false);
        drawManager.line(MPoint::origin, MPoint::origin + MVector::yAxis * uiNormalLength);
        drawManager.cone(MPoint::origin + MVector::yAxis * uiNormalLength, MVector::yAxis, uiConeRadius, uiConeHeight, 10, true);
    }

private:
    float cachedWidth = 5.0f;
    float cachedHeight = 5.0f;
    float uiNormalLength = 2.0f;
    float uiConeRadius = 0.2f;
    float uiConeHeight = 0.4f;
    bool cachedInfinite = false;

    PlaneCollider() : ColliderLocator() {}
    ~PlaneCollider() override {}

};