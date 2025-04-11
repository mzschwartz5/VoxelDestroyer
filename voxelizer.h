#pragma once

#include <maya/MGlobal.h>
#include <maya/MStatus.h>
#include <maya/MBoundingBox.h>
#include <vector>

// See https://michael-schwarz.com/research/publ/files/vox-siga10.pdf
// "Surface voxelization" for a mathematical explanation of the below fields / how they're used.
// This isn't a triangle, per se, as much as it is the required values precomputed for later use in triangle/voxel intersection
struct Triangle {
    MBoundingBox boundingBox; 
    MVector normal;
    // Derived values used in determining triangle plane / voxel overlap
    double d1;           // Distance from the triangle's plane to the critical point c
    double d2;           // Distance from the triangle's plane to the opposite corner (∆p - c)        
    // Derived values used in determining 2D triangle projection / voxel plane overlap
    MVector n_ei_xy[3]; // Edge normals for the xy plane
    MVector n_ei_xz[3]; // Edge normals for the xz plane
    MVector n_ei_yz[3]; // Edge normals for the yz plane
    double d_ei_xy[3];   // Edge distances for the xy plane
    double d_ei_xz[3];   // Edge distances for the xz plane
    double d_ei_yz[3];   // Edge distances for the yz plane
};

class Voxelizer {

public:
    Voxelizer() = default;
    ~Voxelizer() = default;

    void tearDown();
    MStatus voxelizeSelectedMesh(
        float gridEdgeLength = 1.0f,                  // voxel grid must be a cube. User specifies the edge length of the cube
        float voxelSize = 2.0f,                       // edge length of a single voxel. Together with grid, determines resolution.
        MPoint gridCenter = MPoint(0.0f, 0.0f, 0.0f)  // center of the grid in world space
    );

private:

    std::vector<Triangle> getTrianglesOfSelectedMesh(MStatus& status, float voxelSize);
    Triangle processMayaTriangle(
        const MPointArray& vertices, // vertex positions of the triangle
        float voxelSize              // edge length of a single voxel
    );
        
};