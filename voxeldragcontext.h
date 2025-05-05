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
    VoxelDragContext(PBD* pbdSimulator) : MPxContext(), pbdSimulator(pbdSimulator) {
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
        screenDragStartX = mouseX;
        screenDragStartY = mouseY;
        pbdSimulator->updateDragValues({ mouseX, mouseY, mouseX, mouseY, selectRadius });

        pbdSimulator->setIsDragging(true);
        return MS::kSuccess;
    }

    virtual MStatus doDrag(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        static short dragX, dragY;

        // To grow/shrink the circle radius, but not move the drawn circle while doing so, we need separate variables for the drag position and the draw position
        event.getPosition(dragX, dragY);
        short distX = dragX - screenDragStartX;
        short distY = dragY - screenDragStartY;
        if (event.mouseButton() == MEvent::kMiddleMouse) {
            selectRadius = std::clamp(selectRadius + ((static_cast<float>(distX) / viewportWidth) * 40.0f), 5.0f, 400.0f);
            return MS::kSuccess;
        }

        // For the PBD simulation, we want the mouse position on this event AND the last.
        pbdSimulator->updateDragValues({ mouseX, mouseY, dragX, dragY, selectRadius });

        // Only update the circle position if we're not resizing it.
        mouseX = dragX;
        mouseY = dragY;
        return MS::kSuccess;
    }

    virtual MStatus doRelease(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        event.getPosition(mouseX, mouseY);

        pbdSimulator->setIsDragging(false);
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
    short screenDragStartX, screenDragStartY;
    short mouseButton;
    float selectRadius = 50.0f;
    PBD* pbdSimulator = nullptr;
};