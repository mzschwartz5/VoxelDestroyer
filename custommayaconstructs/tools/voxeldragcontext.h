#pragma once
#include "voxelcontextbase.h"
#include <maya/MGlobal.h>
#include <maya/MAnimControl.h>
#include <maya/MTimerMessage.h>
#include <maya/MEventMessage.h>
#include <maya/MConditionMessage.h>
#include <maya/M3dView.h>

// Maya API does not expose a playback direction enum, nor a way to get the current playback direction,
// so we define and track it ourselves.
enum PlaybackDirection {
    FORWARD = 1,
    UNSET = 0,
    BACKWARD = -1
};

/**
 * This class implements a custom mouse context tool for dragging voxel simulation objects interactively during animation playback.
 * While dragging, state change events are fired to listeners. (E.g. this is how the PBD drag shader responds to mouse movements.)
 * 
 * In order to work around Maya limitations regarding interactive playback, this tool also hacks together manually-driven playback control. It's not ideal,
 * but it's the best we can do with the current Maya API. See below for more details.
 */
class VoxelDragContext : public VoxelContextBase<VoxelDragContext>
{
public:
    VoxelDragContext() : VoxelContextBase() {
        setTitleString("Voxel Simulation Tool");
    }
    
    ~VoxelDragContext() override {
        MTimerMessage::removeCallback(timerCallbackId);
        MEventMessage::removeCallback(timeChangedCallbackId);
    }
    
private:
    inline static double lastTimeValue = MAnimControl::currentTime().value();
    inline static double playbackStartTime = MAnimControl::currentTime().value();
    inline static int playbackDirection = PlaybackDirection::UNSET;

    MCallbackId timerCallbackId = 0;
    MCallbackId timeChangedCallbackId = 0;
    MCallbackId playbackChangeCallbackId = 0;
    MStatus status;

    void toolOnSetup(MEvent &event) override {
        VoxelContextBase::toolOnSetup(event);

        setImage("VoxelDrag.png", MPxContext::kImage1);

        timeChangedCallbackId = MEventMessage::addEventCallback("timeChanged", VoxelDragContext::onTimeChanged, NULL, &status);
        playbackChangeCallbackId = MConditionMessage::addConditionCallback("playingBack", VoxelDragContext::onPlaybackChange);
    }

    void toolOffCleanup() override {
        VoxelContextBase::toolOffCleanup();
        MEventMessage::removeCallback(timeChangedCallbackId);
        MConditionMessage::removeCallback(playbackChangeCallbackId);
    }

    MStatus doPress(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        lastTimeValue = MAnimControl::currentTime().value();
        timerCallbackId = MTimerMessage::addTimerCallback(VoxelDragContext::timerRate(), VoxelDragContext::onTimer, NULL, &status);
        return VoxelContextBase::doPress(event, drawMgr, context);
    }

    MStatus doRelease(MEvent &event, MHWRender::MUIDrawManager& drawMgr, const MHWRender::MFrameContext& context) override {
        MTimerMessage::removeCallback(timerCallbackId);
        // Sync Maya's playback state with this class's internal playback state.
        if (MAnimControl::isPlaying()) {
            (playbackDirection == PlaybackDirection::FORWARD) ? MAnimControl::playForward() : MAnimControl::playBackward();
        }
        return VoxelContextBase::doRelease(event, drawMgr, context);
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

	    M3dView::active3dView().refresh(false, true);
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
};