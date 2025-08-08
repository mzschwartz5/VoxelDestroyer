#include "globalsolver.h"
#include <maya/MGlobal.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MDGModifier.h>
#include <maya/MFnPluginData.h>
#include "glm/glm.hpp"
#include "custommayaconstructs/voxeldeformerGPUNode.h"

const MTypeId GlobalSolver::id(0x0013A7B1);
const MString GlobalSolver::globalSolverNodeName("globalSolverNode");
MObject GlobalSolver::globalSolverNodeObject = MObject::kNullObj;
MObject GlobalSolver::aParticleData = MObject::kNullObj;
MObject GlobalSolver::aParticleBufferOffset = MObject::kNullObj;
ComPtr<ID3D11Buffer> GlobalSolver::particleBuffer = nullptr;
MInt64 GlobalSolver::heldMemory = 0;


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

void GlobalSolver::tearDown() {
    MRenderer::theRenderer()->releaseGPUMemory(heldMemory);
    heldMemory = 0;
    particleBuffer.Reset();
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
    if (plug != aParticleData || !(msg & MNodeMessage::kConnectionMade || msg & MNodeMessage::kConnectionBroken)) {
        return;
    }
    MFnDependencyNode globalSolverNode(getMObject());
    MPlug particleDataArrayPlug = globalSolverNode.findPlug(aParticleData, false);
    MPlug particleBufferOffsetArrayPlug = globalSolverNode.findPlug(aParticleBufferOffset, false);

    // Collect all particles from all PBD nodes into one vector to copy to the GPU.
    int totalParticles = 0;
    std::vector<glm::vec4> allParticlePositions;

    uint numElements = particleDataArrayPlug.numElements();
    MObject particleDataObj;
    for (uint i = 0; i < numElements; ++i) {
        // Set the offset for this PBD node in the global particle buffer
        MPlug particleBufferOffsetPlug = particleBufferOffsetArrayPlug.elementByLogicalIndex(i);
        particleBufferOffsetPlug.setValue(totalParticles);

        // Collect the particle data from the PBD node
        MPlug particleDataPlug = particleDataArrayPlug.elementByPhysicalIndex(i);
        MStatus status = plug.getValue(particleDataObj);
        MFnPluginData pluginDataFn(particleDataObj, &status);
        ParticleData* particleData = static_cast<ParticleData*>(pluginDataFn.data(&status));
        const glm::vec4* positions = particleData->getData().particlePositionsCPU;
        uint numParticles = particleData->getData().numParticles;

        allParticlePositions.insert(allParticlePositions.end(), positions, positions + numParticles);
        totalParticles += numParticles;
    }

    createParticleBuffer(allParticlePositions);
}

void GlobalSolver::createParticleBuffer(const std::vector<glm::vec4>& particlePositions) {
    D3D11_BUFFER_DESC bufferDesc = {};
    D3D11_SUBRESOURCE_DATA initData = {};

    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = particlePositions.size() * sizeof(glm::vec4); // glm::vec4 for alignment
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(glm::vec4); // Size of each element in the buffer

    initData.pSysMem = particlePositions.data();
    DirectX::getDevice()->CreateBuffer(&bufferDesc, &initData, particleBuffer.GetAddressOf());

    MRenderer::theRenderer()->holdGPUMemory(bufferDesc.ByteWidth);
    heldMemory += bufferDesc.ByteWidth;

    VoxelDeformerGPUNode::initGlobalParticlesBuffer(particleBuffer);
}

ComPtr<ID3D11UnorderedAccessView> GlobalSolver::createParticleUAV(uint offset, uint numElements) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.FirstElement = offset;
    uavDesc.Buffer.NumElements = numElements;

    ComPtr<ID3D11UnorderedAccessView> particleUAV;
    DirectX::getDevice()->CreateUnorderedAccessView(particleBuffer.Get(), &uavDesc, particleUAV.GetAddressOf());
    return particleUAV;
}

ComPtr<ID3D11ShaderResourceView> GlobalSolver::createParticleSRV(uint offset, uint numElements) {
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.FirstElement = offset;
    srvDesc.Buffer.NumElements = numElements;

    ComPtr<ID3D11ShaderResourceView> particleSRV;
    DirectX::getDevice()->CreateShaderResourceView(particleBuffer.Get(), &srvDesc, particleSRV.GetAddressOf());
    return particleSRV;
}

MStatus GlobalSolver::initialize() {
    MStatus status;

    // Input attribute
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

    // Output attribute
    // Tells PBD nodes where in the global particle buffer their particles start
    MFnNumericAttribute nAttr;
    aParticleBufferOffset = nAttr.create("particlebufferoffset", "pbo", MFnNumericData::kInt, -1, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setStorable(false);
    nAttr.setWritable(false);
    nAttr.setReadable(true);
    nAttr.setArray(true);
    status = addAttribute(aParticleBufferOffset);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MS::kSuccess;
}

// See note in header
MStatus GlobalSolver::compute(const MPlug& plug, MDataBlock& block) {
    return MS::kSuccess;
}

