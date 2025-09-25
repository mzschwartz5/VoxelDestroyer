#pragma once
#include "constants.h"
#include <d3d11.h>
#include <wrl/client.h>
#include "../../resource.h"
#include "../d3dincludehandler.h"
#include "../directx.h"
#include <unordered_map>
#include "../../utils.h"
using Microsoft::WRL::ComPtr;

class ComputeShader
{
public:
    ComputeShader() = default;
    ComputeShader(int id) : id(id) {
        load();
    }
    virtual ~ComputeShader() = default;

    virtual void dispatch() = 0;

    virtual void dispatch(int threadGroupCount) {
        if (threadGroupCount <= 0) return;
        bind();
        DirectX::getContext()->Dispatch(threadGroupCount, 1, 1); 
        unbind();
    };

    static void clearShaderCache() {
        shaderCache.clear();
    }
    
protected:    
    ComPtr<ID3D11ComputeShader> shaderPtr;

    virtual void bind() = 0;
    virtual void unbind() = 0;

    void load() {
        if (shaderCache.find(id) != shaderCache.end()) {
            shaderPtr = shaderCache.at(id);
            return;
        }

        void* data = nullptr;
        DWORD size = Utils::loadResourceFile(DirectX::getPluginInstance(), id, L"SHADER", &data);

        if (size == 0) {
            MGlobal::displayError("Failed to load compute shader resource.");
            return;
        }

        // Replace the shader macros with the actual values
        const std::string deformVerticesThreadStr = std::to_string(DEFORM_VERTICES_THREADS);
        const std::string vgsThreadsStr = std::to_string(VGS_THREADS);
        const std::string buildCollisionGridStr = std::to_string(BUILD_COLLISION_GRID_THREADS);
        const std::string buildCollisionParticleStr = std::to_string(BUILD_COLLISION_PARTICLE_THREADS);
        const std::string prefixScanThreadsStr = std::to_string(PREFIX_SCAN_THREADS);
        const std::string solveCollisionThreadsStr = std::to_string(SOLVE_COLLISION_THREADS);
        const std::string maxCollidersStr = std::to_string(MAX_COLLIDERS);
        D3D_SHADER_MACRO SHADER_MACROS[] = {
            { "DEFORM_VERTICES_THREADS", deformVerticesThreadStr.c_str() },
            { "VGS_THREADS", vgsThreadsStr.c_str() },
            { "BUILD_COLLISION_GRID_THREADS", buildCollisionGridStr.c_str() },
            { "BUILD_COLLISION_PARTICLE_THREADS", buildCollisionParticleStr.c_str() },
            { "PREFIX_SCAN_THREADS", prefixScanThreadsStr.c_str() },
            { "SOLVE_COLLISION_THREADS", solveCollisionThreadsStr.c_str() },
            { "MAX_COLLIDERS", maxCollidersStr.c_str() },
            { NULL, NULL } // Terminate the array
        };

        ID3D10Blob* pPSBuf = NULL;    
        ID3D10Blob* pErrorBlob = NULL;
        HRESULT hr = D3DCompile(data, size, NULL, SHADER_MACROS, &D3DIncludeHandler::instance(), "main", "cs_5_0", 0, 0, &pPSBuf, &pErrorBlob);

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
        shaderCache[id] = shaderPtr;
    }

private:
    // Cache of created shaders to avoid loading and recompiling the same shader multiple times,
    // as multiple instances of the same shader may be used across different PBD nodes.
    inline static std::unordered_map<int, ComPtr<ID3D11ComputeShader>> shaderCache;
    int id;

};