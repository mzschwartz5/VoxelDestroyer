#pragma once
#include <maya/MObject.h>
#include <maya/MFnMesh.h>
#include <maya/MPoint.h>

#include <CGAL/Polygon_mesh_processing/clip.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/Side_of_triangle_mesh.h>

// Forward declarations
struct Triangle;

namespace CGALHelper {
    using Kernel       = CGAL::Exact_predicates_inexact_constructions_kernel;
    using Point_3      = Kernel::Point_3;
    using SurfaceMesh  = CGAL::Surface_mesh<Point_3>;
    using Primitive    = CGAL::AABB_face_graph_triangle_primitive<SurfaceMesh>;
    using AABB_traits  = CGAL::AABB_traits_3<Kernel, Primitive>;
    using Tree         = CGAL::AABB_tree<AABB_traits>;
    using SideTester   = CGAL::Side_of_triangle_mesh<SurfaceMesh, Kernel>;

    /**
     * So we can map Point_3 to indices in unordered_map.
     */
    struct Point3Hash {
        std::size_t operator()(const Point_3& p) const {
            std::size_t hx = std::hash<double>()(p.x());
            std::size_t hy = std::hash<double>()(p.y());
            std::size_t hz = std::hash<double>()(p.z());
            return hx ^ (hy << 1) ^ (hz << 2); // Simple hash combiner
        }
    };

    /**
     * Creates a cube mesh centered at the given point with the specified edge length.
     */
    SurfaceMesh cube(
        const MPoint& minCorner,
        float edgeLength
    );

    /**
     * Converts a Maya mesh (or subset of it) to a CGAL SurfaceMesh.
     * 
     * triangleIndices index into the triangles vector. The latter should be all triangles of the mesh,
     * and the former can be a subset of those triangles.
     */
    SurfaceMesh toSurfaceMesh(
        const MPointArray* const vertices,
        const std::vector<int>& triangleIndices,
        const std::vector<Triangle>* const triangles
    );

    /**
     * Converts a CGAL SurfaceMesh back to a Maya mesh.
     * Parameters are in/out so you can call successively to combine multiple meshes.
     */
    void toMayaMesh(
        const SurfaceMesh& cgalMesh,
        std::unordered_map<Point_3, int, CGALHelper::Point3Hash>& cgalVertexToMayaIdx,
        MPointArray& mayaPoints,
        MIntArray& polygonCounts,
        MIntArray& polygonConnects
    );

    /**
     * Performs a boolean intersection between two meshes where the first mesh
     * is allowed to be open / not water-tight. This is usually prohibited as an intersection
     * is undefined for an open mesh - there is no concept of "inside" or "outside".
     * 
     * Instead, here, we use a reference mesh (which *is* closed) to determine "inside" and "outside".
     * We start by splitting the closedMesh by the openMesh, and then we use the sideTester based on the reference mesh
     * to discard triangles that are not inside the closedMesh. Optionally, triangles can be clipped to the closedMesh boundary.
     * 
     * This is useful for voxelization, where each voxel is small compared to the overall mesh, and
     * each voxel is independent of each other. This way, each voxel can calculate a boolean with just a piece of
     * the whole mesh, which is much faster than calculating the boolean for the whole mesh for each voxel. It's also parallelizable!
     * 
     * See the note in the implementation about the return value (void). The union of the openMesh and closedMesh, after modification, form the resulting intersection.
     */
    void openMeshBooleanIntersection(
        SurfaceMesh& openMesh,
        SurfaceMesh& closedMesh,
        const SideTester* const sideTester,
        bool clipTriangles
    );

} // namespace CGALHelper