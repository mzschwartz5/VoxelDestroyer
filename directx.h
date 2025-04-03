#pragma once
#include <maya/MGlobal.h>
#include <d3d11.h>
#include <maya/MViewport2Renderer.h>
#include <unordered_map>
using namespace MHWRender;

struct ComputeShader {
	std::string name;
    int id;
	ID3D11ComputeShader* shaderPtr;
};

class DirectX
{
public:
    DirectX() = default;
    DirectX(HINSTANCE pluginInstance);
    ~DirectX();

    void tearDown();
    void dispatchComputeShaders();

private:
    void loadComputeShaders();
    void loadComputeShader(ComputeShader& computeShader);

    void bindDemoShader();

    HINSTANCE pluginInstance;

    ID3D11Device* dxDevice = NULL;
    ID3D11DeviceContext* dxContext = NULL;
    MRenderer* renderer = NULL;
	std::vector<ComputeShader> computeShaders;
    //ID3D11ComputeShader* computeShader = NULL;

    //ID3D11UnorderedAccessView* computeResourceView;
};