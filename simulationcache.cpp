#include "simulationcache.h"
#include <maya/MAnimControl.h>
#include <maya/MFnDependencyNode.h>

const MTypeId SimulationCache::id(0x0013A7C1);
const MString SimulationCache::simulationCacheNodeName("SimulationCache");
const MString SimulationCache::timeSliderDrawContextName("SimulationCacheTimeSliderContext");
MObject SimulationCache::simulationCacheObject = MObject::kNullObj;

void SimulationCache::addData(const std::unordered_map<MString, ComPtr<ID3D11Buffer>, Utils::MStringHash, Utils::MStringEq>& buffersToCache) {
    double currentFrame = MAnimControl::currentTime().value();
    addMarkerToTimeline(currentFrame);

    for (const auto& bufferCachePair : buffersToCache) {
        const MString& bufferName = bufferCachePair.first;
        const ComPtr<ID3D11Buffer>& buffer = bufferCachePair.second;
        D3D11_BUFFER_DESC desc;
        buffer->GetDesc(&desc);

        cache[currentFrame][bufferName].desc = desc;
        std::vector<uint8_t>& bufferData = cache[currentFrame][bufferName].data;
        DirectX::copyBufferToVector(buffer, bufferData);
    }
}

void SimulationCache::removeData(double frameKey, const std::vector<MString>& bufferNamesToRemove) {
    auto frameIt = cache.find(frameKey);
    if (frameIt == cache.end()) return;

    for (const MString& bufferName : bufferNamesToRemove) {
        frameIt->second.erase(bufferName);
    }

    if (frameIt->second.empty()) {
        cache.erase(frameIt);
        removeMarkerAtFrame(frameKey);
    }
}

ComPtr<ID3D11Buffer> SimulationCache::getData(const MString& bufferName) {
    double currentFrame = MAnimControl::currentTime().value();

    auto frameIt = cache.find(currentFrame);
    if (frameIt == cache.end()) {
        return nullptr;
    }

    auto bufferIt = frameIt->second.find(bufferName);
    if (bufferIt == frameIt->second.end()) {
        return nullptr;
    }

    const CachedBufferData& data = bufferIt->second;
    ComPtr<ID3D11Buffer> buffer = DirectX::createBufferFromDescriptor(data.desc, &data.data);

    // Creating the buffer tells Maya we're using memory, but this is temporary, so just tell Maya we're done with it.
    DirectX::notifyMayaOfMemoryUsage(buffer, false); 
    return buffer;
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
    customDrawID = MTimeSliderCustomDrawManager::instance().registerCustomDrawOn(timeSliderDrawContextName, 0);
    setExistWithoutOutConnections(true);
}

SimulationCache::~SimulationCache() {
    MTimeSliderCustomDrawManager::instance().deregisterCustomDraw(customDrawID); 
    simulationCacheObject = MObject::kNullObj;
}

void SimulationCache::addMarkerToTimeline(double frameKey) {
    if (hasMarkerAtFrame(frameKey)) return;

    MTimeSliderDrawPrimitive marker(
        MTimeSliderDrawPrimitive::kVerticalLine,
        MTime(frameKey),
        MTime(frameKey),
        MColor(1.0f, 0.0f, 0.0f)
    );

    drawPrimitives.append(marker);
    MTimeSliderCustomDrawManager::instance().setDrawPrimitives(customDrawID, drawPrimitives);
}

bool SimulationCache::hasMarkerAtFrame(double frameKey) {
    for (size_t i = 0; i < drawPrimitives.length(); ++i) {
        MTimeSliderDrawPrimitive prim = drawPrimitives[i];
        if (prim.startTime().value() == frameKey) {
            return true;
        }
    }
    return false;
}

void SimulationCache::removeMarkerAtFrame(double frameKey) {
    MTimeSliderDrawPrimitives newPrims;
    for (size_t i = 0; i < drawPrimitives.length(); ++i) {
        MTimeSliderDrawPrimitive prim = drawPrimitives[i];
        if (prim.startTime().value() != frameKey) {
            newPrims.append(prim);
        }
    }
    drawPrimitives = std::move(newPrims);
    MTimeSliderCustomDrawManager::instance().setDrawPrimitives(customDrawID, drawPrimitives);
}