#pragma once
#include "directx.h"
#include <array>

class PingPongView {
public:
    using ClearFunc = void(*)(const ComPtr<ID3D11UnorderedAccessView>&);
    using CopyFunc = void(*)(const ComPtr<ID3D11View>&, const ComPtr<ID3D11View>&);

    PingPongView(
        const ComPtr<ID3D11ShaderResourceView>& srvA,
        const ComPtr<ID3D11ShaderResourceView>& srvB,
        const ComPtr<ID3D11UnorderedAccessView>& uavA,
        const ComPtr<ID3D11UnorderedAccessView>& uavB
    ) : srvs{ srvA, srvB }, uavs{ uavB, uavA }, initialized(true) // Note the reversed order for UAVs (because when one buffer is used as SRV, the other is used as UAV)
    {}

    PingPongView() = default;

    void swap() {
        currentIndex = 1 - currentIndex;
    }

    void swap(ClearFunc clearFunc) {
        currentIndex = 1 - currentIndex;
        clear(clearFunc);
    }

    void swap(CopyFunc copyFunc) {
        sync(copyFunc);
        currentIndex = 1 - currentIndex;
    }

    void clear(ClearFunc clearFunc) {
        clearFunc(uavs[currentIndex]);
    }

    void sync(CopyFunc copyFunc) {
        copyFunc(uavs[currentIndex], uavs[1 - currentIndex]);
    }

    bool isInitialized() const { return initialized; }

    ComPtr<ID3D11ShaderResourceView> SRV() const { return srvs[currentIndex]; }
    ComPtr<ID3D11UnorderedAccessView> UAV() const { return uavs[currentIndex]; }

private:
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> srvs;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2> uavs;
    int currentIndex = 0;
    bool initialized = false;
};