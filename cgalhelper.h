#pragma once
#include <maya/MObject.h>

#include <CGAL/Polygon_mesh_processing/clip.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Simple_cartesian.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/Side_of_triangle_mesh.h>

// Forward declarations
struct Triangle;

namespace CGALHelper {
    using Kernel = CGAL::Simple_cartesian<double>;
    using Point_3 = Kernel::Point_3;
    using SurfaceMesh = CGAL::Surface_mesh<Point_3>;
    using Primitive    = CGAL::AABB_face_graph_triangle_primitive<SurfaceMesh>;
    using AABB_traits  = CGAL::AABB_traits_3<Kernel, Primitive>;
    using Tree         = CGAL::AABB_tree<AABB_traits>;
    using Point_inside = CGAL::Side_of_triangle_mesh<SurfaceMesh, Kernel>;

    /**
     * Converts a Maya mesh (or subset of it) to a CGAL SurfaceMesh.
     */
    SurfaceMesh toSurfaceMesh(
        const MObject& mayaMesh,
        const std::vector<Triangle>& triangles
    );

    /**
     * Converts a CGAL SurfaceMesh back to a Maya mesh.
     */
    MObject toMayaMesh(const SurfaceMesh& cgalMesh);


    /**
     * Performs a boolean intersection between two meshes where the first mesh
     * is allowed to be open / not water-tight. This is usually prohibited as an intersection
     * is undefined for an open mesh - there is no concept of "inside" or "outside".
     * 
     * Instead, here, we use a reference mesh (which *is* closed) to determine "inside" and "outside".
     * We start by splitting the closedMesh by the openMesh, and then we use the insideOutsideReference
     * to discard triangles that are not inside the closedMesh. Note: insideOutsideReference is an acceleration tree on the reference mesh.
     * 
     * This is useful for voxelization, where each voxel is small compared to the overall mesh, and
     * each voxel is independent of each other. This way, each voxel can calculate a boolean with just a piece of
     * the whole mesh, which is much faster than calculating the boolean for the whole mesh for each voxel. It's also parallelizable!
     * 
     * See the note in the implementation about the return value (void). The openMesh and closedMesh, after modification, form the resulting intersection.
     */
    void openMeshBooleanIntersection(
        SurfaceMesh& openMesh,
        SurfaceMesh& closedMesh,
        const Point_inside& insideOutsideReference
    );

} // namespace CGALHelper