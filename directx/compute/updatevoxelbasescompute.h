#pragma once

#include "computeshader.h"
#include "../../resource.h"

class UpdateVoxelBasesCompute : public ComputeShader
{

public:
    UpdateVoxelBasesCompute() : ComputeShader(IDR_SHADER1) {};

    ComputeShaderType getType() const override
    {
        return UpdateVoxelBasis;
    };

private:

    void bind(ID3D11DeviceContext* dxContext) override
    {
        // Bind the compute shader to the pipeline
        dxContext->CSSetShader(shaderPtr, NULL, 0);
    };

};