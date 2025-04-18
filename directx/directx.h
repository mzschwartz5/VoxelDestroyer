#pragma once
#include <maya/MGlobal.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <maya/MViewport2Renderer.h>
#include <unordered_map>
using namespace MHWRender;

// Forward declare ComputeShader to avoid circular dependency
class DirectX
{
public:
    DirectX() = delete;
    ~DirectX() = delete;

    static void initialize(HINSTANCE pluginInstance);
    
    static ID3D11Device* getDevice();
    static ID3D11DeviceContext* getContext();
    static HINSTANCE getPluginInstance();

private:
    static HINSTANCE pluginInstance;
    static ID3D11Device* dxDevice;
    static ID3D11DeviceContext* dxContext;
    MRenderer* renderer = NULL;
};