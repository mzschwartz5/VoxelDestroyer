#include "voxelizer.h"
#include <maya/MSelectionList.h>
#include <maya/MFnTransform.h>
#include <algorithm>
#include <maya/MFnSet.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MFloatPointArray.h>
#include <numeric>
#include "cgalhelper.h"
#include <maya/MProgressWindow.h>

Voxels Voxelizer::voxelizeSelectedMesh(
    float gridEdgeLength,
    float voxelSize,
    MPoint gridCenter,
    const MDagPath& selectedMeshPath,
    bool voxelizeSurface,
    bool voxelizeInterior,
    bool doBoolean,
    bool clipTriangles,
    MStatus& status
) {
    MFnMesh selectedMesh(selectedMeshPath, &status);
    MDagPath transformPath = selectedMesh.dagPath();
    transformPath.pop(); // Move up to the transform node
    MFnTransform transform(transformPath);
    MPoint originalPivot = transform.rotatePivot(MSpace::kWorld);
    MString originalMeshName = transformPath.partialPathName();
    MString newMeshName = originalMeshName + "_voxelized";

    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    Voxels voxels;
    voxels.voxelSize = voxelSize;
    voxels.resize(voxelsPerEdge * voxelsPerEdge * voxelsPerEdge);
    status = MThreadPool::init();
    
    // Freeze transformations on the original mesh before any processing
    MGlobal::executeCommand(MString("makeIdentity -apply true -t 1 -r 1 -s 1 -n 0 -pn 1"), false, true);

    MProgressWindow::setProgressStatus("Processing mesh triangles...");
    std::vector<Triangle> meshTris = getTrianglesOfMesh(selectedMesh, voxelSize, status);

    if (voxelizeInterior) {
        MProgressWindow::setProgressStatus("Performing interior voxelization...");
        getInteriorVoxels(
            meshTris,
            gridEdgeLength,
            voxelSize,
            gridCenter,
            voxels,
            selectedMesh
        );
    }

    if (voxelizeSurface) {
        MProgressWindow::setProgressStatus("Performing surface voxelization...");
        getSurfaceVoxels(
            meshTris,
            gridEdgeLength,
            voxelSize,
            gridCenter,
            voxels,
            selectedMesh
        );
    }

    MProgressWindow::setProgressStatus("Creating voxels...");
    createVoxels(
        voxels,
        gridEdgeLength,
        voxelSize,
        gridCenter
    );

    MProgressWindow::setProgressStatus("Sorting voxels by Morton code...");
    Voxels sortedVoxels = sortVoxelsByMortonCode(voxels); // note: assign to new var to take advantage of RVO

    MProgressWindow::setProgressStatus("Calculating voxel-mesh intersections...");
    const FaceSelectionStrings faceSelectionStrings = prepareForAndDoVoxelIntersection(
        sortedVoxels,
        selectedMesh,
        meshTris,
        newMeshName,
        doBoolean,
        clipTriangles
    );

    sortedVoxels.voxelizedMeshDagPath = finalizeVoxelMesh(newMeshName, originalMeshName, originalPivot, voxelSize, faceSelectionStrings); // TODO: if no boolean, should get rid of non-manifold geometry
    MGlobal::executeCommand("delete " + originalMeshName, false, true); // TODO: maybe we want to do something non-destructive that also does not obstruct the view of the original mesh (or just allow for undo)

    MThreadPool::release(); // reduce reference count incurred by init()
    return sortedVoxels;
}

std::vector<Triangle> Voxelizer::getTrianglesOfMesh(MFnMesh& meshFn, float voxelSize, MStatus& status) {
    MIntArray triangleCounts;
    MIntArray vertexIndices;
    status = meshFn.getTriangles(triangleCounts, vertexIndices);
    int numTriangles = static_cast<int>(vertexIndices.length() / 3);
    MProgressWindow::setProgressRange(0, numTriangles);
    MProgressWindow::setProgress(0);

    std::vector<Triangle> triangles;
    std::array<int, 3> vertIndices;
    for (int i = 0; i < numTriangles; ++i) {

        vertIndices[0] = vertexIndices[3 * i];
        vertIndices[1] = vertexIndices[3 * i + 1];
        vertIndices[2] = vertexIndices[3 * i + 2];

        Triangle triangle = processMayaTriangle(meshFn, vertIndices, voxelSize);
        triangles.push_back(triangle);
        
        if (i % 100 == 0) MProgressWindow::advanceProgress(100);
    }

    MProgressWindow::setProgress(numTriangles);
	status = MS::kSuccess;
    return triangles;
}

Triangle Voxelizer::processMayaTriangle(const MFnMesh& meshFn, const std::array<int, 3>& vertIndices, float voxelSize) {
    std::array<MPoint, 3> vertices;
    meshFn.getPoint(vertIndices[0], vertices[0], MSpace::kWorld);
    meshFn.getPoint(vertIndices[1], vertices[1], MSpace::kWorld);
    meshFn.getPoint(vertIndices[2], vertices[2], MSpace::kWorld);

    Triangle triangle;
    triangle.indices = vertIndices;
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
        triangle.d_ei_yz_solid[i] = -triangle.n_ei_yz[i] * vi_yz;
        triangle.d_ei_yz[i] = triangle.d_ei_yz_solid[i]
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
    Voxels& voxels,
    const MFnMesh& selectedMesh
) {
    MProgressWindow::setProgressRange(0, static_cast<int>(triangles.size()));
    MProgressWindow::setProgress(0);
    
    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    MPoint gridMin = gridCenter - MVector(gridEdgeLength / 2, gridEdgeLength / 2, gridEdgeLength / 2);

    int triIdx = 0;
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
                    
                    voxels.occupied[index] = true;
                    voxels.isSurface[index] = true;
                    
                    isTriangleCentroidInVoxel(tri, voxelMinCorner, voxelSize, selectedMesh) ? 
                        voxels.containedTris[index].push_back(triIdx) :
                        voxels.overlappingTris[index].push_back(triIdx);
                }
            }
        }

        ++triIdx;
        if (triIdx % 100 == 0) MProgressWindow::advanceProgress(100);
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

bool Voxelizer::isTriangleCentroidInVoxel(
    const Triangle& triangle,
    const MVector& voxelMin,
    double voxelSize,
    const MFnMesh& selectedMesh
) {
    MPoint centroid, point1, point2, point3;
    selectedMesh.getPoint(triangle.indices[0], point1, MSpace::kWorld);
    selectedMesh.getPoint(triangle.indices[1], point2, MSpace::kWorld);
    selectedMesh.getPoint(triangle.indices[2], point3, MSpace::kWorld);
    centroid = (point1 + point2 + point3) / 3.0;

    return centroid.x >= voxelMin.x && centroid.x < voxelMin.x + voxelSize &&
        centroid.y >= voxelMin.y && centroid.y < voxelMin.y + voxelSize &&
        centroid.z >= voxelMin.z && centroid.z < voxelMin.z + voxelSize;
}

void Voxelizer::getInteriorVoxels(
    const std::vector<Triangle>& triangles,
    float gridEdgeLength,
    float voxelSize,
    MPoint gridCenter,
    Voxels& voxels,
    const MFnMesh& selectedMesh
) {
    MProgressWindow::setProgressRange(0, static_cast<int>(triangles.size()));
    MProgressWindow::setProgress(0);
    
    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    MPoint gridMin = gridCenter - MVector(gridEdgeLength / 2, gridEdgeLength / 2, gridEdgeLength / 2);

    int progressCounter = 0;
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
            std::min(voxelsPerEdge - 1, static_cast<int>(std::floor((tri.boundingBox.max().y - (voxelSize / 2.0) - gridMin.y) / voxelSize))),
            std::min(voxelsPerEdge - 1, static_cast<int>(std::floor((tri.boundingBox.max().z - (voxelSize / 2.0) - gridMin.z) / voxelSize)))
        );

        for (int y = static_cast<int>(voxelMin.y); y <= voxelMax.y; ++y) {
            for (int z = static_cast<int>(voxelMin.z); z <= voxelMax.z; ++z) {
                MVector voxelCenter = MVector(
                    0,
                    y * voxelSize + (voxelSize / 2.0) + gridMin.y,
                    z * voxelSize + (voxelSize / 2.0) + gridMin.z
                );
                
                if (!doesTriangleOverlapVoxelCenter(tri, voxelCenter)) continue;
                double xIntercept = getTriangleVoxelCenterIntercept(tri, voxelCenter, selectedMesh);
                int xVoxelMin = std::max(0, static_cast<int>(std::ceil((xIntercept - (voxelSize / 2.0) - gridMin.x) / voxelSize)));

                // Now iterate over all voxels in the column (in +x) and flip their occupancy state
                for (int x = xVoxelMin; x < voxelsPerEdge; ++x) {
                    int index = x * voxelsPerEdge * voxelsPerEdge + y * voxelsPerEdge + z;
                    voxels.occupied[index] = !voxels.occupied[index];
                }
            }
        }

        ++progressCounter;
        if (progressCounter % 100 == 0) MProgressWindow::advanceProgress(100);
    }
}

bool Voxelizer::doesTriangleOverlapVoxelCenter(
    const Triangle& triangle,
    const MVector& voxelCenterYZ  // YZ center of the voxel
) {
    for (int i = 0; i < 3; ++i) {
        // Compute the edge function value
        double edgeFunctionValue = (triangle.n_ei_yz[i] * voxelCenterYZ) + triangle.d_ei_yz_solid[i];

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
    const MVector& voxelCenterYZ, // YZ coords of the voxel column center
    const MFnMesh& selectedMesh
) {
    // Calculate D using one of the triangle's vertices
    MPoint vertex; 
    selectedMesh.getPoint(triangle.indices[0], vertex, MSpace::kWorld);
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
    Voxels& overlappedVoxels,
    float gridEdgeLength, 
    float voxelSize,       
    MPoint gridCenter
) {    
    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    MPoint gridMin = gridCenter - MVector(gridEdgeLength / 2, gridEdgeLength / 2, gridEdgeLength / 2);

    MProgressWindow::setProgressRange(0, voxelsPerEdge * voxelsPerEdge * voxelsPerEdge);
    MProgressWindow::setProgress(0);

    for (int x = 0; x < voxelsPerEdge; ++x) {
        for (int y = 0; y < voxelsPerEdge; ++y) {
            for (int z = 0; z < voxelsPerEdge; ++z) {
                int index = x * voxelsPerEdge * voxelsPerEdge + y * voxelsPerEdge + z;
                if (index % 100 == 0) MProgressWindow::advanceProgress(100);
                if (!overlappedVoxels.occupied[index]) continue;
                
                overlappedVoxels.mortonCodes[index] = Utils::toMortonCode(x, y, z);

                MPoint voxelMin = MPoint(
                    x * voxelSize + gridMin.x,
                    y * voxelSize + gridMin.y,
                    z * voxelSize + gridMin.z
                );

                addVoxelToMesh(voxelMin, voxelSize, overlappedVoxels, index);
                overlappedVoxels.numOccupied++;
            }
        }
    }
}

Voxelizer::FaceSelectionStrings Voxelizer::prepareForAndDoVoxelIntersection(
    Voxels& voxels,      
    MFnMesh& originalMesh,
    const std::vector<Triangle>& meshTris,
    const MString& newMeshName,
    bool doBoolean,
    bool clipTriangles
) 
{

    // Prepare for boolean operations
    // We only want to create the acceleration structure once (which is why we do it here, before all the boolean ops begin)
    std::vector<int> allTriangleIndices(meshTris.size());
    std::iota(allTriangleIndices.begin(), allTriangleIndices.end(), 0); // Fill with indices from 0 to size-1
    MPointArray originalVertices;
    originalMesh.getPoints(originalVertices, MSpace::kWorld);
    SurfaceMesh originalMeshCGAL = CGALHelper::toSurfaceMesh(&originalVertices, allTriangleIndices, &meshTris);
    Tree aabbTree(originalMeshCGAL.faces().first, originalMeshCGAL.faces().second, originalMeshCGAL);
    SideTester sideTester(aabbTree);

    // These strings will be built up in getVoxelMeshIntersection
    FaceSelectionStrings faceSelectionStrings;
    VoxelIntersectionTaskData taskData {
        &voxels,
        &originalVertices,
        &meshTris,
        &sideTester,
        &faceSelectionStrings.surfaceFaces,
        &faceSelectionStrings.interiorFaces,
        doBoolean,
        clipTriangles,
        newMeshName
    };

    MProgressWindow::setProgressRange(0, voxels.numOccupied);
    MThreadPool::newParallelRegion(
        Voxelizer::getVoxelMeshIntersection,
        (void*)&taskData
    );
    MThreadPool::release(); // reduce reference count incurred by opening a new parallel region

    return faceSelectionStrings;
}

MDagPath Voxelizer::finalizeVoxelMesh(
    const MString& newMeshName,
    const MString& originalMeshName,
    const MPoint& originalPivot,
    float voxelSize,
    const FaceSelectionStrings& faceSelectionStrings
) {
    MProgressWindow::setProgressRange(0, 100);
    MProgressWindow::setProgress(0);
    int numSubsteps = 4; // purely for progress bar
    int progressIncrement = 100 / numSubsteps;

    // Retrieve the MObject of the resulting mesh
    MProgressWindow::setProgressStatus("Transferring pivot...");
    MStatus status;
    MSelectionList resultSelectionList;
    resultSelectionList.add(newMeshName);
    MObject resultMeshObject;
    resultSelectionList.getDependNode(0, resultMeshObject);
    MDagPath resultMeshDagPath;
    MDagPath::getAPathTo(resultMeshObject, resultMeshDagPath);
    MFnMesh resultMeshFn(resultMeshDagPath, &status);
    MFnTransform resultTransformFn(resultMeshDagPath, &status);
    resultTransformFn.setRotatePivot(originalPivot, MSpace::kTransform, false); // Set the pivot to the original mesh's pivot
    MProgressWindow::advanceProgress(progressIncrement);

    // Use MEL to transferAttributes the normals / uvs / etc. from the original mesh to the new voxelized/combined one
    MProgressWindow::setProgressStatus("Transferring attributes from original mesh...");
    MGlobal::executeCommand("select -r " + originalMeshName, false, false); 
    MGlobal::executeCommand("select -add " + faceSelectionStrings.surfaceFaces, false, false);
    MGlobal::executeCommand("transferAttributes -transferPositions 0 -transferNormals 1 -transferUVs 2 -transferColors 2 -sampleSpace 1 -sourceUvSpace \"map1\" -targetUvSpace \"map1\" -searchMethod 3 -flipUVs 0 -colorBorders 1;", false, true);
    MProgressWindow::advanceProgress(progressIncrement);
    MProgressWindow::setProgressStatus("Transferring shading sets from original mesh...");
    MGlobal::executeCommand("transferShadingSets", false, false);
    MProgressWindow::advanceProgress(progressIncrement);

    // For now at least, let the interior faces be grey and flat shaded.
    // (Otherwise, they try to extend the shading and normals from the surface and look weird).
    MProgressWindow::setProgressStatus("Setting normals and shading on interior faces...");
    MGlobal::executeCommand("sets -e -forceElement initialShadingGroup " + faceSelectionStrings.interiorFaces, false, false); 
    MGlobal::executeCommand("select -r " + faceSelectionStrings.interiorFaces, false, false);
    MGlobal::executeCommand("polySetToFaceNormal;", false, false);
    MProgressWindow::advanceProgress(progressIncrement);

    MGlobal::executeCommand("delete -ch " + newMeshName); // Delete the history of the combined mesh to decouple it from the original mesh
    MGlobal::executeCommand("select -cl;", false, false); // Clear selection

    return resultMeshDagPath;
}

void Voxelizer::addVoxelToMesh(
    const MPoint& voxelMin,
    float voxelSize,
    Voxels& voxels,
    int index
) {
    CGALHelper::SurfaceMesh cubeMesh = CGALHelper::cube(
        voxelMin,
        voxelSize
    );

    VoxelPositions positions;
    positions.corners = std::array<glm::vec3, 8>();
    int i = 0;
    for (const auto& vertex : cubeMesh.vertices()) {
        const auto& point = cubeMesh.point(vertex);
        positions.corners[i++] = glm::vec3( point.x(), point.y(), point.z());
    }

    voxels.corners[index] = positions;
    voxels.cgalMeshes[index] = cubeMesh;
}

Voxels Voxelizer::sortVoxelsByMortonCode(const Voxels& voxels) {
    // Note that the size of the sortedVoxels is equal to the number of occupied voxels. Thus, we're also filtering in this step.
    Voxels sortedVoxels;
    sortedVoxels.resize(voxels.numOccupied);

    std::vector<uint32_t> voxelIndices(voxels.size());
    std::iota(voxelIndices.begin(), voxelIndices.end(), 0); // fill with 0, 1, 2, ..., size-1

    // Sort the index array by the corresponding voxel's morton code
    std::sort(voxelIndices.begin(), voxelIndices.end(), [&voxels](int a, int b) {
        return voxels.mortonCodes[a] < voxels.mortonCodes[b];
    });

    for (size_t i = 0; i < voxels.numOccupied; ++i) {
        sortedVoxels.isSurface[i] = voxels.isSurface[voxelIndices[i]];
        sortedVoxels.cgalMeshes[i] = voxels.cgalMeshes[voxelIndices[i]];
        sortedVoxels.corners[i] = voxels.corners[voxelIndices[i]];
        sortedVoxels.mortonCodes[i] = voxels.mortonCodes[voxelIndices[i]];
        sortedVoxels.mortonCodesToSortedIdx[voxels.mortonCodes[voxelIndices[i]]] = static_cast<uint32_t>(i);
        sortedVoxels.containedTris[i] = voxels.containedTris[voxelIndices[i]];
        sortedVoxels.overlappingTris[i] = voxels.overlappingTris[voxelIndices[i]];
    }

    sortedVoxels.numOccupied = voxels.numOccupied;
    sortedVoxels.voxelSize = voxels.voxelSize;

    return sortedVoxels;
}

void Voxelizer::getVoxelMeshIntersection(void* data, MThreadRootTask* rootTask) {
    VoxelIntersectionTaskData* taskData = static_cast<VoxelIntersectionTaskData*>(data);
    Voxels* voxels = taskData->voxels;
    MString* surfaceFaces = taskData->surfaceFaces;
    MString* interiorFaces = taskData->interiorFaces;
    const MString& newMeshName = taskData->newMeshName;

    // Threads will write the outputs of the boolean operations to these vectors
    std::vector<MPointArray> meshPointsAfterIntersection(voxels->numOccupied);
    std::vector<MIntArray> polyCountsAfterIntersection(voxels->numOccupied);
    std::vector<MIntArray> polyConnectsAfterIntersection(voxels->numOccupied);
    std::vector<int> numSurfaceFacesAfterIntersection(voxels->numOccupied, 0);

    std::vector<VoxelIntersectionThreadData> threadData(voxels->numOccupied);
    for (int i = 0; i < voxels->numOccupied; ++i) {
        threadData[i].taskData = taskData;
        threadData[i].threadIdx = i;
        threadData[i].meshPointsAfterIntersection = &meshPointsAfterIntersection;
        threadData[i].polyCountsAfterIntersection = &polyCountsAfterIntersection;
        threadData[i].polyConnectsAfterIntersection = &polyConnectsAfterIntersection;
        threadData[i].numSurfaceFacesAfterIntersection = &numSurfaceFacesAfterIntersection;

        MThreadPool::createTask(Voxelizer::getSingleVoxelMeshIntersection, (void *)&threadData[i], rootTask);

        if (i % 100 == 0) {
            MThreadPool::executeAndJoin(rootTask);
            MProgressWindow::advanceProgress(100);
        }
    }
    MThreadPool::executeAndJoin(rootTask); // Execute any remaining tasks
    MProgressWindow::setProgress(voxels->numOccupied);
    threadData.clear();

    // Merge together all the mesh points, poly counts, and poly connects into one mesh
    MPointArray allMeshPoints;
    MIntArray allPolyCounts;
    MIntArray allPolyConnects;
    int startFaceIdx = 0;
    for (int i = 0; i < voxels->numOccupied; ++i) {
        voxels->vertStartIdx[i] = voxels->totalVerts;
        voxels->totalVerts += meshPointsAfterIntersection[i].length();

        int totalFaceCount = polyCountsAfterIntersection[i].length();
        int numSurfaceFaces = numSurfaceFacesAfterIntersection[i];
        int numInteriorFaces = totalFaceCount - numSurfaceFaces;
        if (numSurfaceFaces > 0) {
            (*surfaceFaces) += newMeshName + MString(".f[") + startFaceIdx + MString(":") + (startFaceIdx + numSurfaceFaces - 1) + MString("] ");
        }
        if (numInteriorFaces > 0) {
            (*interiorFaces) += newMeshName + MString(".f[") + (startFaceIdx + numSurfaceFaces) + MString(":") + (startFaceIdx + totalFaceCount - 1) + MString("] ");
        }
        startFaceIdx += totalFaceCount;

        for (unsigned int j = 0; j < meshPointsAfterIntersection[i].length(); ++j) {
            allMeshPoints.append(meshPointsAfterIntersection[i][j]);
        }

        for (unsigned int j = 0; j < polyCountsAfterIntersection[i].length(); ++j) {
            allPolyCounts.append(polyCountsAfterIntersection[i][j]);
        }
        
        for (unsigned int j = 0; j < polyConnectsAfterIntersection[i].length(); ++j) {
            allPolyConnects.append(polyConnectsAfterIntersection[i][j] + voxels->vertStartIdx[i]); // Offset the vertex indices by the start index of this voxel
        }

        // To reduce memory usage at any given time:
        meshPointsAfterIntersection[i].clear();
        polyCountsAfterIntersection[i].clear();
        polyConnectsAfterIntersection[i].clear();
    }

    // Create Maya mesh
    // Note: the new mesh currently has no shading group or vertex attributes.
    // (In the Voxelizer process, these things are transferred from the old mesh at the end of the process.)
    MStatus status;
    MFnMesh fnMesh;
    MObject newMesh = fnMesh.create(
        allMeshPoints.length(),
        allPolyCounts.length(),
        allMeshPoints,
        allPolyCounts,
        allPolyConnects,
        MObject::kNullObj, // No parent transform (could enhance later to allow a parent to be passed in)
        &status
    );

    MFnDagNode dagNode(newMesh, &status);
    dagNode.setName(newMeshName);
}

MThreadRetVal Voxelizer::getSingleVoxelMeshIntersection(void* threadData) {
    const VoxelIntersectionThreadData* data = static_cast<VoxelIntersectionThreadData*>(threadData);
    const VoxelIntersectionTaskData* taskData = data->taskData;

    int voxelIndex = data->threadIdx;
    bool doBoolean = taskData->doBoolean;
    MPointArray& meshPointsAfterIntersection = (*data->meshPointsAfterIntersection)[voxelIndex];
    MIntArray& polyCountsAfterIntersection = (*data->polyCountsAfterIntersection)[voxelIndex];
    MIntArray& polyConnectsAfterIntersection = (*data->polyConnectsAfterIntersection)[voxelIndex];
    int& numSurfaceFacesAfterIntersection = (*data->numSurfaceFacesAfterIntersection)[voxelIndex];
    std::unordered_map<Point_3, int, CGALHelper::Point3Hash> cgalVertexToMayaIdx;

    const Voxels* voxels = taskData->voxels;

    // In this case, we just return the points of the cube without intersection.
    if (!voxels->isSurface[voxelIndex] || !doBoolean) {
        cgalVertexToMayaIdx.reserve(8); // 8 vertices for a cube
        CGALHelper::toMayaMesh(
            voxels->cgalMeshes[voxelIndex],
            cgalVertexToMayaIdx,
            meshPointsAfterIntersection,
            polyCountsAfterIntersection,
            polyConnectsAfterIntersection
        );

        // If we're not doing bool ops, and this is a surface voxel, just call all surface faces
        if (voxels->isSurface[voxelIndex]) numSurfaceFacesAfterIntersection = 12; // 12 because cube = 6 * 2 (triangulated)
        return (MThreadRetVal)0;
    }

    // Each voxel tracks triangles that are contained within it and triangles that just overlap it. 
    // For the boolean intersection, we want the union of these two sets. Then onvert this subset of the original mesh to a CGAL SurfaceMesh.
    std::vector<int> originalMeshTriIndices;
    originalMeshTriIndices.reserve(voxels->containedTris[voxelIndex].size() + voxels->overlappingTris[voxelIndex].size());
    originalMeshTriIndices.insert(originalMeshTriIndices.end(), voxels->containedTris[voxelIndex].begin(), voxels->containedTris[voxelIndex].end());
    originalMeshTriIndices.insert(originalMeshTriIndices.end(), voxels->overlappingTris[voxelIndex].begin(), voxels->overlappingTris[voxelIndex].end());

    SurfaceMesh originalMeshPiece = CGALHelper::toSurfaceMesh(
        taskData->originalVertices,
        originalMeshTriIndices,
        taskData->triangles
    );
    SurfaceMesh cube = voxels->cgalMeshes[voxelIndex];
    originalMeshTriIndices.clear();

    CGALHelper::openMeshBooleanIntersection(
        originalMeshPiece,
        cube,
        taskData->sideTester,
        taskData->clipTriangles
    );
    
    // If we're not clipping triangles, the originalMeshPiece should be reduced to only the triangles
    // that are completely contained within the voxel. That way, we don't duplicate tris across voxels and get z-fighting.
    if (!taskData->clipTriangles) {
        originalMeshPiece = CGALHelper::toSurfaceMesh(
            taskData->originalVertices,
            voxels->containedTris[voxelIndex],
            taskData->triangles
        );
    }
    numSurfaceFacesAfterIntersection = static_cast<int>(originalMeshPiece.faces().size());

    // Convert CGAL meshes back to Maya representation.
    // Use same map and arrays for both calls to make one singular mesh.
    cgalVertexToMayaIdx.reserve(originalMeshPiece.vertices().size() + cube.vertices().size());
    CGALHelper::toMayaMesh(
        originalMeshPiece,
        cgalVertexToMayaIdx,
        meshPointsAfterIntersection,
        polyCountsAfterIntersection,
        polyConnectsAfterIntersection
    );

    CGALHelper::toMayaMesh(
        cube,
        cgalVertexToMayaIdx,
        meshPointsAfterIntersection,
        polyCountsAfterIntersection,
        polyConnectsAfterIntersection
    );

    return (MThreadRetVal)0;
}