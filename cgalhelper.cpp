#include "cgalhelper.h"

#include <maya/MFnMesh.h>
#include <maya/MStatus.h>
#include <maya/MPoint.h>
#include <maya/MPointArray.h>
#include <maya/MFnDagNode.h>
#include <maya/MDagPath.h>
#include "voxelizer.h" // needed for Triangle type definition
#include <unordered_map>

namespace CGALHelper {

SurfaceMesh cube(
    const MPoint& minCorner,
    float edgeLength
)
{
    SurfaceMesh cubeMesh;
    const float halfEdge = edgeLength * 0.5f;
    const MPoint center(minCorner.x + halfEdge, minCorner.y + halfEdge, minCorner.z + halfEdge);

    // Arranged in morton order as needed by the VGS algorithm
    std::array<Point_3, 8> vertices = {
        Point_3(center.x - halfEdge, center.y - halfEdge, center.z - halfEdge),
        Point_3(center.x + halfEdge, center.y - halfEdge, center.z - halfEdge),
        Point_3(center.x - halfEdge, center.y + halfEdge, center.z - halfEdge),
        Point_3(center.x + halfEdge, center.y + halfEdge, center.z - halfEdge),
        Point_3(center.x - halfEdge, center.y - halfEdge, center.z + halfEdge),
        Point_3(center.x + halfEdge, center.y - halfEdge, center.z + halfEdge),
        Point_3(center.x - halfEdge, center.y + halfEdge, center.z + halfEdge),
        Point_3(center.x + halfEdge, center.y + halfEdge, center.z + halfEdge)
    };

    // Add vertices
    std::array<SurfaceMesh::Vertex_index, 8> vertexIndices;
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertexIndices[i] = cubeMesh.add_vertex(vertices[i]);
    }

    std::array<std::array<int, 3>, 12> faces = {
        std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}, // Bottom face
        std::array<int, 3>{4, 5, 6}, std::array<int, 3>{4, 6, 7}, // Top face
        std::array<int, 3>{0, 1, 5}, std::array<int, 3>{0, 5, 4}, // Front face
        std::array<int, 3>{1, 2, 6}, std::array<int, 3>{1, 6, 5}, // Right face
        std::array<int, 3>{2, 3, 7}, std::array<int, 3>{2, 7, 6}, // Back face
        std::array<int, 3>{3, 0, 4}, std::array<int, 3>{3, 4, 7}  // Left face
    };

    // Add faces
    for (const auto& face : faces) {
        cubeMesh.add_face(vertexIndices[face[0]], vertexIndices[face[1]], vertexIndices[face[2]]);
    }

    return cubeMesh;
}

SurfaceMesh toSurfaceMesh(
    const MObject& mayaMesh,
    const MIntArray triangleIndices,
    const std::vector<Triangle>& triangles
){
    SurfaceMesh cgalMesh;
    MStatus status;
    MFnMesh meshFn(mayaMesh, &status);
    MPoint vertex;
    std::unordered_map<int, SurfaceMesh::Vertex_index> mayaVertIdxToCgalIdx;
    std::array<SurfaceMesh::Vertex_index, 3> cgalTriIndices;

    // Iterate over all triangles and add them to the CGAL mesh
    for (const auto& triangleIdx : triangleIndices) {
        for (int i = 0; i < 3; ++i) {
            Triangle triangle = triangles[triangleIdx];
            int vertIdx = triangle.indices[i];

            // If we've seen this vertex before, use the existing index
            if (mayaVertIdxToCgalIdx.find(vertIdx) != mayaVertIdxToCgalIdx.end()) {
                cgalTriIndices[i] = mayaVertIdxToCgalIdx[vertIdx];
                continue;
            }

            // Otherwise, we need to add the vertex to the CGAL mesh
            // And record the index it returns
            meshFn.getPoint(vertIdx, vertex, MSpace::kWorld);
            SurfaceMesh::Vertex_index cgalIdx = cgalMesh.add_vertex(Point_3(vertex.x, vertex.y, vertex.z));
            cgalTriIndices[i] = cgalIdx;
            mayaVertIdxToCgalIdx[vertIdx] = cgalIdx;
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
    //    Note: the new mesh currently has no shading group or vertex attributes.
    //    (In the Voxelizer process, these things are transferred from the old mesh at the end of the process.)
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

    // Get and return the transform object
    MFnDagNode fnDagShape(newMesh);
    MDagPath shapePath;
    fnDagShape.getPath(shapePath);
    shapePath.pop(); // shape -> transform
    return shapePath.node();
}

void openMeshBooleanIntersection(
    SurfaceMesh& openMesh,
    SurfaceMesh& closedMesh,
    const SideTester& sideTester
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
    // that are outside the SideTester reference mesh.
    for (auto face : closedMesh.faces()) {
        // No triangles will straddle the surface of the reference mesh, so the centroid will always tell the truth.
        auto halfedge = closedMesh.halfedge(face);
        const auto& p0 = closedMesh.point(source(halfedge, closedMesh));
        const auto& p1 = closedMesh.point(target(halfedge, closedMesh));
        const auto& p2 = closedMesh.point(target(next(halfedge, closedMesh), closedMesh));
        Point_3 centroid = CGAL::centroid(p0, p1, p2);

        const auto side = sideTester(centroid);
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