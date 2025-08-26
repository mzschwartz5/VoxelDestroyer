#pragma once
#include <maya/MPxGeometryData.h>
#include "voxelgeometryiterator.h"
#include <maya/MPointArray.h>
#include <maya/MIntArray.h>
#include <maya/MVectorArray.h>
#include <maya/MFloatArray.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>
#include <maya/MStatus.h>
#include <maya/MFnSingleIndexedComponent.h>

/**
 * This is the data that gets passed around in Maya's DG. It's a container for the VoxelShape's geometry data.
 * Used by VoxelShape.
 */
class VoxelShapeGeometryData : public MPxGeometryData {

public:
    inline static const MString typeName = "VoxelShapeGeometryData";
    inline static const MTypeId id = 0x8100A;

    VoxelShapeGeometryData() = default;
    ~VoxelShapeGeometryData() override = default;

    static void* creator() {
        return new VoxelShapeGeometryData();
    }

	MTypeId typeId() const override { return id; }
	MString name() const override { return typeName; }

    /**
     * Updates the complete vertex group for the given component.
     * Returns false if nothing was updated. Called by Maya internals.
     */
    bool updateCompleteVertexGroup( MObject & component ) const override {
        MStatus status;
        MFnSingleIndexedComponent fnComponent( component, &status );
        if ( !status || !fnComponent.isComplete() ) return false;
        
        int maxVerts;
        fnComponent.getCompleteData( maxVerts );
        int numVertices = getNumVertices();
        if ( (numVertices <= 0) || (maxVerts == numVertices) ) return false;

        fnComponent.setCompleteData( numVertices );
        return true;
    }

    MPxGeometryIterator* iterator( 
        MObjectArray & componentList,
        MObject & component,
        bool useComponents
    ) override 
    {
        return useComponents ? 
            new VoxelGeometryIterator(&geometry, componentList) : 
            new VoxelGeometryIterator(&geometry, component);
    }
    
    MPxGeometryIterator* iterator(
        MObjectArray & componentList,
        MObject & component,
        bool useComponents,
        bool world) 
    const override 
    {
        return useComponents ? 
            new VoxelGeometryIterator(&geometry, componentList) : 
            new VoxelGeometryIterator(&geometry, component);
    }

    void copy(const MPxData& other) override {
        const VoxelShapeGeometryData* otherData = static_cast<const VoxelShapeGeometryData*>(&other);
        smartCopy(otherData);
    }

    bool smartCopy(const MPxGeometryData* srcGeom) override {
        if (!srcGeom) return false;
        const VoxelShapeGeometryData* voxelSrcGeom = static_cast<const VoxelShapeGeometryData*>(srcGeom);
        geometry = voxelSrcGeom->geometry;
        
        return true;
    }

    int getNumVertices() const {
        return geometry.vertices.length();
    }

    void setGeometry(VoxelShapeGeometry&& geom) {
        geometry = std::move(geom);
    }

    // TODO: ASCII and Binary Read/Write methods for save/load

private:
    VoxelShapeGeometry geometry;
};

