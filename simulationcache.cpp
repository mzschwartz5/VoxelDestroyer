#include "simulationcache.h"
#include "utils.h"
#include <maya/MAnimControl.h>

const MTypeId SimulationCache::id(0x0013A7C1);
const MString SimulationCache::simulationCacheNodeName("SimulationCache");
MObject SimulationCache::simulationCacheObject = MObject::kNullObj;

void SimulationCache::addData(std::unordered_map<std::string, ComPtr<ID3D11Buffer>>& buffersToCache) {
    double currentFrame = MAnimControl::currentTime().value();

    for (auto& bufferCachePair : buffersToCache) {
        const std::string& bufferName = bufferCachePair.first;
        ComPtr<ID3D11Buffer>& buffer = bufferCachePair.second;
        D3D11_BUFFER_DESC desc;
        buffer->GetDesc(&desc);

        cache[currentFrame][bufferName].desc = desc;
        std::vector<uint8_t>& bufferData = cache[currentFrame][bufferName].data;
        DirectX::copyBufferToVector(buffer, bufferData);
    }
}

void SimulationCache::removeData(double frameKey, std::unordered_map<std::string, ComPtr<ID3D11Buffer>>& buffersToCache) {
    auto frameIt = cache.find(frameKey);
    if (frameIt == cache.end()) return;

    for (const auto& bufferCachePair : frameIt->second) {
        const std::string& bufferName = bufferCachePair.first;
        buffersToCache.erase(bufferName);
    }

    if (frameIt->second.empty()) {
        cache.erase(frameIt);
    }
}

ComPtr<ID3D11Buffer> SimulationCache::getData(const std::string& bufferName) {
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

const MObject& SimulationCache::node() {
    if (!simulationCacheObject.isNull()) {
        return simulationCacheObject;
    }

    simulationCacheObject = Utils::createDGNode(simulationCacheNodeName);
    return simulationCacheObject;
}