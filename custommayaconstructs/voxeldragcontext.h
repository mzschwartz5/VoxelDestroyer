#pragma once
#include <maya/MGlobal.h>
#include <maya/MPxContext.h>
#include <maya/MViewport2Renderer.h>
#include <maya/M3dView.h>
#include "pbd.h"
#include <algorithm>
#include <maya/MAnimControl.h>
#include <maya/MTimerMessage.h>
#include <maya/MEventMessage.h>
#include <maya/MConditionMessage.h>

// Maya API does not expose a playback direction enum, nor a way to get the current playback direction,
// so we define and track it ourselves.
enum PlaybackDirection {
    FORWARD = 1,
    UNSET = 0,
    BACKWARD = -1
};

/**
 * This class implements a custom mouse context tool for dragging voxel simulation objects interactively during animation playback.
 * While dragging, mouse position changes are sent to the PBD simulator, which dispatches a compute shader to perform a screen-space drag operation.
 * 
 * In order to work around Maya limitations regarding interactive playback, this tool also hacks together manually-driven playback control. It's not ideal,
 * but it's the best we can do with the current Maya API. See below for more details.
 */
class VoxelDragContext : public MPxContext
{
public:
    VoxelDragContext(PBD* pbdSimulator) : MPxContext(), pbdSimulator(pbdSimulator) {
        setTitleString("Voxel Simulation Tool");
    }

    ~VoxelDragContext() override {
        MTimerMessage::removeCallback(timerCallbackId);
        MEventMessage::removeCallback(timeChangedCallbackId);
    }
    
    virtual void toolOnSetup(MEvent &event) override {
        MPxContext::toolOnSetup(event);

        if (!pbdSimulator) {
            MGlobal::displayError("PBD simulator not initialized.");
            return;
        }

        setImage("TypeSeparateMaterials_200.png", MPxContext::kImage1);

        M3dView view = M3dView::active3dView();
        viewportWidth = view.portWidth();
        timeChangedCallbackId = MEventMessage::addEventCallback("timeChanged", VoxelDragContext::onTimeChanged, NULL, &status);
        playbackChangeCallbackId = MConditionMessage::addConditionCallback("playingBack", VoxelDragContext::onPlaybackChange);
    }

    virtual void toolOffCleanup() override {
        MPxContext::toolOffCleanup();
        MEventMessage::removeCallback(timeChangedCallbackId);
        MConditionMessage::removeCallback(playbackChangeCallbackId);
    }

    virtual MStatus doPress(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        lastTimeValue = MAnimControl::currentTime().value();
        timerCallbackId = MTimerMessage::addTimerCallback(VoxelDragContext::timerRate(), VoxelDragContext::onTimer, NULL, &status);

        event.getPosition(mouseX, mouseY);
        screenDragStartX = mouseX;
        screenDragStartY = mouseY;
                
        isDragging = true;
        pbdSimulator->setIsDragging(isDragging);
        pbdSimulator->updateDragValues({ mouseX, mouseY, mouseX, mouseY, selectRadius });
        return MS::kSuccess;
    }

    virtual MStatus doDrag(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        static short dragX, dragY;

        // To grow/shrink the circle radius, but not move the drawn circle while doing so, we need separate variables for the drag position and the draw position
        event.getPosition(dragX, dragY);
        short distX = dragX - screenDragStartX;
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
        MTimerMessage::removeCallback(timerCallbackId);
        // Sync Maya's playback state with this class's internal playback state.
        if (MAnimControl::isPlaying()) {
            (playbackDirection == PlaybackDirection::FORWARD) ? MAnimControl::playForward() : MAnimControl::playBackward();
        }
        
        event.getPosition(mouseX, mouseY);

        isDragging = false;
        pbdSimulator->setIsDragging(isDragging);
        pbdSimulator->updateDragValues({ mouseX, mouseY,  mouseX, mouseY, selectRadius });
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

    /**
     * Because Maya pauses time when the user is clicking/dragging, in order to facilitate interactive dragging,
     * we're going to use a bit of a hack: drive the anim control time via a timer callback.
     * 
     * Note: for some reason, the `lastTime` parameter passed in by Maya does NOT work, or their docs are incorrect. It's almost always 0.
     * Track lastTime ourselves using MAnimControl::currentTime(). 
     */
    static void onTimer(float elapsedTime, float lastTime, void *clientData) {
        if (!MAnimControl::isPlaying()) return;

        MTime elapsed(elapsedTime, MTime::kSeconds);
        double elapsedInUiUnits = elapsed.as(MTime::uiUnit());
        double currentTime = lastTimeValue + (playbackDirection * elapsedInUiUnits);
        double timeRange = MAnimControl::maxTime().value() - MAnimControl::minTime().value();
        
        if (currentTime >= MAnimControl::maxTime().value() || currentTime <= MAnimControl::minTime().value()) {
            switch (MAnimControl::playbackMode()) {
                case MAnimControl::kPlaybackOnce:
                    MAnimControl::stop();
                    break;
                case MAnimControl::kPlaybackLoop:
                    currentTime -= MAnimControl::minTime().value();
                    currentTime = std::fmod(std::fmod(currentTime, timeRange) + timeRange, timeRange);
                    currentTime += MAnimControl::minTime().value();
                    break;
                case MAnimControl::kPlaybackOscillate:
                    playbackDirection *= -1;
                    break;
            }
        }

        MAnimControl::setCurrentTime(MTime(std::round(currentTime), MTime::uiUnit()));
        lastTimeValue = MTime(currentTime, MTime::uiUnit()).value();
    }

    // Match the timer rate to the playback rate
    static float timerRate() {
		double timePerFrame = MTime(1.0, MTime::uiUnit()).as(MTime::kSeconds);
		double playbackSpeed = MAnimControl::playbackSpeed();
		if (playbackSpeed == 0.0) playbackSpeed = 1.0;
		timePerFrame /= playbackSpeed;
        
        return static_cast<float>(timePerFrame);
    }

    static void onTimeChanged(void* clientData) {    
        double currentTime = MAnimControl::currentTime().value();
        if (playbackDirection == PlaybackDirection::UNSET) {
            playbackDirection = (currentTime > playbackStartTime) ? PlaybackDirection::FORWARD : PlaybackDirection::BACKWARD;
        }

        // In the case where the user manually scrubs the timeline, still need to update the lastTimeValue.  
        if (!MAnimControl::isScrubbing()) return;
        lastTimeValue = currentTime;
    }

    static void onPlaybackChange(bool state, void* clientData) {
        playbackStartTime = MAnimControl::currentTime().value();
        playbackDirection = PlaybackDirection::UNSET;
    }

private:
    inline static double lastTimeValue = MAnimControl::currentTime().value();
    inline static double playbackStartTime = MAnimControl::currentTime().value();
    inline static int playbackDirection = PlaybackDirection::UNSET;

    int viewportWidth;
    bool isDragging = false;
    short mouseX, mouseY;
    short screenDragStartX, screenDragStartY;
    short mouseButton;
    float selectRadius = 50.0f;
    PBD* pbdSimulator = nullptr;
    MCallbackId timerCallbackId = 0;
    MCallbackId timeChangedCallbackId = 0;
    MCallbackId playbackChangeCallbackId = 0;
    MStatus status;
};