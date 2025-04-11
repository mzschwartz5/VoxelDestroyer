#include "voxelizer.h"
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MString.h>

MStatus Voxelizer::voxelizeSelectedMesh(
    float gridEdgeLength,
    float voxelSize,
    MPoint gridCenter
) {
    MStatus status;

    std::vector<Triangle> meshTris = getTrianglesOfSelectedMesh(status, voxelSize);

    return status;
}

std::vector<Triangle> Voxelizer::getTrianglesOfSelectedMesh(MStatus& status, float voxelSize) {
    // Get the current selection
    MSelectionList selection;
    MGlobal::getActiveSelectionList(selection);

    // Check if the selection is empty
    if (selection.isEmpty()) {
        MGlobal::displayError("No mesh selected.");
        status = MS::kFailure;
        return {};
    }

    // Get the first selected item and ensure it's a mesh
    MDagPath activeMeshDagPath;
    status = selection.getDagPath(0, activeMeshDagPath);
    if (status != MS::kSuccess || !activeMeshDagPath.hasFn(MFn::kMesh)) {
        MGlobal::displayError("The selected item is not a mesh.");
        status = MS::kFailure;
        return {};
    }

    // Create an MFnMesh function set to operate on the selected mesh
    MFnMesh meshFn(activeMeshDagPath, &status);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to create MFnMesh.");
        return {};
    }

    // Use MFnMesh::getTriangles to retrieve triangle data
    MIntArray triangleCounts;
    MIntArray triangleVertices;
    status = meshFn.getTriangles(triangleCounts, triangleVertices);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to retrieve triangles.");
        return {};
    }

    // This will later be done on the gpu, one thread per triangle
    std::vector<Triangle> triangles;
    for (unsigned int i = 0; i < triangleVertices.length() / 3; ++i) {
        MPointArray vertices;

        MPoint point;
        meshFn.getPoint(triangleVertices[3 * i], point, MSpace::kWorld);
        vertices.append(point);

        meshFn.getPoint(triangleVertices[3 * i + 1], point, MSpace::kWorld);
        vertices.append(point);

        meshFn.getPoint(triangleVertices[3 * i + 2], point, MSpace::kWorld);
        vertices.append(point);

        Triangle triangle = processMayaTriangle(vertices, voxelSize);
        triangles.push_back(triangle);
    }

	status = MS::kSuccess;
    return triangles;
}

Triangle Voxelizer::processMayaTriangle(const MPointArray& vertices, float voxelSize) {
    Triangle triangle;
    triangle.normal = (vertices[1] - vertices[0]) ^ (vertices[2] - vertices[0]);

    triangle.boundingBox.expand(vertices[0]);
    triangle.boundingBox.expand(vertices[1]);
    triangle.boundingBox.expand(vertices[2]);

    MVector criticalPoint = MVector(
        (triangle.normal.x > 0) ? voxelSize : 0,
        (triangle.normal.y > 0) ? voxelSize : 0,
        (triangle.normal.z > 0) ? voxelSize : 0
    );

    MVector deltaP(voxelSize, voxelSize, voxelSize);
    triangle.d1 = triangle.normal * (criticalPoint - vertices[0]);
    triangle.d2 = triangle.normal * (deltaP - criticalPoint - vertices[0]);

    // Compute edge normals and distances for the XY, XZ, and YZ planes
    for (int i = 0; i < 3; ++i) {
        MVector edge = vertices[(i + 1) % 3] - vertices[i];

        // XY plane
        triangle.n_ei_xy[i] = MVector(
            -edge.y, 
            edge.x * (triangle.normal.z < 0 ? -1 : 1), 
            0.0
        );

        MVector vi_xy(vertices[i].x, vertices[i].y, 0.0); // Project vi onto XY plane
        triangle.d_ei_xy[i] = -triangle.n_ei_xy[i] * vi_xy
            + std::max(0.0, voxelSize * triangle.n_ei_xy[i].x)
            + std::max(0.0, voxelSize * triangle.n_ei_xy[i].y);

        // XZ plane
        triangle.n_ei_xz[i] = MVector(
            -edge.z, 
            0.0, 
            edge.x * (triangle.normal.y < 0 ? -1 : 1)
        );

        MVector vi_xz(vertices[i].x, 0.0, vertices[i].z); // Project vi onto XZ plane
        triangle.d_ei_xz[i] = -triangle.n_ei_xz[i] * vi_xz
            + std::max(0.0, voxelSize * triangle.n_ei_xz[i].x)
            + std::max(0.0, voxelSize * triangle.n_ei_xz[i].z);

        // YZ plane
        triangle.n_ei_yz[i] = MVector(
            0.0, 
            -edge.z, 
            edge.y * (triangle.normal.x < 0 ? -1 : 1)
        );

        MVector vi_yz(0.0, vertices[i].y, vertices[i].z); // Project vi onto YZ plane
        triangle.d_ei_yz[i] = -triangle.n_ei_yz[i] * vi_yz
            + std::max(0.0, voxelSize * triangle.n_ei_yz[i].y)
            + std::max(0.0, voxelSize * triangle.n_ei_yz[i].z);
    }

    return triangle;
}

void Voxelizer::tearDown() {
}