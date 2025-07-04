#pragma once

#include <maya/MGlobal.h>
#include <maya/MStatus.h>
#include <maya/MBoundingBox.h>
#include <maya/MPointArray.h>
#include <maya/MFnMesh.h>
#include <vector>
#include <array>
#include <unordered_map>

#include "utils.h"
#include "glm/glm.hpp"

// See https://michael-schwarz.com/research/publ/files/vox-siga10.pdf
// "Surface voxelization" for a mathematical explanation of the below fields / how they're used.
// This isn't a triangle, per se, as much as it is the required values precomputed for later use in triangle/voxel intersection
struct Triangle {
    MPointArray vertices;
    MBoundingBox boundingBox; 
    MVector normal;
    // Derived values used in determining triangle plane / voxel overlap
    double d1;           // Distance from the triangle's plane to the critical point c
    double d2;           // Distance from the triangle's plane to the opposite corner (∆p - c)        
    // Derived values used in determining 2D triangle projection / voxel plane overlap
    MVector n_ei_xy[3];  // Edge normals of the triangle's xy plane projection
    MVector n_ei_xz[3];  // Edge normals of the triangle's xz plane projection
    MVector n_ei_yz[3];  // Edge normals of the triangle's yz plane projection
    double d_ei_xy[3];   // Edge distances for the xy plane
    double d_ei_xz[3];   // Edge distances for the xz plane
    double d_ei_yz[3];   // Edge distances for the yz plane
    double d_ei_yz_solid[3]; // Edge distances for the yz plane (for solid voxelization)
};

struct VoxelPositions {
    std::array<glm::vec3, 8> corners;
};

struct Voxels {
    std::vector<bool> occupied;           // contains some part (surface or interior) of the underlying mesh
    std::vector<uint> isSurface;          // Use uints instead of bools because vector<bool> packs bools into bits, which will not work for GPU access.
    std::vector<MObject> mayaObjects;     // the actual cube meshes
    std::vector<VoxelPositions> corners;  // ordered according to the VGS expectations

    std::vector<uint> vertStartIdx;       // Each voxel owns a number of vertices contained within (including the corners)
    int totalVerts = 0;                   // total number of vertices in the voxelized mesh

    std::vector<uint32_t> mortonCodes;
    // Answers the question: for a given voxel morton code, what is the index of the corresponding voxel in the sorted array of voxels?
    std::unordered_map<uint32_t, uint32_t> mortonCodesToSortedIdx;

    int numOccupied = 0;
    
    int size() const { return static_cast<int>(occupied.size()); }
    void resize(int size) {
        occupied.resize(size, false);
        isSurface.resize(size, false);
        mayaObjects.resize(size, MObject::kNullObj);
        corners.resize(size, VoxelPositions());
		vertStartIdx.resize(size, -1);
        mortonCodes.resize(size, UINT_MAX);                 
    }
};

class Voxelizer {

public:
    Voxelizer() = default;
    ~Voxelizer() = default;

    Voxels voxelizeSelectedMesh(
        float gridEdgeLength,
        float voxelSize,
        MPoint gridCenter,
        const MDagPath& selectedMeshDagPath,
        MDagPath& voxelizedMeshDagPath,
        bool voxelizeSurface,
        bool voxelizeInterior,
        bool doBoolean,
        MStatus& status
    );

private:

    std::vector<Triangle> getTrianglesOfMesh(MFnMesh& mesh, float voxelSize, MStatus& status);

    Triangle processMayaTriangle(
        const MPointArray& vertices, // vertex positions of the triangle
        float voxelSize              // edge length of a single voxel
    );
    
    // Does a conservative surface voxelization
    void getSurfaceVoxels(
        const std::vector<Triangle>& triangles, // triangles to check against
        float gridEdgeLength,                   // voxel grid must be a cube. User specifies the edge length of the cube
        float voxelSize,                        // edge length of a single voxel
        MPoint gridCenter,                      // center of the grid in world space
        Voxels& voxels
    );

    void getInteriorVoxels(
        const std::vector<Triangle>& triangles, // triangles to check against
        float gridEdgeLength,                   // voxel grid must be a cube. User specifies the edge length of the cube
        float voxelSize,                        // edge length of a single voxel
        MPoint gridCenter,                      // center of the grid in world space
        Voxels& voxels               // output array of voxels (true = occupied, false = empty)
    );

    bool doesTriangleOverlapVoxel(
        const Triangle& triangle, // triangle to check against
        const MVector& voxelMin    // min corner of the voxel
    );

    bool doesTriangleOverlapVoxelCenter(
        const Triangle& triangle,    // triangle to check against
        const MVector& voxelCenterYZ // YZ coords of the voxel column center
    );

    // Returns the X coordinate where the voxel column center intersects the triangle plane
    double getTriangleVoxelCenterIntercept(
        const Triangle& triangle,    // triangle to check against
        const MVector& voxelCenterYZ // YZ coords of the voxel column center
    );

    MDagPath createVoxels(
        Voxels& occupiedVoxels,
        float gridEdgeLength, 
        float voxelSize,       
        MPoint gridCenter,      
        MFnMesh& originalSurface,
        bool doBoolean
    );

    void addVoxelToMesh(
        const MPoint& voxelMin, // min corner of the voxel
        float voxelSize,        // edge length of a single voxel
        Voxels& voxels,
        int index
    );

    Voxels sortVoxelsByMortonCode(
        const Voxels& voxels
    );

    void intersectVoxelWithOriginalMesh(
        Voxels& voxels,
        MObject& cube,
        MObject& originalMesh,
        int index,
        bool doBoolean
    );

    /*
     * Take the boolean voxel pieces of the mesh and combine them. Other book keeping items like transferring attributes,
     * assigning shading groups, and deleting history are also done here.
     */
    MDagPath finalizeVoxelMesh(
        const MString& combinedMeshName,
        const MString& meshNamesConcatenated,
        const MString& originalMesh,
        const MPoint& originalPivot,
        float voxelSize
    );

    MString selectSurfaceFaces(MFnMesh& mesh, const MString& meshName, float voxelSize, MString& interiorFaces);
};