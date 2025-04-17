#include "directx.h"
#include "../resource.h"

HINSTANCE DirectX::pluginInstance = NULL;
ID3D11Device* DirectX::dxDevice = nullptr;
ID3D11DeviceContext* DirectX::dxContext = nullptr;

void DirectX::initialize(HINSTANCE pluginInstance)
{
    DirectX::pluginInstance = pluginInstance;

    // Get the renderer
    MRenderer* renderer = MRenderer::theRenderer();
    if (!renderer) {
        MGlobal::displayError("Failed to get the renderer, check that the viewport is set to Viewport 2.0");
        return;
    }

    // Get the device handle
    void* deviceHandle = renderer->GPUDeviceHandle();
    if (!deviceHandle) {
        MGlobal::displayError("Failed to get the device handle, check that Viewport 2.0 Rendering Engine is set to DirectX 11");
        return;
    }

    // Cast the device handle to ID3D11Device
    DirectX::dxDevice = static_cast<ID3D11Device*>(deviceHandle);
    if (!DirectX::dxDevice) {
        MGlobal::displayError("Failed to cast the device handle to ID3D11Device");
        return;
    }
    
    // Get the device context
    DirectX::dxDevice->GetImmediateContext(&DirectX::dxContext);
}

ID3D11Device* DirectX::getDevice()
{
    return dxDevice;
}

ID3D11DeviceContext* DirectX::getContext()
{
    return dxContext;
}

HINSTANCE DirectX::getPluginInstance()
{
    return pluginInstance;
}