#pragma once
#include "constants.h"
#include <d3d11.h>
#include <wrl/client.h>
#include "../../utils.h"
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
    
    int getId() const { return id; };
    
    ID3D11ComputeShader*& getShaderPtr()  { return shaderPtr; };
    
    virtual void dispatch(int threadGroupCount) {
        bind();
        DirectX::getContext()->Dispatch(threadGroupCount, 1, 1); 
        unbind();
    };
    
protected:    
    MInt64 heldMemory = 0; // Memory held by this shader, used for Maya's GPU memory tracking
    int id;
    ID3D11ComputeShader* shaderPtr = NULL;

    virtual void tearDown() {
        if (shaderPtr) {
            shaderPtr->Release();
            shaderPtr = NULL;
        }
        MRenderer::theRenderer()->releaseGPUMemory(heldMemory);
        heldMemory = 0;
    };
    virtual void bind() = 0;
    virtual void unbind() = 0;

    /**
     * Wrapper around D3D11 CreateBuffer which uses Maya's hardware renderer to log GPU memory usage.
     */
    HRESULT CreateBuffer(
        const D3D11_BUFFER_DESC* pDesc,
        const D3D11_SUBRESOURCE_DATA* pInitialData,
        ID3D11Buffer** ppBuffer
    ) {
        HRESULT hr = DirectX::getDevice()->CreateBuffer(pDesc, pInitialData, ppBuffer);
        MRenderer::theRenderer()->holdGPUMemory(pDesc->ByteWidth);
        heldMemory += pDesc->ByteWidth;
        return hr;
    }

    void load() {
        void* data = nullptr;
        DWORD size = Utils::loadResourceFile(DirectX::getPluginInstance(), id, L"SHADER", &data);

        if (size == 0) {
            MGlobal::displayError("Failed to load compute shader resource.");
            return;
        }

        // Replace the shader macros with the actual values
        const std::string transformVerticesThreadsStr = std::to_string(TRANSFORM_VERTICES_THREADS);
        const std::string vgsThreadsStr = std::to_string(VGS_THREADS);
        const std::string buildCollisionStr = std::to_string(BUILD_COLLISION_THREADS);
        const std::string solveCollisionStr = std::to_string(SOLVE_COLLISION_THREADS);
        const std::string maxVoxelsPerCellStr = std::to_string(MAX_VOXELS_PER_CELL);
        D3D_SHADER_MACRO SHADER_MACROS[] = {
            { "TRANSFORM_VERTICES_THREADS", transformVerticesThreadsStr.c_str() },
            { "VGS_THREADS", vgsThreadsStr.c_str() },
            { "BUILD_COLLISION_THREADS", buildCollisionStr.c_str() },
            { "SOLVE_COLLISION_THREADS", solveCollisionStr.c_str() },
            { "MAX_VOXELS_PER_CELL", maxVoxelsPerCellStr.c_str() },
            { NULL, NULL } // Terminate the array
        };

        ID3D10Blob* pPSBuf = NULL;    
        ID3D10Blob* pErrorBlob = NULL;
        HRESULT hr = D3DCompile(data, size, NULL, SHADER_MACROS, NULL, "main", "cs_5_0", 0, 0, &pPSBuf, &pErrorBlob);

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