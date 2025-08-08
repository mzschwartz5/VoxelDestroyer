#pragma once

#include <maya/MPxNode.h>
#include <maya/MCallbackIdArray.h>
#include "custommayaconstructs/particledata.h"
#include <maya/MNodeMessage.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "directx/directx.h"
using Microsoft::WRL::ComPtr;

/**
 * Global solver node - responsible for inter-voxel collisions, and interactive dragging.
 * Basically, anything that affects any and all particles without regard to which model they belong to.
 */
class GlobalSolver : public MPxNode {

public:
    static const MTypeId id;
    static const MString globalSolverNodeName;
    static MObject aParticleData;
    static MObject aParticleBufferOffset;
    static MObject globalSolverNodeObject;
    static ComPtr<ID3D11Buffer> particleBuffer;
    static MInt64 heldMemory;

    static void* creator() { return new GlobalSolver(); }
    static MStatus initialize();
    static void tearDown();
    static const MObject& createGlobalSolver();
    static const MObject& getMObject();
    static uint getNextParticleDataPlugIndex();
    static void onParticleDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
    static void createParticleBuffer(const std::vector<glm::vec4>& particlePositions);
    static ComPtr<ID3D11UnorderedAccessView> createParticleUAV(uint offset, uint numElements);
    static ComPtr<ID3D11ShaderResourceView> createParticleSRV(uint offset, uint numElements);

    // No compute implementation. All input data will be constant after initialization.
    // Added and removed connections will be handled by attribute callbacks.
    MStatus compute(const MPlug& plug, MDataBlock& block) override;

private:
    GlobalSolver() = default;
    ~GlobalSolver() override;
    void postConstructor() override;
    MCallbackIdArray callbackIds;
};