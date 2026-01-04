#include "simulationcache.h"
#include <maya/MAnimControl.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MEventMessage.h>
#include <maya/MNodeMessage.h>

const MString SimulationCache::timeSliderDrawContextName("SimulationCacheTimeSliderContext");
SimulationCache* SimulationCache::simulationCacheInstance = nullptr;

SimulationCache::SimulationCache() {
    MTimeSliderCustomDrawManager& drawManager = MTimeSliderCustomDrawManager::instance();
    customDrawID = drawManager.registerCustomDrawOutside(MTimeSliderCustomDrawManager::kAbove, timeSliderDrawContextName, MString("Cubit Simulation Cache"), 0);

    MSharedPtr<MStopPrimitiveEditingFct> stopEditCallback = MSharedPtr<MStopPrimitiveEditingFct>(new StopPrimitiveEditCallback());
    drawManager.setStopPrimitiveEditFunction(customDrawID, stopEditCallback);
}

SimulationCache::~SimulationCache() {
    drawPrimitives.clear();
    MTimeSliderCustomDrawManager::instance().deregisterCustomDraw(customDrawID); 
}

void SimulationCache::tearDown() {
    if (simulationCacheInstance) {
        delete simulationCacheInstance;
        simulationCacheInstance = nullptr;
    }
}

SimulationCache* const SimulationCache::instance() {
    if (simulationCacheInstance) {
        return simulationCacheInstance;
    }

    simulationCacheInstance = new SimulationCache();
    return simulationCacheInstance;
}

SimulationCache::Registration SimulationCache::registerBuffer(ComPtr<ID3D11Buffer> buffer) {
    registry.insert(buffer);
    return Registration(buffer);
}

void SimulationCache::unregisterBuffer(ComPtr<ID3D11Buffer> buffer) {
    registry.erase(buffer);
    for (auto& frameCachePair : cache) {
        frameCachePair.second.erase(buffer);
    }
}

void SimulationCache::cacheData(const MTime& time) {
    double currentFrame = time.as(MTime::uiUnit());
    for (const auto& buffer : registry) {
        std::vector<uint8_t>& bufferData = cache[currentFrame][buffer];
        DirectX::copyBufferToVector(buffer, bufferData);
    }

    addMarkerToTimeline(currentFrame);
    MTimeSliderCustomDrawManager::instance().setDrawPrimitives(customDrawID, drawPrimitives);
    MTimeSliderCustomDrawManager::instance().requestTimeSliderRedraw();
}

bool SimulationCache::tryUseCache(const MTime& time) {
    double currentFrame = time.as(MTime::uiUnit());
    auto frameIt = cache.find(currentFrame);
    if (frameIt == cache.end()) {
        return false;
    }

    ID3D11DeviceContext* dxContext = DirectX::getContext();
    for (const auto& bufferDataPair : frameIt->second) {
        const ComPtr<ID3D11Buffer>& buffer = bufferDataPair.first;
        const std::vector<uint8_t>& bufferData = bufferDataPair.second;
        dxContext->UpdateSubresource(buffer.Get(), 0, nullptr, bufferData.data(), 0, 0);
    }

    return true;
}

void SimulationCache::addMarkerToTimeline(double frameKey) {
    if (hasMarkerAtFrame(frameKey)) return;
    MTime time(frameKey, MTime::uiUnit());

    MTimeSliderDrawPrimitive marker(
        MTimeSliderDrawPrimitive::kFilledRect,
        time,
        time + MTime(1.0, MTime::uiUnit()),
        MColor(0.0f, 1.0f, 0.0f),
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