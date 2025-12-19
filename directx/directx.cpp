#include "directx.h"
#include "../resource.h"

HINSTANCE DirectX::pluginInstance = NULL;
ID3D11Device* DirectX::dxDevice = nullptr;
ID3D11DeviceContext* DirectX::dxContext = nullptr;

MStatus DirectX::initialize(HINSTANCE pluginInstance)
{
    DirectX::pluginInstance = pluginInstance;

    MRenderer* renderer = MRenderer::theRenderer(true);
    void* deviceHandle = renderer->GPUDeviceHandle();
    if (!deviceHandle) {
        MGlobal::displayError("Failed to get the GPU device handle, VoxelDestroyer cannot finish initialization.");
        return MStatus::kFailure;
    }

    DirectX::dxDevice = static_cast<ID3D11Device*>(deviceHandle);
    DirectX::dxDevice->GetImmediateContext(&DirectX::dxContext);
    return MStatus::kSuccess;
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
    const ComPtr<ID3D11Buffer>& buffer,
    UINT elementCount,
    UINT offset,
    DXGI_FORMAT viewFormat
) {
    if (!buffer) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    D3D11_BUFFER_DESC bufferDesc = {};
    int numElements = (elementCount == 0) ? getNumElementsInBuffer(buffer) : elementCount;

    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = offset;
    srvDesc.Buffer.NumElements = numElements;
    srvDesc.Format = viewFormat;

    ComPtr<ID3D11ShaderResourceView> srv;
    HRESULT hr = dxDevice->CreateShaderResourceView(buffer.Get(), &srvDesc, srv.GetAddressOf());
    return srv;
}

ComPtr<ID3D11UnorderedAccessView> DirectX::createUAV(
    const ComPtr<ID3D11Buffer>& buffer,
    UINT elementCount,
    UINT offset,
    DXGI_FORMAT viewFormat
) {
    if (!buffer) return nullptr;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    D3D11_BUFFER_DESC bufferDesc = {};
    int numElements = (elementCount == 0) ? getNumElementsInBuffer(buffer) : elementCount;

    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = offset;
    uavDesc.Buffer.NumElements = numElements;
    uavDesc.Format = viewFormat;

    ComPtr<ID3D11UnorderedAccessView> uav;
    HRESULT hr = dxDevice->CreateUnorderedAccessView(buffer.Get(), &uavDesc, uav.GetAddressOf());
    return uav;
}

/**
 * It's a courtesy to let Maya know how much GPU memory we're using, so it can
 * evict other things if necessary.
 */
void DirectX::notifyMayaOfMemoryUsage(const ComPtr<ID3D11Buffer>& buffer, bool acquire) {
    if (!buffer) return;

    D3D11_BUFFER_DESC bufferDesc = {};
    buffer->GetDesc(&bufferDesc);

    if (acquire) {
        MRenderer::theRenderer()->holdGPUMemory(bufferDesc.ByteWidth);
    } else {
        MRenderer::theRenderer()->releaseGPUMemory(bufferDesc.ByteWidth);
    }
}

/**
 * Note: This only works for structured buffers. For typed buffers, the element size
 * would need to be derived from the DXGI_FORMAT. For raw buffers, elements are 4 bytes.
 * And for, say, vertex buffers, there is no structure byte stride.
 */
int DirectX::getNumElementsInBuffer(const ComPtr<ID3D11Buffer>& buffer) {
    if (!buffer) return 0;

    D3D11_BUFFER_DESC desc;
    buffer->GetDesc(&desc);
    
    return desc.ByteWidth / desc.StructureByteStride;
}