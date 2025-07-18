#pragma once
#include <d3dcommon.h>
#include <unordered_map>
#include <string>

class D3DIncludeHandler : public ID3DInclude {
public:
    static D3DIncludeHandler& instance() {
        static D3DIncludeHandler handler;
        return handler;
    }

    // For now, easiest thing to do is just maintain a static mapping of files that may be included,
    // to their Windows Resource ID.
    static const std::unordered_map<std::string, int> shaderNameToID;

    HRESULT Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes) override;
    HRESULT Close(LPCVOID pData) override;
    
private:
    D3DIncludeHandler() = default;
    ~D3DIncludeHandler() = default;

    // Disable copy and move semantics
    D3DIncludeHandler(const D3DIncludeHandler&) = delete;
    D3DIncludeHandler& operator=(const D3DIncludeHandler&) = delete;
    D3DIncludeHandler(D3DIncludeHandler&&) = delete;
    D3DIncludeHandler& operator=(D3DIncludeHandler&&) = delete;
};