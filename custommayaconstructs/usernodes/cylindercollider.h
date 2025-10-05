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
    inline static MObject aColliderData;
    inline static MObject aParentTransformMatrix;
    inline static MObject aFriction;

    static void* creator() { return new CylinderCollider(); }
    static MStatus initialize() {
        MStatus status = initializeBaseAttributes(aColliderData, aParentTransformMatrix, aFriction);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MFnNumericAttribute nAttr;
        aRadius = nAttr.create("radius", "rds", MFnNumericData::kFloat, 1.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0001f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aRadius);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aHeight = nAttr.create("height", "hgt", MFnNumericData::kFloat, 2.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0001f);
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
        ColliderLocator::prepareForDraw();
        MObject thisNode = thisMObject();
        MPlug(thisNode, aRadius).getValue(cachedRadius);
        MPlug(thisNode, aHeight).getValue(cachedHeight);
    }

    void draw(MUIDrawManager& drawManager) override
    {
        if (!shouldDraw) return;
        drawManager.cylinder(MPoint::origin, MVector::yAxis, cachedRadius, cachedHeight, 20, false);
    }

    void writeDataIntoBuffer(const ColliderData* const data, ColliderBuffer& colliderBuffer, int index = -1) override
    {
        if (index == -1) index = colliderBuffer.numColliders++;
        data->getWorldMatrix().inverse().get(colliderBuffer.inverseWorldMatrix[index]);
        data->getWorldMatrix().get(colliderBuffer.worldMatrix[index]);
        colliderBuffer.inverseWorldMatrix[index][3][3] = data->getFriction(); // store friction in inverse world matrix
        // Hijack elements in bottom row to store geometric parameters.
        colliderBuffer.worldMatrix[index][0][3] = data->getRadius();
        colliderBuffer.worldMatrix[index][1][3] = data->getHeight();
        colliderBuffer.worldMatrix[index][3][3] = 3.0f; // collider type 3 = cylinder
    }

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override
    {
        if (plug != aColliderData) return MS::kUnknownParameter;

        MDataHandle parentTransformMatHandle = dataBlock.inputValue(aParentTransformMatrix);
        MMatrix worldMat = Utils::getWorldMatrix(thisMObject());
        MDataHandle radiusHandle = dataBlock.inputValue(aRadius);
        float radius = radiusHandle.asFloat();
        MDataHandle heightHandle = dataBlock.inputValue(aHeight);
        float height = heightHandle.asFloat();
        MDataHandle frictionHandle = dataBlock.inputValue(aFriction);
        float friction = frictionHandle.asFloat();

        Utils::createPluginData<ColliderData>(
            dataBlock,
            aColliderData,
            [&worldMat, &radius, &height, &friction](ColliderData* colliderData) {
                colliderData->setWorldMatrix(worldMat);
                colliderData->setRadius(radius);
                colliderData->setHeight(height);
                colliderData->setFriction(friction);
            }
        );

        return MS::kSuccess;
    }

private:
    float cachedRadius = 1.0f;
    float cachedHeight = 2.0f;

    CylinderCollider() : ColliderLocator() {}
    ~CylinderCollider() override {}
};