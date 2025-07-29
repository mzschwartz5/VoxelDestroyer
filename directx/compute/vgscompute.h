#pragma once

#include "directx/compute/computeshader.h"
#include "glm.hpp"

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
    VGSCompute(
        const float* weights,
        const std::vector<glm::vec4>& particlePositions,
        const VGSConstantBuffer& voxelSimInfo
	) : ComputeShader(IDR_SHADER3)
    {
        // TODO: weights should just be the unused 4th float component of the particle positions
        // (TODO 2: pack particle radius alongside weight into fourth component, at half precision)
        initializeBuffers(weights, particlePositions, voxelSimInfo);
    };

    void dispatch() override
    {
        ComputeShader::dispatch(numWorkgroups);
    };

    void updateConstantBuffer(const VGSConstantBuffer& newCB) {
        ComputeShader::updateConstantBuffer(voxelSimInfoBuffer, newCB);
    }

    const ComPtr<ID3D11ShaderResourceView>& getWeightsSRV() const { return weightsSRV; }
    const ComPtr<ID3D11ShaderResourceView>& getParticlesSRV() const { return particlesSRV; }
    const ComPtr<ID3D11UnorderedAccessView>& getParticlesUAV() const { return particlesUAV; };
    const ComPtr<ID3D11Buffer>& getVoxelSimInfoBuffer() const { return voxelSimInfoBuffer; }
    const ComPtr<ID3D11Buffer>& getParticlesBuffer() const { return particlesBuffer; }

private:
    int numWorkgroups = 0;
    ComPtr<ID3D11Buffer> voxelSimInfoBuffer;
    ComPtr<ID3D11Buffer> weightsBuffer;
    ComPtr<ID3D11Buffer> particlesBuffer; 
    ComPtr<ID3D11ShaderResourceView> weightsSRV;
    ComPtr<ID3D11ShaderResourceView> particlesSRV;
    ComPtr<ID3D11UnorderedAccessView> particlesUAV;
    
    void bind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = {  weightsSRV.Get() };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

		ID3D11UnorderedAccessView* uavs[] = { particlesUAV.Get() };
		DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { voxelSimInfoBuffer.Get() };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void unbind() override
    {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11ShaderResourceView* srvs[] = { nullptr };
        DirectX::getContext()->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        ID3D11UnorderedAccessView* uavs[] = { nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

        ID3D11Buffer* cbvs[] = { nullptr };
        DirectX::getContext()->CSSetConstantBuffers(0, ARRAYSIZE(cbvs), cbvs);
    };

    void initializeBuffers(const float* weights, const std::vector<glm::vec4>& particlePositions, const VGSConstantBuffer& voxelSimInfo) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        int numParticles = static_cast<int>(particlePositions.size());
        numWorkgroups = Utils::divideRoundUp(numParticles / 8, VGS_THREADS);

        // Initialize weights buffer and its SRV
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        bufferDesc.ByteWidth = numParticles * sizeof(float); // Float for weights
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(float);

        initData.pSysMem = weights;
        CreateBuffer(&bufferDesc, &initData, &weightsBuffer);

        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numParticles;

        DirectX::getDevice()->CreateShaderResourceView(weightsBuffer.Get(), &srvDesc, &weightsSRV);

        // Initialize particlesBuffer (positions) and its SRV and UAV
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = numParticles * sizeof(glm::vec4); // glm::vec4 for alignment
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(glm::vec4); // Size of each element in the buffer
    
        initData.pSysMem = particlePositions.data();
        CreateBuffer(&bufferDesc, &initData, &particlesBuffer);
    
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numParticles;
    
        DirectX::getDevice()->CreateShaderResourceView(particlesBuffer.Get(), &srvDesc, &particlesSRV);

        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numParticles;

        DirectX::getDevice()->CreateUnorderedAccessView(particlesBuffer.Get(), &uavDesc, &particlesUAV);

        //Initialize constant buffer
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC; // Dynamic for CPU updates
        bufferDesc.ByteWidth = sizeof(VGSConstantBuffer);  // Size of the constant buffer
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; // Bind as a constant buffer
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // Allow CPU writes
        bufferDesc.MiscFlags = 0;
        initData.pSysMem = &voxelSimInfo;
        CreateBuffer(&bufferDesc, &initData, &voxelSimInfoBuffer);
    }

    void tearDown() override
    {
        ComputeShader::tearDown();
        weightsBuffer.Reset();
        weightsSRV.Reset();
        voxelSimInfoBuffer.Reset();
    };

};