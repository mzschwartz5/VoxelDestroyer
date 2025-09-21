#pragma once

#include "../usernodes/colliderlocator.h"
#include <maya/MPxDrawOverride.h>
#include <maya/MFnDagNode.h>
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

private:
    ColliderDrawOverride(const MObject& obj) : MPxDrawOverride(obj, /* no callback necessary - UI only */ nullptr) {}

};