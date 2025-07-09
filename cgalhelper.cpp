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
        std::array<int, 3>{0, 4, 6}, std::array<int, 3>{0, 6, 2}, // Bottom
        std::array<int, 3>{1, 3, 7}, std::array<int, 3>{1, 7, 5}, // Top
        std::array<int, 3>{0, 1, 5}, std::array<int, 3>{0, 5, 4}, // Front
        std::array<int, 3>{4, 5, 7}, std::array<int, 3>{4, 7, 6}, // Right
        std::array<int, 3>{6, 7, 3}, std::array<int, 3>{6, 3, 2}, // Back
        std::array<int, 3>{2, 3, 1}, std::array<int, 3>{2, 1, 0}  // Left
    };

    // Add faces
    for (const auto& face : faces) {
        cubeMesh.add_face(vertexIndices[face[0]], vertexIndices[face[1]], vertexIndices[face[2]]);
    }

    return cubeMesh;
}

SurfaceMesh toSurfaceMesh(
    const MPointArray* const vertices,
    const std::vector<int>& triangleIndices,
    const std::vector<Triangle>* const triangles
){
    SurfaceMesh cgalMesh;
    std::unordered_map<int, SurfaceMesh::Vertex_index> mayaVertIdxToCgalIdx;
    std::array<SurfaceMesh::Vertex_index, 3> cgalTriIndices;

    // Iterate over all triangles and add them to the CGAL mesh
    for (const auto& triangleIdx : triangleIndices) {
        Triangle triangle = (*triangles)[triangleIdx];

        for (int i = 0; i < 3; ++i) {
            int vertIdx = triangle.indices[i];

            // If we've seen this vertex before, use the existing index
            if (mayaVertIdxToCgalIdx.find(vertIdx) != mayaVertIdxToCgalIdx.end()) {
                cgalTriIndices[i] = mayaVertIdxToCgalIdx[vertIdx];
                continue;
            }

            // Otherwise, we need to add the vertex to the CGAL mesh
            // And record the index it returns in our map.
            const MPoint& vertex = (*vertices)[vertIdx];
            SurfaceMesh::Vertex_index cgalIdx = cgalMesh.add_vertex(Point_3(vertex.x, vertex.y, vertex.z));
            cgalTriIndices[i] = cgalIdx;
            mayaVertIdxToCgalIdx[vertIdx] = cgalIdx;
        }

        cgalMesh.add_face(cgalTriIndices);
    }

    return cgalMesh;
}

void toMayaMesh(
    const SurfaceMesh& cgalMesh,
    std::unordered_map<Point_3, int, CGALHelper::Point3Hash>& cgalVertexToMayaIdx,
    MPointArray& mayaPoints,
    MIntArray& polygonCounts,
    MIntArray& polygonConnects
) {
    MStatus status;

    // Iterate over all triangles of the CGAL mesh to create Maya points and polygons
    // Assumes mesh is triangulated
    for (const auto& triangle : cgalMesh.faces()) {
        int vertsPerFace = 0; // should be 3 always, but lets keep it general.
        // Iterate the 3 vertices of this face
        for (auto vertIdx : vertices_around_face(cgalMesh.halfedge(triangle), cgalMesh)) {
            vertsPerFace++;
            const Point_3& point = cgalMesh.point(vertIdx);

            // If we've seen this vertex before, use the existing Maya index
            if (cgalVertexToMayaIdx.find(point) != cgalVertexToMayaIdx.end()) {
                polygonConnects.append(cgalVertexToMayaIdx[point]);
                continue;
            }

            // Otherwise, we need to add the vertex to the Maya mesh
            // And record the index it returns in our map.
            int mayaIdx = mayaPoints.length();
            cgalVertexToMayaIdx[point] = mayaIdx;
            mayaPoints.append(MPoint(point.x(), point.y(), point.z()));
            polygonConnects.append(mayaIdx);
        }
        polygonCounts.append(vertsPerFace);
    }
}

void openMeshBooleanIntersection(
    SurfaceMesh& openMesh,
    SurfaceMesh& closedMesh,
    const SideTester* const sideTester,
    bool clipTriangles
) {
    // Split adds edges to the target mesh where the two meshes intersect. Clip does the same thing, but also clips the triangles of the open mesh
    // to the closed mesh boundary. The choice here is mostly aesthetic, though clipping has a small upfront performance cost but has savings during simulation time.
    if (clipTriangles) {
        CGAL::Polygon_mesh_processing::clip(
          openMesh,       // (target mesh)
          closedMesh,     // (clipper mesh)
            CGAL::parameters::default_values(),      // np_tm (target mesh)
            CGAL::parameters::default_values()       // np_s (clipper mesh)
        );
    }
    else {
        CGAL::Polygon_mesh_processing::split(
            closedMesh,    // (target mesh)
            openMesh,      // (splitter mesh)
            CGAL::parameters::default_values(),      // np_tm (target mesh)
            CGAL::parameters::do_not_modify(true)    // np_s (splitter mesh)
        );
    }

    // Then iterate through the triangles of the closed mesh, discarding any
    // that are outside the SideTester reference mesh.
    for (auto face : closedMesh.faces()) {
        // By construction, no triangles will straddle the surface of the reference mesh, so the centroid will always tell the truth about which side the triangle is on.
        auto halfedge = closedMesh.halfedge(face);
        const auto& p0 = closedMesh.point(source(halfedge, closedMesh));
        const auto& p1 = closedMesh.point(target(halfedge, closedMesh));
        const auto& p2 = closedMesh.point(target(next(halfedge, closedMesh), closedMesh));
        Point_3 centroid = CGAL::centroid(p0, p1, p2);

        const auto side = (*sideTester)(centroid);
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