#pragma once
#include "directx.h"
#include <array>

class PingPongView {
public:
    using ClearFunc = void(*)(const ComPtr<ID3D11UnorderedAccessView>&);
    static void noopClear(const ComPtr<ID3D11UnorderedAccessView>&) {}

    PingPongView(
        const ComPtr<ID3D11ShaderResourceView>& srvA,
        const ComPtr<ID3D11ShaderResourceView>& srvB,
        const ComPtr<ID3D11UnorderedAccessView>& uavA,
        const ComPtr<ID3D11UnorderedAccessView>& uavB
    ) : srvs{ srvA, srvB }, uavs{ uavB, uavA } // Note the reversed order for UAVs (because when one buffer is used as SRV, the other is used as UAV)
    {}

    PingPongView() = default;

    void swap(ClearFunc clearFunc = &PingPongView::noopClear) {
        currentIndex = 1 - currentIndex;
        clearFunc(uavs[currentIndex]);
    }

    void clear(ClearFunc clearFunc = &PingPongView::noopClear) {
        clearFunc(uavs[currentIndex]);
    }

    ComPtr<ID3D11ShaderResourceView> SRV() const { return srvs[currentIndex]; }
    ComPtr<ID3D11UnorderedAccessView> UAV() const { return uavs[currentIndex]; }

private:
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> srvs;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 2> uavs;
    int currentIndex = 0;
};