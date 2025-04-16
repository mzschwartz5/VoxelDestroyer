#pragma once
#include <maya/MGlobal.h>
#include <d3d11.h>
#include <maya/MViewport2Renderer.h>
#include <unordered_map>
#include "../directx/compute/computeshader.h"
using namespace MHWRender;

class DirectX
{
public:
    DirectX() = default;
    DirectX(HINSTANCE pluginInstance);
    ~DirectX();

    void tearDown();
    void dispatchShaderByType(ComputeShaderType type, int threadGroupCount);

private:
    void loadComputeShaders();
    void loadComputeShader(ComputeShader& computeShader);

    HINSTANCE pluginInstance;

    ID3D11Device* dxDevice = NULL;
    ID3D11DeviceContext* dxContext = NULL;
    MRenderer* renderer = NULL;
	std::unordered_map<ComputeShaderType, ComputeShader> computeShaders;

    //ID3D11UnorderedAccessView* computeResourceView;
};