#pragma once
#include <d3d11.h>

enum ComputeShaderType {
    Unknown,
    UpdateVoxelBasis,
    TransformMeshVertices
};

class ComputeShader
{
public:
    ComputeShader() = default;
    ComputeShader(int id) { this->id = id; };
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

    virtual void dispatch(ID3D11DeviceContext* dxContext, int threadGroupCount) {
        bind(dxContext);
        dxContext->Dispatch(threadGroupCount, 1, 1); // Dispatch a single thread group
        unbind(dxContext);
    };
    
protected:
    virtual void bind(ID3D11DeviceContext* dxContext) {};
    virtual void unbind(ID3D11DeviceContext* dxContext) {};
    int id;
    ID3D11ComputeShader* shaderPtr = NULL;

};