#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "../../resource.h"
#include "../directx.h"
using namespace Microsoft::WRL;


class ComputeShader
{
public:
    ComputeShader() = default;
    ComputeShader(int id) : id(id) {
        load();
    }
    ~ComputeShader() { tearDown(); };

    virtual void tearDown() {
        if (shaderPtr) {
            shaderPtr->Release();
            shaderPtr = NULL;
        }
    };

    int getId() const { return id; };

    ID3D11ComputeShader*& getShaderPtr()  { return shaderPtr; };

    virtual void dispatch(int threadGroupCount) {
        bind();
        DirectX::getContext()->Dispatch(threadGroupCount, 1, 1); 
        unbind();
    };
    
protected:
    virtual void bind() {};
    virtual void unbind() {};
    int id;
    ID3D11ComputeShader* shaderPtr = NULL;

    void load() {
        // Locate the shader resource    
        HRSRC hResource = FindResource(DirectX::getPluginInstance(), MAKEINTRESOURCE(id), L"SHADER");
        if (!hResource) {
            MGlobal::displayError("Failed to find shader resource");
            return;
        }

        // Load the shader resource
        HGLOBAL hResourceData = LoadResource(DirectX::getPluginInstance(), hResource);
        if (!hResourceData) {
            MGlobal::displayError("Failed to load shader resource");
            return;
        }
        
        // Lock the resource data
        void* pResourceData = LockResource(hResourceData);
        if (!pResourceData) {
            MGlobal::displayError("Failed to lock shader resource");
            return;
        }

        // Get the size of the resource
        DWORD resourceSize = SizeofResource(DirectX::getPluginInstance(), hResource);
        if (resourceSize == 0) {
            MGlobal::displayError("Failed to get the size of the shader resource");
            return;
        }
        
        ID3D10Blob* pPSBuf = NULL;    
        HRESULT hr = D3DCompile(pResourceData, resourceSize, NULL, NULL, NULL, "main", "cs_5_0", 0, 0, &pPSBuf, NULL);

        if (FAILED(hr)) {
            MGlobal::displayError("Failed to compile shader");
            return;
        }

        hr = DirectX::getDevice()->CreateComputeShader( ( DWORD* )pPSBuf->GetBufferPointer(), pPSBuf->GetBufferSize(), NULL, &shaderPtr);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create compute shader");
            return;
        }
        pPSBuf->Release();
    }

};