#pragma once

#include <maya/MString.h>
#include <maya/MTimeSliderCustomDrawManager.h>
#include <unordered_set>
#include <unordered_map>
#include "directx/directx.h"
#include <cstdint>

class GlobalSolver; // forward declaration to make friend

class StopPrimitiveEditCallback : public MStopPrimitiveEditingFct {
public:
    void operator()() override {
        MGlobal::displayInfo("Stopped editing primitive.");
    }
};

class SimulationCache {

public:
    static SimulationCache* const instance();

    void unregisterBuffer(ComPtr<ID3D11Buffer> buffer);

    // Small object for managing registration lifetime (automatically unregisters on destruction)
    class Registration {
    public:
        Registration() = default;
        Registration(ComPtr<ID3D11Buffer> buffer) : buffer_(buffer) {}
        // move-only (so two handles don't try to unregister the same buffer)
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        Registration(Registration&& other) noexcept { 
            buffer_ = other.buffer_;
            other.buffer_.Reset();
        }

        Registration& operator=(Registration&& other) noexcept {
            if (this != &other) {
                reset();
                buffer_ = other.buffer_;
                other.buffer_.Reset();

            }
            return *this;
        }

        ~Registration() { reset(); }

        void reset() {
            SimulationCache::instance()->unregisterBuffer(buffer_);
            buffer_.Reset();
        }

    private:
        ComPtr<ID3D11Buffer> buffer_;
    };

    Registration registerBuffer(ComPtr<ID3D11Buffer> buffer);

private:
    friend class GlobalSolver;
    static SimulationCache* simulationCacheInstance;
    static const MString timeSliderDrawContextName;
    // Each pointer points to where a ComPtr<ID3D11Buffer> is stored (typically as a member of another object)
    std::unordered_set<ComPtr<ID3D11Buffer>, DirectX::ComPtrHash> registry;
    // Frame number to map of buffer pointer to cached data (as vector of bytes)
    std::unordered_map<double, std::unordered_map<ComPtr<ID3D11Buffer>, std::vector<uint8_t>, DirectX::ComPtrHash>> cache;
    MTimeSliderDrawPrimitives drawPrimitives;
    int customDrawID = -1;

    SimulationCache();
    ~SimulationCache();
    void tearDown();
    void addMarkerToTimeline(double frameKey);
    bool hasMarkerAtFrame(double frameKey);
    void removeMarkerAtFrame(double frameKey);
    void cacheData(const MTime& time);
    bool tryUseCache(const MTime& time);
};