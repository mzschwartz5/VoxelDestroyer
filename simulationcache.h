#pragma once

#include <maya/MPxNode.h>
#include <maya/MString.h>
#include <maya/MTimeSliderCustomDrawManager.h>
#include <maya/MCallbackIdArray.h>
#include <unordered_set>
#include <unordered_map>
#include "directx/directx.h"
#include <cstdint>

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

    void unregisterBuffer(ComPtr<ID3D11Buffer>* id);

    // Small object for managing registration lifetime (automatically unregisters on destruction)
    class Registration {
    public:
        Registration() = default;
        Registration(ComPtr<ID3D11Buffer>* id) : id_(id) {}
        // move-only (so two handles don't try to unregister the same id)
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        Registration(Registration&& other) noexcept { *this = std::move(other); }

        Registration& operator=(Registration&& other) noexcept {
            if (this != &other) {
                reset();
                id_ = other.id_;
                other.id_ = 0;
            }
            return *this;
        }

        ~Registration() { reset(); }

        void reset() {
            SimulationCache::instance()->unregisterBuffer(id_);
            id_ = 0;
        }

    private:
        ComPtr<ID3D11Buffer>* id_ = 0;
    };

    Registration registerBuffer(ComPtr<ID3D11Buffer>* buffer);

private:
    static MObject simulationCacheObject;
    static const MString timeSliderDrawContextName;
    // Each pointer points to where a ComPtr<ID3D11Buffer> is stored (typically as a member of another object)
    std::unordered_set<ComPtr<ID3D11Buffer>*> registry;
    // Frame number to map of buffer pointer to cached data (as vector of bytes)
    std::unordered_map<double, std::unordered_map<ComPtr<ID3D11Buffer>*, std::vector<uint8_t>>> cache;
    MTimeSliderDrawPrimitives drawPrimitives;
    int customDrawID = -1;
    MCallbackIdArray callbackIds;

    SimulationCache() = default;
    ~SimulationCache();
    static void onTimeChanged(void* clientData);
    void postConstructor() override;
    void addMarkerToTimeline(double frameKey);
    bool hasMarkerAtFrame(double frameKey);
    void removeMarkerAtFrame(double frameKey);
    void cacheData();
};