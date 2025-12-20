#pragma once
#include "shaders/constants.hlsli"
#include <d3d11.h>
#include <wrl/client.h>
#include "../../resource.h"
#include "../directx.h"
#include <unordered_map>
#include "../../utils.h"
using Microsoft::WRL::ComPtr;

class ComputeShader
{
public:
    ComputeShader() = default;
    ComputeShader(int mainId) : mainId(mainId) {
        loadShaderObject(mainId);
    }
    virtual ~ComputeShader() = default;

    virtual void dispatch() = 0;

    virtual void dispatch(int threadGroupCount) {
        dispatch(threadGroupCount, mainId);
    };

    virtual void dispatch(int threadGroupCount, int entryPointId) {
        if (threadGroupCount <= 0) return;
        
        DirectX::getContext()->CSSetShader(shaderCache[entryPointId].Get(), NULL, 0);

        bind();
        DirectX::getContext()->Dispatch(threadGroupCount, 1, 1); 
        unbind();
    };

    static void clearShaderCache() {
        shaderCache.clear();
    }
    
protected:    
    virtual void bind() = 0;
    virtual void unbind() = 0;

    void loadShaderObject(int id) {
        if (shaderCache.find(id) != shaderCache.end()) {
            return;
        }

        void* data = nullptr;
        DWORD size = Utils::loadResourceFile(DirectX::getPluginInstance(), id, L"SHADER", &data);

        if (size == 0) {
            MGlobal::displayError("Failed to load compute shader resource.");
            return;
        }

        ComPtr<ID3D11ComputeShader> shaderPtr;
        HRESULT hr = DirectX::getDevice()->CreateComputeShader(data, size, NULL, shaderPtr.GetAddressOf());
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create compute shader");
            return;
        }

        shaderCache[id] = shaderPtr;
    }

private:
    // Cache of created shaders to avoid loading the same shader multiple times,
    // as multiple instances of the same shader may be used across different nodes.
    inline static std::unordered_map<int, ComPtr<ID3D11ComputeShader>> shaderCache;
    int mainId;
};