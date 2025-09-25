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
    if (!renderer || renderer->drawAPI() != DrawAPI::kDirectX11) {
        MGlobal::displayError("Failed to get the renderer, check that the viewport is set to Viewport 2.0 with DirectX 11 as the rendering engine");
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

ComPtr<ID3D11ShaderResourceView> DirectX::createSRV(
    ComPtr<ID3D11Buffer>& buffer,
    UINT elementCount,
    UINT offset,
    bool rawBuffer
) {
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    D3D11_BUFFER_DESC bufferDesc = {};
    int numElements = elementCount;
    if (numElements == 0) {
        buffer->GetDesc(&bufferDesc);
        numElements = rawBuffer ?  bufferDesc.ByteWidth / 4 : 
                                   bufferDesc.ByteWidth / bufferDesc.StructureByteStride;
    }

    if (rawBuffer) {
        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    } else {
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    }
    srvDesc.Buffer.FirstElement = offset;
    srvDesc.Buffer.NumElements = numElements;

    ComPtr<ID3D11ShaderResourceView> srv;
    HRESULT hr = dxDevice->CreateShaderResourceView(buffer.Get(), &srvDesc, srv.GetAddressOf());
    return srv;

}

ComPtr<ID3D11UnorderedAccessView> DirectX::createUAV(
    ComPtr<ID3D11Buffer>& buffer,
    UINT elementCount,
    UINT offset,
    bool rawBuffer
) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    D3D11_BUFFER_DESC bufferDesc = {};
    int numElements = elementCount;
    if (numElements == 0) {
        buffer->GetDesc(&bufferDesc);
        numElements = rawBuffer ?  bufferDesc.ByteWidth / 4 : 
                                   bufferDesc.ByteWidth / bufferDesc.StructureByteStride;
    }

    if (rawBuffer) {
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    } else {
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    }
    uavDesc.Buffer.FirstElement = offset;
    uavDesc.Buffer.NumElements = numElements;

    ComPtr<ID3D11UnorderedAccessView> uav;
    HRESULT hr = dxDevice->CreateUnorderedAccessView(buffer.Get(), &uavDesc, uav.GetAddressOf());
    return uav;
}