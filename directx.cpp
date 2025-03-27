#include "directx.h"
#include <d3dcompiler.h>
#include "resource.h"

DirectX::DirectX()
{
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

void DirectX::tearDown()
{
    if (computeShader) {
        computeShader->Release();
        computeShader = NULL;
    }

    if (dxContext) {
        dxContext->Release();
        dxContext = NULL;
    }

    if (dxDevice) {
        dxDevice->Release();
        dxDevice = NULL;
    }
}


// This is a hacky way to get the plugin module handle, which we use to find shaders embedded in the .mll
// Since GetModuleHandle() returns the handle of the host application, not the plugin, we use a dummy variable,
// whose address we pass to GetModuleHandleEx() to get the plugin module handle.
//
// Also note: google suggests using DllMain to get and store the module handle, but Maya defines that in MfnPlugin and we can't override it.
static int dummyVariable = 0;
HMODULE GetPluginModuleHandle()
{
    HMODULE hModule = NULL;

    // Use GetModuleHandleEx to retrieve the module handle
    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&dummyVariable), &hModule))
    {
        return hModule;
    }
    else
    {
        // Handle error if needed
        MGlobal::displayError("Failed to get plugin module handle");
        return NULL;
    }
}

void DirectX::loadComputeShaders()
{
    // Locate the shader resource    
    HRSRC hResource = FindResource(GetPluginModuleHandle(), MAKEINTRESOURCE(IDR_SHADER1), L"SHADER");
    if (!hResource) {
        MGlobal::displayError("Failed to find shader resource");
        return;
    }

    // Load the shader resource
    HGLOBAL hResourceData = LoadResource(GetPluginModuleHandle(), hResource);
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
    DWORD resourceSize = SizeofResource(GetPluginModuleHandle(), hResource);
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

    hr = dxDevice->CreateComputeShader( ( DWORD* )pPSBuf->GetBufferPointer(), pPSBuf->GetBufferSize(), NULL, &computeShader );
    if (FAILED(hr)) {
        MGlobal::displayError("Failed to create compute shader");
        return;
    }
    pPSBuf->Release();

    // Here we would set up buffers and resources for the compute shader
    dxContext->CSSetShader(computeShader, NULL, 0);
}

void DirectX::dispatchComputeShaders()
{
    if (!dxContext || !computeShader) {
        MGlobal::displayError("Failed to dispatch compute shaders");
        return;
    }

    dxContext->Dispatch(1, 1, 1);
}