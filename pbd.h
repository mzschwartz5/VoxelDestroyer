#pragma once

#include "glm/glm.hpp"
#include "voxelizer.h"

#include <algorithm>
#include <vector>
#include <array>
#include <numeric>
#include <memory>
#include <functional>
#include "directx/compute/computeshader.h"
#include "directx/compute/vgscompute.h"
#include "directx/compute/prevgscompute.h"
#include "directx/compute/faceconstraintscompute.h"
#include "directx/compute/dragparticlescompute.h"
#include "directx/compute/buildcollisiongridcompute.h"
#include "directx/compute/prefixscancompute.h"
#include "directx/compute/buildcollisionparticlescompute.h"
#include "directx/compute/solvecollisionscompute.h"

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
    std::vector<vec4> positions;
    std::vector<vec4> oldPositions;
    std::vector<float> w; // inverse mass
    int numParticles{ 0 };
};

class PBD : public MPxNode
{
public:
    static MTypeId id;
    static MString pbdNodeName;
    
    PBD() = default;
    ~PBD() override;
    // Functions for Maya to create and initialize the node
    static void* creator() { return new PBD(); }
    static MStatus initialize();
    static void createPBDNode(Voxels&& voxels);
    void postConstructor() override;
    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override;
    MPxNode::SchedulingType schedulingType() const override {
        return MPxNode::kUntrusted; // Compute dispatches must be serial (at least for now - might be able to create deferred DX11 contexts)
    }
    static void onVoxelDataSet(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);

    std::array<std::vector<FaceConstraint>, 3> constructFaceToFaceConstraints(
        const Voxels& voxels, 
        float xTension, float xCompression,
        float yTension, float yCompression,
        float zTension, float zCompression
    );

    Particles createParticles(const Voxels& voxels);

    void createComputeShaders(
        const Voxels& voxels, 
        const Particles& particles,
        const std::array<std::vector<FaceConstraint>, 3>& faceConstraints
    );

    void setRadiusAndVolumeFromLength(float edge_length) {
        PARTICLE_RADIUS = edge_length * 0.25f;
        VOXEL_REST_VOLUME = 8.0f * PARTICLE_RADIUS * PARTICLE_RADIUS * PARTICLE_RADIUS;
    }

    void subscribeToDragStateChange();

    void setInitialized(bool initialized) {
        this->initialized = initialized;
    }
    
private:
    // Attributes
    // Inputs
    static MObject aTime;
    static MObject aVoxelData;
    // Output
    static MObject aTrigger;

    int substeps = 10;
    float timeStep;
    int numParticles;

    bool initialized = false;
    bool isDragging = false;

    MCallbackIdArray callbackIds;

    // Event subscriptions
    std::function<void()> unsubscribeFromDragStateChange;

    // Shaders
	// It seems that they need to be created and managed via unique pointers. Otherwise they dispatch but don't run. Perhaps an issue with copy assignment and DX resources with the non-pointer version.
    std::unique_ptr<VGSCompute> vgsCompute;
	std::unique_ptr<FaceConstraintsCompute> faceConstraintsCompute;
    std::unique_ptr<PreVGSCompute> preVGSCompute;
    std::unique_ptr<DragParticlesCompute> dragParticlesCompute;
    std::unique_ptr<BuildCollisionGridCompute> buildCollisionGridCompute;
    std::unique_ptr<PrefixScanCompute> prefixScanCompute;
    std::unique_ptr<BuildCollisionParticlesCompute> buildCollisionParticleCompute;
    std::unique_ptr<SolveCollisionsCompute> solveCollisionsCompute;
    
    void simulateSubstep();

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
