#include "voxelizer.h"
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MItMeshPolygon.h>

MObject Voxelizer::voxelizeSelectedMesh(
    float gridEdgeLength,
    float voxelSize,
    MPoint gridCenter,
    MStatus& status
) {

    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    std::vector<bool> voxels;
    voxels.resize(voxelsPerEdge * voxelsPerEdge * voxelsPerEdge, false);
    VoxelMesh voxelMesh;

    std::vector<Triangle> meshTris = getTrianglesOfSelectedMesh(status, voxelSize);
    
    getInteriorVoxels(
        meshTris,
        gridEdgeLength,
        voxelSize,
        gridCenter,
        voxels
    );

    // getSurfaceVoxels(
    //     meshTris,
    //     gridEdgeLength,
    //     voxelSize,
    //     gridCenter,
    //     voxels
    // );

    createVoxels(
        voxels,
        gridEdgeLength,
        voxelSize,
        gridCenter,
        voxelMesh
    );

    MObject mesh = voxelMesh.create();
    if (mesh.isNull()) {
        MGlobal::displayError("Failed to create voxel mesh.");
        status = MS::kFailure;
        return MObject::kNullObj;
    }

    status = MS::kSuccess;
    return mesh;
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
    triangle.vertices = vertices;
    triangle.normal = ((vertices[1] - vertices[0]) ^ (vertices[2] - vertices[0])).normal();

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
        triangle.n_ei_xy[i] = (MVector(-edge.y, edge.x, 0.0) * (triangle.normal.z < 0 ? -1 : 1)).normal();

        MVector vi_xy(vertices[i].x, vertices[i].y, 0.0); // Project vi onto XY plane
        triangle.d_ei_xy[i] = -triangle.n_ei_xy[i] * vi_xy
            + std::max(0.0, voxelSize * triangle.n_ei_xy[i].x)
            + std::max(0.0, voxelSize * triangle.n_ei_xy[i].y);
        
        // XZ plane
        // Haven't worked through the math but for some reason the negative sign on this plane is flipped... probably a mistake or implicit assumption I made elsewhere.
        triangle.n_ei_xz[i] = (MVector(edge.z, 0.0, -edge.x ) * (triangle.normal.y < 0 ? -1 : 1)).normal();

        MVector vi_xz(vertices[i].x, 0.0, vertices[i].z); // Project vi onto XZ plane
        triangle.d_ei_xz[i] = -triangle.n_ei_xz[i] * vi_xz
            + std::max(0.0, voxelSize * triangle.n_ei_xz[i].x)
            + std::max(0.0, voxelSize * triangle.n_ei_xz[i].z);

        // YZ plane
        triangle.n_ei_yz[i] = (MVector(0.0, -edge.z, edge.y) * (triangle.normal.x < 0 ? -1 : 1)).normal();

        MVector vi_yz(0.0, vertices[i].y, vertices[i].z); // Project vi onto YZ plane
        triangle.d_ei_yz[i] = -triangle.n_ei_yz[i] * vi_yz
            + std::max(0.0, voxelSize * triangle.n_ei_yz[i].y)
            + std::max(0.0, voxelSize * triangle.n_ei_yz[i].z);
    }

    return triangle;
}

void Voxelizer::getSurfaceVoxels(
    const std::vector<Triangle>& triangles,
    float gridEdgeLength,
    float voxelSize,
    MPoint gridCenter,
    std::vector<bool>& voxels
) {
    
    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    MPoint gridMin = gridCenter - MVector(gridEdgeLength / 2, gridEdgeLength / 2, gridEdgeLength / 2);

    for (const Triangle& tri : triangles) {
        MPoint voxelMin = MPoint(
            std::max(0, static_cast<int>(std::floor((tri.boundingBox.min().x - gridMin.x) / voxelSize))),
            std::max(0, static_cast<int>(std::floor((tri.boundingBox.min().y - gridMin.y) / voxelSize))),
            std::max(0, static_cast<int>(std::floor((tri.boundingBox.min().z - gridMin.z) / voxelSize)))
        );
        
        MPoint voxelMax = MPoint(
            std::min(voxelsPerEdge - 1, static_cast<int>(std::floor((tri.boundingBox.max().x - gridMin.x) / voxelSize))),
            std::min(voxelsPerEdge - 1, static_cast<int>(std::floor((tri.boundingBox.max().y - gridMin.y) / voxelSize))),
            std::min(voxelsPerEdge - 1, static_cast<int>(std::floor((tri.boundingBox.max().z - gridMin.z) / voxelSize)))
        );

        for (int x = static_cast<int>(voxelMin.x); x <= voxelMax.x; ++x) {
            for (int y = static_cast<int>(voxelMin.y); y <= voxelMax.y; ++y) {
                for (int z = static_cast<int>(voxelMin.z); z <= voxelMax.z; ++z) {
                    int index = x * voxelsPerEdge * voxelsPerEdge + y * voxelsPerEdge + z;
                    
                    MVector voxelMinCorner(MVector(x, y, z) * voxelSize + gridMin);
                    if (!doesTriangleOverlapVoxel(tri, voxelMinCorner)) continue;
                    voxels[index] = true;
                }
            }
        }
    }
}

bool Voxelizer::doesTriangleOverlapVoxel(
    const Triangle& triangle,
    const MVector& voxelMin
) {
    // Test 1: Triangle's plane overlaps voxel
    double triNormalDotVoxelMin = triangle.normal * voxelMin;
    if ((triNormalDotVoxelMin + triangle.d1) * (triNormalDotVoxelMin + triangle.d2) > 0) return false;

    // Test 2: The 2D projections of Triangle and Voxel overlap in each of the three coordinate planes (xy, xz, yz)
    for (int i = 0; i < 3; ++i) {
        if ((triangle.n_ei_xy[i] * MVector(voxelMin.x, voxelMin.y, 0)) + triangle.d_ei_xy[i] < 0) return false;
        if ((triangle.n_ei_xz[i] * MVector(voxelMin.x, 0, voxelMin.z)) + triangle.d_ei_xz[i] < 0) return false;
        if ((triangle.n_ei_yz[i] * MVector(0, voxelMin.y, voxelMin.z)) + triangle.d_ei_yz[i] < 0) return false;
    }

    return true;
}

void Voxelizer::getInteriorVoxels(
    const std::vector<Triangle>& triangles,
    float gridEdgeLength,
    float voxelSize,
    MPoint gridCenter,
    std::vector<bool>& voxels
) {
    
    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    MPoint gridMin = gridCenter - MVector(gridEdgeLength / 2, gridEdgeLength / 2, gridEdgeLength / 2);

    for (const Triangle& tri : triangles) {
        // The algorithm for interior voxels only examines the YZ plane of the triangle
        // Then we search over every voxel in each X column whose YZ center is overlapped by the triangle.
        MPoint voxelMin = MPoint(
            0,
            std::max(0, static_cast<int>(std::ceil((tri.boundingBox.min().y - (voxelSize / 2.0) - gridMin.y) / voxelSize))),
            std::max(0, static_cast<int>(std::ceil((tri.boundingBox.min().z - (voxelSize / 2.0) - gridMin.z) / voxelSize)))
        );

        MPoint voxelMax = MPoint(
            0,
            std::min(voxelsPerEdge - 1, static_cast<int>(std::ceil((tri.boundingBox.max().y - (voxelSize / 2.0) - gridMin.y) / voxelSize))),
            std::min(voxelsPerEdge - 1, static_cast<int>(std::ceil((tri.boundingBox.max().z - (voxelSize / 2.0) - gridMin.z) / voxelSize)))
        );

        for (int y = static_cast<int>(voxelMin.y); y <= voxelMax.y; ++y) {
            for (int z = static_cast<int>(voxelMin.z); z <= voxelMax.z; ++z) {
                MVector voxelCenter = MVector(
                    0,
                    y * voxelSize + (voxelSize / 2.0) + gridMin.y,
                    z * voxelSize + (voxelSize / 2.0) + gridMin.z
                );
                
                if (!doesTriangleOverlapVoxelCenter(tri, voxelCenter)) continue;
                double xIntercept = getTriangleVoxelCenterIntercept(tri, voxelCenter);
                int xVoxelMin = std::max(0, static_cast<int>(std::ceil((xIntercept - (voxelSize / 2.0) - gridMin.x) / voxelSize)));

                // Now iterate over all voxels in the column (in +x) and flip their occupancy state
                for (int x = xVoxelMin; x <= voxelsPerEdge - 1; ++x) {
                    int index = x * voxelsPerEdge * voxelsPerEdge + y * voxelsPerEdge + z;
                    voxels[index] = !voxels[index];
                }
            }
        }
    }
}

bool Voxelizer::doesTriangleOverlapVoxelCenter(
    const Triangle& triangle,
    const MVector& voxelCenterYZ  // YZ center of the voxel
) {
    for (int i = 0; i < 3; ++i) {
        // Compute the edge function value
        double edgeFunctionValue = (triangle.n_ei_yz[i] * voxelCenterYZ) + triangle.d_ei_yz[i];

        // Apply the top-left fill rule
        double f_yz_ei = 0.0;
        if (triangle.n_ei_yz[i].y > 0 || 
            (triangle.n_ei_yz[i].y == 0 && triangle.n_ei_yz[i].z < 0)) {
            f_yz_ei = std::numeric_limits<double>::epsilon();
        }

        if (edgeFunctionValue + f_yz_ei <= 0) return false;
    }
    return true;
}

// Using the plane equation of the triangle to find the intercept
// (Can alternatively think of this as a projection)
double Voxelizer::getTriangleVoxelCenterIntercept(
    const Triangle& triangle,
    const MVector& voxelCenterYZ // YZ coords of the voxel column center
) {
    // Calculate D using one of the triangle's vertices
    const MPoint& vertex = triangle.vertices[0];
    double D = -(triangle.normal.x * vertex.x + triangle.normal.y * vertex.y + triangle.normal.z * vertex.z);

    // Check for vertical plane (Nx == 0)
    if (triangle.normal.x == 0) {
        return triangle.boundingBox.min().x;
    }

    // Compute the X-coordinate using the plane equation
    double X_intercept = -(triangle.normal.y * voxelCenterYZ.y + triangle.normal.z * voxelCenterYZ.z + D) / triangle.normal.x;
    return X_intercept;
}

void Voxelizer::createVoxels(
    const std::vector<bool>& overlappedVoxels,
    float gridEdgeLength, 
    float voxelSize,       
    MPoint gridCenter,      
    VoxelMesh& voxelMesh // output mesh
) {
    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    MPoint gridMin = gridCenter - MVector(gridEdgeLength / 2, gridEdgeLength / 2, gridEdgeLength / 2);

    for (int x = 0; x < voxelsPerEdge; ++x) {
        for (int y = 0; y < voxelsPerEdge; ++y) {
            for (int z = 0; z < voxelsPerEdge; ++z) {
                int index = x * voxelsPerEdge * voxelsPerEdge + y * voxelsPerEdge + z;
                if (!overlappedVoxels[index]) continue;

                MPoint voxelMin = MPoint(
                    x * voxelSize + gridMin.x,
                    y * voxelSize + gridMin.y,
                    z * voxelSize + gridMin.z
                );

                addVoxelToMesh(voxelMin, voxelSize, voxelMesh);
            }
        }
    }
}

int faceIndices[6][4] = {
    {0, 2, 3, 1}, // Bottom
    {4, 5, 7, 6}, // Top
    {0, 1, 5, 4}, // Front
    {1, 3, 7, 5}, // Right
    {3, 2, 6, 7}, // Back
    {2, 0, 4, 6}  // Left
};

void Voxelizer::addVoxelToMesh(
    const MPoint& voxelMin,
    float voxelSize,
    VoxelMesh& voxelMesh
) {
    MPoint voxelMax = voxelMin + MVector(voxelSize, voxelSize, voxelSize);
    int baseIndex = voxelMesh.vertices.length();

    voxelMesh.vertices.append(voxelMin);
    voxelMesh.vertices.append(MPoint(voxelMin.x, voxelMin.y, voxelMax.z));
    voxelMesh.vertices.append(MPoint(voxelMin.x, voxelMax.y, voxelMin.z));
    voxelMesh.vertices.append(MPoint(voxelMin.x, voxelMax.y, voxelMax.z));
    voxelMesh.vertices.append(MPoint(voxelMax.x, voxelMin.y, voxelMin.z));
    voxelMesh.vertices.append(MPoint(voxelMax.x, voxelMin.y, voxelMax.z));
    voxelMesh.vertices.append(MPoint(voxelMax.x, voxelMax.y, voxelMin.z));
    voxelMesh.vertices.append(voxelMax);

    for (int i = 0; i < 6; ++i) {
        voxelMesh.faceCounts.append(4);
        for (int j = 0; j < 4; ++j) {
            voxelMesh.faceConnects.append(baseIndex + faceIndices[i][j]);
        }
    }
}

void Voxelizer::tearDown() {
}