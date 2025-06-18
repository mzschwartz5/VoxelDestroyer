#include "directx.h"
#include "../resource.h"

HINSTANCE DirectX::pluginInstance = NULL;
ID3D11Device* DirectX::dxDevice = nullptr;
ID3D11DeviceContext* DirectX::dxContext = nullptr;

clCreateFromD3D11Buffer_fn clCreateFromD3D11Buffer = nullptr;
clEnqueueAcquireD3D11Objects_fn clEnqueueAcquireD3D11Objects = nullptr;
clEnqueueReleaseD3D11Objects_fn clEnqueueReleaseD3D11Objects = nullptr;

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

    // Get function pointers to OpenCL-D3D interop functions (part of the OpenCL KHR sharing extension)
    // Used by the custom MpxGPUDeformer (which uses an OpenCL kernel)
    loadOpenCLD3DInteropFunctions();
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

void DirectX::loadOpenCLD3DInteropFunctions()
{
    cl_platform_id clPlatformId = getOpenCLPlatformId();
        
    if (!clPlatformId) {
        MGlobal::displayError("Failed to get the OpenCL platform ID from the OpenCL context");
        return;
    }

    std::string interopExtSuffix = openCLD3DInteropExtSuffix(clPlatformId);

    if (interopExtSuffix.empty()) {
        MGlobal::displayError("OpenCL-D3D interop is not supported by the OpenCL platform");
        return;
    }

    clCreateFromD3D11Buffer = (clCreateFromD3D11Buffer_fn)clGetExtensionFunctionAddressForPlatform(
        clPlatformId, std::string("clCreateFromD3D11Buffer" + interopExtSuffix).c_str());
    if (!clCreateFromD3D11Buffer) {
        MGlobal::displayError(MString("Failed to get the OpenCL function clCreateFromD3D11Buffer") + interopExtSuffix.c_str());
        return;
    }

    clEnqueueAcquireD3D11Objects = (clEnqueueAcquireD3D11Objects_fn)clGetExtensionFunctionAddressForPlatform(
        clPlatformId, std::string("clEnqueueAcquireD3D11Objects" + interopExtSuffix).c_str());
    if (!clEnqueueAcquireD3D11Objects) {
        MGlobal::displayError(MString("Failed to get the OpenCL function clEnqueueAcquireD3D11Objects") + interopExtSuffix.c_str());
        return;
    }

    clEnqueueReleaseD3D11Objects = (clEnqueueReleaseD3D11Objects_fn)clGetExtensionFunctionAddressForPlatform(
        clPlatformId, std::string("clEnqueueReleaseD3D11Objects" + interopExtSuffix).c_str());
    if (!clEnqueueReleaseD3D11Objects) {
        MGlobal::displayError(MString("Failed to get the OpenCL function clEnqueueReleaseD3D11Objects") + interopExtSuffix.c_str());
        return;
    }
}

cl_platform_id DirectX::getOpenCLPlatformId()
{
    cl_context clContext = MOpenCLInfo::getOpenCLContext();
    cl_context_properties* properties = nullptr;
    size_t propertiesSize = 0;
    cl_platform_id clPlatformId = nullptr;

    // TODO: return boolean to indicate success or failure. Also, move this stuff to a helper function.
    clGetContextInfo(clContext, CL_CONTEXT_PROPERTIES, 0, nullptr, &propertiesSize);
    if (propertiesSize > 0) {
        std::vector<cl_context_properties> properties(propertiesSize / sizeof(cl_context_properties));
        clGetContextInfo(clContext, CL_CONTEXT_PROPERTIES, propertiesSize, properties.data(), nullptr);

        for (size_t i = 0; i < properties.size(); i += 2) {
            if (properties[i] != CL_CONTEXT_PLATFORM) continue;
            
            clPlatformId = reinterpret_cast<cl_platform_id>(properties[i + 1]);
        }
    }

    return clPlatformId;
}

/**
 * Checks if the OpenCL platform supports D3D11 interop.
 * On NVIDIA devices, the interop extension functions end with "NV".ABC
 * 
 * On other devices, which support the Khronos extension, the interop extension functions end with "KHR".
 */
std::string DirectX::openCLD3DInteropExtSuffix(cl_platform_id clPlatformId)
{
    std::string interopExtensionSuffix = "";

    // Query platform extensions
    size_t extSize = 0;
    clGetPlatformInfo(clPlatformId, CL_PLATFORM_EXTENSIONS, 0, nullptr, &extSize);
    std::vector<char> extData(extSize);
    clGetPlatformInfo(clPlatformId, CL_PLATFORM_EXTENSIONS, extSize, extData.data(), nullptr);
    std::string extensions(extData.begin(), extData.end());

    if (extensions.find("cl_khr_d3d11_sharing") != std::string::npos) {
        interopExtensionSuffix = "KHR";
    }

    if (extensions.find("cl_nv_d3d11_sharing") != std::string::npos) {
        interopExtensionSuffix = "NV";
    }

    return interopExtensionSuffix;
}