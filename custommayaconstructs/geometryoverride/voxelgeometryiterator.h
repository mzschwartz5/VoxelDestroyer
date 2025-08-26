#pragma once
#include "voxelshapegeometrydata.h"
#include <maya/MPxGeometryIterator.h>
#include <maya/MPoint.h>
#include <maya/MObjectArray.h>
#include <maya/MObject.h>
#include <maya/MFloatVectorArray.h>

class VoxelShapeGeometry {
public:
    VoxelShapeGeometry() = default;
    ~VoxelShapeGeometry() = default;
    MPointArray   vertices;
    MIntArray     face_counts;
    MIntArray     face_connects;
    MFloatVectorArray  normals;
    MFloatArray   ucoord;
    MFloatArray   vcoord;
};

class VoxelGeometryIterator: public MPxGeometryIterator {

public:
    VoxelGeometryIterator(const VoxelShapeGeometry* userGeometry, MObjectArray& components) : 
        MPxGeometryIterator(const_cast<VoxelShapeGeometry*>(userGeometry), components),
        geometry(const_cast<VoxelShapeGeometry*>(userGeometry))
    {
        reset();
    }

    VoxelGeometryIterator(const VoxelShapeGeometry* userGeometry, MObject& component) : 
        MPxGeometryIterator(const_cast<VoxelShapeGeometry*>(userGeometry), component),
        geometry(const_cast<VoxelShapeGeometry*>(userGeometry))
    {
        reset();
    }

    ~VoxelGeometryIterator() override = default;

    MPoint point() const override {
        MPoint point;
        if (!geometry) return point;

        unsigned int idx = index();
        
        if (idx < geometry->vertices.length()) {
            point = geometry->vertices[idx];
        }
        return point;
    }

    void setPoint(const MPoint& point) const override {
        if (!geometry) return;

        unsigned int idx = index();
        if (idx >= geometry->vertices.length()) return;
        
        geometry->vertices.set(point, idx);
    }

    void reset() {
        MPxGeometryIterator::reset();
        setCurrentPoint(0);
        if (!geometry) return;

        int maxVertex = geometry->vertices.length();
        setMaxPoints(maxVertex);
    }

    int iteratorCount() const override {
        return geometry->vertices.length();
    }

    bool hasPoints() const override {
        return true;
    }

    bool hasNormals() const override {
        return true;
    }

private:
    VoxelShapeGeometry* geometry;

};