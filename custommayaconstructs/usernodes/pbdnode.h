#pragma once

#include "../../voxelizer.h"
#include "../../pbd.h"
#include "../../globalsolver.h"
#include "../data/voxeldata.h"
#include "../data/particledata.h"
#include "../data/functionaldata.h"
#include "../data/d3d11data.h"
#include "../usernodes/voxelizernode.h"
#include "../../utils.h"

#include <vector>
#include <array>

#include <maya/MGlobal.h>
#include <maya/MPxNode.h>
#include <maya/MDagPath.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MPlug.h>
#include <maya/MStatus.h>
#include <maya/MNodeMessage.h>
#include <maya/MDataBlock.h>
#include <maya/MCallbackIdArray.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>

class PBDNode : public MPxNode
{
public:
    inline static const MString pbdNodeName{"PBD"};
    inline static const MTypeId id{0x0013A7B0};
    // Attributes
    inline static MObject aMeshOwner;
    // Inputs
    inline static MObject aTriggerIn;
    inline static MObject aVoxelDataIn;
    inline static MObject aParticleBufferOffset;
    // Output
    inline static MObject aTriggerOut;
    inline static MObject aVoxelDataOut;
    inline static MObject aParticleData;
    inline static MObject aParticleSRV;
    inline static MObject aSimulateSubstepFunction;
    
    PBDNode() = default;
    ~PBDNode() override = default;
    // Functions for Maya to create and initialize the node
    static void* creator() { return new PBDNode(); }
    
    static MStatus initialize() {
        MStatus status;

        // Special message attribute for associating a PBD node with a mesh (for lifetime management)
        MFnMessageAttribute mAttr;
        aMeshOwner = mAttr.create("mesh", "msh", &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        mAttr.setStorable(true);
        mAttr.setWritable(true);
        mAttr.setReadable(false);
        addAttribute(aMeshOwner);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Input attribute for GlobalSolver to trigger updates
        MFnNumericAttribute nAttr;
        aTriggerIn = nAttr.create("triggerin", "tgi", MFnNumericData::kBoolean, false, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setStorable(false);
        nAttr.setWritable(true);
        nAttr.setReadable(false);
        status = addAttribute(aTriggerIn);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Output attribute to trigger downstream updates
        aTriggerOut = nAttr.create("triggerout", "tgo", MFnNumericData::kBoolean, false, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setStorable(false);
        nAttr.setWritable(false);
        nAttr.setReadable(true);
        status = addAttribute(aTriggerOut);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        
        // Voxel data input attribute
        MFnTypedAttribute tVoxelDataAttr;
        aVoxelDataIn = tVoxelDataAttr.create("voxeldatain", "vxdi", VoxelData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tVoxelDataAttr.setCached(false);
        tVoxelDataAttr.setStorable(true);
        tVoxelDataAttr.setWritable(true);
        status = addAttribute(aVoxelDataIn);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aVoxelDataOut = tVoxelDataAttr.create("voxeldataout", "vxdo", VoxelData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tVoxelDataAttr.setWritable(false);
        tVoxelDataAttr.setReadable(true);
        status = addAttribute(aVoxelDataOut);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        
        // Output particle data to facilitate GPU buffer resource initialization in the GPU deformer override
        MFnTypedAttribute tParticleDataAttr;
        aParticleData = tParticleDataAttr.create("particledata", "ptd", ParticleData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tParticleDataAttr.setStorable(false); // NOT storable - just for initialization
        tParticleDataAttr.setWritable(false);
        tParticleDataAttr.setReadable(true); 
        status = addAttribute(aParticleData);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Output simulateSubstep function for GlobalSolver to use
        MFnTypedAttribute tSimulateSubstepAttr;
        aSimulateSubstepFunction = tSimulateSubstepAttr.create("simulatesubstepfunc", "ssf", FunctionalData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tSimulateSubstepAttr.setStorable(false);
        tSimulateSubstepAttr.setWritable(false);
        tSimulateSubstepAttr.setReadable(true);
        status = addAttribute(aSimulateSubstepFunction);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Particle buffer offset tells PBD node and deformer node where in the global particle buffer its particles start
        MFnNumericAttribute nParticleBufferOffsetAttr;
        aParticleBufferOffset = nParticleBufferOffsetAttr.create("particlebufferoffset", "pbo", MFnNumericData::kInt, -1, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nParticleBufferOffsetAttr.setStorable(false);
        nParticleBufferOffsetAttr.setWritable(true);
        nParticleBufferOffsetAttr.setReadable(false);
        status = addAttribute(aParticleBufferOffset);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        MFnTypedAttribute tParticleSRVAttr;
        aParticleSRV = tParticleSRVAttr.create("particleSRV", "psrv", D3D11Data::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tParticleSRVAttr.setStorable(false);
        tParticleSRVAttr.setWritable(false);
        tParticleSRVAttr.setReadable(true);
        status = addAttribute(aParticleSRV);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        status = attributeAffects(aTriggerIn, aTriggerOut);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        status = attributeAffects(aParticleBufferOffset, aParticleSRV);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        status = attributeAffects(aVoxelDataIn, aVoxelDataOut);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        return MS::kSuccess;
    }
    
    static MObject createPBDNode(const MObject& voxelizerNode, const MDagPath& meshDagPath) {
        MObject pbdNodeObj = Utils::createDGNode(PBDNode::pbdNodeName);
        Utils::connectPlugs(pbdNodeObj, aMeshOwner, meshDagPath.node(), MPxNode::message);
        Utils::connectPlugs(voxelizerNode, VoxelizerNode::aVoxelData, pbdNodeObj, aVoxelDataIn);

        // Connect the particle data attribute output to the global solver node's particle data (array) input.
        // And connect the particle buffer offset attribute to the global solver node's particle buffer offset (array) output.
        MObject globalSolverNodeObj = GlobalSolver::getOrCreateGlobalSolver();
        uint plugIndex = Utils::getNextArrayPlugIndex(globalSolverNodeObj, GlobalSolver::aParticleData);
        // Note: the GlobalSolver operates on the assumption that the buffer offset plug is connected before the particle data plug
        Utils::connectPlugs(globalSolverNodeObj, GlobalSolver::aTrigger, pbdNodeObj, aTriggerIn);
        Utils::connectPlugs(pbdNodeObj, aSimulateSubstepFunction, globalSolverNodeObj, GlobalSolver::aSimulateFunction, -1, plugIndex);
        Utils::connectPlugs(globalSolverNodeObj, GlobalSolver::aParticleBufferOffset, pbdNodeObj, aParticleBufferOffset, plugIndex, -1);
        Utils::connectPlugs(pbdNodeObj, aParticleData, globalSolverNodeObj, GlobalSolver::aParticleData, -1, plugIndex);
        return pbdNodeObj;
    }
    
    void postConstructor() override {
        MStatus status;
        MPxNode::postConstructor();
        
        MCallbackId callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onVoxelDataConnected, this, &status);
        callbackIds.append(callbackId);

        callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onMeshConnectionDeleted, this, &status);
        callbackIds.append(callbackId);

        // Effectively a destructor callback to clean up when the node is deleted
        // This is more reliable than a destructor, because Maya won't necessarily call destructors on node deletion (unless undo queue is flushed)
        callbackId = MNodeMessage::addNodePreRemovalCallback(thisMObject(), [](MObject& node, void* clientData) {
            PBDNode* pbdNode = static_cast<PBDNode*>(clientData);
            MMessage::removeCallbacks(pbdNode->callbackIds);
        }, this, &status);
        callbackIds.append(callbackId);
    }
    
    MPxNode::SchedulingType schedulingType() const override {
        // Evaluated serially amongst nodes of the same type
        // Necessary because Maya provides a single threaded D3D11 device
        return MPxNode::kGloballySerial; 
    }
    
    static void onVoxelDataConnected(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
        // Only respond to changes to the voxel data attribute
        if (plug != aVoxelDataIn || !(msg & MNodeMessage::kConnectionMade)) {
            return;
        }

        Utils::PluginData<VoxelData> voxelData(plug);
        MSharedPtr<Voxels> voxels = voxelData.get()->getVoxels();

        PBDNode* pbdNode = static_cast<PBDNode*>(clientData);
        PBD& pbd = pbdNode->pbd;
        pbd.setRadiusAndVolumeFromLength(voxels->voxelSize);
        ParticleDataContainer particleDataContainer = pbd.createParticles(voxels);

        Utils::createPluginData<ParticleData>(
            pbdNode->thisMObject(),
            aParticleData,
            [&particleDataContainer](ParticleData* particleData) {
                particleData->setData(particleDataContainer);
            }
        );
        
        std::array<std::vector<FaceConstraint>, 3> faceConstraints 
            = pbd.constructFaceToFaceConstraints(voxels, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX);
        
        pbd.createComputeShaders(voxels, faceConstraints);

        Utils::createPluginData<FunctionalData>(
            pbdNode->thisMObject(),
            aSimulateSubstepFunction,
            [&pbd](FunctionalData* functionalData) {
                functionalData->setFunction([&pbd]() { pbd.simulateSubstep(); });
            }
        );
    }
    
    static void onMeshConnectionDeleted(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
        if (plug != aMeshOwner || !(msg & MNodeMessage::kConnectionBroken)) {
            return;
        }

        PBDNode* pbdNode = static_cast<PBDNode*>(clientData);
        MObject pbdNodeObj = pbdNode->thisMObject();
        if (pbdNodeObj.isNull()) return;

        // On idle - don't want to delete the node while it's processing graph connection changes.
        MGlobal::executeCommandOnIdle("delete " + pbdNode->name(), false);
    }
    
private:
    PBD pbd;
    MCallbackIdArray callbackIds;

    void onParticleBufferOffsetChanged(int particleBufferOffset, MDataBlock& dataBlock) {
        int numberParticles = pbd.numParticles();
        int voxelOffset = particleBufferOffset / 8;
        int numVoxels = numberParticles / 8;
        ComPtr<ID3D11UnorderedAccessView> particleUAV = DirectX::createUAV(GlobalSolver::getBuffer(GlobalSolver::BufferType::PARTICLE), numberParticles, particleBufferOffset);
        ComPtr<ID3D11UnorderedAccessView> oldParticlesUAV = DirectX::createUAV(GlobalSolver::getBuffer(GlobalSolver::BufferType::OLDPARTICLE), numberParticles, particleBufferOffset);
        ComPtr<ID3D11ShaderResourceView> particleSRV = DirectX::createSRV(GlobalSolver::getBuffer(GlobalSolver::BufferType::PARTICLE), numberParticles, particleBufferOffset);
        ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV = DirectX::createUAV(GlobalSolver::getBuffer(GlobalSolver::BufferType::SURFACE), numVoxels, voxelOffset);
        ComPtr<ID3D11ShaderResourceView> isDraggingSRV = DirectX::createSRV(GlobalSolver::getBuffer(GlobalSolver::BufferType::DRAGGING), numVoxels, voxelOffset);

        pbd.setGPUResourceHandles(particleUAV, oldParticlesUAV, isSurfaceUAV, isDraggingSRV);

        Utils::createPluginData<D3D11Data>(
            dataBlock,
            aParticleSRV,
            [&particleSRV](D3D11Data* d3d11Data) {
                d3d11Data->setSRV(particleSRV);
            }
        );

        pbd.setInitialized(true);
    }

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override {
        if (plug == aVoxelDataOut) {
            MDataHandle inHandle = dataBlock.inputValue(aVoxelDataIn);
            MObject voxelDataObj = inHandle.data();

            MDataHandle outHandle = dataBlock.outputValue(aVoxelDataOut);
            outHandle.set(voxelDataObj);
            outHandle.setClean();
            return MS::kSuccess;
        }

        if (plug == aParticleSRV) {
            int particleBufferOffset = dataBlock.inputValue(aParticleBufferOffset).asInt();
            onParticleBufferOffsetChanged(particleBufferOffset, dataBlock);
            return MS::kSuccess;
        }

        return MS::kUnknownParameter;
    }

};