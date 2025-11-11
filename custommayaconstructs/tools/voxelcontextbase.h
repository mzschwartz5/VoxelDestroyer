#pragma once
#include <maya/MPxContext.h>
#include <maya/MViewport2Renderer.h>
#include <maya/M3dView.h>
#include <algorithm>
#include "../../event.h"

struct MousePosition
{   
    int x{ 0 };
    int y{ 0 };
};

struct DragState
{
    bool isDragging{ false };
    float selectRadius{ 50.0f };
    MousePosition mousePosition;
};

/**
 * This is a base class for custom tools that need to handle dragging in the viewport and act on
 * a circular area around the mouse cursor. It emits events for drag state changes and mouse position changes.
 * 
 * Note: this class uses CRTP to allow static event members while still being a base class, thus the template parameter.
 */
template <typename Derived>
class VoxelContextBase : public MPxContext
{
public:
    static EventBase::Unsubscribe subscribeToDragStateChange(
        const Event<const DragState&>::Listener& listener) 
    {
        return dragStateChangedEvent.subscribe(listener);
    }

    static EventBase::Unsubscribe subscribeToMousePositionChange(
        const Event<const MousePosition&>::Listener& listener) 
    {
        return mousePositionChangedEvent.subscribe(listener);
    }
    
private:
    inline static Event<const DragState&> dragStateChangedEvent;
    inline static Event<const MousePosition&> mousePositionChangedEvent;

    int viewportWidth;
    bool isDragging = false;
    short mouseX, mouseY;
    short screenDragStartX, screenDragStartY;
    float selectRadius = 50.0f;
    MStatus status;

protected:
    virtual void toolOnSetup(MEvent &event) override {
        MPxContext::toolOnSetup(event);
    }

    virtual void toolOffCleanup() override {
        MPxContext::toolOffCleanup();
    }

    virtual MStatus doPress(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        if (event.mouseButton() == MEvent::kMiddleMouse) {
            M3dView view = M3dView::active3dView();
            viewportWidth = view.portWidth();
        }

        event.getPosition(mouseX, mouseY);
        screenDragStartX = mouseX;
        screenDragStartY = mouseY;
                
        isDragging = true;
        dragStateChangedEvent.notify({ isDragging, selectRadius, { mouseX, mouseY } });
        return MS::kSuccess;
    }

    virtual MStatus doDrag(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        // To grow/shrink the circle radius, but not move the drawn circle while doing so, we need separate variables for the drag position and the draw position
        short dragX, dragY;
        event.getPosition(dragX, dragY);
        short distX = dragX - screenDragStartX;
        if (event.mouseButton() == MEvent::kMiddleMouse) {
            setSelectRadius(std::clamp(getSelectRadius() + ((static_cast<float>(distX) / viewportWidth) * 40.0f), 5.0f, 400.0f));
            return MS::kSuccess;
        }

        mousePositionChangedEvent.notify({ mouseX, mouseY });

        // Only update the circle position if we're not resizing it.
        mouseX = dragX;
        mouseY = dragY;
        return MS::kSuccess;
    }

    virtual MStatus doRelease(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        event.getPosition(mouseX, mouseY);

        isDragging = false;
        dragStateChangedEvent.notify({ isDragging, selectRadius, { mouseX, mouseY } });
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
        isDragging ? drawMgr.setColor(MColor(0.5f, 1.0f, 0.5f)) : drawMgr.setColor(MColor(0.5f, 0.5f, 0.5f));
        isDragging ? drawMgr.setLineStyle(MHWRender::MUIDrawManager::kSolid) : drawMgr.setLineStyle(MHWRender::MUIDrawManager::kShortDashed);
        drawMgr.setLineWidth(2.0f);
    
        // Draw a circle at the mouse position
        const unsigned int segments = 40;
        drawMgr.circle2d(mousePoint2D, selectRadius, segments, false);  // false = not filled
    
        drawMgr.endDrawable();

        return MS::kSuccess;
    }

    virtual void setSelectRadius(float radius) {
        selectRadius = radius;
    }

    virtual float getSelectRadius() const {
        return selectRadius;
    }
};