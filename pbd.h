#pragma once

#include "glm/glm.hpp"
#include "voxelizer.h"

#include <algorithm>
#include <vector>
#include <array>
#include <numeric>
#include <memory>
#include "directx/compute/computeshader.h"
#include "directx/compute/transformverticescompute.h"
#include "directx/compute/bindverticescompute.h"
#include "directx/compute/vgscompute.h"
#include "directx/compute/prevgscompute.h"
#include "directx/compute/postvgscompute.h"
#include "directx/compute/faceconstraintscompute.h"

#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MPlug.h>
#include <maya/MStatus.h>
#include <maya/MPlugArray.h>
#include <maya/MItDag.h>
#include <maya/MFnMesh.h>

using glm::vec3;
using glm::vec4;

struct Particles
{
    std::vector<vec4> positions;
    std::vector<vec4> oldPositions;
    std::vector<vec4> velocities;
    std::vector<float> w; // inverse mass
    int numParticles{ 0 };
};

class PBD
{
public:
    PBD() = default;
    ~PBD() = default;
    void initialize(const Voxels& voxels, float voxelSize, const MDagPath& meshDagPath);
    void simulateStep();
    void updateMeshVertices();
    
	Particles getParticles() const { return particles; }
    
private:
    Particles particles;
    std::array<std::vector<FaceConstraint>, 3> faceConstraints; //0 = x, 1 = y, 2 = z
    int substeps = 10;
    float timeStep;
    MDagPath meshDagPath;
    std::array<glm::vec4, 2> voxelSimInfo;

    // Shaders
	// It seems that they need to be created and managed via unique pointers. Otherwise they dispatch but don't run. Perhaps an issue with copy assignment and DX resources with the non-pointer version.
	int transformVerticesNumWorkgroups;
	std::unique_ptr<TransformVerticesCompute> transformVerticesCompute;
	std::unique_ptr<BindVerticesCompute> bindVerticesCompute;
    std::unique_ptr<VGSCompute> vgsCompute;
	std::unique_ptr<FaceConstraintsCompute> faceConstraintsCompute;
    std::unique_ptr<PreVGSCompute> preVGSCompute;
    std::unique_ptr<PostVGSCompute> postVGSCompute;
    
    void simulateSubstep();

    void constructFaceToFaceConstraints(const Voxels& voxels);

    void createParticles(const Voxels& voxels);

    void setSimValuesFromUI(const MDagPath& dagPath);

    vec4 project(vec4 x, vec4 y);

    float BETA{ 0.99f };
    float PARTICLE_RADIUS{ 0.1f };
    float RELAXATION{ 0.5f };
    float VOXEL_REST_VOLUME{ 1.0f };

    void setRadiusAndVolumeFromLength(float edge_length) {
        PARTICLE_RADIUS = edge_length * 0.25f;
        VOXEL_REST_VOLUME = edge_length * edge_length * edge_length;
    }

    void addFaceConstraint(FaceConstraint constraint, int axis) { faceConstraints[axis].push_back(constraint); };

    void updateAxis(int axis) {
        voxelSimInfo[1][1] = float(axis);
        vgsCompute->updateVoxelSimInfo(voxelSimInfo);
    }
};
