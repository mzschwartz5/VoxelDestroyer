#include "../utils.h" // This has to be first because it transitively includes MGlobal.h which throws a fit if it's not included first.
#include "d3dincludehandler.h"
#include "./directx.h"
#include "../resource.h"

// Map of shaders that may be included, to their Windows Resource ID.
const std::unordered_map<std::string, int> D3DIncludeHandler::shaderNameToID = {
    {"vgs_core.hlsl", IDR_SHADER6},
    {"particle_collisions_shared.hlsl", IDR_SHADER10}
};

HRESULT D3DIncludeHandler::Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes) {
    auto it = shaderNameToID.find(pFileName);
    if (it == shaderNameToID.end() || it->second == 0) {
        MGlobal::displayError(MString("Failed to find shader resource ID for: ") + pFileName);
        return E_FAIL;
    }
    int shaderResourceID = it->second;

    void* resourcePtr = nullptr;
    DWORD size = Utils::loadResourceFile(DirectX::getPluginInstance(), shaderResourceID, L"SHADER", &resourcePtr);
    if (size == 0 || resourcePtr == nullptr) {
        MGlobal::displayError(MString("Failed to load shader resource: ") + pFileName);
        return E_FAIL;
    }

    // Allocate memory for and copy the shader data (the ID3DInclude interface needs to own and manage the memory).
    void* buffer = malloc(size);
    if (!buffer) {
        MGlobal::displayError(MString("Failed to allocate memory for shader resource: ") + pFileName);
        return E_OUTOFMEMORY;
    }
    
    memcpy(buffer, resourcePtr, size);
    *ppData = buffer;
    *pBytes = size;

    return S_OK;
}

HRESULT D3DIncludeHandler::Close(LPCVOID pData) {
    if (pData) {
        free(const_cast<void*>(pData));
    }
    return S_OK;
}
