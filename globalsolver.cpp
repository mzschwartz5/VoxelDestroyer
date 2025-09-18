#include "globalsolver.h"
#include <maya/MGlobal.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MDGModifier.h>
#include <maya/MFnPluginData.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MItDependencyNodes.h>
#include "custommayaconstructs/tools/voxeldragcontext.h"
#include "custommayaconstructs/usernodes/colliderlocator.h"

const MTypeId GlobalSolver::id(0x0013A7B1);
const MString GlobalSolver::globalSolverNodeName("globalSolverNode");
MObject GlobalSolver::globalSolverNodeObject = MObject::kNullObj;
MObject GlobalSolver::aParticleData = MObject::kNullObj;
MObject GlobalSolver::aColliderData = MObject::kNullObj;
MObject GlobalSolver::aParticleBufferOffset = MObject::kNullObj;
MObject GlobalSolver::aTime = MObject::kNullObj;
MObject GlobalSolver::aTrigger = MObject::kNullObj;
MObject GlobalSolver::aSimulateFunction = MObject::kNullObj;
std::unordered_map<GlobalSolver::BufferType, ComPtr<ID3D11Buffer>> GlobalSolver::buffers = {
    { GlobalSolver::BufferType::PARTICLE, nullptr },
    { GlobalSolver::BufferType::SURFACE, nullptr },
    { GlobalSolver::BufferType::DRAGGING, nullptr }
};
std::unordered_map<uint, std::function<void()>> GlobalSolver::pbdSimulateFuncs;
int GlobalSolver::numPBDNodes = 0;
ColliderBuffer GlobalSolver::colliderBuffer;
std::unordered_set<int> GlobalSolver::dirtyColliderIndices;
MTime GlobalSolver::lastComputeTime = MTime();

const MObject& GlobalSolver::getOrCreateGlobalSolver() {
    if (!globalSolverNodeObject.isNull()) {
        return globalSolverNodeObject;
    }

    MStatus status;
    MDGModifier dgMod;
    globalSolverNodeObject = dgMod.createNode(GlobalSolver::globalSolverNodeName, &status);
	dgMod.doIt();

    MPlug timePlug = getGlobalTimePlug();
    MGlobal::executeCommandOnIdle("connectAttr " + timePlug.name() + " " + MFnDependencyNode(globalSolverNodeObject).name() + "." + MFnAttribute(aTime).name(), false);

    return globalSolverNodeObject;
}

MPlug GlobalSolver::getGlobalTimePlug() {
    MItDependencyNodes it(MFn::kTime);
    // Assumes there's only one time node in the scene, which is a pretty safe assumption.
    if (!it.isDone()) {
        MFnDependencyNode timeNode(it.thisNode());
        return timeNode.findPlug("outTime", false);
    }

    return MPlug();
}

void GlobalSolver::postConstructor() {
    MPxNode::postConstructor();
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
}

void GlobalSolver::tearDown() {
    pbdSimulateFuncs.clear();
    numPBDNodes = 0;
    lastComputeTime = MTime();
    for (auto& buffer : buffers) {
        if (buffer.second) {
            resetBuffer(buffer.second);
        }
    }
    globalSolverNodeObject = MObject::kNullObj;
    colliderBuffer = ColliderBuffer();
    dirtyColliderIndices.clear();
}

/**
 * Logical indices are sparse, mapped to contiguous physical indices.
 * This method finds the next available logical index for creating a new plug in the array.
 */
uint GlobalSolver::getNextArrayPlugIndex(MPlug& arrayPlug) {
    MStatus status;

    uint nextIndex = 0;
    const uint numElements = arrayPlug.evaluateNumElements(&status);
    for (uint i = 0; i < numElements; ++i) {
        uint idx = arrayPlug.elementByPhysicalIndex(i, &status).logicalIndex();
        if (idx >= nextIndex) {
            nextIndex = idx + 1;
        }
    }
    return nextIndex;
}

// TODO: on file load, we don't want to recreate the buffer for each connection, just once. Is total numConnections known at load?
void GlobalSolver::onParticleDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
    if (plug != aParticleData || !(msg & (MNodeMessage::kConnectionMade | MNodeMessage::kConnectionBroken))) {
        return;
    }
    
    numPBDNodes += (msg & MNodeMessage::kConnectionMade) ? 1 : -1;
    if (numPBDNodes == 0) {
        // All PBD nodes have been deleted
        // GlobalSolver node will delete itself upon seeing it has no connections
        return;
    }

    std::unordered_map<int, int> offsetForLogicalPlug;
    float maximumParticleRadius = 0;
    calculateNewOffsetsAndParticleRadius(plug, msg, offsetForLogicalPlug, maximumParticleRadius);
    if (msg & MNodeMessage::kConnectionMade) {
        addParticleData(plug);
    } else {
        deleteParticleData(plug);
    }

    GlobalSolver* globalSolver = static_cast<GlobalSolver*>(clientData);
    globalSolver->createGlobalComputeShaders(maximumParticleRadius);

    // Set these *after* creating buffers, because doing so triggers each PBD node to go make UAVs / SRVs.
    MFnDependencyNode globalSolverNode(getOrCreateGlobalSolver());
    MPlug particleBufferOffsetArrayPlug = globalSolverNode.findPlug(aParticleBufferOffset, false);
    for (const auto& [logicalIndex, offset] : offsetForLogicalPlug) {
        MPlug particleBufferOffsetPlug = particleBufferOffsetArrayPlug.elementByLogicalIndex(logicalIndex);
        particleBufferOffsetPlug.setInt(offset);
    }

    // Now, disconnect parallel-array plug entries associated with this PBD node.
    if (msg & MNodeMessage::kConnectionBroken) {
        MDGModifier dgMod;
        uint logicalIndex = plug.logicalIndex();
        MPlug particleBufferOffsetPlug = particleBufferOffsetArrayPlug.elementByLogicalIndex(logicalIndex);
        dgMod.removeMultiInstance(particleBufferOffsetPlug, true);
        
        MPlug simulateFunctionArrayPlug = globalSolverNode.findPlug(aSimulateFunction, false);
        MPlug simulateFunctionPlug = simulateFunctionArrayPlug.elementByLogicalIndex(logicalIndex);
        dgMod.removeMultiInstance(simulateFunctionPlug, true);
        
        dgMod.removeMultiInstance(plug, true);
        dgMod.doIt();
    }
}

// Updates the offsets into the global buffers for each connected PBD node
// Also finds the maximum particle radius of all connected PBD nodes
// These two operations are done together because they both need to iterate through all connected PBD nodes
void GlobalSolver::calculateNewOffsetsAndParticleRadius(MPlug changedPlug, MNodeMessage::AttributeMessage changeType, std::unordered_map<int, int>& offsetForLogicalPlug, float& maximumParticleRadius)
{
    MStatus status;

    // Retrieve data from the changed plug
    MObject particleDataObj;
    MFnPluginData pluginDataFn;
    changedPlug.getValue(particleDataObj);
    pluginDataFn.setObject(particleDataObj);
    ParticleData* particleData = static_cast<ParticleData*>(pluginDataFn.data(&status));
    uint numChangedParticles = particleData->getData().numParticles;
    float particleRadius = particleData->getData().particleRadius;

    // If plug was added, take care of it specially, because it won't be iterated over below.
    if (changeType & MNodeMessage::kConnectionMade) {
        maximumParticleRadius = particleRadius;
        offsetForLogicalPlug[changedPlug.logicalIndex()] = 0; // Newly added plug data is prepended to buffers, so offset is 0.
    }

    MFnDependencyNode globalSolverNode(getOrCreateGlobalSolver());
    MPlug particleDataArrayPlug = globalSolverNode.findPlug(aParticleData, false);
    MPlug particleBufferOffsetArrayPlug = globalSolverNode.findPlug(aParticleBufferOffset, false);
    int numBufferOffsetPlugs = particleBufferOffsetArrayPlug.evaluateNumElements();

    int offset_i;
    for (int i = 0; i < numBufferOffsetPlugs; ++i) {
        MPlug particleBufferOffsetPlug = particleBufferOffsetArrayPlug.elementByPhysicalIndex(i);        
        uint plugLogicalIndex = particleBufferOffsetPlug.logicalIndex();
        if (plugLogicalIndex == changedPlug.logicalIndex()) continue;

        MPlug particleDataPlug = particleDataArrayPlug.elementByLogicalIndex(plugLogicalIndex); // parallel array, shares logical indices with offset plug array

        // Update maximum particle radius
        particleDataPlug.getValue(particleDataObj);
        pluginDataFn.setObject(particleDataObj);
        ParticleData* particleData = static_cast<ParticleData*>(pluginDataFn.data(&status));
        float particleRadius = particleData->getData().particleRadius;
        maximumParticleRadius = (particleRadius > maximumParticleRadius) ? particleRadius : maximumParticleRadius;

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
    MStatus status;
    MFnDependencyNode globalSolverNode(getOrCreateGlobalSolver());
    
    MObject particleDataObj;
    particleDataToAddPlug.getValue(particleDataObj);
    MFnPluginData pluginDataFn(particleDataObj);
    ParticleData* particleData = static_cast<ParticleData*>(pluginDataFn.data(&status));

    const MFloatPoint* positions = particleData->getData().particlePositionsCPU;
    uint numNewParticles = particleData->getData().numParticles;

    // Create new particle buffer big enough for all particles
    int totalParticles = getTotalParticles();
    std::vector<MFloatPoint> paddedParticlePositions(totalParticles + numNewParticles);
    std::copy(positions, positions + numNewParticles, paddedParticlePositions.begin());
    ComPtr<ID3D11Buffer> newParticleBuffer;
    createBuffer<MFloatPoint>(paddedParticlePositions, newParticleBuffer);

    // If any particle data has already been added, combine old buffer into new buffer, offset by numNewParticles
    if (buffers[BufferType::PARTICLE]) {
        copyBufferSubregion<MFloatPoint>(
            buffers[BufferType::PARTICLE], 
            newParticleBuffer, 
            0,
            numNewParticles,
            totalParticles
        );
    }
    buffers[BufferType::PARTICLE] = newParticleBuffer;

    // Create new surface buffer big enough for all voxels
    const uint* surfaceVal = particleData->getData().isSurface;
    uint numNewVoxels = numNewParticles / 8;
    uint totalVoxels = totalParticles / 8;

    std::vector<uint> paddedSurfaceValues(totalVoxels + numNewVoxels);
    std::copy(surfaceVal, surfaceVal + numNewVoxels, paddedSurfaceValues.begin());
    ComPtr<ID3D11Buffer> newSurfaceBuffer;
    createBuffer<uint>(paddedSurfaceValues, newSurfaceBuffer);

    // If any surface data has already been added, combine old buffer into new buffer, offset by numNewVoxels
    if (buffers[BufferType::SURFACE]) {
        copyBufferSubregion<uint>(
            buffers[BufferType::SURFACE],
            newSurfaceBuffer,
            0,
            numNewVoxels,  
            totalVoxels
        );
    }
    buffers[BufferType::SURFACE] = newSurfaceBuffer;

    return;
}

// Deletes a region of particle data (corresponding to a deleted model) from the particle (and related) buffer(s)
void GlobalSolver::deleteParticleData(MPlug& particleDataToRemovePlug) {
    MFnDependencyNode globalSolverNode(getOrCreateGlobalSolver());

    MStatus status;
    MObject particleDataObj;
    particleDataToRemovePlug.getValue(particleDataObj);
    MFnPluginData pluginDataFn(particleDataObj);
    ParticleData* particleData = static_cast<ParticleData*>(pluginDataFn.data(&status));

    // Create new particle buffer sized for the data minus the deleted particles
    uint totalParticles = getTotalParticles();
    uint numRemovedParticles = particleData->getData().numParticles;
    std::vector<MFloatPoint> positions(totalParticles - numRemovedParticles);
    ComPtr<ID3D11Buffer> newParticleBuffer;
    createBuffer<MFloatPoint>(positions, newParticleBuffer);

    // Get the removed node's offset into the old particle buffer
    int offset;
    uint plugLogicalIndex = particleDataToRemovePlug.logicalIndex();
    MPlug particleBufferOffsetArrayPlug = globalSolverNode.findPlug(aParticleBufferOffset, false);
    MPlug particleBufferOffsetPlug = particleBufferOffsetArrayPlug.elementByLogicalIndex(plugLogicalIndex);
    particleBufferOffsetPlug.getValue(offset);

    // Combine the old particle data into the new buffer in (up to) two copies: 
    // the particles before those being removed, and those after.
    if (offset > 0) {
        copyBufferSubregion<MFloatPoint>(
            buffers[BufferType::PARTICLE],
            newParticleBuffer,
            0,             // src copy offset
            0,             // dst copy offset
            offset         // num elements to copy
        );
    }

    if (static_cast<uint>(offset) + numRemovedParticles < totalParticles) {
        copyBufferSubregion<MFloatPoint>(
            buffers[BufferType::PARTICLE],
            newParticleBuffer,
            offset + numRemovedParticles,                   // src copy offset
            offset,                                         // dst copy offset
            totalParticles - (offset + numRemovedParticles) // num elements to copy
        );
    }
    buffers[BufferType::PARTICLE] = newParticleBuffer;

    // Create new surface buffer sized for the data minus the deleted voxels
    uint numRemovedVoxels = numRemovedParticles / 8;
    uint totalVoxels = totalParticles / 8;
    std::vector<uint> surfaceValues(totalVoxels - numRemovedVoxels);
    ComPtr<ID3D11Buffer> newSurfaceBuffer;
    createBuffer<uint>(surfaceValues, newSurfaceBuffer);

    // Combine the old surface data into the new buffer in (up to) two copies:
    // the voxels before those being removed, and those after.
    offset /= 8;
    if (offset > 0) {
        copyBufferSubregion<uint>(
            buffers[BufferType::SURFACE],
            newSurfaceBuffer,
            0,             // src copy offset
            0,             // dst copy offset
            offset         // num elements to copy
        );
    }

    if (static_cast<uint>(offset) + numRemovedVoxels < totalVoxels) {
        copyBufferSubregion<uint>(
            buffers[BufferType::SURFACE],
            newSurfaceBuffer,
            offset + numRemovedVoxels,                // src copy offset
            offset,                                   // dst copy offset
            totalVoxels - (offset + numRemovedVoxels) // num elements to copy
        );
    }
    buffers[BufferType::SURFACE] = newSurfaceBuffer;

    return;
}

void GlobalSolver::createGlobalComputeShaders(float maximumParticleRadius) {
    int totalParticles = getTotalParticles();
    int totalVoxels = totalParticles / 8;
    ComPtr<ID3D11ShaderResourceView> particleSRV = createSRV(0, totalParticles, BufferType::PARTICLE);
    ComPtr<ID3D11UnorderedAccessView> particleUAV = createUAV(0, totalParticles, BufferType::PARTICLE);
    ComPtr<ID3D11ShaderResourceView> isSurfaceSRV = createSRV(0, totalVoxels, BufferType::SURFACE);

    buildCollisionGridCompute = BuildCollisionGridCompute(
        totalParticles,
        maximumParticleRadius // For collision assumptions to work, grid cell must be at least as big as the biggest particle
    );
    buildCollisionGridCompute.setParticlePositionsSRV(particleSRV);
    buildCollisionGridCompute.setIsSurfaceSRV(isSurfaceSRV);

    prefixScanCompute = PrefixScanCompute(
        buildCollisionGridCompute.getCollisionCellParticleCountsUAV()
    );

    buildCollisionParticleCompute = BuildCollisionParticlesCompute(
        totalParticles,
        buildCollisionGridCompute.getCollisionCellParticleCountsUAV(),
        buildCollisionGridCompute.getParticleCollisionCB()
    );
    buildCollisionParticleCompute.setParticlePositionsSRV(particleSRV);
    buildCollisionParticleCompute.setIsSurfaceSRV(isSurfaceSRV);

    solveCollisionsCompute = SolveCollisionsCompute(
        buildCollisionGridCompute.getHashGridSize(),
        buildCollisionParticleCompute.getParticlesByCollisionCellSRV(),
        buildCollisionGridCompute.getCollisionCellParticleCountsSRV(),
        buildCollisionGridCompute.getParticleCollisionCB()
    );
    solveCollisionsCompute.setParticlePositionsUAV(particleUAV);

    dragParticlesCompute = DragParticlesCompute(totalVoxels);
    dragParticlesCompute.setParticlesUAV(particleUAV);
    buffers[BufferType::DRAGGING] = dragParticlesCompute.getIsDraggingBuffer();

    solvePrimitiveCollisionsCompute = SolvePrimitiveCollisionsCompute(totalParticles, colliderBuffer);
    solvePrimitiveCollisionsCompute.setParticlePositionsUAV(particleUAV);
    buffers[BufferType::COLLIDER] = solvePrimitiveCollisionsCompute.getColliderBuffer();
}

void GlobalSolver::onSimulateFunctionConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
    if (plug != GlobalSolver::aSimulateFunction || !(msg & (MNodeMessage::kConnectionMade | MNodeMessage::kConnectionBroken))) {
        return;
    }

    MStatus status;
    MObject functionalDataObj;
    plug.getValue(functionalDataObj);
    MFnPluginData pluginDataFn(functionalDataObj); 
    FunctionalData* functionalData = static_cast<FunctionalData*>(pluginDataFn.data(&status));

    if (msg & MNodeMessage::kConnectionMade) {
        pbdSimulateFuncs[plug.logicalIndex()] = functionalData->getFunction();
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
    if (plug != GlobalSolver::aColliderData || !(msg & (MNodeMessage::kConnectionMade | MNodeMessage::kConnectionBroken))) {
        return;
    }
    bool connectionRemoved = (msg & MNodeMessage::kConnectionBroken);

    MFnDependencyNode globalSolverNode(getOrCreateGlobalSolver());
    MPlug colliderDataArrayPlug = globalSolverNode.findPlug(aColliderData, false);
    int numColliders = colliderDataArrayPlug.evaluateNumElements(); // Does not reflect the removed plug yet, if this is a kConnectionBroken callback
    int plugLogicalIndex = plug.logicalIndex();

    if (numColliders > MAX_COLLIDERS) {
        MGlobal::displayError("Voxel destroyer supports " + MString(std::to_string(MAX_COLLIDERS).c_str()) + " or fewer collider primitives. The added collider will not participate in collisions.");
        return;
    }

    MObject colliderDataObj;
    MFnPluginData colliderDataFn; 
    MPlugArray connectedColliderPlug;
    for (int i = 0; i < numColliders; ++i) {
        MPlug colliderDataPlug = colliderDataArrayPlug.elementByPhysicalIndex(i);
        if (connectionRemoved && colliderDataPlug.logicalIndex() == plugLogicalIndex) {
            // On removal, the removed plug is still in the array at this point. Skip it.
            continue;
        }

        colliderDataPlug.getValue(colliderDataObj);
        colliderDataFn.setObject(colliderDataObj);
        const ColliderData* const colliderData = static_cast<ColliderData*>(colliderDataFn.data());

        colliderDataPlug.connectedTo(connectedColliderPlug, true, false);
        MObject connectedLocatorObj = connectedColliderPlug[0].node();
        MFnDependencyNode connectedLocatorNode(connectedLocatorObj);
        ColliderLocator* colliderLocator = static_cast<ColliderLocator*>(connectedLocatorNode.userNode());

        colliderLocator->writeDataIntoBuffer(colliderData, colliderBuffer);
    }

    GlobalSolver* globalSolver = static_cast<GlobalSolver*>(clientData);
    globalSolver->solvePrimitiveCollisionsCompute.updateColliderBuffer(colliderBuffer);
    
    // Finally, remove the disconnected plug from the array
    if (connectionRemoved) {
        MDGModifier dgMod;
        dgMod.removeMultiInstance(plug, true);
        dgMod.doIt();
    }
}

void GlobalSolver::onColliderDataDirty(MObject& node, MPlug& plug, void* clientData) {
    if (plug != aColliderData) return;
    dirtyColliderIndices.insert(plug.logicalIndex());
}

ComPtr<ID3D11UnorderedAccessView> GlobalSolver::createUAV(uint offset, uint numElements, BufferType bufferType) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.FirstElement = offset;
    uavDesc.Buffer.NumElements = numElements;

    ComPtr<ID3D11UnorderedAccessView> particleUAV;
    ComPtr<ID3D11Buffer> buffer = buffers[bufferType];
    DirectX::getDevice()->CreateUnorderedAccessView(buffer.Get(), &uavDesc, particleUAV.GetAddressOf());
    return particleUAV;
}

ComPtr<ID3D11ShaderResourceView> GlobalSolver::createSRV(uint offset, uint numElements, BufferType bufferType) {
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.FirstElement = offset;
    srvDesc.Buffer.NumElements = numElements;

    ComPtr<ID3D11ShaderResourceView> particleSRV;
    ComPtr<ID3D11Buffer> buffer = buffers[bufferType];
    DirectX::getDevice()->CreateShaderResourceView(buffer.Get(), &srvDesc, particleSRV.GetAddressOf());
    return particleSRV;
}

MStatus GlobalSolver::initialize() {
    MStatus status;

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
    MFnNumericAttribute nAttr;
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
        MPlugArray connectedColliderPlug;
        MArrayDataHandle colliderDataArrayHandle = block.inputArrayValue(aColliderData);
        MPlug colliderDataArrayPlug = MFnDependencyNode(getOrCreateGlobalSolver()).findPlug(aColliderData, false);
        int numElements = colliderDataArrayPlug.numElements();

        for (int i = 0; i < numElements; ++i) {
            int logicalIndex = colliderDataArrayPlug.elementByPhysicalIndex(i).logicalIndex();
            if (dirtyColliderIndices.find(logicalIndex) == dirtyColliderIndices.end()) {
                continue;
            }

            colliderDataArrayHandle.jumpToElement(logicalIndex);
            MDataHandle colliderDataHandle = colliderDataArrayHandle.inputValue();
            ColliderData* colliderData = static_cast<ColliderData*>(colliderDataHandle.asPluginData());
            
            // TODO: consider packing a function in ColliderData to write itself into the buffer, rather than needing to find the locator node here. (How might this be affected by serialization needs though?)
            MPlug colliderDataPlug = colliderDataArrayPlug.elementByLogicalIndex(logicalIndex);
            colliderDataPlug.connectedTo(connectedColliderPlug, true, false);
            MObject connectedLocatorObj = connectedColliderPlug[0].node();
            MFnDependencyNode connectedLocatorNode(connectedLocatorObj);
            ColliderLocator* colliderLocator = static_cast<ColliderLocator*>(connectedLocatorNode.userNode());
            colliderLocator->writeDataIntoBuffer(colliderData, colliderBuffer, i);
        }

        solvePrimitiveCollisionsCompute.updateColliderBuffer(colliderBuffer);
        dirtyColliderIndices.clear();
    }

    // Sometimes aTrigger gets triggered even when time has not explicitly changed.
    // To guard against that, cache off time on each compute and compare to last.
    MTime time = block.inputValue(aTime).asTime();
    if (time == lastComputeTime) {
        return MS::kSuccess;
    }
    lastComputeTime = time;

    for (int i = 0; i < SUBSTEPS; ++i) {
        for (const auto& [j, pbdSimulateFunc] : pbdSimulateFuncs) {
            pbdSimulateFunc();
        }

        if (isDragging) {
            dragParticlesCompute.dispatch();
        }   

        buildCollisionGridCompute.dispatch();
        prefixScanCompute.dispatch(); 
        buildCollisionParticleCompute.dispatch();
        solveCollisionsCompute.dispatch();
    }

    return MS::kSuccess;
}