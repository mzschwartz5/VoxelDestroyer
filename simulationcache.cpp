#include "simulationcache.h"
#include <maya/MAnimControl.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MEventMessage.h>
#include <maya/MNodeMessage.h>
#include "utils.h"

const MTypeId SimulationCache::id(0x0013A7C1);
const MString SimulationCache::simulationCacheNodeName("SimulationCache");
const MString SimulationCache::timeSliderDrawContextName("SimulationCacheTimeSliderContext");
MObject SimulationCache::simulationCacheObject = MObject::kNullObj;

SimulationCache::Registration SimulationCache::registerBuffer(ComPtr<ID3D11Buffer>* pBuffer) {
    registry.insert(pBuffer);
    return Registration(pBuffer);
}

void SimulationCache::unregisterBuffer(ComPtr<ID3D11Buffer>* pBuffer) {
    registry.erase(pBuffer);
    for (auto& frameCachePair : cache) {
        frameCachePair.second.erase(pBuffer);
    }
}

void SimulationCache::cacheData() {
    double currentFrame = MAnimControl::currentTime().as(MTime::uiUnit());
    for (const auto& bufferPtr : registry) {
        const ComPtr<ID3D11Buffer>& buffer = *bufferPtr;
        std::vector<uint8_t>& bufferData = cache[currentFrame][bufferPtr];
        DirectX::copyBufferToVector(buffer, bufferData);
    }
}

MStatus SimulationCache::initialize() {
    return MStatus::kSuccess;
}

SimulationCache* const SimulationCache::instance() {
    if (!simulationCacheObject.isNull()) {
        return static_cast<SimulationCache*>(MFnDependencyNode(simulationCacheObject).userNode());
    }

    simulationCacheObject = Utils::createDGNode(simulationCacheNodeName);
    return static_cast<SimulationCache*>(MFnDependencyNode(simulationCacheObject).userNode());
}

void SimulationCache::postConstructor() {
    MPxNode::postConstructor();
    MTimeSliderCustomDrawManager& drawManager = MTimeSliderCustomDrawManager::instance();
    customDrawID = drawManager.registerCustomDrawOutside(MTimeSliderCustomDrawManager::kAbove, timeSliderDrawContextName, MString("Cubit Simulation Cache"), 0);

    MSharedPtr<MStopPrimitiveEditingFct> stopEditCallback = MSharedPtr<MStopPrimitiveEditingFct>(new StopPrimitiveEditCallback());
    drawManager.setStopPrimitiveEditFunction(customDrawID, stopEditCallback);

    MCallbackId callbackId = MEventMessage::addEventCallback("timeChanged", SimulationCache::onTimeChanged, this, nullptr);
    callbackIds.append(callbackId);

    callbackId = MNodeMessage::addNodePreRemovalCallback(thisMObject(), [](MObject& node, void* clientData) {
        SimulationCache* simCache = static_cast<SimulationCache*>(clientData);
        MMessage::removeCallbacks(simCache->callbackIds);
    }, this);
    callbackIds.append(callbackId);

    setExistWithoutOutConnections(true);
}

SimulationCache::~SimulationCache() {
    MTimeSliderCustomDrawManager::instance().deregisterCustomDraw(customDrawID); 
    simulationCacheObject = MObject::kNullObj;
    MMessage::removeCallbacks(callbackIds);
}

void SimulationCache::addMarkerToTimeline(double frameKey) {
    if (hasMarkerAtFrame(frameKey)) return;
    MTime time(frameKey, MTime::uiUnit());

    MTimeSliderDrawPrimitive marker(
        MTimeSliderDrawPrimitive::kFilledRect,
        time,
        time + MTime(1.0, MTime::uiUnit()),
        MColor(1.0f, 0.0f, 0.0f),
        -1,
        0
    );

    drawPrimitives.append(marker);
}

bool SimulationCache::hasMarkerAtFrame(double frameKey) {
    for (size_t i = 0; i < drawPrimitives.length(); ++i) {
        MTimeSliderDrawPrimitive prim = drawPrimitives[i];
        if (prim.startTime().as(MTime::uiUnit()) == frameKey) {
            return true;
        }
    }
    return false;
}

void SimulationCache::removeMarkerAtFrame(double frameKey) {
    MTimeSliderDrawPrimitives newPrims;
    for (size_t i = 0; i < drawPrimitives.length(); ++i) {
        MTimeSliderDrawPrimitive prim = drawPrimitives[i];
        if (prim.startTime().as(MTime::uiUnit()) != frameKey) {
            newPrims.append(prim);
        }
    }
    drawPrimitives = std::move(newPrims);
}

void SimulationCache::onTimeChanged(void* clientData) {
    SimulationCache* simCache = static_cast<SimulationCache*>(clientData);
    if (!simCache) return;
    
    double currentFrame = MAnimControl::currentTime().as(MTime::uiUnit());

    // simCache->cacheData(); // TODO: this can't be called from non-main thread. Will change when SimCache is moved to be called from GlobalSolver in compute.
    simCache->addMarkerToTimeline(currentFrame);
    
    MTimeSliderCustomDrawManager::instance().setDrawPrimitives(simCache->customDrawID, simCache->drawPrimitives);
    MTimeSliderCustomDrawManager::instance().requestTimeSliderRedraw();
}