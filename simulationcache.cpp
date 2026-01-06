#include "simulationcache.h"
#include <maya/MAnimControl.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MEventMessage.h>
#include <maya/MNodeMessage.h>
#include "globalsolver.h"

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
    singleFrameCacheSize += static_cast<int>(bufferData.size());

    return Registration(buffer);
}

void SimulationCache::unregisterBuffer(ComPtr<ID3D11Buffer> buffer) {
    registry.erase(buffer);

    for (auto it = cache.begin(); it != cache.end(); ) {
        it->second.erase(buffer);

        if (it->second.empty()) {
            removeMarkerAtFrame(it->first);
            it = cache.erase(it);
        } else {
            ++it;
        }
    }

    MTimeSliderCustomDrawManager::instance().setDrawPrimitives(customDrawID, drawPrimitives);
}

void SimulationCache::cacheData(const MTime& time) {
    double currentFrame = std::floor(time.as(MTime::uiUnit()));
    int maxCacheSizeMB = MPlug(GlobalSolver::getOrCreateGlobalSolver() , GlobalSolver::aMaxCacheSize).asInt();
    const uint64_t maxBytes = (uint64_t)maxCacheSizeMB * 1024ull * 1024ull;
    bool overLimit = (currentCacheSize + singleFrameCacheSize > (maxBytes - singleFrameCacheSize)); // subtract one frame size to account for start frame
    
    // If we've exceeded the cache budget, reuse the data from an existing cached frame (the one furthest from the current frame)
    double frameToUse = currentFrame;
    if (overLimit) {
        double lowestCachedFrame = cachedFrames.empty() ? DBL_MAX : *cachedFrames.begin();
        double highestCachedFrame = cachedFrames.empty() ? -DBL_MAX : *cachedFrames.rbegin();
        bool direction = std::abs(currentFrame - lowestCachedFrame) > std::abs(currentFrame - highestCachedFrame);
        frameToUse = (direction) ? lowestCachedFrame : highestCachedFrame;

        // Exclude start frame from eviction
        MTime startTime = MAnimControl::minTime();
        double startFrame = std::floor(startTime.as(MTime::uiUnit()));
        if (frameToUse == startFrame) frameToUse = direction ? *std::next(cachedFrames.begin()) : *std::prev(std::prev(cachedFrames.end()));
    }

    for (const auto& buffer : registry) {
        std::vector<uint8_t>& bufferData = cache[frameToUse][buffer];
        DirectX::copyBufferToVector(buffer, bufferData);
    }

    // With the data copied, if we were over the limit we can now evict the old frame
    if (overLimit) {
        cache[currentFrame] = std::move(cache[frameToUse]);
        cache.erase(frameToUse);
        cachedFrames.erase(frameToUse);
        removeMarkerAtFrame(frameToUse);
    } else {
        currentCacheSize += singleFrameCacheSize;
    }

    addMarkerToTimeline(currentFrame);
    MTimeSliderCustomDrawManager::instance().setDrawPrimitives(customDrawID, drawPrimitives);

    cachedFrames.insert(currentFrame);
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

bool SimulationCache::hasCacheData(const MTime& time) {
    double currentFrame = std::floor(time.as(MTime::uiUnit()));
    return cache.find(currentFrame) != cache.end();
}

void SimulationCache::resetCache() {
    // This is a little outside the purview of what a cache should do, but it's very useful:
    // Before resetting the cache, reset the simulation to the start frame so we never lose the initial state.
    MTime startTime = MAnimControl::minTime();
    double startFrame = std::floor(startTime.as(MTime::uiUnit()));
    tryUseCache(startTime); // effectively resets buffer data to start state
    
    cache.clear();
    drawPrimitives.clear();
    currentCacheSize = 0; 
    cachedFrames.clear();

    // And recache the initial data for the start frame
    cacheData(startTime);
    currentCacheSize = 0; // zero again because cacheData already accounts for start frame and we don't want to double count it
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