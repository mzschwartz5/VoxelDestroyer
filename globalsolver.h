#pragma once

#include <maya/MPxNode.h>
#include <maya/MCallbackIdArray.h>
#include "custommayaconstructs/data/particledata.h"
#include "custommayaconstructs/data/functionaldata.h"
#include "custommayaconstructs/data/colliderdata.h"
#include "directx/compute/dragparticlescompute.h"
#include "directx/compute/buildcollisiongridcompute.h"
#include "directx/compute/prefixscancompute.h"
#include "directx/compute/buildcollisionparticlescompute.h"
#include "directx/compute/solvecollisionscompute.h"
#include "directx/compute/solveprimitivecollisionscompute.h"
#include <maya/MNodeMessage.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "directx/directx.h"
#include <unordered_map>
#include <functional>
#include <unordered_set>
using Microsoft::WRL::ComPtr;

struct ColliderBuffer; // forward declaration

/**
 * Global solver node - responsible for inter-voxel collisions, and interactive dragging.
 * Basically, anything that affects any and all particles without regard to which model they belong to.
 * 
 * Most of the code in this class is bookkeeping (updating buffers as things are added / removed / dirtied).
 */
class GlobalSolver : public MPxNode {
    
public:
    enum BufferType {
        PARTICLE,
        OLDPARTICLE,
        SURFACE,
        DRAGGING,
        COLLIDER
    };
    static std::unordered_map<BufferType, ComPtr<ID3D11Buffer>> buffers;
    static const ComPtr<ID3D11Buffer>& getBuffer(BufferType bufferType) {
        return buffers[bufferType];
    }

    static const MTypeId id;
    static const MString globalSolverNodeName;
    // User-set attributes
    static MObject aNumSubsteps;
    // Input attributes
    static MObject aTime;
    static MObject aParticleData;
    static MObject aColliderData;
    static MObject aSimulateFunction;
    // Output attributes
    static MObject aParticleBufferOffset;
    static MObject aTrigger;

    static MObject globalSolverNodeObject;

    static void* creator() { return new GlobalSolver(); }
    static MStatus initialize();
    static void tearDown();
    static const MObject& getOrCreateGlobalSolver();

    // Time input triggers compute which runs simulation step for all connected PBD nodes.
    MStatus compute(const MPlug& plug, MDataBlock& block) override;

private:
    GlobalSolver() = default;
    ~GlobalSolver() override = default;
    void postConstructor() override;
    static void onSimulateFunctionConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
    static void onParticleDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
    static void onColliderDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
    static void onColliderDataDirty(MObject& node, MPlug& plug, void* clientData);
    static void addParticleData(MPlug& particleDataToAddPlug);
    static void deleteParticleData(MPlug& particleDataToRemovePlug);
    static void calculateNewOffsetsAndParticleRadius(MPlug changedPlug, MNodeMessage::AttributeMessage changeType, std::unordered_map<int, int>& offsetForLogicalPlug, float& maximumParticleRadius);
    static void maybeDeleteGlobalSolver();
    MCallbackIdArray callbackIds;

    static constexpr int SUBSTEPS = 10;

    // Maps PBD node plug index to its simulate function.
    // Essentially a cache so we don't have to retrieve the function from plugs every frame.
    static std::unordered_map<uint, std::function<void()>> pbdSimulateFuncs;
    static ColliderBuffer colliderBuffer;
    static std::unordered_set<int> dirtyColliderIndices;
    static MTime lastComputeTime;

    // Global compute shaders
    void createGlobalComputeShaders(float maxParticleRadius);
    DragParticlesCompute dragParticlesCompute;
    BuildCollisionGridCompute buildCollisionGridCompute;
    PrefixScanCompute prefixScanCompute;
    BuildCollisionParticlesCompute buildCollisionParticleCompute;
    SolveCollisionsCompute solveCollisionsCompute;
    SolvePrimitiveCollisionsCompute solvePrimitiveCollisionsCompute;
    
    bool isDragging = false;
    EventBase::Unsubscribe unsubscribeFromDragStateChange;

    static int getTotalParticles() {
        return DirectX::getNumElementsInBuffer(buffers[BufferType::PARTICLE]);
    }
};