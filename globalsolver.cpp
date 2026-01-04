#include "globalsolver.h"
#include <maya/MGlobal.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MAnimControl.h>
#include "custommayaconstructs/tools/voxeldragcontext.h"
#include "custommayaconstructs/usernodes/colliderlocator.h"
#include "custommayaconstructs/data/particledata.h"
#include "custommayaconstructs/data/functionaldata.h"
#include "custommayaconstructs/data/colliderdata.h"
#include "simulationcache.h"

const MTypeId GlobalSolver::id(0x0013A7B1);
const MString GlobalSolver::globalSolverNodeName("GlobalSolver");
MObject GlobalSolver::globalSolverNodeObject = MObject::kNullObj;
MObject GlobalSolver::aNumSubsteps = MObject::kNullObj;
MObject GlobalSolver::aParticleCollisionsEnabled = MObject::kNullObj;
MObject GlobalSolver::aPrimitiveCollisionsEnabled = MObject::kNullObj;
MObject GlobalSolver::aParticleFriction = MObject::kNullObj;
MObject GlobalSolver::aParticleData = MObject::kNullObj;
MObject GlobalSolver::aColliderData = MObject::kNullObj;
MObject GlobalSolver::aParticleBufferOffset = MObject::kNullObj;
MObject GlobalSolver::aTime = MObject::kNullObj;
MObject GlobalSolver::aTrigger = MObject::kNullObj;
MObject GlobalSolver::aSimulateFunction = MObject::kNullObj;
std::unordered_map<GlobalSolver::BufferType, ComPtr<ID3D11Buffer>> GlobalSolver::buffers;
std::unordered_map<uint, std::function<void()>> GlobalSolver::pbdSimulateFuncs;
ColliderBuffer GlobalSolver::colliderBuffer;
std::unordered_set<int> GlobalSolver::dirtyColliderIndices;
MTime GlobalSolver::lastComputeTime = MTime();

GlobalSolver::~GlobalSolver() {
    // As with other Maya nodes, preRemovalCallback is not always called (e.g. on a new scene load), so also do cleanup here.
    MMessage::removeCallbacks(callbackIds);
    unsubscribeFromDragStateChange();
    tearDown();
}

const MObject& GlobalSolver::getOrCreateGlobalSolver() {
    if (!globalSolverNodeObject.isNull()) {
        return globalSolverNodeObject;
    }

    globalSolverNodeObject = Utils::createDGNode(GlobalSolver::globalSolverNodeName);
    Utils::connectPlugs(Utils::getGlobalTimePlug(), MPlug(globalSolverNodeObject, aTime));
    lastComputeTime = MAnimControl::currentTime(); // Not safe to read plug value during node creation

    return globalSolverNodeObject;
}

void GlobalSolver::postConstructor() {
    MPxNode::postConstructor();
    setExistWithoutOutConnections(true);
    MStatus status;
    
    unsubscribeFromDragStateChange = VoxelDragContext::subscribeToDragStateChange([this](const DragState& dragState) {
        isDragging = dragState.isDragging;
    });

    MCallbackId callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onParticleDataConnectionChange, this, &status);
    callbackIds.append(callbackId);
    callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onSimulateFunctionConnectionChange, this, &status);
    callbackIds.append(callbackId);
    callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onColliderDataConnectionChange, this, &status);
    callbackIds.append(callbackId);
    callbackId = MNodeMessage::addNodeDirtyPlugCallback(thisMObject(), onColliderDataDirty, this);
    callbackIds.append(callbackId);

    // Effectively a destructor callback to clean up when the node is deleted
    // This is more reliable than a destructor, because Maya won't necessarily call destructors on node deletion (unless undo queue is flushed)
    callbackId = MNodeMessage::addNodePreRemovalCallback(thisMObject(), [](MObject& node, void* clientData) {
        GlobalSolver* globalSolver = static_cast<GlobalSolver*>(clientData);
        MMessage::removeCallbacks(globalSolver->callbackIds);
        globalSolver->unsubscribeFromDragStateChange();
        tearDown();
    }, this, &status);
    callbackIds.append(callbackId);
}

void GlobalSolver::maybeDeleteGlobalSolver() {
    MObject globalSolverObj = getOrCreateGlobalSolver();
    if (Utils::arrayPlugNumElements(globalSolverObj, aParticleData) > 0) return;
    if (Utils::arrayPlugNumElements(globalSolverObj, aColliderData) > 0) return;

    // Delete must happen on idle (node cannot delete itself from a callback)
    MGlobal::executeCommandOnIdle("delete " + MFnDependencyNode(globalSolverObj).name());
}

void GlobalSolver::tearDown() {
    pbdSimulateFuncs.clear();
    lastComputeTime = MTime();
    for (auto& buffer : buffers) {
        if (buffer.second) {
            DirectX::notifyMayaOfMemoryUsage(buffer.second);
            buffer.second.Reset();
        }
    }
    globalSolverNodeObject = MObject::kNullObj;
    colliderBuffer = ColliderBuffer();
    dirtyColliderIndices.clear();
    SimulationCache::instance()->tearDown();
}

// TODO: on file load, we don't want to recreate the buffer for each connection, just once. Is total numConnections known at load?
void GlobalSolver::onParticleDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
    if (plug != aParticleData || !(msg & (MNodeMessage::kConnectionMade | MNodeMessage::kConnectionBroken))) {
        return;
    }
    bool connectionMade = (msg & MNodeMessage::kConnectionMade);
    
    MObject globalSolverObj = getOrCreateGlobalSolver();
    int numPBDNodes = Utils::arrayPlugNumElements(globalSolverObj, aParticleData);
    numPBDNodes -= connectionMade ? 0 : 1; // If disconnecting, the plug is still counted in numElements, so subtract 1.

    std::unordered_map<int, int> offsetForLogicalPlug;
    float maximumParticleRadius = 0;
    calculateNewOffsetsAndParticleRadius(plug, msg, offsetForLogicalPlug, maximumParticleRadius);
    if (connectionMade) {
        addParticleData(plug);
    } else {
        deleteParticleData(plug);
    }

    GlobalSolver* globalSolver = static_cast<GlobalSolver*>(clientData);
    globalSolver->createGlobalComputeShaders(maximumParticleRadius);

    // Set these *after* creating buffers, because doing so triggers each PBD node to go make UAVs / SRVs.
    MPlug particleBufferOffsetArrayPlug(globalSolverObj, aParticleBufferOffset);
    for (const auto& [logicalIndex, offset] : offsetForLogicalPlug) {
        MPlug particleBufferOffsetPlug = particleBufferOffsetArrayPlug.elementByLogicalIndex(logicalIndex);
        particleBufferOffsetPlug.setInt(offset);
    }

    // Now, disconnect parallel-array plug entries associated with this PBD node.
    if (msg & MNodeMessage::kConnectionBroken) {
        uint logicalIndex = plug.logicalIndex();
        Utils::removePlugMultiInstance(particleBufferOffsetArrayPlug, logicalIndex);
        Utils::removePlugMultiInstance(MPlug(globalSolverObj, aSimulateFunction), logicalIndex);
        Utils::removePlugMultiInstance(plug);
    }

    maybeDeleteGlobalSolver();
}

// Updates the offsets into the global buffers for each connected PBD node
// Also finds the maximum particle radius of all connected PBD nodes
// These two operations are done together because they both need to iterate through all connected PBD nodes
void GlobalSolver::calculateNewOffsetsAndParticleRadius(MPlug changedPlug, MNodeMessage::AttributeMessage changeType, std::unordered_map<int, int>& offsetForLogicalPlug, float& maximumParticleRadius)
{
    Utils::PluginData<ParticleData> particleData(changedPlug);
    uint numChangedParticles = particleData.get()->getData().numParticles;
    float particleRadius = particleData.get()->getData().particleRadius;

    // If plug was added, take care of it specially, because it won't be iterated over below.
    if (changeType & MNodeMessage::kConnectionMade) {
        maximumParticleRadius = particleRadius;
        offsetForLogicalPlug[changedPlug.logicalIndex()] = 0; // Newly added plug data is prepended to buffers, so offset is 0.
    }

    MObject globalSolverObj = getOrCreateGlobalSolver();
    MPlug particleDataArrayPlug(globalSolverObj, aParticleData);
    MPlug particleBufferOffsetArrayPlug(globalSolverObj, aParticleBufferOffset);
    int numBufferOffsetPlugs = particleBufferOffsetArrayPlug.evaluateNumElements();

    int offset_i;
    for (int i = 0; i < numBufferOffsetPlugs; ++i) {
        MPlug particleBufferOffsetPlug = particleBufferOffsetArrayPlug.elementByPhysicalIndex(i);        
        uint plugLogicalIndex = particleBufferOffsetPlug.logicalIndex();
        if (plugLogicalIndex == changedPlug.logicalIndex()) continue;

        MPlug particleDataPlug = particleDataArrayPlug.elementByLogicalIndex(plugLogicalIndex); // parallel array, shares logical indices with offset plug array
        Utils::PluginData<ParticleData> currentParticleData(particleDataPlug);

        float currentParticleRadius = currentParticleData.get()->getData().particleRadius;
        maximumParticleRadius = (currentParticleRadius > maximumParticleRadius) ? currentParticleRadius : maximumParticleRadius;

        // Now update offset
        particleBufferOffsetPlug.getValue(offset_i);
        // If a new node was added (prepended), just add its added particles to each offset
        if (changeType & MNodeMessage::kConnectionMade) {
            offsetForLogicalPlug[plugLogicalIndex] = (offset_i + numChangedParticles);
            continue;
        }

        // If the node was deleted and was created after this ith node (i.e. prepended in front of the ith node),
        // subtract the number of removed particles from the ith node's offset.
        if (changedPlug.logicalIndex() > plugLogicalIndex) {
            offsetForLogicalPlug[plugLogicalIndex] = (offset_i - numChangedParticles);
            continue;
        }

        // Otherwise, the offset stays the same
        offsetForLogicalPlug[plugLogicalIndex] = offset_i;
    }
}

// Prepends new particle data (corresponding to a new model) to the particle (and related) buffer(s)
void GlobalSolver::addParticleData(MPlug& particleDataToAddPlug) {
    Utils::PluginData<ParticleData> particleData(particleDataToAddPlug);
    uint totalParticles = getTotalParticles();
    
    std::vector<Particle>* const particles = particleData.get()->getData().particles;
    DirectX::addToBuffer<Particle>(buffers[BufferType::PARTICLE], *particles);
    DirectX::addToBuffer<Particle>(buffers[BufferType::OLDPARTICLE], *particles);

    std::vector<uint>* const surfaceVal = particleData.get()->getData().isSurface;
    DirectX::addToBuffer<uint>(buffers[BufferType::SURFACE], *surfaceVal);

    return;
}

// Deletes a region of particle data (corresponding to a deleted model) from the particle (and related) buffer(s)
void GlobalSolver::deleteParticleData(MPlug& particleDataToRemovePlug) {
    MObject globalSolverObj = getOrCreateGlobalSolver();

    Utils::PluginData<ParticleData> particleData(particleDataToRemovePlug);
    uint totalParticles = getTotalParticles();
    uint numRemovedParticles = particleData.get()->getData().numParticles;

    // Get the removed node's offset into the old particle buffer
    int offset;
    uint plugLogicalIndex = particleDataToRemovePlug.logicalIndex();
    MPlug particleBufferOffsetArrayPlug(globalSolverObj, aParticleBufferOffset);
    MPlug particleBufferOffsetPlug = particleBufferOffsetArrayPlug.elementByLogicalIndex(plugLogicalIndex);
    particleBufferOffsetPlug.getValue(offset);

    DirectX::deleteFromBuffer<MFloatPoint>(buffers[BufferType::PARTICLE], numRemovedParticles, offset);
    DirectX::deleteFromBuffer<MFloatPoint>(buffers[BufferType::OLDPARTICLE], numRemovedParticles, offset);
    DirectX::deleteFromBuffer<uint>(buffers[BufferType::SURFACE], numRemovedParticles / 8, offset / 8);

    return;
}

void GlobalSolver::createGlobalComputeShaders(float maximumParticleRadius) {
    int totalParticles = getTotalParticles();
    int totalVoxels = totalParticles / 8;
    ComPtr<ID3D11ShaderResourceView> particleSRV = DirectX::createSRV(buffers[BufferType::PARTICLE]);
    ComPtr<ID3D11ShaderResourceView> oldParticlesSRV = DirectX::createSRV(buffers[BufferType::OLDPARTICLE]);
    ComPtr<ID3D11UnorderedAccessView> particleUAV = DirectX::createUAV(buffers[BufferType::PARTICLE]);
    ComPtr<ID3D11ShaderResourceView> isSurfaceSRV = DirectX::createSRV(buffers[BufferType::SURFACE]);

    buildCollisionGridCompute = BuildCollisionGridCompute(
        totalParticles,
        maximumParticleRadius // For collision assumptions to work, grid cell must be at least as big as the biggest particle
    );
    buildCollisionGridCompute.setParticlesSRV(particleSRV);
    buildCollisionGridCompute.setIsSurfaceSRV(isSurfaceSRV);

    prefixScanCompute = PrefixScanCompute(
        buildCollisionGridCompute.getCollisionCellParticleCountsUAV()
    );

    buildCollisionParticleCompute = BuildCollisionParticlesCompute(
        totalParticles,
        buildCollisionGridCompute.getCollisionCellParticleCountsUAV(),
        buildCollisionGridCompute.getParticleCollisionCB()
    );
    buildCollisionParticleCompute.setParticlesSRV(particleSRV);
    buildCollisionParticleCompute.setIsSurfaceSRV(isSurfaceSRV);

    solveCollisionsCompute = SolveCollisionsCompute(
        buildCollisionGridCompute.getHashGridSize(),
        buildCollisionParticleCompute.getParticlesByCollisionCellSRV(),
        buildCollisionGridCompute.getCollisionCellParticleCountsSRV(),
        buildCollisionGridCompute.getParticleCollisionCB()
    );
    solveCollisionsCompute.setParticlesUAV(particleUAV);
    solveCollisionsCompute.setOldParticlesSRV(oldParticlesSRV);

    dragParticlesCompute = DragParticlesCompute(totalVoxels);
    dragParticlesCompute.setParticlesUAV(particleUAV);
    buffers[BufferType::DRAGGING] = dragParticlesCompute.getIsDraggingBuffer();

    colliderBuffer.totalParticles = totalParticles;
    solvePrimitiveCollisionsCompute = SolvePrimitiveCollisionsCompute(colliderBuffer);
    solvePrimitiveCollisionsCompute.setParticlesUAV(particleUAV);
    solvePrimitiveCollisionsCompute.setOldParticlesSRV(oldParticlesSRV);
    buffers[BufferType::COLLIDER] = solvePrimitiveCollisionsCompute.getColliderBuffer();
}

void GlobalSolver::onSimulateFunctionConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
    if (plug != GlobalSolver::aSimulateFunction || !(msg & (MNodeMessage::kConnectionMade | MNodeMessage::kConnectionBroken))) {
        return;
    }

    Utils::PluginData<FunctionalData> functionalData(plug);
    if (msg & MNodeMessage::kConnectionMade) {
        pbdSimulateFuncs[plug.logicalIndex()] = functionalData.get()->getFunction();
        return;
    }
    if (msg & MNodeMessage::kConnectionBroken) {
        pbdSimulateFuncs.erase(plug.logicalIndex());
        return;
    }
}

/**
 * Whenever any collider is added or removed, rebuild the entire collider buffer from scratch. Since the amount of data here is very, very small (compared to
 * something like particle data), this is not a big performance concern. (By contrast, for particle data, we append or shift data rather than reconstructing the entire buffer.)
 */
void GlobalSolver::onColliderDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
    if (plug != GlobalSolver::aColliderData || !(msg & (MNodeMessage::kConnectionMade | MNodeMessage::kConnectionBroken))) return;
    bool connectionRemoved = (msg & MNodeMessage::kConnectionBroken);

    MObject globalSolverObj = getOrCreateGlobalSolver();
    MPlug colliderDataArrayPlug(globalSolverObj, aColliderData);
    int numColliders = colliderDataArrayPlug.evaluateNumElements(); // Does not reflect the removed plug yet, if this is a kConnectionBroken callback
    int plugLogicalIndex = plug.logicalIndex();

    if (numColliders > MAX_COLLIDERS) {
        MGlobal::displayError("cubit supports " + MString(std::to_string(MAX_COLLIDERS).c_str()) + " or fewer collider primitives. The added collider will not participate in collisions.");
        return;
    }

    ColliderBuffer newColliderBuffer;
    newColliderBuffer.totalParticles = colliderBuffer.totalParticles;
    
    for (int i = 0; i < numColliders; ++i) {
        MPlug colliderDataPlug = colliderDataArrayPlug.elementByPhysicalIndex(i);
        if (connectionRemoved && colliderDataPlug.logicalIndex() == plugLogicalIndex) {
            // On removal, the removed plug is still in the array at this point. Skip it.
            continue;
        }

        ColliderLocator* colliderLocator = static_cast<ColliderLocator*>(Utils::connectedNode(colliderDataPlug));
        if (!colliderLocator) continue; // Should not happen, but just in case

        Utils::PluginData<ColliderData> colliderData(colliderDataPlug);
        colliderLocator->writeDataIntoBuffer(colliderData.get(), newColliderBuffer);
    }

    colliderBuffer = newColliderBuffer;
    GlobalSolver* globalSolver = static_cast<GlobalSolver*>(clientData);
    globalSolver->solvePrimitiveCollisionsCompute.updateColliderBuffer(colliderBuffer);
    
    // Finally, remove the disconnected plug from the array
    if (connectionRemoved) Utils::removePlugMultiInstance(plug);
    maybeDeleteGlobalSolver();
}

void GlobalSolver::onColliderDataDirty(MObject& node, MPlug& plug, void* clientData) {
    if (plug != aColliderData) return;
    if (plug.isArray()) {
        // If the parent array plug is dirty, mark all elements dirty.
        // Unforunately, this is the case when animating a collider. Maya marks the parent dirty rather than the child.
        // TODO: look for way to improve this.
        for (unsigned int i = 0; i < plug.evaluateNumElements(); ++i) {
            dirtyColliderIndices.insert(plug.elementByPhysicalIndex(i).logicalIndex());
        }
        return;
    }

    dirtyColliderIndices.insert(plug.logicalIndex());
}

MStatus GlobalSolver::initialize() {
    MStatus status;

    // User-set attributes
    MFnNumericAttribute nAttr;
    aNumSubsteps = nAttr.create("numSubsteps", "nss", MFnNumericData::kInt, 10, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(1);
    nAttr.setSoftMin(5);
    nAttr.setSoftMax(20);
    nAttr.setMax(30);
    nAttr.setStorable(true);
    nAttr.setWritable(true);
    nAttr.setReadable(true);
    status = addAttribute(aNumSubsteps);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MFnNumericAttribute nBoolAttr;
    aParticleCollisionsEnabled = nBoolAttr.create("particleCollisionsEnabled", "pce", MFnNumericData::kBoolean, true, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nBoolAttr.setStorable(true);
    nBoolAttr.setWritable(true);
    nBoolAttr.setReadable(true);
    status = addAttribute(aParticleCollisionsEnabled);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    aPrimitiveCollisionsEnabled = nBoolAttr.create("primitiveCollisionsEnabled", "pre", MFnNumericData::kBoolean, true, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nBoolAttr.setStorable(true);
    nBoolAttr.setWritable(true);
    nBoolAttr.setReadable(true);
    status = addAttribute(aPrimitiveCollisionsEnabled);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MFnNumericAttribute nFloatAttr;
    aParticleFriction = nFloatAttr.create("particleFriction", "pf", MFnNumericData::kFloat, 0.5f, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nFloatAttr.setMin(0.0f);
    nFloatAttr.setMax(1.0f);
    nFloatAttr.setStorable(true);
    nFloatAttr.setWritable(true);
    nFloatAttr.setReadable(true);
    status = addAttribute(aParticleFriction);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // Input attribute
    // Time attribute
    MFnUnitAttribute uTimeAttr;
    aTime = uTimeAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    uTimeAttr.setStorable(false);
    uTimeAttr.setWritable(true);
    uTimeAttr.setReadable(false);
    status = addAttribute(aTime);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // Contains pointer to particle data
    // NOTE: cannot use kDelete on disconnect behavior, because callback needs to copy data before plug is deleted.
    MFnTypedAttribute tAttr;
    aParticleData = tAttr.create("particledata", "ptd", ParticleData::id, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tAttr.setStorable(false);
    tAttr.setWritable(true);
    tAttr.setReadable(false);
    tAttr.setArray(true);
    status = addAttribute(aParticleData);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // Simulation function attribute
    aSimulateFunction = tAttr.create("simulatefunction", "ssf", FunctionalData::id, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tAttr.setStorable(false);
    tAttr.setWritable(true);
    tAttr.setReadable(false);
    tAttr.setArray(true);
    status = addAttribute(aSimulateFunction);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // Static collider primitives (spheres, boxes, planes)
    aColliderData = tAttr.create("colliderdata", "cld", ColliderData::id, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tAttr.setStorable(false);
    tAttr.setWritable(true);
    tAttr.setReadable(false);
    tAttr.setArray(true);
    status = addAttribute(aColliderData);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // Output attribute
    // Trigger - tells PBD nodes to propogate changes to their deformers
    aTrigger = nAttr.create("trigger", "trg", MFnNumericData::kBoolean, false, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setStorable(false);
    nAttr.setWritable(false);
    nAttr.setReadable(true);
    status = addAttribute(aTrigger);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // Tells PBD nodes where in the global particle buffer their particles start
    aParticleBufferOffset = nAttr.create("particlebufferoffset", "pbo", MFnNumericData::kInt, -1, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setStorable(false);
    nAttr.setWritable(false);
    nAttr.setReadable(true);
    nAttr.setArray(true);
    status = addAttribute(aParticleBufferOffset);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = attributeAffects(aTime, aTrigger);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MS::kSuccess;
}

/**
 * On each time change, run the simulate step function for each PBD node connected to the global solver.
 * This is probably not how the DG is supposed to be used, but the alternative is using plugs to communicate back-and-forth
 * many times per time step, which is complicated and likely slow.
 */
MStatus GlobalSolver::compute(const MPlug& plug, MDataBlock& block) 
{
    if (plug != aTrigger) return MS::kSuccess;

    if (dirtyColliderIndices.size() > 0) {
        MArrayDataHandle colliderDataArrayHandle = block.inputArrayValue(aColliderData);
        MPlug colliderDataArrayPlug(getOrCreateGlobalSolver(), aColliderData);
        int numElements = colliderDataArrayPlug.numElements();

        for (int i = 0; i < numElements; ++i) {
            MPlug colliderDataPlug = colliderDataArrayPlug.elementByPhysicalIndex(i);
            int logicalIndex = colliderDataPlug.logicalIndex();
            if (dirtyColliderIndices.find(logicalIndex) == dirtyColliderIndices.end()) {
                continue;
            }

            colliderDataArrayHandle.jumpToElement(logicalIndex);
            MDataHandle colliderDataHandle = colliderDataArrayHandle.inputValue();
            ColliderData* colliderData = static_cast<ColliderData*>(colliderDataHandle.asPluginData());
            
            ColliderLocator* colliderLocator = static_cast<ColliderLocator*>(Utils::connectedNode(colliderDataPlug));
            if (!colliderLocator) continue; // Should not happen, but just in case
            colliderLocator->writeDataIntoBuffer(colliderData, colliderBuffer, i);
        }

        solvePrimitiveCollisionsCompute.updateColliderBuffer(colliderBuffer);
        dirtyColliderIndices.clear();
    }

    // Sometimes aTrigger gets triggered even when time has not explicitly changed (like on initialization)
    // To guard against that, cache off time on each compute and compare to last.
    MTime time = block.inputValue(aTime).asTime();
    if (time == lastComputeTime) {
        return MS::kSuccess;
    }
    lastComputeTime = time;

    bool particleCollisionsEnabled = block.inputValue(aParticleCollisionsEnabled).asBool();
    bool primitiveCollisionsEnabled = block.inputValue(aPrimitiveCollisionsEnabled).asBool();
    float particleFriction = block.inputValue(aParticleFriction).asFloat();
    buildCollisionGridCompute.setFriction(particleFriction);
    int substeps = block.inputValue(aNumSubsteps).asInt();
    dragParticlesCompute.setNumSubsteps(substeps);

    for (int i = 0; i < substeps; ++i) {
        for (const auto& [j, pbdSimulateFunc] : pbdSimulateFuncs) {
            pbdSimulateFunc();
        }

        if (isDragging) {
            dragParticlesCompute.dispatch();
        }   

        if (particleCollisionsEnabled) {
            buildCollisionGridCompute.dispatch();
            prefixScanCompute.dispatch(); 
            buildCollisionParticleCompute.dispatch();
            solveCollisionsCompute.dispatch();
        }

        if (primitiveCollisionsEnabled) {
            solvePrimitiveCollisionsCompute.dispatch();
        }
    }

    SimulationCache::instance()->cacheData(time);
    return MS::kSuccess;
}