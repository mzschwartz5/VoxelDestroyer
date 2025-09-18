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

    void writeDataIntoBuffer(const ColliderData* const data, ColliderBuffer& colliderBuffer, int index = -1) override
    {
        if (index == -1) index = colliderBuffer.numColliders++;
        data->getWorldMatrix().get(colliderBuffer.worldMatrix[index]);
        // Hijack diagonal elements to store geometric parameters. Collider locators are all locked to unit-scale, anyway.
        colliderBuffer.worldMatrix[index][0][0] = data->getRadius();
        colliderBuffer.worldMatrix[index][3][3] = 1.0f; // collider type 1 = sphere
    }

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override
    {
        if (plug != aColliderData) return MS::kUnknownParameter;
        MDataHandle worldMatrixHandle = dataBlock.inputValue(aWorldMatrix);
        MMatrix worldMat = worldMatrixHandle.asMatrix();
        MDataHandle radiusHandle = dataBlock.inputValue(aRadius);
        float radius = radiusHandle.asFloat();

        MFnPluginData fnData;
        MObject newDataObj = fnData.create(ColliderData::id);
        ColliderData* colliderData = static_cast<ColliderData*>(fnData.data());
        
        colliderData->setWorldMatrix(worldMat);
        colliderData->setRadius(radius);
        
        MDataHandle colliderDataHandle = dataBlock.outputValue(aColliderData);
        colliderDataHandle.set(colliderData);
        dataBlock.setClean(plug);

        return MS::kSuccess;
    }

private:
    float cachedRadius = 1.0f;

    SphereCollider() : ColliderLocator() {}
    ~SphereCollider() override {}
};