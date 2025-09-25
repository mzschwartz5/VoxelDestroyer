#pragma once
#include <maya/MGlobal.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <vector>
#include <maya/MViewport2Renderer.h>
#include <wrl/client.h>
using namespace MHWRender;
using Microsoft::WRL::ComPtr;

class DirectX
{
public:
    DirectX() = delete;
    ~DirectX() = delete;

    static void initialize(HINSTANCE pluginInstance);
    
    static ID3D11Device* getDevice();
    static ID3D11DeviceContext* getContext();
    static HINSTANCE getPluginInstance();

    template<typename T>
    static ComPtr<ID3D11Buffer> createReadOnlyBuffer(
        std::vector<T>& data,
        UINT additionalBindFlags = 0,
        bool rawBuffer = false
    ) {
        D3D11_BUFFER_DESC bufferDesc = {};

        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        bufferDesc.ByteWidth = static_cast<UINT>(sizeof(T) * data.size());
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | additionalBindFlags;
        bufferDesc.CPUAccessFlags = 0;
        if (rawBuffer) {
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        }
        else {
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bufferDesc.StructureByteStride = sizeof(T);
        }

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = data.data();

        ComPtr<ID3D11Buffer> buffer;
        HRESULT hr = dxDevice->CreateBuffer(&bufferDesc, &initData, &buffer);
        return buffer;   
    }

    template<typename T>
    static ComPtr<ID3D11Buffer> createReadWriteBuffer(
        std::vector<T>& data,
        UINT additionalBindFlags = 0,
        bool rawBuffer = false
    ) {
        D3D11_BUFFER_DESC bufferDesc = {};

        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = static_cast<UINT>(sizeof(T) * data.size());
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | additionalBindFlags;
        bufferDesc.CPUAccessFlags = 0;

        if (rawBuffer) {
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        }
        else {
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bufferDesc.StructureByteStride = sizeof(T);
        }

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = data.data();

        ComPtr<ID3D11Buffer> buffer;
        HRESULT hr = dxDevice->CreateBuffer(&bufferDesc, &initData, &buffer);
        return buffer;   
    }
    
    template<typename T>
    static ComPtr<ID3D11Buffer> createConstantBuffer(const T& data) {
        D3D11_BUFFER_DESC bufferDesc = {};

        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = static_cast<UINT>(sizeof(T));
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = 0;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = &data;

        ComPtr<ID3D11Buffer> buffer;
        HRESULT hr = dxDevice->CreateBuffer(&bufferDesc, &initData, &buffer);
        return buffer;   
    }

    static ComPtr<ID3D11ShaderResourceView> createSRV(
        ComPtr<ID3D11Buffer>& buffer,
        UINT elementCount = 0,
        UINT offset = 0,
        bool rawBuffer = false
    );

    static ComPtr<ID3D11UnorderedAccessView> createUAV(
        ComPtr<ID3D11Buffer>& buffer,
        UINT elementCount = 0,
        UINT offset = 0,
        bool rawBuffer = false
    );

private:
    static HINSTANCE pluginInstance;
    static ID3D11Device* dxDevice;
    static ID3D11DeviceContext* dxContext;
};