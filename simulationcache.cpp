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

    // Add its initial data to the cache start frame (special frame that persists even when clearing the cache)
    MTime startTime = MAnimControl::minTime();
    double startFrame = std::floor(startTime.as(MTime::uiUnit()));
    std::vector<uint8_t>& bufferData = cache[startFrame][buffer];
    DirectX::copyBufferToVector(buffer, bufferData);

    return Registration(buffer);
}

void SimulationCache::unregisterBuffer(ComPtr<ID3D11Buffer> buffer) {
    registry.erase(buffer);
    for (auto& frameCachePair : cache) {
        frameCachePair.second.erase(buffer);
        if (frameCachePair.second.empty()) {
            removeMarkerAtFrame(frameCachePair.first);
        }
    }

    MTimeSliderCustomDrawManager::instance().setDrawPrimitives(customDrawID, drawPrimitives);
}

void SimulationCache::cacheData(const MTime& time) {
    double currentFrame = std::floor(time.as(MTime::uiUnit()));
    for (const auto& buffer : registry) {
        std::vector<uint8_t>& bufferData = cache[currentFrame][buffer];
        DirectX::copyBufferToVector(buffer, bufferData);
    }

    addMarkerToTimeline(currentFrame);
    MTimeSliderCustomDrawManager::instance().setDrawPrimitives(customDrawID, drawPrimitives);
}

bool SimulationCache::tryUseCache(const MTime& time) {
    double currentFrame = std::floor(time.as(MTime::uiUnit()));
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

void SimulationCache::resetCache() {
    // This is a little outside the purview of what a cache should do, but it's very useful:
    // Before resetting the cache, reset the simulation to the start frame so we never lose the initial state.
    MTime startTime = MAnimControl::minTime();
    double startFrame = std::floor(startTime.as(MTime::uiUnit()));
    tryUseCache(startTime); // effectively resets buffer data to start state

    cache.clear();
    drawPrimitives.clear();

    // And recache the initial data for the start frame
    cacheData(startTime);
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