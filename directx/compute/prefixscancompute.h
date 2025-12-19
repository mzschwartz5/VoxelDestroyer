#pragma once
#include "directx/compute/computeshader.h"
#include "./prefixscancollectcompute.h"
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
    PrefixScanCompute() = default;

    PrefixScanCompute(
        const ComPtr<ID3D11UnorderedAccessView>& collisionCellParticleCountsUAV
    ) : ComputeShader(IDR_SHADER2), collisionCellParticleCountsUAV(collisionCellParticleCountsUAV) {
        if (!collisionCellParticleCountsUAV) return;
        initializeBuffers();
    }

    ~PrefixScanCompute() {
        for (int i = 0; i < numScans; ++i) {
           DirectX::notifyMayaOfMemoryUsage(partialSumsBuffers[i]);
        }
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
        for (scanIdx = numScans - 1; scanIdx > 1; --scanIdx) {
            collectComputePass.collect(
                partialSumsUAVs[scanIdx - 2],           // collect into the scan sums from two levels higher up
                partialSumsSRVs[scanIdx - 1],           // read sums from the one level up
                2 * numWorkgroupsForScan[scanIdx - 1]   // Factor of 2 bc each collect thread only processes one element; each scan thread processes two.
            );
        }

        // Final collect into original collision cell particle count buffer
        // TODO: consider consolidating this UAV into the vector of partial sums UAVs and treating it just like the others. (Would need some renaming of vars, refactoring index logic, etc.)
        if (scanIdx == 1) {
            collectComputePass.collect(
                collisionCellParticleCountsUAV,         
                partialSumsSRVs[0],                     
                2 * numWorkgroupsForScan[0]                 
            );
            scanIdx = 0; // Reset scan index for next dispatch
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
    PrefixScanCollectCompute collectComputePass;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavQueryDesc;
    int numElements = 0;
    int numScans = 0;
    int scanIdx = 0;

    void bind() override {
        ID3D11UnorderedAccessView* uavs[] = { activeUAVForScan.Get(), partialSumsUAVs[scanIdx].Get() };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    }

    void unbind() override {
        ID3D11UnorderedAccessView* uavs[] = { nullptr, nullptr };
        DirectX::getContext()->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
    }

    void initializeBuffers() {
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
            numWorkgroupsForScan[i] = Utils::divideRoundUp(numElementsInBuffer, PREFIX_SCAN_THREADS);
            
            // In the event that the scan can be done in a single workgroup, integer division results in 0 elements - so clamp to 1.
            numElementsInBuffer /= base; 
            numElementsInBuffer = std::max(numElementsInBuffer, 1);

            std::vector<unsigned int> emptyData(numElementsInBuffer, 0u);
            partialSumsBuffers[i] = DirectX::createReadWriteBuffer(emptyData);
            partialSumsSRVs[i] = DirectX::createSRV(partialSumsBuffers[i]);
            partialSumsUAVs[i] = DirectX::createUAV(partialSumsBuffers[i]);
        }

    }
};