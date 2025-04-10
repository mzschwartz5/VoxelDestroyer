#pragma once
#include <maya/MStatus.h>
#include <maya/MIntArray.h>

struct Triangle {
    int vertexIndices[3]; // Indices of the vertices that form the triangle
};

class Voxelizer {

public:
    Voxelizer() = default;
    ~Voxelizer() = default;

    void tearDown();
    MStatus voxelizeSelectedMesh();

private:

    MStatus getTrianglesOfSelectedMesh();
    MIntArray triangleCounts;
    MIntArray triangleVertices;
};