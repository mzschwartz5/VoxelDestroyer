#pragma once
#include "voxelcontextbase.h"
#include <maya/MEvent.h>
#include <maya/MToolsInfo.h>
#include <maya/MTimerMessage.h>
#include <maya/MColor.h>

enum class BrushMode {
    SUBTRACT,
    SET,
    ADD
};

// Richer payload for paint-tool specific drag state change event
struct PaintDragState : DragState {
    BrushMode brushMode{ BrushMode::SET };
    float brushValue{ 0.0f };
    bool cameraBased{ true };
    MColor lowColor;
    MColor highColor;
    int componentMask{ 0b111111 }; // All directions enabled by default
};

class VoxelPaintContext : public VoxelContextBase<VoxelPaintContext> {

public:
    VoxelPaintContext() : VoxelContextBase() {
        setTitleString("Voxel Paint Tool");
    }
    
    ~VoxelPaintContext() override {}

    void toolOnSetup(MEvent &event) override {
        VoxelContextBase::toolOnSetup(event);

        setImage("VoxelPaint.png", MPxContext::kImage1);
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI

        // Subscribe to and forward base class event with extended payload
        unsubscribeBaseDragStateEvent = VoxelContextBase::subscribeToDragStateChange([this](const DragState& baseState) {
            PaintDragState paintDragState {
                baseState.isDragging,
                baseState.selectRadius,
                baseState.mousePosition,
                brushMode,
                brushValue,
                cameraBased,
                lowColor,
                highColor,
                componentMask
            };
            paintDragStateChangedEvent.notify(paintDragState);
        });
    }

    void toolOffCleanup() override {
        VoxelContextBase::toolOffCleanup();
        unsubscribeBaseDragStateEvent();
    }

    void getClassName(MString& name) const override {
        name.set("VoxelPaintContext");
    }

    void setSelectRadius(float radius) override {
        VoxelContextBase::setSelectRadius(radius);
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    float getSelectRadius() const override {
        return VoxelContextBase::getSelectRadius();
    }

    void setBrushMode(BrushMode mode) {
        brushMode = mode;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    BrushMode getBrushMode() const {
        return brushMode;
    }

    float getBrushValue() const {
        return brushValue;
    }

    void setBrushValue(float value) {
        brushValue = value;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    void setCameraBased(bool enabled) {
        cameraBased = enabled;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    bool isCameraBased() const {
        return cameraBased;
    }

    void setLowColor(const MColor& color) {
        lowColor = color;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    void setHighColor(const MColor& color) {
        highColor = color;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    MColor getLowColor() const {
        return lowColor;
    }

    MColor getHighColor() const {
        return highColor;
    }

    void setComponentMask(uint8_t mask) {
        componentMask = mask;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    int getComponentMask() const {
        return componentMask;
    }

    // Maya doesn't refresh while the mouse is held down, so force it to do so.
    // However, we don't want to refresh on EVERY mouse event, just at 60FPS. Use a timer for this.
    MStatus doPress(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        timerCallbackId = MTimerMessage::addTimerCallback(1.0f / 60.0f, &VoxelPaintContext::timerCallbackFunc);
        return VoxelContextBase::doPress(event, drawMgr, context);
    }

    MStatus doRelease(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        MTimerMessage::removeCallback(timerCallbackId);
        return VoxelContextBase::doRelease(event, drawMgr, context);
    }

    static void timerCallbackFunc(float, float, void*) {
        MGlobal::executeCommand("refresh");
    }

    static EventBase::Unsubscribe subscribeToPaintDragStateChange(
        const Event<const PaintDragState&>::Listener& listener)
    {
        return paintDragStateChangedEvent.subscribe(listener);
    }

private:
    inline static Event<const PaintDragState&> paintDragStateChangedEvent;
    EventBase::Unsubscribe unsubscribeBaseDragStateEvent;
    BrushMode brushMode = BrushMode::SET;
    float brushValue = 0.5f;
    bool cameraBased = true;
    MCallbackId timerCallbackId;
    MColor lowColor = MColor(1.0f, 0.0f, 0.0f, 0.0f);
    MColor highColor = MColor(1.0f, 0.0f, 0.0f, 1.0f);
    int componentMask = 0b111111; // All directions enabled by default
};