#include "voxelizer.h"
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MFnTransform.h>
#include <algorithm>
#include <maya/MFnSet.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MFloatPointArray.h>
#include <numeric>

Voxels Voxelizer::voxelizeSelectedMesh(
    float gridEdgeLength,
    float voxelSize,
    MPoint gridCenter,
    const MDagPath& selectedMeshPath,
    MDagPath& voxelizedMeshPath,
    MStatus& status
) {

    int voxelsPerEdge = static_cast<int>(floor(gridEdgeLength / voxelSize));
    Voxels voxels;
    voxels.resize(voxelsPerEdge * voxelsPerEdge * voxelsPerEdge);
    
    MFnMesh selectedMesh(selectedMeshPath, &status);
    // This is what Maya does when you select a mesh and click Modify > Freeze Transformations
    // It's neceessary for the boolean operations to work correctly.
    MGlobal::executeCommand(MString("makeIdentity -apply true -t 1 -r 1 -s 1 -n 0 -pn 1"), false, true);

    std::vector<Triangle> meshTris = getTrianglesOfMesh(selectedMesh, voxelSize, status);

    getInteriorVoxels(
        meshTris,
        gridEdgeLength,
        voxelSize,
        gridCenter,
        voxels
    );

    getSurfaceVoxels(
        meshTris,
        gridEdgeLength,
        voxelSize,
        gridCenter,
        voxels
    );

    voxelizedMeshPath = createVoxels(
        voxels,
        gridEdgeLength,
        voxelSize,
        gridCenter,
        selectedMesh
    );

    status = MS::kSuccess;
    return voxels;
}

std::vector<Triangle> Voxelizer::getTrianglesOfMesh(MFnMesh& meshFn, float voxelSize, MStatus& status) {
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
    Voxels& voxels
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
                double xIntercept = getTriangleVoxelCenterIntercept(tri, voxelCenter);
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

MDagPath Voxelizer::createVoxels(
    Voxels& overlappedVoxels,
    float gridEdgeLength, 
    float voxelSize,       
    MPoint gridCenter,      
    MFnMesh& originalMesh
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

    MDagPath transformPath = originalMesh.dagPath();
    transformPath.pop(); // Move up to the transform node
    MString originalMeshName = transformPath.partialPathName();
    MString combinedMeshName = originalMeshName + "_voxelized";
    MString meshNamesConcatenated;
    for (int i = 0; i < overlappedVoxels.numOccupied; ++i) {
        MObject cube = overlappedVoxels.mayaObjects[i];
        meshNamesConcatenated += " " + MFnMesh(cube).name();

        intersectVoxelWithOriginalMesh(overlappedVoxels, cube, originalMesh.object(), i);
    }

    MDagPath finalizedVoxelMeshDagPath = finalizeVoxelMesh(combinedMeshName, meshNamesConcatenated, originalMeshName, voxelSize);
    // TODO: maybe we want to do something non-destructive that also does not obstruct the view of the original mesh
    MGlobal::executeCommand("delete " + originalMeshName, false, true); // Delete the original mesh to clean up the scene

    return finalizedVoxelMeshDagPath;
}

MDagPath Voxelizer::finalizeVoxelMesh(
    const MString& combinedMeshName,
    const MString& meshNamesConcatenated,
    const MString& originalMeshName,
    float voxelSize
) {
    MStatus status;
    // Use MEL to combine all the voxels into one mesh
    MGlobal::executeCommand(MString("polyUnite -ch 0 -mergeUVSets 1 -name ") + combinedMeshName + " " + meshNamesConcatenated, false, true);
    MGlobal::executeCommand(MString("select -r ") + combinedMeshName, false, true);
    MGlobal::executeCommand("polySetToFaceNormal;", false, true);

    // Retrieve the MObject of the resulting mesh
    MSelectionList resultSelectionList;
    resultSelectionList.add(combinedMeshName);
    MObject resultMeshObject;
    resultSelectionList.getDependNode(0, resultMeshObject);
    MDagPath resultMeshDagPath;
    MDagPath::getAPathTo(resultMeshObject, resultMeshDagPath);
    MFnMesh resultMeshFn(resultMeshDagPath, &status);

    // Use MEL to transferAttributes the normals from the original mesh to the new voxelized/combined one
    MString interiorFaces;
    MGlobal::executeCommand(MString("select -r ") + originalMeshName, false, true); // Select the original mesh first
    MString surfaceFaces = selectSurfaceFaces(resultMeshFn, combinedMeshName, voxelSize, interiorFaces);
    MGlobal::executeCommand("transferAttributes -transferPositions 0 -transferNormals 1 -transferUVs 2 -transferColors 2 -sampleSpace 1 -sourceUvSpace \"map1\" -targetUvSpace \"map1\" -searchMethod 3 -flipUVs 0 -colorBorders 1;", false, true);

    // Add a shading group to the new mesh
    // Retrieve the shading groups assigned to the original mesh and assign the first one to the surface faces of the new mesh
    MString melCmd;
    melCmd += "string $sourceObject = \"" + originalMeshName + "\";\n";
    melCmd += "string $targetObject = \"" + combinedMeshName + "\";\n";
    melCmd += "string $shapes[] = `listRelatives -s $sourceObject`;\n";
    melCmd += "string $connections[] = `listConnections $shapes[0]`;\n";
    melCmd += "string $shadingGroup = \"\";\n";
    melCmd += "for ($conn in $connections) {\n";
    melCmd += "    if (`nodeType $conn` == \"shadingEngine\") {\n";
    melCmd += "        $shadingGroup = $conn;\n";
    melCmd += "        break;\n";
    melCmd += "    }\n";
    melCmd += "}\n";
    melCmd += "if ($shadingGroup != \"\") {\n";
    melCmd += "    sets -e -forceElement $shadingGroup $targetObject;\n";
    melCmd += "} else {\n";
    melCmd += "    warning(\"No shading group found on \" + $sourceObject);\n";
    melCmd += "}\n";

    MGlobal::executeCommand(melCmd, false, true);

    // MGlobal::executeCommand("sets -e -forceElement initialShadingGroup " + interiorFaces, false, true); // Add the non-surface faces to the initial shading group

    MGlobal::executeCommand("delete -ch " + combinedMeshName + ";"); // Delete the history of the combined mesh to decouple it from the original mesh
    MGlobal::executeCommand("select -cl;", false, true);

    return resultMeshDagPath;
}

MString Voxelizer::selectSurfaceFaces(MFnMesh& meshFn, const MString& meshName, float voxelSize, MString& interiorFaces) {
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
        }
        
        interiorFaces += meshName + ".f[" + faceIter.index() + "] ";
        faceIter.next();
    }

    MGlobal::executeCommand(faceSelectionCommand + surfaceFaces, false, true);
    return surfaceFaces;
}

int faceIndices[6][4] = {
    {0, 4, 6, 2}, // Bottom
    {1, 3, 7, 5}, // Top
    {0, 1, 5, 4}, // Front
    {4, 5, 7, 6}, // Right
    {6, 7, 3, 2}, // Back
    {2, 3, 1, 0}  // Left
};

void Voxelizer::addVoxelToMesh(
    const MPoint& voxelMin,
    float voxelSize,
    Voxels& voxels,
    int index
) {
    MPointArray cubeVertices;
    MPoint voxelMax = MPoint(
        voxelMin.x + voxelSize,
        voxelMin.y + voxelSize,
        voxelMin.z + voxelSize
    );

    MPoint voxelCenter = MPoint(
        voxelMin.x + 0.5 * voxelSize,
        voxelMin.y + 0.5 * voxelSize,
        voxelMin.z + 0.5 * voxelSize
    );
    
    float halfVoxelSize = voxelSize * 0.5f;

    cubeVertices.append(voxelMin);
    cubeVertices.append(MPoint(voxelMax.x, voxelMin.y, voxelMin.z));
    cubeVertices.append(MPoint(voxelMin.x, voxelMax.y, voxelMin.z));
    cubeVertices.append(MPoint(voxelMax.x, voxelMax.y, voxelMin.z));
    cubeVertices.append(MPoint(voxelMin.x, voxelMin.y, voxelMax.z));
    cubeVertices.append(MPoint(voxelMax.x, voxelMin.y, voxelMax.z));
    cubeVertices.append(MPoint(voxelMin.x, voxelMax.y, voxelMax.z));
    cubeVertices.append(voxelMax);

    VoxelPositions newPositions;
    newPositions.corners = std::array<glm::vec3, 8>();
    for (int i = 0; i < 8; ++i) {
		newPositions.corners[i] = glm::vec3(
			cubeVertices[i].x,
			cubeVertices[i].y,
			cubeVertices[i].z
		);
    }

    voxels.corners[index] = newPositions;

    MIntArray faceCounts;
    MIntArray faceConnects;
    for (int i = 0; i < 6; ++i) {
        faceCounts.append(4);
        for (int j = 0; j < 4; ++j) {
            faceConnects.append(faceIndices[i][j]);
        }
    }

    MFnTransform cubeTransformFn;
    MObject cubeTransform = cubeTransformFn.create();
    cubeTransformFn.setRotatePivot(voxelCenter, MSpace::kTransform, true);

    // Create the cube mesh under the transform
    MFnMesh cubeMeshFn;
    MObject cube = cubeMeshFn.create(
        cubeVertices.length(),
        faceCounts.length(),
        cubeVertices,
        faceCounts,
        faceConnects,
        cubeTransform
    );

    voxels.mayaObjects[index] = cube;
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
        sortedVoxels.mayaObjects[i] = voxels.mayaObjects[voxelIndices[i]];
        sortedVoxels.corners[i] = voxels.corners[voxelIndices[i]];
        sortedVoxels.mortonCodes[i] = voxels.mortonCodes[voxelIndices[i]];
        sortedVoxels.mortonCodesToSortedIdx[voxels.mortonCodes[voxelIndices[i]]] = static_cast<uint32_t>(i);
    }

    sortedVoxels.numOccupied = voxels.numOccupied;

    return sortedVoxels;
}

void Voxelizer::intersectVoxelWithOriginalMesh(
    Voxels& voxels,
    MObject& cube,
    MObject& originalMesh,
    int index
) {
    voxels.vertStartIdx[index] = voxels.totalVerts;

    if (!voxels.isSurface[index]) {
        voxels.numVerts[index] = 8;
        voxels.totalVerts += 8;
        return;
    }

    MFnMesh cubeMeshFn(cube);

    MObjectArray objsToIntersect;
    objsToIntersect.append(cube);
    objsToIntersect.append(originalMesh);
    cubeMeshFn.booleanOps(MFnMesh::kIntersection, objsToIntersect);

    voxels.numVerts[index] = cubeMeshFn.numVertices();
    voxels.totalVerts += cubeMeshFn.numVertices();
}