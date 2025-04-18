#pragma once
#include "computeshader.h"
#include "../glm/glm.hpp"

class TransformVerticesCompute : public ComputeShader
{

public:
    TransformVerticesCompute(
        int numVerts,
        const ComPtr<ID3D11ShaderResourceView>& particlesSRV,
        const ComPtr<ID3D11ShaderResourceView>& vertstartIdsSRV,
        const ComPtr<ID3D11ShaderResourceView>& numVerticesSRV,
        const ComPtr<ID3D11ShaderResourceView>& localRestPositionsSRV
    ) : ComputeShader(IDR_SHADER1), particlesSRV(particlesSRV), vertStartIdsSRV(vertstartIdsSRV), numVerticesSRV(numVerticesSRV), localRestPositionsSRV(localRestPositionsSRV)
    {
        initializeBuffers(numVerts);
    };
        
private:
    ComPtr<ID3D11Buffer> transformedVertsBuffer;
    ComPtr<ID3D11UnorderedAccessView> transformedVertsUAV;
    ComPtr<ID3D11ShaderResourceView> particlesSRV;          // Owned by the bindVertices compute shader, but used here
    ComPtr<ID3D11ShaderResourceView> vertStartIdsSRV;       // Owned by the bindVertices compute shader, but used here
    ComPtr<ID3D11ShaderResourceView> numVerticesSRV;        // Owned by the bindVertices compute shader, but used here
    ComPtr<ID3D11ShaderResourceView> localRestPositionsSRV; // Owned by the bindVertices compute shader, but used here
    
    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);
        
        ID3D11ShaderResourceView* srvs[] = { particlesSRV.Get(), vertStartIdsSRV.Get(), numVerticesSRV.Get(), localRestPositionsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
        
        ID3D11UnorderedAccessView* uavs[] = { transformedVertsUAV.Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, 1, uavs, nullptr); // No need for initialCounts
    };
    
    void initializeBuffers(int numVerts) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

        // Initialize transformedVertsBuffer and its UAV 
        bufferDesc.Usage = D3D11_USAGE_DEFAULT; // Allow GPU write access
        bufferDesc.ByteWidth = sizeof(float) * 4 * numVerts;
        bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0; 
        bufferDesc.StructureByteStride = sizeof(float) * 4;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    
        DirectX::getDevice()->CreateBuffer(&bufferDesc, nullptr, &transformedVertsBuffer);
    
        uavDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffer, no format
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numVerts;
    
        DirectX::getDevice()->CreateUnorderedAccessView(transformedVertsBuffer.Get(), &uavDesc, &transformedVertsUAV);
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
        transformedVertsBuffer.Reset();
        transformedVertsUAV.Reset();
    };
};