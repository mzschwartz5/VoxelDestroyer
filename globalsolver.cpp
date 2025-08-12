#include "globalsolver.h"
#include <maya/MGlobal.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MDGModifier.h>
#include <maya/MFnPluginData.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MItDependencyNodes.h>
#include "glm/glm.hpp"
#include "custommayaconstructs/voxeldeformerGPUNode.h"
#include "custommayaconstructs/voxeldragcontext.h"

const MTypeId GlobalSolver::id(0x0013A7B1);
const MString GlobalSolver::globalSolverNodeName("globalSolverNode");
MObject GlobalSolver::globalSolverNodeObject = MObject::kNullObj;
MObject GlobalSolver::aParticleData = MObject::kNullObj;
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
MInt64 GlobalSolver::heldMemory = 0;
int GlobalSolver::numPBDNodes = 0;


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
    callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onSimulateFunctionConnectionChange, this, &status);
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
    MRenderer::theRenderer()->releaseGPUMemory(heldMemory);
    heldMemory = 0;
    pbdSimulateFuncs.clear();
    numPBDNodes = 0;
    for (auto& buffer : buffers) {
        if (buffer.second) {
            buffer.second.Reset();
        }
    }
    globalSolverNodeObject = MObject::kNullObj;
}

/**
 * Logical indices are sparse, mapped to contiguous physical indices.
 * This method finds the next available logical index for creating a new particle data plug in the array.
 */
uint GlobalSolver::getNextParticleDataPlugIndex() {
    MStatus status;
    MFnDependencyNode globalSolverNode(globalSolverNodeObject, &status);
    MPlug particleDataArrayPlug = globalSolverNode.findPlug(aParticleData, false, &status);

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

// TODO: on file load, we don't want to recreate the buffer for each connection, just once. Is total numConnections known at load?
void GlobalSolver::onParticleDataConnectionChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
    if (plug != aParticleData || !(msg & (MNodeMessage::kConnectionMade | MNodeMessage::kConnectionBroken))) {
        return;
    }

    MFnDependencyNode globalSolverNode(getOrCreateGlobalSolver());
    MPlug particleDataArrayPlug = globalSolverNode.findPlug(aParticleData, false);
    numPBDNodes = particleDataArrayPlug.numElements();

    if ((msg & MNodeMessage::kConnectionBroken) && numPBDNodes == 0) {
        MGlobal::executeCommandOnIdle("delete " + globalSolverNode.name(), false);
        return;
    }

    // Collect all particles from all PBD nodes into one vector to copy to the GPU.
    // Same for the isSurface buffer values
    int totalParticles = 0;
    float maximumParticleRadius = 0.0f;
    std::vector<glm::vec4> allParticlePositions;
    std::vector<uint> isSurface;
    std::vector<int> offsets(numPBDNodes);

    MObject particleDataObj;
    MFnPluginData pluginDataFn; 
    MStatus status;
    for (int i = 0; i < numPBDNodes; ++i) {
        offsets[i] = totalParticles;

        MPlug particleDataPlug = particleDataArrayPlug.elementByPhysicalIndex(i);
        status = particleDataPlug.getValue(particleDataObj);
        pluginDataFn.setObject(particleDataObj);
        ParticleData* particleData = static_cast<ParticleData*>(pluginDataFn.data(&status));
        
        const glm::vec4* positions = particleData->getData().particlePositionsCPU;
        const uint* surfaceVal = particleData->getData().isSurface;
        uint numParticles = particleData->getData().numParticles;
        uint numVoxels = numParticles / 8;
        float particleRadius = particleData->getData().particleRadius;

        allParticlePositions.insert(allParticlePositions.end(), positions, positions + numParticles);
        isSurface.insert(isSurface.end(), surfaceVal, surfaceVal + numVoxels);
        totalParticles += numParticles;
        maximumParticleRadius = std::max(maximumParticleRadius, particleRadius);
    }

    createBuffer<glm::vec4>(allParticlePositions, buffers[BufferType::PARTICLE]);
    createBuffer<uint>(isSurface, buffers[BufferType::SURFACE]);
    VoxelDeformerGPUNode::initGlobalParticlesBuffer(buffers[BufferType::PARTICLE]);

    GlobalSolver* globalSolver = static_cast<GlobalSolver*>(clientData);
    globalSolver->createGlobalComputeShaders(totalParticles, maximumParticleRadius);

    // Set these *after* creating buffers, because doing so triggers each PBD node to go make UAVs / SRVs.
    MPlug particleBufferOffsetArrayPlug = globalSolverNode.findPlug(aParticleBufferOffset, false);
    for (int i = 0; i < numPBDNodes; ++i) {
        // Set the offset for this PBD node in the global particle buffer
        MPlug particleBufferOffsetPlug = particleBufferOffsetArrayPlug.elementByLogicalIndex(i);
        particleBufferOffsetPlug.setValue(offsets[i]);
    }
}

void GlobalSolver::createGlobalComputeShaders(int totalParticles, float maximumParticleRadius) {
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

    dragParticlesCompute = DragParticlesCompute(
        totalVoxels,
        SUBSTEPS
    );
    dragParticlesCompute.setParticlesUAV(particleUAV);
    buffers[BufferType::DRAGGING] = dragParticlesCompute.getIsDraggingBuffer();
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
    if (!(msg & MNodeMessage::kConnectionBroken)) {
        pbdSimulateFuncs.erase(plug.logicalIndex());
        return;
    }
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

    // Contains pointer to particle data and number of particles
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
    
    for (int i = 0; i < SUBSTEPS; ++i) {
        for (int j = 0; j < numPBDNodes; ++j) {
            auto pbdSimFuncIt = pbdSimulateFuncs.find(j);
            if (pbdSimFuncIt == pbdSimulateFuncs.end()) continue;
    
            // Call the simulate function for this PBD node
            pbdSimFuncIt->second(); 
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