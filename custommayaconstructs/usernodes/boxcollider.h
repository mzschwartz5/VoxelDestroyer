#pragma once

#include "colliderlocator.h"
#include <maya/MFnNumericAttribute.h>
#include <maya/MStatus.h>

class BoxCollider : public ColliderLocator {

public:
    inline static const MTypeId id = MTypeId(0x810F2);
    inline static const MString typeName = MString("BoxCollider");

    inline static MObject aBoxWidth;
    inline static MObject aBoxHeight;
    inline static MObject aBoxDepth;

    static void* creator() { return new BoxCollider(); }
    static MStatus initialize() {
        MStatus status;
        MFnNumericAttribute nAttr;
        aBoxWidth = nAttr.create("boxWidth", "bw", MFnNumericData::kFloat, 1.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aBoxWidth);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aBoxHeight = nAttr.create("boxHeight", "bh", MFnNumericData::kFloat, 1.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aBoxHeight);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aBoxDepth = nAttr.create("boxDepth", "bd", MFnNumericData::kFloat, 1.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aBoxDepth);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        return MS::kSuccess;
    }

    void prepareForDraw() override
    {
        MObject thisNode = thisMObject();
        MPlug(thisNode, aBoxWidth).getValue(cachedWidth);
        MPlug(thisNode, aBoxHeight).getValue(cachedHeight);
        MPlug(thisNode, aBoxDepth).getValue(cachedDepth);
    }

    void draw(MUIDrawManager& drawManager) override
    {
        drawManager.beginDrawable();
        drawManager.beginDrawInXray();
        drawManager.setColor(color);

        drawManager.box(MPoint::origin, MVector::yAxis, MVector::xAxis, cachedWidth / 2.0f, cachedHeight / 2.0f, cachedDepth / 2.0f, false);
        
        drawManager.endDrawInXray();
        drawManager.endDrawable();
    }

private:
    float cachedWidth = 1.0f;
    float cachedHeight = 1.0f;
    float cachedDepth = 1.0f;

    BoxCollider() : ColliderLocator() {}
    ~BoxCollider() override {}

};