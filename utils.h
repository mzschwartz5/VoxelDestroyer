#pragma once
#include <maya/MGlobal.h>
#include <maya/MFloatVector.h>
#include <maya/MPlug.h>
#include <maya/MFnPluginData.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include <windows.h>
#include <cstdint>

namespace Utils {

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

uint16_t floatToHalf(float value);

float packTwoFloatsAsHalfs(float a, float b);

MFloatVector sign(const MFloatVector& v);

/**
 * Helper to get the MPxData from a plug of type MFnPluginData.
 * We use a struct rather than a function because the plug object and MFnPluginData
 * must remain alive while the returned MPxData pointer is used.
 */
template<typename T>
struct PluginData {
    MObject plugObj;
    MFnPluginData plugFn;
    T* data = nullptr;

    PluginData(const MObject& dependencyNode, const MObject& plugAttribute) {
        MPlug plug(dependencyNode, plugAttribute);
        plug.getValue(plugObj);
        plugFn.setObject(plugObj);
        data = static_cast<T*>(plugFn.data());
    }

    PluginData(const MPlug& plug) {
        plug.getValue(plugObj);
        plugFn.setObject(plugObj);
        data = static_cast<T*>(plugFn.data());
    }

    T* get() const { return data; }
};

} // namespace Utils