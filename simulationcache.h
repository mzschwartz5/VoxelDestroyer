#pragma once

#include <maya/MPxNode.h>
#include <maya/MString.h>
#include <maya/MTimeSliderCustomDrawManager.h>
#include <maya/MCallbackIdArray.h>
#include <unordered_map>
#include "directx/directx.h"
#include "utils.h"

struct CachedBufferData {
    std::vector<uint8_t> data;
    D3D11_BUFFER_DESC desc;
};

class StopPrimitiveEditCallback : public MStopPrimitiveEditingFct {
public:
    void operator()() override {
        MGlobal::displayInfo("Stopped editing primitive.");
    }
};

class SimulationCache : public MPxNode {

public:
    static const MTypeId id;
    static const MString simulationCacheNodeName;

    static void* creator() { return new SimulationCache(); }
    static MStatus initialize();
    static SimulationCache* const instance();

    void addData(const std::unordered_map<MString, ComPtr<ID3D11Buffer>, Utils::MStringHash, Utils::MStringEq>& buffersToCache);
    void removeData(double frameKey, const std::vector<MString>& bufferNamesToRemove);
    ComPtr<ID3D11Buffer> getData(const MString& bufferName);

private:
    static MObject simulationCacheObject;
    static const MString timeSliderDrawContextName;
    std::unordered_map<double, std::unordered_map<MString, CachedBufferData, Utils::MStringHash, Utils::MStringEq>> cache;
    MTimeSliderDrawPrimitives drawPrimitives;
    int customDrawID = -1;
    MCallbackIdArray callbackIds;
    bool dataAdded = false;

    SimulationCache() = default;
    ~SimulationCache();
    static void onTimeChanged(void* clientData);
    void postConstructor() override;
    void addMarkerToTimeline(double frameKey);
    bool hasMarkerAtFrame(double frameKey);
    void removeMarkerAtFrame(double frameKey);
};