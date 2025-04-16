#include "directx.h"
#include <d3dcompiler.h>
#include "../resource.h"
#include "../directx/compute/updatevoxelbasescompute.h"

DirectX::DirectX(HINSTANCE pluginInstance)
{
    this->pluginInstance = pluginInstance;

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
    dxDevice = static_cast<ID3D11Device*>(deviceHandle);
    if (!dxDevice) {
        MGlobal::displayError("Failed to cast the device handle to ID3D11Device");
        return;
    }
    
    // Get the device context
    dxDevice->GetImmediateContext(&dxContext);

    loadComputeShaders();
}

DirectX::~DirectX()
{
    tearDown();
}

void DirectX::dispatchShaderByType(ComputeShaderType type, int threadGroupCount)
{
    auto it = computeShaders.find(type);
    if (it == computeShaders.end()) {
        MGlobal::displayError("Compute shader type not found");
        return;
    }

    ComputeShader& computeShader = it->second;
    computeShader.dispatch(dxContext, threadGroupCount);
}

void DirectX::tearDown()
{
    for (auto& computeShader : computeShaders) {
        computeShader.second.tearDown();
    }

    computeShaders.clear();

    if (dxContext) {
        dxContext->Release();
        dxContext = NULL;
    }

    if (dxDevice) {
        dxDevice->Release();
        dxDevice = NULL;
    }
}

void DirectX::loadComputeShaders()
{
    UpdateVoxelBasesCompute updateVoxelBasisCompute;
    loadComputeShader(updateVoxelBasisCompute);
}

void DirectX::loadComputeShader(ComputeShader& computeShader)
{
	computeShaders[computeShader.getType()] = computeShader;

    // Locate the shader resource    
    HRSRC hResource = FindResource(pluginInstance, MAKEINTRESOURCE(computeShader.getId()), L"SHADER");
    if (!hResource) {
        MGlobal::displayError("Failed to find shader resource");
        return;
    }

    // Load the shader resource
    HGLOBAL hResourceData = LoadResource(pluginInstance, hResource);
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
    DWORD resourceSize = SizeofResource(pluginInstance, hResource);
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

    hr = dxDevice->CreateComputeShader( ( DWORD* )pPSBuf->GetBufferPointer(), pPSBuf->GetBufferSize(), NULL, &computeShader.getShaderPtr());
    if (FAILED(hr)) {
        MGlobal::displayError("Failed to create compute shader");
        return;
    }
    pPSBuf->Release();
}