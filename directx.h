#pragma once
#include <maya/MGlobal.h>
#include <d3d11.h>
#include <maya/MViewport2Renderer.h>
using namespace MHWRender;

class DirectX
{
public:
    DirectX();
    ~DirectX();

    void tearDown();
    void dispatchComputeShaders();

private:
    void loadComputeShaders();

    ID3D11Device* dxDevice = NULL;
    ID3D11DeviceContext* dxContext = NULL;
    MRenderer* renderer = NULL;
    ID3D11ComputeShader* computeShader = NULL;

    ID3D11UnorderedAccessView* computeResourceView;
};