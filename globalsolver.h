#pragma once

#include <maya/MPxNode.h>
#include <maya/MCallbackIdArray.h>
#include "custommayaconstructs/particledata.h"
#include <maya/MNodeMessage.h>

/**
 * Global solver node - responsible for inter-voxel collisions, and interactive dragging.
 * Basically, anything that affects any and all particles without regard to which model they belong to.
 */
class GlobalSolver : public MPxNode {

public:
    static const MTypeId id;
    static const MString globalSolverNodeName;

    static void* creator() { return new GlobalSolver(); }
    static MStatus initialize();
    static void onParticleDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
    static const MObject& createGlobalSolver();
    static const MObject& getMObject();
    static uint getNextParticleDataPlugIndex();

    // No compute implementation. All input data will be constant after initialization.
    // Added and removed connections will be handled by attribute callbacks.
    MStatus compute(const MPlug& plug, MDataBlock& block) override;

private:
    GlobalSolver() = default;
    ~GlobalSolver() override;
    void postConstructor() override;

    static MObject aParticleData;
    static MObject globalSolverNodeObject;
    MCallbackIdArray callbackIds;
};