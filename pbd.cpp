#include "pbd.h"
#include <maya/MGlobal.h>
#include <float.h>
#include "utils.h"
#include "constants.h"
#include "custommayaconstructs/data/voxeldata.h"
#include "custommayaconstructs/data/particledata.h"
#include "custommayaconstructs/data/functionaldata.h"
#include "custommayaconstructs/data/d3d11data.h"
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnPluginData.h>
#include "globalsolver.h"
#include <maya/MFnAttribute.h>

MObject PBD::aMeshOwner;
MObject PBD::aTriggerIn;
MObject PBD::aTriggerOut;
MObject PBD::aVoxelDataIn;
MObject PBD::aVoxelDataOut;
MObject PBD::aParticleData;
MObject PBD::aParticleBufferOffset;
MObject PBD::aParticleSRV;
MObject PBD::aSimulateSubstepFunction;
MTypeId PBD::id(0x0013A7B0);
MString PBD::pbdNodeName("PBD");

MStatus PBD::initialize() {
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

    MFnTypedAttribute tParticleUAVAttr;
    aParticleSRV = tParticleUAVAttr.create("particleUAV", "puav", D3D11Data::id, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tParticleUAVAttr.setStorable(false);
    tParticleUAVAttr.setWritable(false);
    tParticleUAVAttr.setReadable(true);
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

void PBD::postConstructor() {
    MStatus status;
    MPxNode::postConstructor();
    
    MCallbackId callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onVoxelDataSet, this, &status);
    callbackIds.append(callbackId);

    callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onMeshConnectionDeleted, this, &status);
    callbackIds.append(callbackId);

    // Effectively a destructor callback to clean up when the node is deleted
    // This is more reliable than a destructor, because Maya won't necessarily call destructors on node deletion (unless undo queue is flushed)
    callbackId = MNodeMessage::addNodePreRemovalCallback(thisMObject(), [](MObject& node, void* clientData) {
        PBD* pbdNode = static_cast<PBD*>(clientData);
        MMessage::removeCallbacks(pbdNode->callbackIds);
    }, this, &status);
    callbackIds.append(callbackId);
}

/**
 * Static factory method to create a PBD node, setting up its connections and attributes.
 */
MObject PBD::createPBDNode(Voxels& voxels, const VoxelizationGrid& voxelizationGrid, const MDagPath& meshDagPath) {
    MStatus status;
    MDGModifier dgMod;
    MObject pbdNodeObj = dgMod.createNode(PBD::pbdNodeName, &status);
	dgMod.doIt();
	MFnDependencyNode pbdNode(pbdNodeObj);

    // Connect the mesh owner attribute to the mesh's dag path
    MPlug meshOwnerPlug = pbdNode.findPlug(aMeshOwner, false, &status);
    MFnDependencyNode meshFn(meshDagPath.node());
    MPlug meshMessagePlug = meshFn.findPlug("message", false, &status); // built in to every MObject
    MGlobal::executeCommandOnIdle("connectAttr " + meshMessagePlug.name() + " " + meshOwnerPlug.name(), false);

    // Set voxel data to node attribute
	MFnPluginData pluginDataFn;
	MObject pluginDataObj = pluginDataFn.create( VoxelData::id, &status );
	VoxelData* voxelData = static_cast<VoxelData*>(pluginDataFn.data(&status));
	voxelData->setVoxels(voxels);
    voxelData->setVoxelizationGrid(voxelizationGrid);

	MPlug voxelDataInPlug = pbdNode.findPlug(aVoxelDataIn, false, &status);
	voxelDataInPlug.setValue(pluginDataObj);

    // Connect the particle data attribute output to the global solver node's particle data (array) input.
    // And connect the particle buffer offset attribute to the global solver node's particle buffer offset (array) output.
    MObject globalSolverNodeObj = GlobalSolver::getOrCreateGlobalSolver();
    MPlug globalSolverTriggerPlug = MFnDependencyNode(globalSolverNodeObj).findPlug(GlobalSolver::aTrigger, false, &status);
    MPlug globalSolverParticleBufferOffsetPlugArray = MFnDependencyNode(globalSolverNodeObj).findPlug(GlobalSolver::aParticleBufferOffset, false, &status);
    MPlug globalSolverParticleDataPlugArray = MFnDependencyNode(globalSolverNodeObj).findPlug(GlobalSolver::aParticleData, false, &status);
    MPlug globalSolverSimulateFunctionPlugArray = MFnDependencyNode(globalSolverNodeObj).findPlug(GlobalSolver::aSimulateFunction, false, &status);
    
    uint plugIndex = GlobalSolver::getNextParticleDataPlugIndex();
    MPlug globalSolverParticleBufferOffsetPlug = globalSolverParticleBufferOffsetPlugArray.elementByLogicalIndex(plugIndex, &status);
    MPlug globalSolverParticleDataPlug = globalSolverParticleDataPlugArray.elementByLogicalIndex(plugIndex, &status);
    MPlug globalSolverSimulateFunctionPlug = globalSolverSimulateFunctionPlugArray.elementByLogicalIndex(plugIndex, &status);
    
    MDGModifier connMod;
    MPlug pbdTriggerInPlug = pbdNode.findPlug(aTriggerIn, false, &status);
    MPlug pbdSimulateSubstepPlug = pbdNode.findPlug(aSimulateSubstepFunction, false, &status);
    MPlug pbdParticleBufferOffsetPlug = pbdNode.findPlug(aParticleBufferOffset, false, &status);
    MPlug pbdParticleDataPlug = pbdNode.findPlug(aParticleData, false, &status);
    // Note: the GlobalSolver operates on the assumption that the buffer offset plug is connected before the particle data plug
    connMod.connect(globalSolverTriggerPlug, pbdTriggerInPlug);
    connMod.connect(pbdSimulateSubstepPlug, globalSolverSimulateFunctionPlug);
    connMod.connect(globalSolverParticleBufferOffsetPlug, pbdParticleBufferOffsetPlug);
    connMod.connect(pbdParticleDataPlug, globalSolverParticleDataPlug);
    connMod.doIt();

    return pbdNodeObj;
}

/**
 * This is effectively our "constructor" for the PBD node, since Maya doesn't expose a constructor with arguments for custom nodes.
 * When the voxel data attribute is set, this function will be called. Since the voxel data attribute isn't writeable, this only 
 * occurs on initial creation, file load, or undo/redo events.
 * 
 * TODO: protect against redundant calls to this function?
 */
void PBD::onVoxelDataSet(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
    // Only respond to changes to the voxel data attribute
    // Only respond to attribute value changes
    if (plug != aVoxelDataIn || !(msg & MNodeMessage::kAttributeSet)) {
        return;
    }

    MObject voxelDataObj;
    MStatus status = plug.getValue(voxelDataObj);
    MFnPluginData fnData(voxelDataObj, &status);
    VoxelData* voxelData = static_cast<VoxelData*>(fnData.data(&status));
    const Voxels& voxels = voxelData->getVoxels();
    PBD* pbdNode = static_cast<PBD*>(clientData);

    pbdNode->setRadiusAndVolumeFromLength(voxels.voxelSize);
    pbdNode->createParticles(voxels);
    std::array<std::vector<FaceConstraint>, 3> faceConstraints 
        = pbdNode->constructFaceToFaceConstraints(voxels, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX);
    
    pbdNode->createComputeShaders(voxels, faceConstraints);

    // Set the simulateSubstep function attribute (used by GlobalSolver to invoke simulation on each node)
    MObject simulateSubstepObj = fnData.create(FunctionalData::id, &status);
    FunctionalData* simulateSubstepData = static_cast<FunctionalData*>(fnData.data(&status));
    simulateSubstepData->setFunction([pbdNode]() { pbdNode->simulateSubstep(); });
    MFnDependencyNode pbdDepNode(pbdNode->thisMObject());
    MPlug simulateSubstepPlug = pbdDepNode.findPlug(aSimulateSubstepFunction, false);
    simulateSubstepPlug.setValue(simulateSubstepObj);
}

void PBD::onParticleBufferOffsetChanged(int particleBufferOffset, MDataHandle& particleUAVHandle) {
    int numberParticles = numParticles();
    int voxelOffset = particleBufferOffset / 8;
    int numVoxels = numberParticles / 8;
    ComPtr<ID3D11UnorderedAccessView> particleUAV = GlobalSolver::createUAV(particleBufferOffset, numberParticles, GlobalSolver::BufferType::PARTICLE);
    ComPtr<ID3D11ShaderResourceView> particleSRV = GlobalSolver::createSRV(particleBufferOffset, numberParticles, GlobalSolver::BufferType::PARTICLE);
    ComPtr<ID3D11UnorderedAccessView> isSurfaceUAV = GlobalSolver::createUAV(voxelOffset, numVoxels, GlobalSolver::BufferType::SURFACE);
    ComPtr<ID3D11ShaderResourceView> isSurfaceSRV = GlobalSolver::createSRV(voxelOffset, numVoxels, GlobalSolver::BufferType::SURFACE);
    ComPtr<ID3D11ShaderResourceView> isDraggingSRV = GlobalSolver::createSRV(voxelOffset, numVoxels, GlobalSolver::BufferType::DRAGGING);

    vgsCompute.setParticlesUAV(particleUAV);
    faceConstraintsCompute.setPositionsUAV(particleUAV);
    faceConstraintsCompute.setIsSurfaceUAV(isSurfaceUAV);
    preVGSCompute.setPositionsUAV(particleUAV);
    preVGSCompute.setIsDraggingSRV(isDraggingSRV);

	MFnPluginData pluginDataFn;
	MObject pluginDataObj = pluginDataFn.create(D3D11Data::id);
	D3D11Data* d3d11Data = static_cast<D3D11Data*>(pluginDataFn.data());
	d3d11Data->setSRV(particleSRV);
    particleUAVHandle.set(pluginDataObj);

    setInitialized(true);
}

// Used to tie the lifetime of the PBD node to the lifetime of the mesh it simulates.
// Note that, technically, this will trigger on a connection being broken - not just the mesh being deleted. It's a proxy for deletion.
// There IS a node message for deletion, but it's difficult to have it capture the PBD node's pointer in a static context.
void PBD::onMeshConnectionDeleted(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
    if (plug != aMeshOwner || !(msg & MNodeMessage::kConnectionBroken)) {
        return;
    }

    PBD* pbdNode = static_cast<PBD*>(clientData);
    MObject pbdNodeObj = pbdNode->thisMObject();
    if (pbdNodeObj.isNull()) return;

    // On idle - don't want to delete the node while it's processing graph connection changes.
    MGlobal::executeCommandOnIdle("delete " + pbdNode->name(), false);
}

void PBD::createComputeShaders(
    const Voxels& voxels, 
    const std::array<std::vector<FaceConstraint>, 3>& faceConstraints
) {
    vgsCompute = VGSCompute(
        numParticles(),
        VGSConstantBuffer{ RELAXATION, BETA, PARTICLE_RADIUS, VOXEL_REST_VOLUME, 3.0f, FTF_RELAXATION, FTF_BETA, voxels.size() }
    );

	faceConstraintsCompute = FaceConstraintsCompute(
		faceConstraints,
        vgsCompute.getVoxelSimInfoBuffer()
	);

    PreVGSConstantBuffer preVGSConstants{GRAVITY_STRENGTH, GROUND_COLLISION_Y, TIMESTEP, numParticles()};
    preVGSCompute = PreVGSCompute(
        numParticles(),
        particles.oldPositions.data(),
		preVGSConstants
    );
}

std::array<std::vector<FaceConstraint>, 3> PBD::constructFaceToFaceConstraints(const Voxels& voxels,
    float xTension, float xCompression,
    float yTension, float yCompression,
    float zTension, float zCompression) {
    std::array<std::vector<FaceConstraint>, 3> faceConstraints;

    for (int i = 0; i < voxels.numOccupied; i++) {
        uint32_t x, y, z;
        Utils::fromMortonCode(voxels.mortonCodes[i], x, y, z);

        int rightVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x + 1, y, z));
        int upVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x, y + 1, z));
        int frontVoxelMortonCode = static_cast<int>(Utils::toMortonCode(x, y, z + 1));

        // Checks that the right voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(rightVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int rightNeighborIndex = voxels.mortonCodesToSortedIdx.at(rightVoxelMortonCode);
            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = rightNeighborIndex;
            newConstraint.compressionLimit = xCompression;
            newConstraint.tensionLimit = xTension;
            faceConstraints[0].push_back(newConstraint);
        }

        // Checks that the up voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(upVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int upNeighborIndex = voxels.mortonCodesToSortedIdx.at(upVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = upNeighborIndex;
            newConstraint.compressionLimit = yCompression;
            newConstraint.tensionLimit = yTension;
            faceConstraints[1].push_back(newConstraint);
        }

        // Checks that the front voxel is in the grid and is occupied
        if (voxels.mortonCodesToSortedIdx.find(frontVoxelMortonCode) != voxels.mortonCodesToSortedIdx.end()) {
            int frontNeighborIndex = voxels.mortonCodesToSortedIdx.at(frontVoxelMortonCode);

            FaceConstraint newConstraint;
            newConstraint.voxelOneIdx = i;
            newConstraint.voxelTwoIdx = frontNeighborIndex;
            newConstraint.compressionLimit = zCompression;
            newConstraint.tensionLimit = zTension;
            faceConstraints[2].push_back(newConstraint);
        }
    }

    return faceConstraints;
}

void PBD::createParticles(const Voxels& voxels) {
    for (int i = 0; i < voxels.numOccupied; i++) {
        glm::vec3 voxelCenter = 0.5f * (voxels.corners[i].corners[0] + voxels.corners[i].corners[7]);

        for (const auto& corner : voxels.corners[i].corners) {
            // Offset the corner towards the center by the radius of the particle
            const glm::vec3& position = corner - (PARTICLE_RADIUS * glm::sign(corner - voxelCenter));
            float packedRadiusAndW = Utils::packTwoFloatsAsHalfs(PARTICLE_RADIUS, 1.0f); // for now, w is hardcoded to 1.0f
            particles.positions.push_back(vec4(position, packedRadiusAndW));
            particles.oldPositions.push_back(vec4(position, packedRadiusAndW));
            particles.numParticles++;
        }
    }

    // Set the particle data attribute
    MStatus status;
    MFnPluginData particleDataFn;
    MObject particleDataObj = particleDataFn.create( ParticleData::id, &status );
    ParticleData* particleData = static_cast<ParticleData*>(particleDataFn.data(&status));
    particleData->setData({
        particles.numParticles,
        particles.positions.data(),
        PARTICLE_RADIUS,
        voxels.isSurface.data()
    });

    MFnDependencyNode pbdNode(thisMObject());
    MPlug particleDataPlug = pbdNode.findPlug(aParticleData, false);
    particleDataPlug.setValue(particleDataObj);
}

MStatus PBD::compute(const MPlug& plug, MDataBlock& dataBlock) {
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
        MDataHandle particleUAVHandle = dataBlock.outputValue(aParticleSRV);
        onParticleBufferOffsetChanged(particleBufferOffset, particleUAVHandle);
        return MS::kSuccess;
    }

    return MS::kUnknownParameter;
}

void PBD::simulateSubstep() {
    if (!initialized) return;

    preVGSCompute.dispatch();
    vgsCompute.dispatch();
    
    for (int i = 0; i < 3; i++) {
        faceConstraintsCompute.updateActiveConstraintAxis(i);
		faceConstraintsCompute.dispatch();
    }
}