#pragma once
#include "constants.h"
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
        
        // Replace the shader macros with the actual values
        const std::string updateVoxelBasesThreadsStr = std::to_string(UPDATE_VOXEL_BASES_THEADS);
        const std::string registerVerticesThreadsStr = std::to_string(BIND_VERTICES_THREADS);
        D3D_SHADER_MACRO SHADER_MACROS[] = {
            { "UPDATE_VOXEL_BASES_THEADS", updateVoxelBasesThreadsStr.c_str() },
            { "BIND_VERTICES_THREADS", registerVerticesThreadsStr.c_str() },
            { NULL, NULL } // Terminate the array
        };

        ID3D10Blob* pPSBuf = NULL;    
        ID3D10Blob* pErrorBlob = NULL;
        HRESULT hr = D3DCompile(pResourceData, resourceSize, NULL, SHADER_MACROS, NULL, "main", "cs_5_0", 0, 0, &pPSBuf, &pErrorBlob);

        if (FAILED(hr)) {
            if (pErrorBlob) {
                const char* errorMessage = static_cast<const char*>(pErrorBlob->GetBufferPointer());
                MGlobal::displayError(MString(("Failed to compile shader: " + std::string(errorMessage)).c_str()));
                pErrorBlob->Release();
            } else {
                MGlobal::displayError("Failed to compile shader: Unknown error");
            }
            return;
        }

        hr = DirectX::getDevice()->CreateComputeShader( pPSBuf->GetBufferPointer(), pPSBuf->GetBufferSize(), NULL, &shaderPtr);
        if (FAILED(hr)) {
            MGlobal::displayError("Failed to create compute shader");
            return;
        }
        pPSBuf->Release();
    }

};