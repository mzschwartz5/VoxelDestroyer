#pragma once

#include <maya/MPxNode.h>
#include <maya/MCallbackIdArray.h>
#include "custommayaconstructs/data/particledata.h"
#include "custommayaconstructs/data/functionaldata.h"
#include "directx/compute/dragparticlescompute.h"
#include "directx/compute/buildcollisiongridcompute.h"
#include "directx/compute/prefixscancompute.h"
#include "directx/compute/buildcollisionparticlescompute.h"
#include "directx/compute/solvecollisionscompute.h"
#include <maya/MNodeMessage.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "directx/directx.h"
#include <unordered_map>
#include <functional>
using Microsoft::WRL::ComPtr;

/**
 * Global solver node - responsible for inter-voxel collisions, and interactive dragging.
 * Basically, anything that affects any and all particles without regard to which model they belong to.
 */
class GlobalSolver : public MPxNode {
    
public:
    enum BufferType {
        PARTICLE,
        SURFACE,
        DRAGGING
    };
    static std::unordered_map<BufferType, ComPtr<ID3D11Buffer>> buffers;

    static const MTypeId id;
    static const MString globalSolverNodeName;
    // Input attributes
    static MObject aTime;
    static MObject aParticleData;
    // Output attributes
    static MObject aParticleBufferOffset;
    static MObject aTrigger;
    static MObject aSimulateFunction;

    static MObject globalSolverNodeObject;

    static void* creator() { return new GlobalSolver(); }
    static MStatus initialize();
    static void tearDown();
    static const MObject& getOrCreateGlobalSolver();
    static uint getNextParticleDataPlugIndex();
    static ComPtr<ID3D11UnorderedAccessView> createUAV(uint offset, uint numElements, BufferType bufferType);
    static ComPtr<ID3D11ShaderResourceView> createSRV(uint offset, uint numElements, BufferType bufferType);

    // Time input triggers compute which runs simulation step for all connected PBD nodes.
    MStatus compute(const MPlug& plug, MDataBlock& block) override;

private:
    GlobalSolver() = default;
    ~GlobalSolver() override = default;
    void postConstructor() override;
    static void onSimulateFunctionConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
    static void onParticleDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
    static void addParticleData(MPlug& particleDataToAddPlug);
    static void deleteParticleData(MPlug& particleDataToRemovePlug);
    static void calculateNewOffsetsAndParticleRadius(MPlug changedPlug, MNodeMessage::AttributeMessage changeType, std::unordered_map<int, int>& offsetForLogicalPlug, float& maximumParticleRadius);
    static MPlug getGlobalTimePlug();
    MCallbackIdArray callbackIds;

    static constexpr int SUBSTEPS = 10;

    // Maps PBD node plug index to its simulate function.
    // Essentially a cache so we don't have to retrieve the function from plugs every frame.
    static std::unordered_map<uint, std::function<void()>> pbdSimulateFuncs;
    static int numPBDNodes;
    static MTime lastComputeTime;

    // Global compute shaders
    void createGlobalComputeShaders(float maxParticleRadius);
    DragParticlesCompute dragParticlesCompute;
    BuildCollisionGridCompute buildCollisionGridCompute;
    PrefixScanCompute prefixScanCompute;
    BuildCollisionParticlesCompute buildCollisionParticleCompute;
    SolveCollisionsCompute solveCollisionsCompute;
    
    bool isDragging = false;
    EventBase::Unsubscribe unsubscribeFromDragStateChange;

    template<typename T>
    static void createBuffer(const std::vector<T>& data, ComPtr<ID3D11Buffer>& buffer) {
        D3D11_BUFFER_DESC bufferDesc = {};
        D3D11_SUBRESOURCE_DATA initData = {};

        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = static_cast<UINT>(data.size() * sizeof(T));
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bufferDesc.CPUAccessFlags = 0;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(T);

        initData.pSysMem = data.data();
        DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, buffer.GetAddressOf());
        MRenderer::theRenderer()->holdGPUMemory(bufferDesc.ByteWidth);
    }

    template<typename T>
    static void copyBufferSubregion(ComPtr<ID3D11Buffer>& srcBuffer, ComPtr<ID3D11Buffer>& dstBuffer, uint srcOffset, uint dstOffset, uint numElements) {
        D3D11_BOX srcBox = {};
        srcBox.left = srcOffset * sizeof(T);
        srcBox.right = (srcOffset + numElements) * sizeof(T);
        srcBox.top = 0;
        srcBox.bottom = 1;
        srcBox.front = 0;
        srcBox.back = 1;

        DirectX::getContext()->CopySubresourceRegion(
            dstBuffer.Get(),
            0, 
            sizeof(T) * dstOffset, 
            0, 0, 
            srcBuffer.Get(),
            0, 
            &srcBox
        );
    }

    static int getTotalParticles() {
        if (!buffers[BufferType::PARTICLE]) return 0;

        D3D11_BUFFER_DESC desc;
        buffers[BufferType::PARTICLE]->GetDesc(&desc);
        return desc.ByteWidth / sizeof(glm::vec4);
    }

    static void resetBuffer(ComPtr<ID3D11Buffer>& buffer) {
        if (!buffer) return;
        
        D3D11_BUFFER_DESC desc;
        buffer->GetDesc(&desc);
        MRenderer::theRenderer()->releaseGPUMemory(desc.ByteWidth);
        buffer.Reset();
    }
};