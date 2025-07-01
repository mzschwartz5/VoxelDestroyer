#pragma once
#include <maya/MGlobal.h>
#include <windows.h>
#include <cstdint>

namespace Utils {

uint32_t morton3DGetThirdBits(uint32_t coord);
uint32_t toMortonCode(uint32_t x, uint32_t y, uint32_t z);
void fromMortonCode(uint32_t mortonCode, uint32_t& x, uint32_t& y, uint32_t& z);
DWORD loadResourceFile(HINSTANCE pluginInstance, int id, const wchar_t* type, void** resourceData);

} // namespace Utils