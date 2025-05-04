#pragma once
#include <maya/MPxContext.h>
#include <maya/MViewport2Renderer.h>
#include <maya/MGlobal.h>
#include "pbd.h"

class VoxelDragContext : public MPxContext
{
public:
    VoxelDragContext(PBD* pbdSimulator) : MPxContext(), pbdSimulator(pbdSimulator), dragging(false), mouseButton(0), pickedObject(MObject::kNullObj) {
        setTitleString("Voxel Simulation Tool");
    }
    
    virtual void toolOnSetup(MEvent &event) override {
        MPxContext::toolOnSetup(event);

        if (!pbdSimulator) {
            MGlobal::displayError("PBD simulator not initialized.");
            return;
        }
    }

    virtual void toolOffCleanup() override {
        MPxContext::toolOffCleanup();
    }

    virtual MStatus doPress(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        if (event.mouseButton() == MEvent::kLeftMouse) {
            MPoint hitPoint;
            if (pickObjectUnderCursor(event, pickedObject, hitPoint)) {
                worldDragStart = hitPoint;
                dragging = true;
            }
        }
        return MS::kSuccess;
    }

    virtual MStatus doDrag(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        if (!dragging) return MS::kSuccess;

        MPoint newWorldPoint;
        if (pickObjectUnderCursor(event, pickedObject, newWorldPoint)) {
            updateSimulatedObjectPosition(newWorldPoint);
        }

        return MS::kSuccess;
    }

    virtual MStatus doRelease(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        dragging = false;
        pickedObject = MObject::kNullObj;
        return MS::kSuccess;
    }

private:
    short mouseButton;
    MPoint worldDragStart;
    bool dragging = false;
    MObject pickedObject;
    PBD* pbdSimulator = nullptr;

    bool pickObjectUnderCursor(MEvent& event, MObject& outObject, MPoint& worldPoint) {
        short x, y;
        event.getPosition(x, y);
    
        // Implement picking logic here
        return false;
    }
    
    void updateSimulatedObjectPosition(const MPoint &newWorldPoint) {
        // Implement object position update logic here
    }
};