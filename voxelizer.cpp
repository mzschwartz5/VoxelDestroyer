#include "voxelizer.h"
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MFnTransform.h>
#include <algorithm>
#include <maya/MFnSet.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MFloatPointArray.h>
#include <numeric>
#include "cgalhelper.h"

Voxels Voxelizer::voxelizeSelectedMesh(
    float gridEdgeLength,
    float voxelSize,
    MPoint gridCenter,
    const MDagPath& selectedMeshPath,
    MDagPath& voxelizedMeshPath,
    bool voxelizeSurface,
    bool voxelizeInterior,
    bool doBoolean,
    bool clipTriangles,
    MStatus& status
) {

    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    Voxels voxels;
    voxels.resize(voxelsPerEdge * voxelsPerEdge * voxelsPerEdge);
    status = MThreadPool::init();
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to initialize Maya thread pool for voxelization.");
        return voxels;
    }
    
    MFnMesh selectedMesh(selectedMeshPath, &status);
    // This is what Maya does when you select a mesh and click Modify > Freeze Transformations
    // It's neceessary for the boolean operations to work correctly.
    MGlobal::executeCommand(MString("makeIdentity -apply true -t 1 -r 1 -s 1 -n 0 -pn 1"), false, true);

    std::vector<Triangle> meshTris = getTrianglesOfMesh(selectedMesh, voxelSize, status);

    if (voxelizeInterior) {
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
        getSurfaceVoxels(
            meshTris,
            gridEdgeLength,
            voxelSize,
            gridCenter,
            voxels
        );
    }

    voxelizedMeshPath = createVoxels(
        voxels,
        gridEdgeLength,
        voxelSize,
        gridCenter,
        selectedMesh,
        meshTris,
        doBoolean,
        clipTriangles
    );

    MThreadPool::release(); // reduce reference count incurred by init()
    return voxels;
}

std::vector<Triangle> Voxelizer::getTrianglesOfMesh(MFnMesh& meshFn, float voxelSize, MStatus& status) {
    MIntArray triangleCounts;
    MIntArray vertexIndices;
    status = meshFn.getTriangles(triangleCounts, vertexIndices);

    // This will later be done on the gpu, one thread per triangle
    std::vector<Triangle> triangles;
    std::array<int, 3> vertIndices;
    for (unsigned int i = 0; i < vertexIndices.length() / 3; ++i) {

        vertIndices[0] = vertexIndices[3 * i];
        vertIndices[1] = vertexIndices[3 * i + 1];
        vertIndices[2] = vertexIndices[3 * i + 2];

        Triangle triangle = processMayaTriangle(meshFn, vertIndices, voxelSize);
        triangles.push_back(triangle);
    }

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
    Voxels& voxels
) {
    
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
                    voxels.triangleIndices[index].push_back(triIdx);
                }
            }
        }

        ++triIdx;
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
    Voxels& voxels,
    const MFnMesh& selectedMesh
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

MDagPath Voxelizer::createVoxels(
    Voxels& overlappedVoxels,
    float gridEdgeLength, 
    float voxelSize,       
    MPoint gridCenter,      
    MFnMesh& originalMesh,
    const std::vector<Triangle>& meshTris,
    bool doBoolean,
    bool clipTriangles
) {
    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    MPoint gridMin = gridCenter - MVector(gridEdgeLength / 2, gridEdgeLength / 2, gridEdgeLength / 2);

    for (int x = 0; x < voxelsPerEdge; ++x) {
        for (int y = 0; y < voxelsPerEdge; ++y) {
            for (int z = 0; z < voxelsPerEdge; ++z) {
                int index = x * voxelsPerEdge * voxelsPerEdge + y * voxelsPerEdge + z;
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

    overlappedVoxels = sortVoxelsByMortonCode(overlappedVoxels);

    // Prepare for boolean operations
    // We only want to create the acceleration structure once (which is why we do it here, before all the boolean ops begin)
    std::vector<int> allTriangleIndices(meshTris.size());
    std::iota(allTriangleIndices.begin(), allTriangleIndices.end(), 0); // Fill with indices from 0 to size-1
    MPointArray originalVertices;
    originalMesh.getPoints(originalVertices, MSpace::kWorld);
    SurfaceMesh originalMeshCGAL = CGALHelper::toSurfaceMesh(&originalVertices, allTriangleIndices, &meshTris);
    Tree aabbTree(originalMeshCGAL.faces().first, originalMeshCGAL.faces().second, originalMeshCGAL);
    SideTester sideTester(aabbTree);

    MDagPath transformPath = originalMesh.dagPath();
    transformPath.pop(); // Move up to the transform node
    MFnTransform transform(transformPath);
    MPoint originalPivot = transform.rotatePivot(MSpace::kWorld);
    MString originalMeshName = transformPath.partialPathName();
    MStringArray resultMeshNames; // the names of each voxel after intersection with the original mesh
    resultMeshNames.setLength(overlappedVoxels.numOccupied); 

    VoxelIntersectionTaskData taskData {
        &overlappedVoxels,
        &originalVertices,
        &meshTris,
        &sideTester,
        doBoolean,
        clipTriangles,
        &resultMeshNames
    };

    MThreadPool::newParallelRegion(
        Voxelizer::getVoxelMeshIntersection,
        (void*)&taskData
    );
    MThreadPool::release(); // reduce reference count incurred by opening a new parallel region

    MString resultMeshNamesConcatenated;
    for (const MString& meshName : resultMeshNames) {
        if (meshName.length() == 0) continue; // Skip empty names
        resultMeshNamesConcatenated += meshName + " ";
    }

    // TODO: if no boolean, should get rid of non-manifold geometry
    MDagPath finalizedVoxelMeshDagPath = finalizeVoxelMesh(resultMeshNamesConcatenated, originalMeshName, originalPivot, voxelSize);
    // TODO: maybe we want to do something non-destructive that also does not obstruct the view of the original mesh (or just allow for undo)
    MGlobal::executeCommand("delete " + originalMeshName, false, true); // Delete the original mesh to clean up the scene

    return finalizedVoxelMeshDagPath;
}

MDagPath Voxelizer::finalizeVoxelMesh(
    const MString& resultMeshNamesConcatenated,
    const MString& originalMeshName,
    const MPoint& originalPivot,
    float voxelSize
) {
    MStatus status;
    // Use MEL to combine all the voxels into one mesh
    MString combinedMeshName = originalMeshName + "_voxelized";
    MGlobal::executeCommand(MString("polyUnite -ch 0 -mergeUVSets 1 -name ") + combinedMeshName + " " + resultMeshNamesConcatenated, false, true);
    MGlobal::executeCommand(MString("select -r ") + combinedMeshName, false, true);
    MGlobal::executeCommand("polySetToFaceNormal;", false, true); // Helps with the step where we select surface faces (use normals as ray marcing direction)

    // Retrieve the MObject of the resulting mesh
    MSelectionList resultSelectionList;
    resultSelectionList.add(combinedMeshName);
    MObject resultMeshObject;
    resultSelectionList.getDependNode(0, resultMeshObject);
    MDagPath resultMeshDagPath;
    MDagPath::getAPathTo(resultMeshObject, resultMeshDagPath);
    MFnMesh resultMeshFn(resultMeshDagPath, &status);
    MFnTransform resultTransformFn(resultMeshDagPath, &status);
    resultTransformFn.setRotatePivot(originalPivot, MSpace::kTransform, false); // Set the pivot to the original mesh's pivot

    // Use MEL to transferAttributes the normals from the original mesh to the new voxelized/combined one
    MString interiorFaces;
    MGlobal::executeCommand("select -r " + originalMeshName, false, false); // Select the original mesh first
    // MGlobal::executeCommand("select -add " + combinedMeshName, false, false); // Then add the new mesh to the selection
    MString surfaceFaces = selectSurfaceFaces(resultMeshFn, combinedMeshName, voxelSize, interiorFaces);
    MGlobal::executeCommand("transferAttributes -transferPositions 0 -transferNormals 1 -transferUVs 2 -transferColors 2 -sampleSpace 1 -sourceUvSpace \"map1\" -targetUvSpace \"map1\" -searchMethod 3 -flipUVs 0 -colorBorders 1;", false, true);

    // Transfer shading group to the new mesh
    MGlobal::executeCommand("select -r " + originalMeshName, false, false); // Select the original mesh first
    MGlobal::executeCommand("select -add " + combinedMeshName, false, false); // Then add the new mesh to the selection
    MGlobal::executeCommand("transferShadingSets", false, false);

    MGlobal::executeCommand("sets -e -forceElement initialShadingGroup " + interiorFaces, false, true); // Add the non-surface faces to the initial shading group

    MGlobal::executeCommand("delete -ch " + combinedMeshName); // Delete the history of the combined mesh to decouple it from the original mesh
    MGlobal::executeCommand("select -cl;", false, false); // Clear selection

    return resultMeshDagPath;
}

/**
 * Use raycasting to find and select the surface faces of the voxelized mesh (after combining all voxels).
 * Only surface faces will not have any intersections with the mesh along their normal direction.
 */
MString Voxelizer::selectSurfaceFaces(MFnMesh& meshFn, const MString& meshName, float voxelSize, MString& interiorFaces) {
    // Can short circuit raycasting by bounding the ray length to the diagonal of a voxel. If no intersection is found by then, it must be a surface face.
    float maxDistance = 1.73f * 1.1f * voxelSize; // 1.73 is the diagonal of a cube with edge length 1.0
    MMeshIsectAccelParams accelParams = MFnMesh::autoUniformGridParams();
    MItMeshPolygon faceIter(meshFn.object());

    MString surfaceFaces = "";
    MString faceSelectionCommand = "select -add ";
    while (!faceIter.isDone()) {
        MVector normal;
        faceIter.getNormal(normal, MSpace::kWorld);
        MPoint faceCenter = faceIter.center(MSpace::kWorld);
        MPoint raySource = faceCenter + normal * 0.01; // Offset slightly along the normal
        MVector rayDirection = normal;

        MFloatPoint hitPoint;
        bool hit = meshFn.anyIntersection(raySource, rayDirection, nullptr, nullptr, false, MSpace::kWorld, maxDistance, false, &accelParams, hitPoint, nullptr, nullptr, nullptr, nullptr, nullptr);

        if (!hit) {
            surfaceFaces += meshName + ".f[" + faceIter.index() + "] ";
            faceIter.next();
            continue;
        }
        
        interiorFaces += meshName + ".f[" + faceIter.index() + "] ";
        faceIter.next();
    }

    MGlobal::executeCommand(faceSelectionCommand + surfaceFaces, false, true);
    return surfaceFaces;
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
        sortedVoxels.triangleIndices[i] = voxels.triangleIndices[voxelIndices[i]];
    }

    sortedVoxels.numOccupied = voxels.numOccupied;

    return sortedVoxels;
}

void Voxelizer::getVoxelMeshIntersection(void* data, MThreadRootTask* rootTask) {
    VoxelIntersectionTaskData* taskData = static_cast<VoxelIntersectionTaskData*>(data);
    Voxels* voxels = taskData->voxels;
    MStringArray* resultMeshNames = taskData->resultMeshNames;

    // Threads will write the outputs of the boolean operations to these vectors
    std::vector<MPointArray> meshPointsAfterIntersection(voxels->numOccupied);
    std::vector<MIntArray> polyCountsAfterIntersection(voxels->numOccupied);
    std::vector<MIntArray> polyConnectsAfterIntersection(voxels->numOccupied);

    std::vector<VoxelIntersectionThreadData> threadData(voxels->numOccupied); // Could avoid this by passing a pointer to everything but index
    for (int i = 0; i < voxels->numOccupied; ++i) {
        threadData[i].taskData = taskData;
        threadData[i].threadIdx = i;
        threadData[i].meshPointsAfterIntersection = &meshPointsAfterIntersection;
        threadData[i].polyCountsAfterIntersection = &polyCountsAfterIntersection;
        threadData[i].polyConnectsAfterIntersection = &polyConnectsAfterIntersection;

        MThreadPool::createTask(Voxelizer::getSingleVoxelMeshIntersection, (void *)&threadData[i], rootTask);
    }
    MThreadPool::executeAndJoin(rootTask);
    threadData.clear();

    for (int i = 0; i < voxels->numOccupied; ++i) {
        voxels->vertStartIdx[i] = voxels->totalVerts;
        
        // Create Maya mesh
        // Note: the new mesh currently has no shading group or vertex attributes.
        // (In the Voxelizer process, these things are transferred from the old mesh at the end of the process.)
        MStatus status;
        MFnMesh fnMesh;
        MObject newMesh = fnMesh.create(
            meshPointsAfterIntersection[i].length(),
            polyCountsAfterIntersection[i].length(),
            meshPointsAfterIntersection[i],
            polyCountsAfterIntersection[i],
            polyConnectsAfterIntersection[i],
            MObject::kNullObj, // No parent transform (could enhance later to allow a parent to be passed in)
            &status
        );

        voxels->totalVerts += fnMesh.numVertices();

        MString newMeshName = MFnDependencyNode(newMesh).name();
        (*resultMeshNames)[i] = newMeshName;

        // Erase vector entries at current index to avoid potential memory pressure issues
        meshPointsAfterIntersection[i].clear();
        polyCountsAfterIntersection[i].clear();
        polyConnectsAfterIntersection[i].clear();
    }
}

MThreadRetVal Voxelizer::getSingleVoxelMeshIntersection(void* threadData) {
    const VoxelIntersectionThreadData* data = static_cast<VoxelIntersectionThreadData*>(threadData);
    const VoxelIntersectionTaskData* taskData = data->taskData;

    int voxelIndex = data->threadIdx;
    bool doBoolean = taskData->doBoolean;
    MPointArray& meshPointsAfterIntersection = (*data->meshPointsAfterIntersection)[voxelIndex];
    MIntArray& polyCountsAfterIntersection = (*data->polyCountsAfterIntersection)[voxelIndex];
    MIntArray& polyConnectsAfterIntersection = (*data->polyConnectsAfterIntersection)[voxelIndex];
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
        return (MThreadRetVal)0;
    }

    // Each voxel contains the triangles of the original mesh that overlap it
    // This is what we want to use for the boolean intersection. Convert this subset of the original mesh to a CGAL SurfaceMesh.
    SurfaceMesh originalMeshPiece = CGALHelper::toSurfaceMesh(
        taskData->originalVertices,
        voxels->triangleIndices[voxelIndex],
        taskData->triangles
    );
    SurfaceMesh cube = voxels->cgalMeshes[voxelIndex];

    cgalVertexToMayaIdx.reserve(originalMeshPiece.vertices().size() + cube.vertices().size());

    CGALHelper::openMeshBooleanIntersection(
        originalMeshPiece,
        cube,
        taskData->sideTester,
        taskData->clipTriangles
    );

    // Convert CGAL meshes back to Maya representation.
    // Use same map and arrays for both calls to make one singular mesh.
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