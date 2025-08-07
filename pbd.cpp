#include "pbd.h"
#include <maya/MGlobal.h>
#include <float.h>
#include "utils.h"
#include "constants.h"
#include "custommayaconstructs/voxeldeformerGPUNode.h"
#include "custommayaconstructs/voxeldragcontext.h"
#include "custommayaconstructs/voxeldata.h"
#include "custommayaconstructs/particledata.h"
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnPluginData.h>

MObject PBD::aTime;
MObject PBD::aVoxelData;
MObject PBD::aTrigger;
MObject PBD::aParticleData;
MTypeId PBD::id(0x0013A7B0);
MString PBD::pbdNodeName("PBD");

MStatus PBD::initialize() {
    MStatus status;

    // Time attribute
    MFnUnitAttribute uTimeAttr;
    aTime = uTimeAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    status = addAttribute(aTime);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    uTimeAttr.setCached(false);
    uTimeAttr.setStorable(false);
    uTimeAttr.setWritable(true);
    
    // Voxel data attribute
    MFnTypedAttribute tVoxelDataAttr;
    aVoxelData = tVoxelDataAttr.create("voxeldata", "vxd", VoxelData::id, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    status = addAttribute(aVoxelData);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tVoxelDataAttr.setCached(false);
    tVoxelDataAttr.setStorable(true);
    tVoxelDataAttr.setWritable(true);

    // Output attribute to trigger downstream updates
    MFnNumericAttribute nAttr;
    aTrigger = nAttr.create("trigger", "trg", MFnNumericData::kBoolean, false, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    status = addAttribute(aTrigger);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setStorable(false);
    nAttr.setWritable(false);
    
    // Output particle data to facilitate GPU buffer resource initialization in the GPU deformer override
    MFnTypedAttribute tParticleDataAttr;
    aParticleData = tParticleDataAttr.create("particledata", "dfd", ParticleData::id, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tParticleDataAttr.setStorable(false); // NOT storable - just for initialization
    tParticleDataAttr.setWritable(false);
    tParticleDataAttr.setReadable(true); 
    status = addAttribute(aParticleData);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    status = attributeAffects(aTime, aTrigger);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    return MS::kSuccess;
}

void PBD::postConstructor() {
    MStatus status;
    MPxNode::postConstructor();
    
    MCallbackId callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onVoxelDataSet, this, &status);
    callbackIds.append(callbackId);
    
    unsubscribeFromDragStateChange = VoxelDragContext::subscribeToDragStateChange([this](const DragState& dragState) {
        isDragging = dragState.isDragging;
    });
}

/**
 * Static factory method to create a PBD node with the given voxel data as an attribute.
 */
MObject PBD::createPBDNode(Voxels& voxels) {
    MStatus status;
    MDGModifier dgMod;
    MObject pbdNodeObj = dgMod.createNode(PBD::pbdNodeName, &status);
	dgMod.doIt();

	MFnPluginData pluginDataFn;
	MObject pluginDataObj = pluginDataFn.create( VoxelData::id, &status );

	VoxelData* voxelData = static_cast<VoxelData*>(pluginDataFn.data(&status));
	voxelData->setVoxels(voxels);

	MFnDependencyNode pbdNode(pbdNodeObj);
	MPlug voxelDataPlug = pbdNode.findPlug("voxeldata", false, &status);
	voxelDataPlug.setValue(pluginDataObj);

    MGlobal::executeCommandOnIdle("connectAttr time1.outTime " + pbdNode.name() + ".time", false);
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
    pbdNode->setInitialized(true);
}

void PBD::createComputeShaders(
    const Voxels& voxels, 
    const std::array<std::vector<FaceConstraint>, 3>& faceConstraints
) {
    vgsCompute = VGSCompute(
        particles.w.data(),
        particles.positions,
        VGSConstantBuffer{ RELAXATION, BETA, PARTICLE_RADIUS, VOXEL_REST_VOLUME, 3.0f, FTF_RELAXATION, FTF_BETA, voxels.size() }
    );

    // TODO: this logic will change / be moved around a bit when we implement a global
    // collision solver node. That node will handle / own all particles from all PBD nodes.
    MStatus status;
    MFnPluginData particleDataFn;
    MObject particleDataObj = particleDataFn.create( ParticleData::id, &status );
    ParticleData* particleData = static_cast<ParticleData*>(particleDataFn.data(&status));
    particleData->setData({
        vgsCompute.getParticlesBuffer().Get(),
        particles.numParticles,
        particles.positions.data()
    });

    MFnDependencyNode pbdNode(thisMObject());
    MPlug particleDataPlug = pbdNode.findPlug(aParticleData, false);
    particleDataPlug.setValue(particleDataObj);

	faceConstraintsCompute = FaceConstraintsCompute(
		faceConstraints,
        voxels.isSurface,
		vgsCompute.getParticlesUAV(),
		vgsCompute.getWeightsSRV(),
        vgsCompute.getVoxelSimInfoBuffer()
	);

    buildCollisionGridCompute = BuildCollisionGridCompute(
        PARTICLE_RADIUS,
        vgsCompute.getParticlesSRV(),
        faceConstraintsCompute.getIsSurfaceSRV()
    );

    prefixScanCompute = PrefixScanCompute(
        buildCollisionGridCompute.getCollisionCellParticleCountsUAV()
    );

    buildCollisionParticleCompute = BuildCollisionParticlesCompute(
        vgsCompute.getParticlesSRV(),
        buildCollisionGridCompute.getCollisionCellParticleCountsUAV(),
        buildCollisionGridCompute.getParticleCollisionCB(),
        faceConstraintsCompute.getIsSurfaceSRV()
    );

    solveCollisionsCompute = SolveCollisionsCompute(
        buildCollisionGridCompute.getHashGridSize(),
        vgsCompute.getParticlesUAV(),
        buildCollisionParticleCompute.getParticlesByCollisionCellSRV(),
        buildCollisionGridCompute.getCollisionCellParticleCountsSRV(),
        buildCollisionGridCompute.getParticleCollisionCB()
    );

    dragParticlesCompute = DragParticlesCompute(
        vgsCompute.getParticlesUAV(),
        substeps
    );

    PreVGSConstantBuffer preVGSConstants{GRAVITY_STRENGTH, GROUND_COLLISION_Y, TIMESTEP, particles.numParticles};
    preVGSCompute = PreVGSCompute(
        particles.numParticles,
        particles.oldPositions.data(),
		preVGSConstants,
        vgsCompute.getWeightsSRV(),
        vgsCompute.getParticlesUAV(),
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

    numParticles = particles.numParticles;
}

MStatus PBD::compute(const MPlug& plug, MDataBlock& dataBlock) 
{   
    if (!initialized) {
        return MS::kSuccess;
    }

    for (int i = 0; i < substeps; i++)
    {
        simulateSubstep();
    }
    
    MDataHandle triggerHandle = dataBlock.outputValue(aTrigger);
    triggerHandle.setBool(true);
    triggerHandle.setClean();
    return MS::kSuccess;
}

void PBD::simulateSubstep() {
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