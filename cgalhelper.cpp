#include "cgalhelper.h"

#include <maya/MFnMesh.h>
#include <maya/MStatus.h>
#include <maya/MPoint.h>
#include <maya/MPointArray.h>
#include "voxelizer.h" // needed for Triangle type definition
#include <unordered_map>

namespace CGALHelper {

SurfaceMesh toSurfaceMesh(
        const MObject& mayaMesh,
        const std::vector<Triangle>& triangles
){
    SurfaceMesh cgalMesh;
    MStatus status;
    MFnMesh meshFn(mayaMesh, &status);
    MPoint vertex;
    std::unordered_map<int, SurfaceMesh::Vertex_index> mayaTriIdxToCgalIdx;
    std::array<SurfaceMesh::Vertex_index, 3> cgalTriIndices;

    // Iterate over all triangles and add them to the CGAL mesh
    for (const auto& triangle : triangles) {
        for (int i = 0; i < 3; ++i) {
            int triIdx = triangle.indices[i];
            // If we've seen this vertex before, use the existing index
            if (mayaTriIdxToCgalIdx.find(triIdx) != mayaTriIdxToCgalIdx.end()) {
                cgalTriIndices[i] = mayaTriIdxToCgalIdx[triIdx];
                continue;
            }

            // Otherwise, we need to add the vertex to the CGAL mesh
            // And record the index it returns
            meshFn.getPoint(triIdx, vertex, MSpace::kWorld);
            SurfaceMesh::Vertex_index cgalIdx = cgalMesh.add_vertex(Point_3(vertex.x, vertex.y, vertex.z));
            mayaTriIdxToCgalIdx[triIdx] = cgalIdx;
        }

        cgalMesh.add_face(cgalTriIndices);
    }

    return cgalMesh;
}

MObject toMayaMesh(const SurfaceMesh& cgalMesh) {
    MStatus status;

    // 1. Extract vertices from CGAL mesh
    MPointArray mayaPoints;
    for (auto v : cgalMesh.vertices()) {
        const Point_3& p = cgalMesh.point(v);
        mayaPoints.append(MPoint(p.x(), p.y(), p.z()));
    }

    // 2. Extract polygon counts and vertex indices
    MIntArray polygonCounts;   // number of vertices per polygon
    MIntArray polygonConnects; // all vertex indices flattened
    for (auto face : cgalMesh.faces()) {
        int count = 0;
        for (auto v : CGAL::vertices_around_face(cgalMesh.halfedge(face), cgalMesh)) {
            polygonConnects.append(static_cast<int>(v)); // implicitly convertible to int
            ++count;
        }
        polygonCounts.append(count);
    }

    // 3. Create Maya mesh
    MFnMesh fnMesh;
    MObject newMesh = fnMesh.create(
        mayaPoints.length(),
        polygonCounts.length(),
        mayaPoints,
        polygonCounts,
        polygonConnects,
        MObject::kNullObj, // No parent transform (could enhance later to allow a parent to be passed in)
        &status
    );

    // Note: the new mesh currently has no shading group or vertex attributes.
    // (In the Voxelizer process, these things are transferred from the old mesh at the end of the process.)
    return newMesh;
}

void openMeshBooleanIntersection(
    SurfaceMesh& openMesh,
    SurfaceMesh& closedMesh,
    const Point_inside& insideOutsideReference
) {
    // TODO: add a "clip" flag to allow first clipping the open mesh's triangles to the closed mesh.

    // Add edges to the closed mesh where it intersects with the open mesh
    CGAL::Polygon_mesh_processing::split(
        closedMesh,    // (target mesh)
        openMesh,      // (splitter mesh)
        CGAL::parameters::default_values(),      // np_tm (target mesh)
        CGAL::parameters::do_not_modify(true)    // np_s (splitter mesh)
    );

    // Then iterate through the triangles of the closed mesh, discarding any
    // that are outside the insideOutsideReference mesh.
    for (auto face : closedMesh.faces()) {
        // No triangles will straddle the surface of the reference mesh, so the centroid will always tell the truth.
        auto halfedge = closedMesh.halfedge(face);
        const auto& p0 = closedMesh.point(source(halfedge, closedMesh));
        const auto& p1 = closedMesh.point(target(halfedge, closedMesh));
        const auto& p2 = closedMesh.point(target(next(halfedge, closedMesh), closedMesh));
        Point_3 centroid = CGAL::centroid(p0, p1, p2);

        const auto side = insideOutsideReference(centroid);
        if (side == CGAL::ON_UNBOUNDED_SIDE) {
            // It's fine to modify the mesh while iterating over it,
            // because this only marks faces for removal, does not delete them immediately.
            // As long as we don't traverse the mesh halfedge structure, we're okay.
            closedMesh.remove_face(face);
        }
    }
    closedMesh.collect_garbage(); // Clean up the mesh after removing faces

    // Note: the last step is left to the caller. The openMesh and closedMesh, together, form a water tight mesh.
    // The caller can either do a logical join on the two, or merge their vertices together by distance into a manifold mesh.
    // This is most easily done after converting back to a Maya mesh, via the Maya API's polyUnite and polyMergeVertex commands.
}

}