#include "pbd.h"
#include <maya/MGlobal.h>
#include <float.h>
#include "utils.h"
#include "constants.h"
#include "custommayaconstructs/voxeldeformerGPUNode.h"
#include "custommayaconstructs/voxeldragcontext.h"
#include "custommayaconstructs/voxeldata.h"
#include "custommayaconstructs/particledata.h"
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnPluginData.h>
#include "globalsolver.h"
#include <maya/MFnAttribute.h>

MObject PBD::aTriggerIn;
MObject PBD::aTriggerOut;
MObject PBD::aVoxelData;
MObject PBD::aParticleData;
MObject PBD::aParticleBufferOffsetIn;
MObject PBD::aParticleBufferOffsetOut;
MTypeId PBD::id(0x0013A7B0);
MString PBD::pbdNodeName("PBD");

MStatus PBD::initialize() {
    MStatus status;

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
    
    // Voxel data attribute
    MFnTypedAttribute tVoxelDataAttr;
    aVoxelData = tVoxelDataAttr.create("voxeldata", "vxd", VoxelData::id, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tVoxelDataAttr.setCached(false);
    tVoxelDataAttr.setStorable(true);
    tVoxelDataAttr.setWritable(true);
    status = addAttribute(aVoxelData);
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

    // Input/Output attribute: particle buffer offset tells PBD node and deformer node where in the global particle buffer its particles start
    MFnNumericAttribute nParticleBufferOffsetAttr;
    aParticleBufferOffsetIn = nParticleBufferOffsetAttr.create("particlebufferoffsetin", "pboi", MFnNumericData::kInt, -1, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nParticleBufferOffsetAttr.setStorable(false);
    nParticleBufferOffsetAttr.setWritable(true);
    nParticleBufferOffsetAttr.setReadable(false);
    status = addAttribute(aParticleBufferOffsetIn);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    aParticleBufferOffsetOut = nParticleBufferOffsetAttr.create("particlebufferoffsetout", "pboo", MFnNumericData::kInt, -1, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nParticleBufferOffsetAttr.setStorable(true);
    nParticleBufferOffsetAttr.setWritable(false);
    nParticleBufferOffsetAttr.setReadable(true);
    status = addAttribute(aParticleBufferOffsetOut);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = attributeAffects(aTriggerIn, aTriggerOut);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MS::kSuccess;
}

void PBD::postConstructor() {
    MStatus status;
    MPxNode::postConstructor();
    
    MCallbackId callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onVoxelDataSet, this, &status);
    callbackIds.append(callbackId);

    callbackId = MNodeMessage::addNodeDirtyPlugCallback(thisMObject(), onParticleBufferOffsetChanged, this, &status);
    callbackIds.append(callbackId);

    unsubscribeFromDragStateChange = VoxelDragContext::subscribeToDragStateChange([this](const DragState& dragState) {
        isDragging = dragState.isDragging;
    });
}

/**
 * Static factory method to create a PBD node, setting up its connections and attributes.
 */
MObject PBD::createPBDNode(Voxels& voxels) {
    MStatus status;
    MDGModifier dgMod;
    MObject pbdNodeObj = dgMod.createNode(PBD::pbdNodeName, &status);
	dgMod.doIt();
	MFnDependencyNode pbdNode(pbdNodeObj);

    // Connect the particle data attribute output to the global solver node's particle data (array) input.
    // And connect the particle buffer offset attribute to the global solver node's particle buffer offset (array) output.
    MObject globalSolverNodeObj = GlobalSolver::getMObject();
    MPlug globalSolverTriggerPlug = MFnDependencyNode(globalSolverNodeObj).findPlug(GlobalSolver::aTrigger, false, &status);
    MPlug globalSolverParticleDataPlugArray = MFnDependencyNode(globalSolverNodeObj).findPlug(GlobalSolver::aParticleData, false, &status);
    MPlug globalSolverParticleBufferOffsetPlugArray = MFnDependencyNode(globalSolverNodeObj).findPlug(GlobalSolver::aParticleBufferOffset, false, &status);
    
    uint plugIndex = GlobalSolver::getNextParticleDataPlugIndex();
    MPlug globalSolverParticleDataPlug = globalSolverParticleDataPlugArray.elementByLogicalIndex(plugIndex, &status);
    MPlug globalSolverParticleBufferOffsetPlug = globalSolverParticleBufferOffsetPlugArray.elementByLogicalIndex(plugIndex, &status);

    MGlobal::executeCommandOnIdle("connectAttr " + globalSolverTriggerPlug.name() + " " 
                                                 + pbdNode.name() + "." + MFnAttribute(aTriggerIn).name(), false);

    MGlobal::executeCommandOnIdle("connectAttr " + pbdNode.name() + "." +  MFnAttribute(aParticleData).name() + " " 
                                                 + globalSolverParticleDataPlug.name(), false);

    MGlobal::executeCommandOnIdle("connectAttr " + globalSolverParticleBufferOffsetPlug.name() + " " 
                                                 + pbdNode.name() + "." + MFnAttribute(aParticleBufferOffsetIn).name(), false);

    // Set voxel data to node attribute
    // This will trigger the onVoxelDataSet callback, which will create particles (in turn triggering GlobalSolver... see createParticles)
	MFnPluginData pluginDataFn;
	MObject pluginDataObj = pluginDataFn.create( VoxelData::id, &status );
	VoxelData* voxelData = static_cast<VoxelData*>(pluginDataFn.data(&status));
	voxelData->setVoxels(voxels);

	MPlug voxelDataPlug = pbdNode.findPlug(aVoxelData, false, &status);
	voxelDataPlug.setValue(pluginDataObj);

    return pbdNodeObj;
}

PBD::~PBD() {
    unsubscribeFromDragStateChange();
    MMessage::removeCallbacks(callbackIds);
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
    if (plug != aVoxelData || !(msg & MNodeMessage::kAttributeSet)) {
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
}

void PBD::onParticleBufferOffsetChanged(MObject& node, MPlug& plug, void* clientData) {
    // Only respond to changes to the particle buffer offset attribute
    if (plug != aParticleBufferOffsetIn) return;

    if (node.isNull() || !node.hasFn(MFn::kPluginDependNode)) {
        return;
    }

    int newOffset;
    plug.getValue(newOffset);

    PBD* pbdNode = static_cast<PBD*>(clientData);

    // Passthrough the offset to the output particle buffer offset attribute
    MFnDependencyNode pbdNodeFn(pbdNode->thisMObject());
    MPlug particleBufferOffsetOutPlug = pbdNodeFn.findPlug(aParticleBufferOffsetOut, false);
    particleBufferOffsetOutPlug.setValue(newOffset);

    ComPtr<ID3D11UnorderedAccessView> particleUAV = GlobalSolver::createParticleUAV(newOffset, pbdNode->numParticles());
    ComPtr<ID3D11ShaderResourceView> particleSRV = GlobalSolver::createParticleSRV(newOffset, pbdNode->numParticles());

    pbdNode->vgsCompute.setParticlesUAV(particleUAV);
    pbdNode->faceConstraintsCompute.setPositionsUAV(particleUAV);
    pbdNode->buildCollisionGridCompute.setParticlePositionsSRV(particleSRV);
    pbdNode->buildCollisionParticleCompute.setParticlePositionsSRV(particleSRV);
    pbdNode->solveCollisionsCompute.setParticlePositionsUAV(particleUAV);
    pbdNode->dragParticlesCompute.setParticlesUAV(particleUAV);
    pbdNode->preVGSCompute.setPositionsUAV(particleUAV);

    pbdNode->setInitialized(true);
}

void PBD::createComputeShaders(
    const Voxels& voxels, 
    const std::array<std::vector<FaceConstraint>, 3>& faceConstraints
) {
    vgsCompute = VGSCompute(
        numParticles(),
        particles.w.data(),
        VGSConstantBuffer{ RELAXATION, BETA, PARTICLE_RADIUS, VOXEL_REST_VOLUME, 3.0f, FTF_RELAXATION, FTF_BETA, voxels.size() }
    );

	faceConstraintsCompute = FaceConstraintsCompute(
		faceConstraints,
        voxels.isSurface,
		vgsCompute.getWeightsSRV(),
        vgsCompute.getVoxelSimInfoBuffer()
	);

    buildCollisionGridCompute = BuildCollisionGridCompute(
        numParticles(),
        PARTICLE_RADIUS,
        faceConstraintsCompute.getIsSurfaceSRV()
    );

    prefixScanCompute = PrefixScanCompute(
        buildCollisionGridCompute.getCollisionCellParticleCountsUAV()
    );

    buildCollisionParticleCompute = BuildCollisionParticlesCompute(
        numParticles(),
        buildCollisionGridCompute.getCollisionCellParticleCountsUAV(),
        buildCollisionGridCompute.getParticleCollisionCB(),
        faceConstraintsCompute.getIsSurfaceSRV()
    );

    solveCollisionsCompute = SolveCollisionsCompute(
        buildCollisionGridCompute.getHashGridSize(),
        buildCollisionParticleCompute.getParticlesByCollisionCellSRV(),
        buildCollisionGridCompute.getCollisionCellParticleCountsSRV(),
        buildCollisionGridCompute.getParticleCollisionCB()
    );

    dragParticlesCompute = DragParticlesCompute(
        voxels.size(),
        10 // hardcoded - will be addressed when moved to global solver
    );

    PreVGSConstantBuffer preVGSConstants{GRAVITY_STRENGTH, GROUND_COLLISION_Y, TIMESTEP, numParticles()};
    preVGSCompute = PreVGSCompute(
        numParticles(),
        particles.oldPositions.data(),
		preVGSConstants,
        vgsCompute.getWeightsSRV(),
        dragParticlesCompute.getIsDraggingUAV()
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
            particles.positions.push_back(vec4(position, 1.0f));
            particles.oldPositions.push_back(vec4(position, 1.0f));
            particles.w.push_back(1.0f);
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
        [this]() { this->simulateSubstep(); } // TODO: put this in own plug
    });

    // This will trigger the global solver to (re-)create the global particle buffer,
    // and send back an offset to this node (see onParticleBufferOffsetChanged).
    MFnDependencyNode pbdNode(thisMObject());
    MPlug particleDataPlug = pbdNode.findPlug(aParticleData, false);
    particleDataPlug.setValue(particleDataObj);
}

void PBD::simulateSubstep() {
    if (!initialized) return;

    preVGSCompute.dispatch();

    if (isDragging) {
        dragParticlesCompute.dispatch();
    }

    vgsCompute.dispatch();
    
    for (int i = 0; i < 3; i++) {
        faceConstraintsCompute.updateActiveConstraintAxis(i);
		faceConstraintsCompute.dispatch();
    }

    buildCollisionGridCompute.dispatch();
    prefixScanCompute.dispatch(); 
    buildCollisionParticleCompute.dispatch();
    solveCollisionsCompute.dispatch();
}