#pragma once

#include <maya/MPxNode.h>
#include <unordered_map>
#include <maya/MString.h>
#include "directx/directx.h"
#include <maya/MTimeSliderCustomDrawManager.h>
#include "utils.h"

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

    void addData(std::unordered_map<MString, ComPtr<ID3D11Buffer>, Utils::MStringHash, Utils::MStringEq>& buffersToCache);
    void removeData(double frameKey, std::unordered_map<MString, ComPtr<ID3D11Buffer>, Utils::MStringHash, Utils::MStringEq>& buffersToCache);
    ComPtr<ID3D11Buffer> getData(const MString& bufferName);

private:
    static MObject simulationCacheObject;
    static const MString timeSliderDrawContextName;
    std::unordered_map<double, std::unordered_map<MString, CachedBufferData, Utils::MStringHash, Utils::MStringEq>> cache;
    MTimeSliderDrawPrimitives drawPrimitives;
    int customDrawID = -1;

    SimulationCache() = default;
    ~SimulationCache();
    void postConstructor() override;
    void addMarkerToTimeline(double frameKey);
    bool hasMarkerAtFrame(double frameKey);
    void removeMarkerAtFrame(double frameKey);
};