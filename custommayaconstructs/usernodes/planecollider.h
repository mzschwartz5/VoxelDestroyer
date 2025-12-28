#pragma once

#include "colliderlocator.h"
#include <maya/MFnNumericAttribute.h>
#include <maya/MStatus.h>
#include <maya/MVector.h>
#include <maya/MItDependencyNodes.h>

class PlaneCollider : public ColliderLocator {

public:
    inline static const MTypeId id = MTypeId(0x810F6);
    inline static const MString typeName = MString("PlaneCollider");

    inline static MObject aWidth;
    inline static MObject aHeight;
    inline static MObject aInfinite;
    inline static MObject aColliderData;
    inline static MObject aParentTransformMatrix;
    inline static MObject aFriction;

    static void* creator() { return new PlaneCollider(); }
    static MStatus initialize() {
        MStatus status = initializeBaseAttributes(aColliderData, aParentTransformMatrix, aFriction);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MFnNumericAttribute nAttr;
        aWidth = nAttr.create("width", "wdt", MFnNumericData::kFloat, 5.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0001f);
        nAttr.setSoftMax(100.0f);
        nAttr.setStorable(true);
        nAttr.setReadable(true);
        nAttr.setWritable(true);
        status = addAttribute(aWidth);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aHeight = nAttr.create("height", "hgt", MFnNumericData::kFloat, 5.0f);
        nAttr.setKeyable(true);
        nAttr.setMin(0.0001f);
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

        attributeAffects(aWidth, aColliderData);
        attributeAffects(aHeight, aColliderData);
        attributeAffects(aInfinite, aColliderData);

        return MS::kSuccess;
    }

    void prepareForDraw() override
    {
        ColliderLocator::prepareForDraw();
        MObject thisNode = thisMObject();
        MPlug(thisNode, aWidth).getValue(cachedWidth);
        MPlug(thisNode, aHeight).getValue(cachedHeight);

        uiNormalLength = std::max(cachedWidth, cachedHeight) * 0.5f;
        uiConeRadius = uiNormalLength * 0.1f;
        uiConeHeight = uiNormalLength * 0.2f;

        MPlug(thisNode, aInfinite).getValue(cachedInfinite);
    }

    void draw(MUIDrawManager& drawManager) override
    {
        if (!shouldDraw) return;
        drawManager.rect(MPoint::origin, MVector::zAxis, MVector::yAxis, 0.5f * cachedWidth, 0.5f * cachedHeight, false);
    }

    void writeDataIntoBuffer(const ColliderData* const data, ColliderBuffer& colliderBuffer, int index = -1) override
    {
        if (index == -1) index = colliderBuffer.numColliders++;
        data->getWorldMatrix().get(colliderBuffer.worldMatrix[index]);
        // Hijack elements in bottom row to store geometric parameters.
        colliderBuffer.worldMatrix[index][0][3] = data->getWidth();
        colliderBuffer.worldMatrix[index][1][3] = data->getHeight();
        colliderBuffer.worldMatrix[index][2][3] = data->isInfinite() ? 1.0f : 0.0f; 
        colliderBuffer.worldMatrix[index][3][3] = 4.0f; // collider type 4 = plane
        colliderBuffer.inverseWorldMatrix[index][3][3] = data->getFriction(); // store friction in inverse world matrix
    }

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override
    {
        if (plug != aColliderData) return MS::kUnknownParameter;

        MDataHandle parentTransformMatHandle = dataBlock.inputValue(aParentTransformMatrix);
        MMatrix worldMat = Utils::getWorldMatrixWithoutScale(thisMObject());
        MDataHandle widthHandle = dataBlock.inputValue(aWidth);
        float width = widthHandle.asFloat();
        MDataHandle heightHandle = dataBlock.inputValue(aHeight);
        float height = heightHandle.asFloat();
        MDataHandle infiniteHandle = dataBlock.inputValue(aInfinite);
        bool infinite = infiniteHandle.asBool();
        MDataHandle frictionHandle = dataBlock.inputValue(aFriction);
        float friction = frictionHandle.asFloat();

        Utils::createPluginData<ColliderData>(
            dataBlock,
            aColliderData,
            [&worldMat, &width, &height, &infinite, &friction](ColliderData* colliderData) {
                colliderData->setWorldMatrix(worldMat);
                colliderData->setWidth(width);
                colliderData->setHeight(height);
                colliderData->setInfinite(infinite);
                colliderData->setFriction(friction);
            }
        );

        return MS::kSuccess;
    }

    // TODO: when gravity becomes directional, make ground plane in the "down" direction.
    // Also, the act of searching for existing colliders should be a utility.
    // Potentially have this as an opt-out feature as well (checkbox in the voxelizer menu)
    static MObject createGroundColliderIfNoneExists() {
        // First, see if there are any colliders in the scene already.
        MItDependencyNodes it;
        for (; !it.isDone(); it.next()) {
            MObject node = it.thisNode();
            if (ColliderLocator::isColliderNode(node)) return MObject::kNullObj;
        }

        return Utils::createDagNode(PlaneCollider::typeName, MObject::kNullObj, "GroundPlaneCollider");
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