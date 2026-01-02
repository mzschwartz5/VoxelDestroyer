#pragma once

#include <maya/MPxNode.h>
#include <unordered_map>
#include <string>
#include "directx/directx.h"

struct CachedBufferData {
    std::vector<uint8_t> data;
    D3D11_BUFFER_DESC desc;
};

class SimulationCache : public MPxNode {

public:
    static const MTypeId id;
    static const MString simulationCacheNodeName;

    static void* creator() { return new SimulationCache(); }
    static MStatus initialize();
    static const MObject& node();

    void addData(std::unordered_map<std::string, ComPtr<ID3D11Buffer>>& buffersToCache);
    void removeData(double frameKey, std::unordered_map<std::string, ComPtr<ID3D11Buffer>>& buffersToCache);
    
    ComPtr<ID3D11Buffer> getData(const std::string& bufferName);

private:
    SimulationCache() = default;
    ~SimulationCache() = default;
    static MObject simulationCacheObject;

    std::unordered_map<double, std::unordered_map<std::string, CachedBufferData>> cache;
};