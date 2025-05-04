#pragma once
#include <maya/MGlobal.h>
#include <maya/MPxContext.h>
#include <maya/MViewport2Renderer.h>
#include <maya/M3dView.h>
#include "pbd.h"
#include <algorithm>

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

        M3dView view = M3dView::active3dView();
        viewportWidth = view.portWidth();
    }

    virtual void toolOffCleanup() override {
        MPxContext::toolOffCleanup();
    }

    virtual MStatus doPress(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        event.getPosition(mouseX, mouseY);
        screenDragStart = MPoint(mouseX, mouseY, 0.0);

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
        static short dragX, dragY;

        // To grow/shrink the circle radius, but not move the drawn circle while doing so, we need separate variables for the drag position and the draw position
        event.getPosition(dragX, dragY);
        if (event.mouseButton() == MEvent::kMiddleMouse) {
            double distX = dragX - screenDragStart.x;
            selectRadius = std::clamp(selectRadius + static_cast<float>((distX / viewportWidth) * 40.0), 5.0f, 400.0f);
            return MS::kSuccess;
        }

        event.getPosition(mouseX, mouseY);
        if (!dragging) return MS::kSuccess;

        MPoint newWorldPoint;
        if (pickObjectUnderCursor(event, pickedObject, newWorldPoint)) {
            updateSimulatedObjectPosition(newWorldPoint);
        }

        return MS::kSuccess;
    }

    virtual MStatus doRelease(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        event.getPosition(mouseX, mouseY);

        dragging = false;
        pickedObject = MObject::kNullObj;
        return MS::kSuccess;
    }

    virtual MStatus doPtrMoved(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        event.getPosition(mouseX, mouseY);

        return MS::kSuccess;
    }

    virtual MStatus drawFeedback(MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& frameContext) override {
        MPoint mousePoint2D(mouseX, mouseY, 0.0);
        
        drawMgr.beginDrawable();
    
        // Set color and line width
        drawMgr.setColor(MColor(0.5f, 0.5f, 0.5f));
        drawMgr.setLineWidth(2.0f);
    
        // Draw a circle at the mouse position
        const unsigned int segments = 40;
        drawMgr.circle2d(mousePoint2D, selectRadius, segments, false);  // false = not filled
    
        drawMgr.endDrawable();

        return MS::kSuccess;
    }

private:
    int viewportWidth;
    short mouseX, mouseY;
    short mouseButton;
    float selectRadius = 50.0f;
    MPoint worldDragStart;
    MPoint screenDragStart;
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