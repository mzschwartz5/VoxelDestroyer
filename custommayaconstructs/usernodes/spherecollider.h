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
    inline static MObject aParentTransformMatrix;
    inline static MObject aFriction;

    static void* creator() { return new SphereCollider(); }
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

        attributeAffects(aRadius, aColliderData);

        return MS::kSuccess;
    }

    void prepareForDraw() override
    {
        ColliderLocator::prepareForDraw();
        MObject thisNode = thisMObject();
        MPlug(thisNode, aRadius).getValue(cachedRadius);
    }

    void draw(MUIDrawManager& drawManager) override
    {
        if (!shouldDraw) return;
        drawManager.sphere(MPoint::origin, cachedRadius, 20, 20, false);
    }

    void writeDataIntoBuffer(const ColliderData* const data, ColliderBuffer& colliderBuffer, int index = -1) override
    {
        if (index == -1) index = colliderBuffer.numColliders++;
        data->getWorldMatrix().get(colliderBuffer.worldMatrix[index]);
        // Hijack elements in bottom row to store geometric parameters.
        colliderBuffer.worldMatrix[index][0][3] = data->getRadius();
        colliderBuffer.worldMatrix[index][3][3] = 1.0f; // collider type 1 = sphere
        colliderBuffer.inverseWorldMatrix[index][3][3] = data->getFriction(); // store friction in inverse world matrix
    }

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override
    {
        if (plug != aColliderData) return MS::kUnknownParameter;
        MDataHandle parentTransformMatHandle = dataBlock.inputValue(aParentTransformMatrix);
        MMatrix worldMat = Utils::getWorldMatrix(thisMObject());
        MDataHandle radiusHandle = dataBlock.inputValue(aRadius);
        float radius = radiusHandle.asFloat();
        MDataHandle frictionHandle = dataBlock.inputValue(aFriction);
        float friction = frictionHandle.asFloat();

        Utils::createPluginData<ColliderData>(
            dataBlock,
            aColliderData,
            [&worldMat, &radius, &friction](ColliderData* colliderData) {
                colliderData->setWorldMatrix(worldMat);
                colliderData->setRadius(radius);
                colliderData->setFriction(friction);
            });

        return MS::kSuccess;
    }

private:
    float cachedRadius = 1.0f;

    SphereCollider() : ColliderLocator() {}
    ~SphereCollider() override {}
};