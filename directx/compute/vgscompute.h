#pragma once

#include "directx/compute/computeshader.h"
struct VGSConstantBuffer {
    float relaxation;
    float edgeUniformity;
    float particleRadius;
    float voxelRestVolume;
    float iterCount;
    float ftfRelaxation;
    float ftfEdgeUniformity;
    int numVoxels;
};

class VGSCompute : public ComputeShader
{
public:
    VGSCompute() = default;

    VGSCompute(
        int numParticles,
        const VGSConstantBuffer& voxelSimInfo
	) : ComputeShader(IDR_SHADER3)
    {
        numWorkgroups = Utils::divideRoundUp(numParticles / 8, VGS_THREADS);
        initializeBuffers(voxelSimInfo);
    };

    void dispatch() override
    {
        ComputeShader::dispatch(numWorkgroups);
    };

    void updateConstantBuffer(const VGSConstantBuffer& newCB) {
        ComputeShader::updateConstantBuffer(voxelSimInfoBuffer, newCB);
    }

    const ComPtr<ID3D11Buffer>& getVoxelSimInfoBuffer() const { return voxelSimInfoBuffer; }

    void setParticlesUAV(const ComPtr<ID3D11UnorderedAccessView>& particlesUAV) {
        this->particlesUAV = particlesUAV;
    }

private:
    int numWorkgroups = 0;
    ComPtr<ID3D11Buffer> voxelSimInfoBuffer;
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    
    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr.Get(), NULL, 0);

		ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
		DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { voxelSimInfoBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(nullptr, NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(const VGSConstantBuffer& voxelSimInfo) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};

        //Initialize constant buffer
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC; // Dynamic for CPU updates
        bufferDesc.ByteWidth = sizeof(VGSConstantBuffer);  // Size of the constant buffer
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; // Bind as a constant buffer
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // Allow CPU writes
        bufferDesc.MiscFlags = 0;
        initData.pSysMem = &voxelSimInfo;
        CreateBuffer(&bufferDesc, &initData, &voxelSimInfoBuffer);
    }
};