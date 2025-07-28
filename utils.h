#pragma once
#include <maya/MGlobal.h>
#include <windows.h>
#include <cstdint>

namespace Utils {

uint32_t morton3DGetThirdBits(uint32_t coord);
uint32_t toMortonCode(uint32_t x, uint32_t y, uint32_t z);
void fromMortonCode(uint32_t mortonCode, uint32_t& x, uint32_t& y, uint32_t& z);
DWORD loadResourceFile(HINSTANCE pluginInstance, int id, const wchar_t* type, void** resourceData);
std::string HResultToString(const HRESULT& hr);

inline int divideRoundUp(int numerator, int denominator) {
    return (numerator + denominator - 1) / denominator;
}

inline int ilogbaseceil(int x, int base) {
    // Using change of bases property of logarithms:
    return static_cast<int>(std::ceil(std::log(x) / std::log(base)));
}

} // namespace Utils