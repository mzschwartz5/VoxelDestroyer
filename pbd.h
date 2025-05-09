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
#include "directx/compute/dragparticlescompute.h"
#include "directx/compute/buildcollisiongridcompute.h"

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
#include <maya/MMatrix.h>

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

	void setRelaxation(float relaxation) { RELAXATION = relaxation; }
	void setBeta(float beta) { BETA = beta; }
	void setFTFRelaxation(float relaxation) { FTF_RELAXATION = relaxation; }
	void setFTFBeta(float beta) { FTF_BETA = beta; }
	void setGravityStrength(float strength) { GRAVITY_STRENGTH = strength; }

    void updateVGSInfo() {
        vgsInfo[0] = glm::vec4(RELAXATION, BETA, PARTICLE_RADIUS, VOXEL_REST_VOLUME);
        vgsInfo[1] = glm::vec4(3.0, 0.0, FTF_RELAXATION, FTF_BETA);
        vgsCompute->updateVoxelSimInfo(vgsInfo);
    }

    void updateSimInfo() {
        simInfo = glm::vec4(GRAVITY_STRENGTH, GROUND_COLLISION_ENABLED, GROUND_COLLISION_Y, TIMESTEP);
        preVGSCompute->updateSimConstants(&simInfo);
    }

    bool isInitialized() const { return initialized; }
    
    void setIsDragging(bool isDragging) { this->isDragging = isDragging; }
    
    void updateDragValues(const DragValues& dragValues) {
        dragParticlesCompute->updateDragValues(dragValues, isDragging);
    }
    
    void updateDepthResourceHandle(void* depthResourceHandle) {
        if (dragParticlesCompute == nullptr) {
            return;
        }

        dragParticlesCompute->updateDepthBuffer(depthResourceHandle);
    }

    void updateCameraMatrices(MMatrix viewProjMatrix, MMatrix invViewProjMatrix, int viewportWidth, int viewportHeight) {
        dragParticlesCompute->updateCameraMatrices({ static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), mayaMatrixToGlm(viewProjMatrix), mayaMatrixToGlm(invViewProjMatrix)});
    }
    
private:
    Particles particles;
    std::array<std::vector<FaceConstraint>, 3> faceConstraints; //0 = x, 1 = y, 2 = z
    int substeps = 10;
    float timeStep;
    MDagPath meshDagPath;

    std::array<glm::vec4, 2> vgsInfo;
    glm::vec4 simInfo;
    bool initialized = false;
    bool isDragging = false;

    // Shaders
	// It seems that they need to be created and managed via unique pointers. Otherwise they dispatch but don't run. Perhaps an issue with copy assignment and DX resources with the non-pointer version.
	int transformVerticesNumWorkgroups;
	std::unique_ptr<TransformVerticesCompute> transformVerticesCompute;
	std::unique_ptr<BindVerticesCompute> bindVerticesCompute;
    std::unique_ptr<VGSCompute> vgsCompute;
	std::unique_ptr<FaceConstraintsCompute> faceConstraintsCompute;
    std::unique_ptr<PreVGSCompute> preVGSCompute;
    std::unique_ptr<PostVGSCompute> postVGSCompute;
    std::unique_ptr<DragParticlesCompute> dragParticlesCompute;
    std::unique_ptr<BuildCollisionGridCompute> buildCollisionGridCompute;
    
    void simulateSubstep();

    void constructFaceToFaceConstraints(const Voxels& voxels, 
        float xTension, float xCompression,
        float yTension, float yCompression,
        float zTension, float zCompression);

    void createParticles(const Voxels& voxels);

    void setSimValuesFromUI();

    float BETA{ 0.99f };
    float PARTICLE_RADIUS{ 0.1f };

    float RELAXATION{ 0.5f };
    float VOXEL_REST_VOLUME{ 1.0f };

    float FTF_BETA{ 0.f };
    float FTF_RELAXATION{ 0.75f };

	float GRAVITY_STRENGTH { -10.f };
	float GROUND_COLLISION_ENABLED{ 1.f };
	float GROUND_COLLISION_Y{ 0.f };
    float TIMESTEP{ 0.00166666666666667f };

    void setRadiusAndVolumeFromLength(float edge_length) {
        PARTICLE_RADIUS = edge_length * 0.25f;
        VOXEL_REST_VOLUME = edge_length * edge_length * edge_length;
    }

    void addFaceConstraint(FaceConstraint constraint, int axis) { faceConstraints[axis].push_back(constraint); };

    void updateAxis(int axis) {
        vgsInfo[1][1] = float(axis);
        vgsCompute->updateVoxelSimInfo(vgsInfo);
    }

    inline glm::mat4 mayaMatrixToGlm(const MMatrix& matrix) {
        return glm::mat4(
            matrix(0, 0), matrix(1, 0), matrix(2, 0), matrix(3, 0),
            matrix(0, 1), matrix(1, 1), matrix(2, 1), matrix(3, 1),
            matrix(0, 2), matrix(1, 2), matrix(2, 2), matrix(3, 2),
            matrix(0, 3), matrix(1, 3), matrix(2, 3), matrix(3, 3)
        );
    }
};
