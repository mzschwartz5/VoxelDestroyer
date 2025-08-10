#pragma once
#include <maya/MGlobal.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <maya/MViewport2Renderer.h>
#include <unordered_map>
#include <clew/clew.h>
#include <maya/MOpenCLInfo.h>
using namespace MHWRender;

/* Typedefs for OpenCL-D3D interop functions */
typedef cl_mem (CL_API_CALL *clCreateFromD3D11Buffer_fn)(
    cl_context, cl_mem_flags, ID3D11Buffer*, cl_int*);
extern clCreateFromD3D11Buffer_fn clCreateFromD3D11Buffer;

typedef cl_int (CL_API_CALL *clEnqueueAcquireD3D11Objects_fn)(
    cl_command_queue, cl_uint, const cl_mem*, cl_uint, const cl_event*, cl_event*);
extern clEnqueueAcquireD3D11Objects_fn clEnqueueAcquireD3D11Objects;

typedef cl_int (CL_API_CALL *clEnqueueReleaseD3D11Objects_fn)(
    cl_command_queue, cl_uint, const cl_mem*, cl_uint, const cl_event*, cl_event*);
extern clEnqueueReleaseD3D11Objects_fn clEnqueueReleaseD3D11Objects;

class DirectX
{
public:
    DirectX() = delete;
    ~DirectX() = delete;

    static void initialize(HINSTANCE pluginInstance);
    
    static ID3D11Device* getDevice();
    static ID3D11DeviceContext* getContext();
    static HINSTANCE getPluginInstance();

private:
    static HINSTANCE pluginInstance;
    static ID3D11Device* dxDevice;
    static ID3D11DeviceContext* dxContext;

    static void loadOpenCLD3DInteropFunctions();
    static cl_platform_id getOpenCLPlatformId();
    static std::string openCLD3DInteropExtSuffix(cl_platform_id clPlatformId);
};