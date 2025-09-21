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
    inline static MObject aColliderData;
    inline static MObject aWorldMatrix;
    inline static MObject aFriction;

    static void* creator() { return new BoxCollider(); }
    static MStatus initialize() {
        MStatus status = initializeBaseAttributes(aColliderData, aWorldMatrix, aFriction);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MFnNumericAttribute nAttr;
        aBoxWidth = nAttr.create("boxWidth", "bw", MFnNumericData::kFloat, 1.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0001f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aBoxWidth);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aBoxHeight = nAttr.create("boxHeight", "bh", MFnNumericData::kFloat, 1.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0001f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aBoxHeight);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aBoxDepth = nAttr.create("boxDepth", "bd", MFnNumericData::kFloat, 1.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0001f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aBoxDepth);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        attributeAffects(aBoxWidth, aColliderData);
        attributeAffects(aBoxHeight, aColliderData);
        attributeAffects(aBoxDepth, aColliderData);

        return MS::kSuccess;
    }

    void prepareForDraw() override
    {
        ColliderLocator::prepareForDraw();
        MObject thisNode = thisMObject();
        MPlug(thisNode, aBoxWidth).getValue(cachedWidth);
        MPlug(thisNode, aBoxHeight).getValue(cachedHeight);
        MPlug(thisNode, aBoxDepth).getValue(cachedDepth);
    }

    void draw(MUIDrawManager& drawManager) override
    {
        if (!shouldDraw) return;
        drawManager.box(MPoint::origin, MVector::yAxis, MVector::xAxis, 0.5f * cachedWidth, 0.5f * cachedHeight, 0.5f * cachedDepth, false);
    }

    void writeDataIntoBuffer(const ColliderData* const data, ColliderBuffer& colliderBuffer, int index = -1) override
    {
        if (index == -1) index = colliderBuffer.numColliders++;
        data->getWorldMatrix().inverse().get(colliderBuffer.inverseWorldMatrix[index]);
        data->getWorldMatrix().get(colliderBuffer.worldMatrix[index]);
        colliderBuffer.inverseWorldMatrix[index][3][3] = data->getFriction(); // store friction in inverse world matrix
        // Hijack elements in bottom row to store geometric parameters.
        colliderBuffer.worldMatrix[index][0][3] = data->getWidth();
        colliderBuffer.worldMatrix[index][1][3] = data->getHeight();
        colliderBuffer.worldMatrix[index][2][3] = data->getDepth();
        colliderBuffer.worldMatrix[index][3][3] = 0.0f; // collider type 0 = box
    }

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override
    {
        if (plug != aColliderData) return MS::kUnknownParameter;

        MDataHandle worldMatrixHandle = dataBlock.inputValue(aWorldMatrix);
        MMatrix worldMat = worldMatrixHandle.asMatrix();
        MDataHandle widthHandle = dataBlock.inputValue(aBoxWidth);
        float width = widthHandle.asFloat();
        MDataHandle heightHandle = dataBlock.inputValue(aBoxHeight);
        float height = heightHandle.asFloat();
        MDataHandle depthHandle = dataBlock.inputValue(aBoxDepth);
        float depth = depthHandle.asFloat();
        MDataHandle frictionHandle = dataBlock.inputValue(aFriction);
        float friction = frictionHandle.asFloat();

        MFnPluginData fnData;
        MObject newDataObj = fnData.create(ColliderData::id);
        ColliderData* colliderData = static_cast<ColliderData*>(fnData.data());
        
        colliderData->setWorldMatrix(worldMat);
        colliderData->setWidth(width);
        colliderData->setHeight(height);
        colliderData->setDepth(depth);
        colliderData->setFriction(friction);

        MDataHandle colliderDataHandle = dataBlock.outputValue(aColliderData);
        colliderDataHandle.set(colliderData);
        dataBlock.setClean(plug);

        return MS::kSuccess;
    }

private:
    float cachedWidth = 1.0f;
    float cachedHeight = 1.0f;
    float cachedDepth = 1.0f;

    BoxCollider() : ColliderLocator() {}
    ~BoxCollider() override {}
};