#pragma once
#include "directx/compute/computeshader.h"
#include "./prefixscancollectcompute.h"
#include <memory>
#include <algorithm>
#include "constants.h"

/**
 * Class to perform an *inclusive* parallel prefix scan via GPU compute (GPU Gems algorithm).
 * While this could easily be generalized to any buffer, for this project it's tied to the collision grid cell buffer.
 * 
 * NOTE: this scan assumes a power-of-2 number of elements - pre-padded if necessary.
 */
class PrefixScanCompute : public ComputeShader
{
public:
    PrefixScanCompute(
        const ComPtr<ID3D11UnorderedAccessView>& collisionCellParticleCountsUAV
    ) : ComputeShader(IDR_SHADER2), collisionCellParticleCountsUAV(collisionCellParticleCountsUAV) {
        collectComputePass = std::make_unique<PrefixScanCollectCompute>();
        initializeBuffers();
    }

    void dispatch() override {
        activeUAVForScan = collisionCellParticleCountsUAV;
        ComputeShader::dispatch(numWorkgroupsForScan[0]);

        // Scan the partial sums, and the partial sums of the partial sums (if necessary), etc.
        // Written to be iterative (using analytically prederived `numScans`) rather than recursive
        // Note that, for realistic buffer / workgroup sizes, this is generally 0-3 scans (and most often just 1).
        for (scanIdx = 1; scanIdx < numScans; ++scanIdx) {
            activeUAVForScan = partialSumsUAVs[scanIdx - 1];
            ComputeShader::dispatch(numWorkgroupsForScan[scanIdx - 1]);
        }

        // Now go back up the chain, adding the partial sums to the scanned data they came from.
        for (scanIdx = numScans - 1; scanIdx > 0; --scanIdx) {
            collectComputePass->collect(
                partialSumsUAVs[scanIdx - 1],           // collect into the scan sums from one level higher up
                partialSumsSRVs[scanIdx],               // read sums from the current level
                2 * numWorkgroupsForScan[scanIdx - 1]   // Factor of 2 bc each collect thread only processes one element; each scan thread processes two.
            );
        }
    }

private:
    // In the event that the number of partial sums exceeds what can be scanned in a single workgroup, the partial sums scan
    // itself requires a partial sums buffer - recursively, to be fully general. We can figure out analytically, however, how
    // many scans (and thus buffers, views) we will need.
    std::vector<ComPtr<ID3D11Buffer>> partialSumsBuffers; // Tracks partial sums from each workgroup
    std::vector<ComPtr<ID3D11ShaderResourceView>> partialSumsSRVs; 
    std::vector<ComPtr<ID3D11UnorderedAccessView>> partialSumsUAVs;
    std::vector<int> numWorkgroupsForScan;
    ComPtr<ID3D11UnorderedAccessView> activeUAVForScan;
    ComPtr<ID3D11UnorderedAccessView> collisionCellParticleCountsUAV;
    std::unique_ptr<PrefixScanCollectCompute> collectComputePass;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavQueryDesc;
    int numElements = 0;
    int numScans = 0;
    int scanIdx = 0;

    void bind() override {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { activeUAVForScan.Get(), partialSumsUAVs[scanIdx].Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    }

    void unbind() override {
        DirectX::getContext()->CSSetShader(shaderPtr, NULL, 0);

        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    }

    void initializeBuffers() {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

        collisionCellParticleCountsUAV->GetDesc(&uavQueryDesc);
        numElements = uavQueryDesc.Buffer.NumElements;

        // How many times do we need to do the scan? (*not* how many workgroups, but how many iterations of scanning + partial sums)
        // For realistic buffer sizes, this is generally 1-3.
        int base = 2 * PREFIX_SCAN_THREADS;
        numScans = Utils::ilogbaseceil(numElements, base);
        
        int numElementsInBuffer = numElements;
        numWorkgroupsForScan.resize(numScans); 
        partialSumsBuffers.resize(numScans);
        partialSumsSRVs.resize(numScans);
        partialSumsUAVs.resize(numScans);
        for (int i = 0; i < numScans; ++i) {
            numElementsInBuffer /= base; 
            // In the event that the scan can be done in a single workgroup, integer division above results in 0 elements.
            numElementsInBuffer = std::max(numElementsInBuffer, 1);
            numWorkgroupsForScan[i] = Utils::divideRoundUp(numElementsInBuffer, PREFIX_SCAN_THREADS);

            // Create the partial sums buffer and its UAV and SRV
            bufferDesc.Usage = D3D11_USAGE_DEFAULT;
            bufferDesc.ByteWidth = numElementsInBuffer * sizeof(unsigned int);
            bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            bufferDesc.CPUAccessFlags = 0;
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bufferDesc.StructureByteStride = sizeof(unsigned int);
            CreateBuffer(&bufferDesc, nullptr, &partialSumsBuffers[i]);

            srvDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffer, no specific format
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = numElementsInBuffer;
            DirectX::getDevice()->CreateShaderResourceView(partialSumsBuffers[i].Get(), &srvDesc, &partialSumsSRVs[i]);
    
            uavDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffer, no specific format
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = numElementsInBuffer;
            DirectX::getDevice()->CreateUnorderedAccessView(partialSumsBuffers[i].Get(), &uavDesc, &partialSumsUAVs[i]);
        }

    }

    void tearDown() override {
        ComputeShader::tearDown();
        for (auto& srv : partialSumsSRVs) {
            srv.Reset();
        }

        for (auto& uav : partialSumsUAVs) {
            uav.Reset();
        }

        for (auto& buffer : partialSumsBuffers) {
            buffer.Reset();
        }
    }
};