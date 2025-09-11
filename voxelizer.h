#pragma once

#include <maya/MGlobal.h>
#include <maya/MStatus.h>
#include <maya/MBoundingBox.h>
#include <maya/MPointArray.h>
#include <maya/MFnMesh.h>
#include <maya/MDagPath.h>
#include <vector>
#include <array>
#include <unordered_map>

#include "utils.h"
#include <maya/MThreadPool.h>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/Side_of_triangle_mesh.h>
using Kernel       = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3      = Kernel::Point_3;
using SurfaceMesh  = CGAL::Surface_mesh<Point_3>;
using Primitive    = CGAL::AABB_face_graph_triangle_primitive<SurfaceMesh>;
using AABB_traits  = CGAL::AABB_traits_3<Kernel, Primitive>;
using Tree = CGAL::AABB_tree<AABB_traits>;
using SideTester   = CGAL::Side_of_triangle_mesh<SurfaceMesh, Kernel>;

// See https://michael-schwarz.com/research/publ/files/vox-siga10.pdf
// "Surface voxelization" for a mathematical explanation of the below fields / how they're used.
// This struct is more than just the geometry - it's precomputed values for later use in triangle/voxel intersection
struct Triangle {
    std::array<int, 3> indices;
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

struct VoxelizationGrid {
    double gridEdgeLength;
    int voxelsPerEdge;
    MPoint gridCenter;
};

struct VoxelDimensions {
    MFloatPoint min;
    double edgeLength;
};

struct Voxels {
    std::vector<bool> occupied;             // Contains some part (surface or interior) of the underlying mesh
    std::vector<uint> isSurface;            // Use uints instead of bools because vector<bool> packs bools into bits, which will not work for GPU access.
    std::vector<VoxelDimensions> dimensions;  // Position and size of each voxel
    std::vector<uint32_t> mortonCodes;
    // Answers the question: for a given voxel morton code, what is the index of the corresponding voxel in the sorted array of voxels?
    std::unordered_map<uint32_t, uint32_t> mortonCodesToSortedIdx;
    std::vector<std::vector<int>> containedTris;   // Indices of triangles whose centroids are contained within the voxel
    std::vector<std::vector<int>> overlappingTris; // Indicies of triangles that overlap the voxel, but whose centroids are not contained within the voxel
    MDagPath voxelizedMeshDagPath;
    
    int totalVerts = 0; // total number of vertices in the voxelized mesh
    int numOccupied = 0;
    double voxelSize;
    
    Voxels() = default;

    // Copy constructor
    Voxels(const Voxels& other)
        : occupied(other.occupied),
          isSurface(other.isSurface),
          dimensions(other.dimensions),
          mortonCodes(other.mortonCodes),
          mortonCodesToSortedIdx(other.mortonCodesToSortedIdx),
          containedTris(other.containedTris),
          overlappingTris(other.overlappingTris),
          voxelizedMeshDagPath(other.voxelizedMeshDagPath),
          totalVerts(other.totalVerts),
          numOccupied(other.numOccupied),
          voxelSize(other.voxelSize),
          _size(other._size)
    {}

    // Copy assignment operator
    Voxels& operator=(const Voxels& other) {
        if (this != &other) {
            occupied = other.occupied;
            isSurface = other.isSurface;
            dimensions = other.dimensions;
            mortonCodes = other.mortonCodes;
            mortonCodesToSortedIdx = other.mortonCodesToSortedIdx;
            containedTris = other.containedTris;
            overlappingTris = other.overlappingTris;
            voxelizedMeshDagPath = other.voxelizedMeshDagPath;
            totalVerts = other.totalVerts;
            numOccupied = other.numOccupied;
            voxelSize = other.voxelSize;
            _size = other._size;
        }
        return *this;
    }

    // Move constructor
    Voxels(Voxels&& other) noexcept
        : occupied(std::move(other.occupied)),
          isSurface(std::move(other.isSurface)),
          dimensions(std::move(other.dimensions)),
          mortonCodes(std::move(other.mortonCodes)),
          mortonCodesToSortedIdx(std::move(other.mortonCodesToSortedIdx)),
          containedTris(std::move(other.containedTris)),
          overlappingTris(std::move(other.overlappingTris)),
          voxelizedMeshDagPath(std::move(other.voxelizedMeshDagPath)),
          totalVerts(other.totalVerts),
          numOccupied(other.numOccupied),
          voxelSize(other.voxelSize),
          _size(other._size)
    {}
    
    int _size = 0;
    int size() const { return _size; }
    void resize(int size) {
        _size = size;
        occupied.resize(size, false);
        isSurface.resize(size, false);
        dimensions.resize(size);
        mortonCodes.resize(size, UINT_MAX);
        containedTris.resize(size);
        overlappingTris.resize(size);
    }
};

class Voxelizer {

public:
    Voxelizer() = default;
    ~Voxelizer() = default;

    Voxels voxelizeSelectedMesh(
        const VoxelizationGrid& grid,
        const MDagPath& selectedMeshDagPath,
        bool voxelizeSurface,
        bool voxelizeInterior,
        bool doBoolean,
        bool clipTriangles,
        MStatus& status
    );

private:

    // Iterates over Maya triangles and processes each one, calculating quantities needed for voxelization
    std::vector<Triangle> getTrianglesOfMesh(MFnMesh& mesh, double voxelSize, MStatus& status);

    Triangle processMayaTriangle(
        const MFnMesh& meshFn,                   // the overall mesh
        const std::array<int, 3>& vertIndices,   // indices of the triangle vertices in the mesh
        double voxelSize                         // edge length of a single voxel
    );
    
    // Does a conservative surface voxelization
    void getSurfaceVoxels(
        const std::vector<Triangle>& triangles, // triangles to check against
        const VoxelizationGrid& grid,           // grid parameters
        Voxels& voxels,
        const MFnMesh& selectedMesh
    );

    // Does an interior voxelization
    void getInteriorVoxels(
        const std::vector<Triangle>& triangles, // triangles to check against
        const VoxelizationGrid& grid,           // grid parameters
        Voxels& voxels,                         // output array of voxels (true = occupied, false = empty)
        const MFnMesh& selectedMesh             // the original mesh to use for boolean operations
    );

    bool doesTriangleOverlapVoxel(
        const Triangle& triangle,  // triangle to check against
        const MVector& voxelMin    // min corner of the voxel
    );

    bool doesTriangleOverlapVoxelCenter(
        const Triangle& triangle,     // triangle to check against
        const MVector& voxelCenterYZ  // YZ coords of the voxel column center
    );

    // Returns the X coordinate where the voxel column center intersects the triangle plane
    double getTriangleVoxelCenterIntercept(
        const Triangle& triangle,     // triangle to check against
        const MVector& voxelCenterYZ, // YZ coords of the voxel column center
        const MFnMesh& selectedMesh   // the original mesh to use for boolean operations
    );

    bool isTriangleCentroidInVoxel(
        const Triangle& triangle,  
        const MVector& voxelMin,
        double voxelSize,
        const MFnMesh& selectedMesh
    );

    // Iterates over voxels and, for each that is occupied (overlaps or is contained in the mesh), 
    // creates a cube mesh for it.
    void createVoxels(
        Voxels& occupiedVoxels,
        const VoxelizationGrid& grid
    );

    // Sorts the voxels by their Morton code, which helps later on with efficient GPU memory access.
    Voxels sortVoxelsByMortonCode(
        const Voxels& voxels
    );

    struct FaceSelectionStrings {
        MString surfaceFaces;
        MString interiorFaces;
    };

    // Sets up the CGAL acceleration tree and launches the MThreadPool (which, itself sets up each thread).
    // (There are too many "do intersection" here functions IMO, but Maya's thread pool forces you into a pattern of manager functions that don't do much themselves)
    FaceSelectionStrings prepareForAndDoVoxelIntersection(
        Voxels& voxels,      
        MFnMesh& originalMesh,
        const std::vector<Triangle>& meshTris,
        const MString& newMeshName,
        bool doBoolean,
        bool clipTriangles
    );

    // Payload for the function that sets up all threads.
    struct VoxelIntersectionTaskData {
        Voxels* voxels;
        const MPointArray* const originalVertices;
        const std::vector<Triangle>* const triangles;
        const SideTester* const sideTester;
        MString* const surfaceFaces; // pass by pointer as this string will be very long and modified by the task
        MString* const interiorFaces;
        bool doBoolean;
        bool clipTriangles;
        MString newMeshName;
    };

    struct VoxelIntersectionThreadData {
        const VoxelIntersectionTaskData* taskData;
        // TODO: rather than pass the whole vector to each thread, just pass the relevant bit.
        std::vector<MPointArray>* meshPointsAfterIntersection;
        std::vector<MIntArray>* polyCountsAfterIntersection;
        std::vector<MIntArray>* polyConnectsAfterIntersection;
        std::vector<int>* numSurfaceFacesAfterIntersection;
        int threadIdx;
    };

    // The Maya thradpool callback for setting up all the threads and their data.
    // Also processes the results afterwards and creates the resulting MObject mesh.
    static void getVoxelMeshIntersection(
        void* taskData,
        MThreadRootTask* rootTask
    );

    // A per-thread function that does the actual intersection of the voxel mesh with the triangles.
    static MThreadRetVal getSingleVoxelMeshIntersection(void* threadData);

    /*
     * Miscellaneous steps to finish the voxelization process
     * Transfers attributes (uvs, normals), transfers shading sets, etc.
     * Currently, this takes the majority of the time in the voxelization process.
     */
    MDagPath finalizeVoxelMesh(
        const MString& newMeshName,
        const MString& originalMesh,
        const MPoint& originalPivot,
        const FaceSelectionStrings& faceSelectionStrings
    );
};