#pragma once
#include "voxelcontextbase.h"
#include <maya/MEvent.h>
#include <maya/MToolsInfo.h>
#include <maya/MTimerMessage.h>
#include <maya/MColor.h>
#include <maya/M3dView.h>

enum class BrushMode {
    SUBTRACT,
    SET,
    ADD
};

// Richer payload for paint-tool specific drag state change event
struct PaintDragState : DragState {
    BrushMode brushMode{ BrushMode::SET };
    MColor lowColor;
    MColor highColor;
    int faceComponentMask{ 0b111111 }; // All directions enabled by default
    int particleComponentMask{ 0b11111111 }; // All directions enabled by default
    bool infiniteStrength{ false };
};

class VoxelPaintContext : public VoxelContextBase<VoxelPaintContext> {

public:
    VoxelPaintContext() : VoxelContextBase() {
        setTitleString("Voxel Paint Tool");
        setHelpString("Paint various simulation weights onto the voxel grid.");
        setImage("VoxelPaint.png", MPxContext::kImage1);
        setCursor(MCursor::crossHairCursor);
        setSelectStrength(50.0f); // Default brush value
    }
    
    ~VoxelPaintContext() override {}

    void toolOnSetup(MEvent &event) override {
        VoxelContextBase::toolOnSetup(event);

        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI

        // Subscribe to and forward base class event with extended payload
        unsubscribeBaseDragStateEvent = VoxelContextBase::subscribeToDragStateChange([this](const DragState& baseState) {
            PaintDragState paintDragState {
                baseState.isDragging,
                baseState.selectRadius,
                baseState.selectStrength,
                baseState.mousePosition,
                baseState.cameraBased,
                brushMode,
                lowColor,
                highColor,
                faceComponentMask,
                particleComponentMask,
                infiniteStrength
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

    void setBrushMode(BrushMode mode) {
        brushMode = mode;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    BrushMode getBrushMode() const {
        return brushMode;
    }

    void setLowColor(const MColor& color) {
        lowColor = color;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
        // Drag state didn't change, but this tells the paint render operation to change colors
        paintDragStateChangedEvent.notify(PaintDragState{
            false,
            getSelectRadius(),
            getSelectStrength(),
            getMousePosition(),
            isCameraBased(),
            brushMode,
            lowColor,
            highColor,
            faceComponentMask,
            infiniteStrength
        });
        M3dView::active3dView().refresh(false, true);
    }

    void setHighColor(const MColor& color) {
        highColor = color;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
        // Drag state didn't change, but this tells the paint render operation to change colors
        paintDragStateChangedEvent.notify(PaintDragState{
            false,
            getSelectRadius(),
            getSelectStrength(),
            getMousePosition(),
            isCameraBased(),
            brushMode,
            lowColor,
            highColor,
            faceComponentMask,
            infiniteStrength
        });
        M3dView::active3dView().refresh(false, true);
    }

    MColor getLowColor() const {
        return lowColor;
    }

    MColor getHighColor() const {
        return highColor;
    }

    void setFaceComponentMask(uint8_t mask) {
        faceComponentMask = mask;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    int getFaceComponentMask() const {
        return faceComponentMask;
    }

    void setParticleComponentMask(uint8_t mask) {
        particleComponentMask = mask;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    int getParticleComponentMask() const {
        return particleComponentMask;
    }

    void setInfiniteStrength(bool enabled) {
        infiniteStrength = enabled;
        MToolsInfo::setDirtyFlag(*this); // Tells Maya to refresh the tool settings UI
    }

    bool isInfiniteStrength() const {
        return infiniteStrength;
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
        M3dView::active3dView().refresh(false, true);
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
    MCallbackId timerCallbackId;
    MColor lowColor = MColor(1.0f, 0.0f, 0.0f, 0.0f);
    MColor highColor = MColor(1.0f, 0.0f, 0.0f, 1.0f);
    int faceComponentMask = 0b111111; // All directions enabled by default
    int particleComponentMask = 0b11111111; // All directions enabled by default
    bool infiniteStrength{ false };
};