#pragma once
#include <maya/MPxContext.h>
#include <maya/MViewport2Renderer.h>
#include <maya/MGlobal.h>

class VoxelDragContext : public MPxContext
{
public:
    VoxelDragContext() : MPxContext(), dragging(false), mouseButton(0), pickedObject(MObject::kNullObj) {
        setTitleString("Voxel Simulation Tool");
    }
    
    virtual void toolOnSetup(MEvent &event) override {
        MPxContext::toolOnSetup(event);
        MGlobal::displayInfo("Voxel Simulation Tool activated.");
    }

    virtual void toolOffCleanup() override {
        MPxContext::toolOffCleanup();
    }

    virtual MStatus doPress(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        MGlobal::displayInfo("Voxel Simulation Tool pressed.");

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
        MGlobal::displayInfo("Voxel Simulation Tool dragging.");
        if (!dragging) return MS::kSuccess;

        MPoint newWorldPoint;
        if (pickObjectUnderCursor(event, pickedObject, newWorldPoint)) {
            updateSimulatedObjectPosition(newWorldPoint);
        }

        // Optionally, draw feedback in the viewport
        drawFeedback(drawMgr, context);

        return MS::kSuccess;
    }

    virtual MStatus doRelease(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        MGlobal::displayInfo("Voxel Simulation Tool released.");
        dragging = false;
        pickedObject = MObject::kNullObj;
        return MS::kSuccess;
    }

    virtual MStatus drawFeedback(MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        return MS::kSuccess;
    }

private:
    short mouseButton;
    MPoint worldDragStart;
    bool dragging = false;
    MObject pickedObject;

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