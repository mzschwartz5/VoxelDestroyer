#include "voxelizer.h"
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MString.h>

MStatus Voxelizer::voxelizeSelectedMesh() {
    getTrianglesOfSelectedMesh();
}

MStatus Voxelizer::getTrianglesOfSelectedMesh() {
    MStatus status;

    // Get the current selection
    MSelectionList selection;
    MGlobal::getActiveSelectionList(selection);

    // Check if the selection is empty
    if (selection.isEmpty()) {
        MGlobal::displayError("No mesh selected.");
        return MS::kFailure;
    }

    // Get the first selected item and ensure it's a mesh
    MDagPath activeMeshDagPath;
    status = selection.getDagPath(0, activeMeshDagPath);
    if (status != MS::kSuccess || !activeMeshDagPath.hasFn(MFn::kMesh)) {
        MGlobal::displayError("The selected item is not a mesh.");
        return MS::kFailure;
    }

    // Create an MFnMesh function set to operate on the selected mesh
    MFnMesh meshFn(activeMeshDagPath, &status);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to create MFnMesh.");
        return status;
    }

    // Use MFnMesh::getTriangles to retrieve triangle data
    status = meshFn.getTriangles(triangleCounts, triangleVertices);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Failed to retrieve triangles.");
        return status;
    }

	return MS::kSuccess;
}

void Voxelizer::tearDown() {
    triangleCounts.clear();
    triangleVertices.clear();
}