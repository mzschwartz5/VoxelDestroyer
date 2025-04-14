#pragma once

#include <maya/MGlobal.h>
#include <maya/MStatus.h>
#include <maya/MBoundingBox.h>
#include <maya/MPointArray.h>
#include <maya/MFnMesh.h>
#include <vector>

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

// Had to name it this to avoid collision with PBD's Voxel struct
// But in the future let's rename this Voxel and delete that struct / merge their fields together.
struct MeshVoxel {
    bool occupied = false; // contains some part (surface or interior) of the underlying mesh
    bool isSurface = false;
    MPointArray vertices; // vertices owned by this voxel (to be transformed during the PBD (VGS) simulation)
};

class Voxelizer {

public:
    Voxelizer() = default;
    ~Voxelizer() = default;

    MObject voxelizeSelectedMesh(
        float gridEdgeLength,
        float voxelSize,
        MPoint gridCenter,
        MStatus& status
    );

private:

    MDagPath getSelectedMesh(MStatus& status);

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
        std::vector<MeshVoxel>& voxels
    );

    void getInteriorVoxels(
        const std::vector<Triangle>& triangles, // triangles to check against
        float gridEdgeLength,                   // voxel grid must be a cube. User specifies the edge length of the cube
        float voxelSize,                        // edge length of a single voxel
        MPoint gridCenter,                      // center of the grid in world space
        std::vector<MeshVoxel>& voxels               // output array of voxels (true = occupied, false = empty)
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

    MObject createVoxels(
        std::vector<MeshVoxel>& occupiedVoxels,
        float gridEdgeLength, 
        float voxelSize,       
        MPoint gridCenter,      
        MFnMesh& originalSurface
    );

    MObject addVoxelToMesh(
        const MPoint& voxelMin, // min corner of the voxel
        float voxelSize,        // edge length of a single voxel
        MeshVoxel& meshVoxel,
        MFnMesh& originalSurface
    );
};