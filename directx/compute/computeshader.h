#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "../../resource.h"
using namespace Microsoft::WRL;

enum ComputeShaderType {
    Unknown,
    UpdateVoxelBasis,
    TransformMeshVertices
};

class ComputeShader
{
public:
    ComputeShader() = default;
    ComputeShader(int id, ID3D11Device* device, ID3D11DeviceContext* dxContext) : id(id), dxDevice(device), dxContext(dxContext) {}
    ~ComputeShader() { tearDown(); };

    void tearDown() {
        if (shaderPtr) {
            shaderPtr->Release();
            shaderPtr = NULL;
        }
    };

    int getId() const { return id; };

    ID3D11ComputeShader*& getShaderPtr()  { return shaderPtr; };

    virtual ComputeShaderType getType() const { return Unknown; };

    virtual void dispatch(int threadGroupCount) {
        bind(dxContext);
        dxContext->Dispatch(threadGroupCount, 1, 1); // Dispatch a single thread group
        unbind(dxContext);
    };
    
protected:
    virtual void bind(ID3D11DeviceContext* dxContext) {};
    virtual void unbind(ID3D11DeviceContext* dxContext) {};
    ID3D11Device* dxDevice = nullptr;
    ID3D11DeviceContext* dxContext = nullptr;
    int id;
    ID3D11ComputeShader* shaderPtr = NULL;

};