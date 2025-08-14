#pragma once

#include "glm/glm.hpp"
#include "voxelizer.h"

#include <algorithm>
#include <vector>
#include <array>
#include <numeric>
#include <functional>
#include "directx/compute/computeshader.h"
#include "directx/compute/vgscompute.h"
#include "directx/compute/prevgscompute.h"
#include "directx/compute/faceconstraintscompute.h"

#include <maya/MGlobal.h>
#include <maya/MPxNode.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MPlug.h>
#include <maya/MStatus.h>
#include <maya/MPlugArray.h>
#include <maya/MItDag.h>
#include <maya/MFnMesh.h>
#include <maya/MMatrix.h>
#include <maya/MNodeMessage.h>
#include <maya/MDataBlock.h>
#include <maya/MCallbackIdArray.h>


using glm::vec3;
using glm::vec4;

struct Particles
{
    // Inverse mass (w) and particle radius stored, packed at half-precision, as 4th component.
    std::vector<vec4> positions;
    std::vector<vec4> oldPositions;
    int numParticles{ 0 };
};

class PBD : public MPxNode
{
public:
    static MTypeId id;
    static MString pbdNodeName;
    // Attributes
    static MObject aMeshOwner;
    // Inputs
    static MObject aTriggerIn;
    static MObject aVoxelData;
    static MObject aParticleBufferOffsetIn;
    // Output
    static MObject aTriggerOut;
    static MObject aParticleData;
    static MObject aParticleBufferOffsetOut;
    static MObject aSimulateSubstepFunction;
    
    PBD() = default;
    ~PBD() override = default;
    // Functions for Maya to create and initialize the node
    static void* creator() { return new PBD(); }
    static MStatus initialize();
    static MObject createPBDNode(Voxels& voxels, const MDagPath& meshDagPath);
    void postConstructor() override;
    MPxNode::SchedulingType schedulingType() const override {
        // Evaluated serially amongst nodes of the same type
        // Necessary because Maya provides a single threaded D3D11 device
        return MPxNode::kGloballySerial; 
    }
    static void onVoxelDataSet(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
    static void onMeshConnectionDeleted(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);

    std::array<std::vector<FaceConstraint>, 3> constructFaceToFaceConstraints(
        const Voxels& voxels, 
        float xTension, float xCompression,
        float yTension, float yCompression,
        float zTension, float zCompression
    );

    void createParticles(const Voxels& voxels);

    void createComputeShaders(
        const Voxels& voxels, 
        const std::array<std::vector<FaceConstraint>, 3>& faceConstraints
    );

    void setRadiusAndVolumeFromLength(float edge_length) {
        PARTICLE_RADIUS = edge_length * 0.25f;
        VOXEL_REST_VOLUME = 8.0f * PARTICLE_RADIUS * PARTICLE_RADIUS * PARTICLE_RADIUS;
    }

    void setInitialized(bool initialized) {
        this->initialized = initialized;
    }

    int numParticles() const {
        return particles.numParticles;
    }
    
private:
    Particles particles;

    bool initialized = false;
    MCallbackIdArray callbackIds;

    // Shaders
    VGSCompute vgsCompute;
    FaceConstraintsCompute faceConstraintsCompute;
    PreVGSCompute preVGSCompute;

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override;
    void simulateSubstep();
    void onParticleBufferOffsetChanged(int newOffset);

    float BETA{ 0.0f };
    float PARTICLE_RADIUS{ 0.25f };

    float RELAXATION{ 0.5f };
    // This is really the rest volume of the volume between particles, which are offset one particle radius from each corner of the voxel
    // towards the center of the voxel. So with a particle radius = 1/4 voxel edge length, the rest volume is (2 * 1/4 edge length)^3 or 8 * (particle radius^3) 
    float VOXEL_REST_VOLUME{ 1.0f };

    float FTF_BETA{ 0.f };
    float FTF_RELAXATION{ 0.5f };

	float GRAVITY_STRENGTH { -10.f };
	float GROUND_COLLISION_ENABLED{ 1.f };
	float GROUND_COLLISION_Y{ 0.f };
    float TIMESTEP{ 0.00166666666666667f };
};
