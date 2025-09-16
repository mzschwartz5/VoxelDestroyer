#pragma once

#include "../usernodes/colliderlocator.h"
#include <maya/MPxDrawOverride.h>
#include <maya/MFnDagNode.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MVector.h>
using namespace MHWRender;

class ColliderDrawOverride : public MPxDrawOverride {
public:
    inline static MString drawDbClassification = "drawdb/geometry/collider";
    inline static MString drawRegistrantId = "ColliderDrawOverrideRegistrant";

    static MPxDrawOverride* creator(const MObject& obj) { return new ColliderDrawOverride(obj); }

    bool excludedFromPostEffects() const override { return true; }
    bool hasUIDrawables() const override { return true; }
    bool disableInternalBoundingBoxDraw() const override { return true; }

    DrawAPI supportedDrawAPIs() const override {
        return kDirectX11;
    }

    MUserData* prepareForDraw(
        const MDagPath& objPath,
        const MDagPath& cameraPath,
        const MFrameContext& frameContext,
        MUserData* oldData) override
    {
        MObject node = objPath.node();
        ColliderLocator* locator = static_cast<ColliderLocator*>(MFnDagNode(node).userNode());
        if (!locator) return nullptr;

        locator->prepareForDraw();
        return nullptr;
    }

    void addUIDrawables (
        const MDagPath& objPath,
        MUIDrawManager& drawManager,
        const MFrameContext& frameContext,
        const MUserData* data
    ) override {
        MObject node = objPath.node();
        ColliderLocator* locator = static_cast<ColliderLocator*>(MFnDagNode(node).userNode());
        if (!locator) return;

        locator->draw(drawManager);
    }

    /**
     * Overridden to return the object's transform matrix without scale (which would otherwise complicate collider calculations for non-box colliders).
     */
    MMatrix transform(const MDagPath& objPath, const MDagPath& cameraPath) const override {
        MMatrix worldMatrix = objPath.inclusiveMatrix();
        MTransformationMatrix transformMatrix(worldMatrix);

        double qx = 0.0, qy = 0.0, qz = 0.0, qw = 1.0;
        transformMatrix.getRotationQuaternion(qx, qy, qz, qw);
        MVector translation = transformMatrix.getTranslation(MSpace::kWorld);

        MTransformationMatrix unscaledMatrix;
        unscaledMatrix.setTranslation(translation, MSpace::kWorld);
        unscaledMatrix.setRotationQuaternion(qx, qy, qz, qw);

        return unscaledMatrix.asMatrix();
    }

private:
    ColliderDrawOverride(const MObject& obj) : MPxDrawOverride(obj, /* no callback necessary - UI only */ nullptr) {}

};