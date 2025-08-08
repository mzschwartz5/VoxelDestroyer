#include "globalsolver.h"
#include <maya/MGlobal.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MDGModifier.h>

const MTypeId GlobalSolver::id(0x0013A7B1);
const MString GlobalSolver::globalSolverNodeName("globalSolverNode");
MObject GlobalSolver::globalSolverNodeObject = MObject::kNullObj;
MObject GlobalSolver::aParticleData = MObject::kNullObj;

const MObject& GlobalSolver::createGlobalSolver() {
    if (!globalSolverNodeObject.isNull()) {
        return globalSolverNodeObject;
    }

    MStatus status;
    MDGModifier dgMod;
    globalSolverNodeObject = dgMod.createNode(GlobalSolver::globalSolverNodeName, &status);
	dgMod.doIt();

    return globalSolverNodeObject;
}

const MObject& GlobalSolver::getMObject() {
    return globalSolverNodeObject;
}

void GlobalSolver::postConstructor() {
    MPxNode::postConstructor();
    MStatus status;

    MCallbackId callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onParticleDataConnectionChange, this, &status);
    callbackIds.append(callbackId);
}

GlobalSolver::~GlobalSolver() {
    MMessage::removeCallbacks(callbackIds);
    globalSolverNodeObject = MObject::kNullObj;
}

/**
 * Logical indices are sparse, mapped to contiguous physical indices.
 * This method finds the next available logical index for creating a new particle data plug in the array.
 */
uint GlobalSolver::getNextParticleDataPlugIndex() {
    MStatus status;
    MFnDependencyNode globalSolverNode(globalSolverNodeObject, &status);
    MPlug particleDataArrayPlug = globalSolverNode.findPlug("particledata", false, &status);

    uint nextIndex = 0;
    const uint numElements = particleDataArrayPlug.numElements(&status);
    for (uint i = 0; i < numElements; ++i) {
        uint idx = particleDataArrayPlug.elementByPhysicalIndex(i, &status).logicalIndex();
        if (idx >= nextIndex) {
            nextIndex = idx + 1;
        }
    }
    return nextIndex;
}

void GlobalSolver::onParticleDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
    if (plug != aParticleData || !(msg & MNodeMessage::kConnectionMade || msg & MNodeMessage::kConnectionBroken)) {
        return;
    }

    MGlobal::displayInfo("GlobalSolver: Particle data connection changed.");
}

MStatus GlobalSolver::initialize() {
    MStatus status;

    MFnTypedAttribute tAttr;
    aParticleData = tAttr.create("particledata", "ptd", ParticleData::id, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tAttr.setStorable(false);
    tAttr.setWritable(true);
    tAttr.setReadable(false);
    tAttr.setArray(true);
    status = addAttribute(aParticleData);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MS::kSuccess;
}

// See note in header
MStatus GlobalSolver::compute(const MPlug& plug, MDataBlock& block) {
    return MS::kSuccess;
}

