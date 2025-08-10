#pragma once

#include <maya/MPxNode.h>
#include <maya/MCallbackIdArray.h>
#include "custommayaconstructs/particledata.h"
#include "custommayaconstructs/functionaldata.h"
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
    static ComPtr<ID3D11Buffer> particleBuffer;
    static MInt64 heldMemory;

    static void* creator() { return new GlobalSolver(); }
    static MStatus initialize();
    static void tearDown();
    static const MObject& createGlobalSolver();
    static const MObject& getMObject();
    static uint getNextParticleDataPlugIndex();
    static ComPtr<ID3D11UnorderedAccessView> createParticleUAV(uint offset, uint numElements);
    static ComPtr<ID3D11ShaderResourceView> createParticleSRV(uint offset, uint numElements);

    // Time input triggers compute which runs simulation step for all connected PBD nodes.
    MStatus compute(const MPlug& plug, MDataBlock& block) override;

private:
    GlobalSolver() = default;
    ~GlobalSolver() override;
    void postConstructor() override;
    static void createParticleBuffer(const std::vector<glm::vec4>& particlePositions);
    static void onParticleDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
    static void onSimulateFunctionConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
    static MPlug getGlobalTimePlug();
    MCallbackIdArray callbackIds;

    static constexpr int SUBSTEPS = 10;

    // Maps PBD node plug index to its simulate function.
    // Essentially a cache so we don't have to retrieve the function from plugs every frame.
    static std::unordered_map<uint, std::function<void()>> pbdSimulateFuncs;
    static int numPBDNodes;
};