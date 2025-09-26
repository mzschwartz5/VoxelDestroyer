#pragma once
#include <maya/MGlobal.h>
#include <maya/MFloatVector.h>
#include <maya/MPlug.h>
#include <maya/MFnPluginData.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include <windows.h>
#include <cstdint>
#include <type_traits>
#include <utility>

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

/**
 * Create an instance of (subclass of) MPxData, initialize it using the provided initializer function,
 * then set the provided plug on the dependency node to the new data object.
 */
template<typename T, typename Initializer>
MStatus createPluginData(const MObject& dependencyNode, const MObject& plugAttribute, Initializer&& initializer) {
    MStatus status;
    MFnPluginData fnData;
    MObject dataObj = fnData.create(T::id, &status);
    if (status != MStatus::kSuccess) return status;

    T* data = static_cast<T*>(fnData.data(&status));
    if (status != MStatus::kSuccess || !data) return status;

    // Call the initializer function passed in by the user
    std::forward<Initializer>(initializer)(data);

    // Set the plug value to the new data object
    MPlug plug(dependencyNode, plugAttribute);
    status = plug.setValue(dataObj);
    return status;
}

/**
 * Overload: create MPxData, initialize it, and set it onto an MDataBlock output handle.
 * Useful inside compute() implementations.
 */
template<typename T, typename Initializer>
MStatus createPluginData(MDataBlock& dataBlock, const MObject& outputAttribute, Initializer&& initializer) {
    MStatus status;
    MFnPluginData fnData;
    MObject dataObj = fnData.create(T::id, &status);
    if (status != MStatus::kSuccess) return status;

    T* data = static_cast<T*>(fnData.data(&status));
    if (status != MStatus::kSuccess || !data) return status;

    // Initialize the MPxData instance
    std::forward<Initializer>(initializer)(data);

    // Get the output handle and attach the MObject
    MDataHandle outHandle = dataBlock.outputValue(outputAttribute, &status);
    if (status != MStatus::kSuccess) return status;

    outHandle.setMObject(dataObj);
    outHandle.setClean();
    return status;
}

} // namespace Utils