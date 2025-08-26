#pragma once
#include <maya/MPxSurfaceShape.h>
#include "voxelshapegeometrydata.h"
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnPluginData.h>
#include <maya/MArrayDataBuilder.h>

class VoxelShape : public MPxSurfaceShape {
    
public:
    inline static MTypeId id = { 0x0012A3B4 };
    inline static MString typeName = "VoxelShape";
    inline static MString drawDbClassification = "drawdb/geometry/voxelshape";
    // Attributes (required to work with deformers)
    inline static MObject aInputGeom; 
    inline static MObject aOutputGeom;
    inline static MObject aWorldGeom;
    inline static MObject aCachedGeom;
    
    static void* creator() { return new VoxelShape(); }
    
    static MStatus initialize() {
        MStatus status;
        MFnTypedAttribute tAttr;
        aInputGeom = tAttr.create("inMesh", "in", VoxelShapeGeometryData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(false);
        tAttr.setReadable(true);
        status = addAttribute(aInputGeom);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aOutputGeom = tAttr.create("outMesh", "out", VoxelShapeGeometryData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(false);
        tAttr.setWritable(false);
        tAttr.setReadable(true);
        status = addAttribute(aOutputGeom);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aWorldGeom = tAttr.create("worldMesh", "wmesh", VoxelShapeGeometryData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setWritable(false);
        tAttr.setCached(false);
        tAttr.setReadable(true);
        tAttr.setArray(true);
        tAttr.setStorable(false);
        tAttr.setUsesArrayDataBuilder(true);
        tAttr.setDisconnectBehavior(MFnAttribute::kDelete);
        status = addAttribute(aWorldGeom);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aCachedGeom = tAttr.create("cachedMesh", "cm", VoxelShapeGeometryData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(true);
        tAttr.setWritable(true);
        tAttr.setReadable(true);
        status = addAttribute(aCachedGeom);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        attributeAffects(aInputGeom, aOutputGeom);
        attributeAffects(aInputGeom, aWorldGeom);
        attributeAffects(aCachedGeom, aOutputGeom);
        attributeAffects(aCachedGeom, aWorldGeom);

        return MS::kSuccess;
    }

    void postConstructor() override {
        MPxSurfaceShape::postConstructor();
        setRenderable(true); 
    }

    VoxelShape() = default;
    ~VoxelShape() override = default;
   
    bool isBounded() const override { return false; }
    
    MObject localShapeInAttr() const override { return aInputGeom; }
    MObject localShapeOutAttr() const override { return aOutputGeom; }
    MObject worldShapeOutAttr() const override { return aWorldGeom; }
    MObject cachedShapeAttr() const override { return aCachedGeom; }
    
    MObject geometryData() const override { 
        // Following pattern set out in API examples
        VoxelShape* nonConstThis = const_cast<VoxelShape*>(this);
        MDataBlock dataBlock = nonConstThis->forceCache();
        MDataHandle geomHandle = dataBlock.inputValue(aInputGeom);
        if (geomHandle.data().isNull()) return MObject::kNullObj;

        return geomHandle.data();
    }

    bool acceptsGeometryIterator(bool writeable = true) override {
        return true;
    }

    bool acceptsGeometryIterator(MObject& object, bool writeable = true, bool forReadOnly = false) override {
        return true;
    }

    MPxGeometryIterator* geometryIteratorSetup(MObjectArray& componentList, MObject& component, bool forReadOnly = false ) {
        VoxelShapeGeometryData* geometry = getGeometry();
        if (!geometry) return nullptr;

        bool useComponents = (componentList.length() > 0);
        return geometry->iterator(componentList, component, useComponents);
    }

    bool excludeAsPluginShape() const {
        // Always display this shape in the outliner, even when plugin shapes are excluded.
        return false;
    }

    VoxelShapeGeometryData* getGeometry() const {
        MStatus status;
        MObject geomData = geometryData();
        if (geomData.isNull()) return nullptr;

        MFnPluginData fnData(geomData, &status);
        return static_cast<VoxelShapeGeometryData*>(fnData.data(&status));
    }

    MObject createFullVertexGroup() const override {
        MFnSingleIndexedComponent fnComponent;
        MObject fullComponent = fnComponent.create( MFn::kMeshVertComponent );
        VoxelShapeGeometryData* geometry = getGeometry();
        if (!geometry) return MObject::kNullObj;

        int numVertices = geometry->getNumVertices();
        fnComponent.setCompleteData(numVertices);
        return fullComponent;
    }

    bool match( const MSelectionMask & mask, const MObjectArray& componentList ) const override {
        if( componentList.length() == 0 ) {
            return mask.intersects( MSelectionMask::kSelectMeshes );
        }

        for ( uint i=0; i < componentList.length(); ++i ) {
            if ((componentList[i].apiType() == MFn::kMeshVertComponent) 
                && (mask.intersects(MSelectionMask::kSelectMeshVerts))) 
            {
                return true;
            }
        }
        return false;
    }

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override {
        if (plug == aOutputGeom) {
            computeOutputGeom(plug, dataBlock);
            return MS::kSuccess;
        }
        else if (plug == aCachedGeom) {
            computeOutputGeom(plug, dataBlock);
            return MS::kSuccess;
        }
        else if (plug == aWorldGeom) {
            computeWorldGeom(plug, dataBlock);
            return MS::kSuccess;
        }

        return MS::kUnknownParameter;
    }

    MStatus computeCachedGeom(const MPlug& plug, MDataBlock& dataBlock) {
        VoxelShapeGeometryData* inputGeom = static_cast<VoxelShapeGeometryData*>(dataBlock.inputValue(aInputGeom).asPluginData());
        if (!inputGeom) return MS::kFailure;

        // Create an object for cached geom and copy the input surface into it.
        MFnPluginData cachedDataFn;
        MObject cachedDataObj = cachedDataFn.create(VoxelShapeGeometryData::id);
        VoxelShapeGeometryData* voxelShapeData = static_cast<VoxelShapeGeometryData*>(cachedDataFn.data());
        voxelShapeData->smartCopy(inputGeom);
        dataBlock.outputValue(aCachedGeom).set(voxelShapeData);

        return MS::kSuccess;
    }

    MStatus computeOutputGeom(const MPlug& plug, MDataBlock& dataBlock) {
        if (!computeCachedGeom(plug, dataBlock)) return MS::kFailure;

        VoxelShapeGeometryData* cachedData = static_cast<VoxelShapeGeometryData*>(dataBlock.outputValue(aCachedGeom).asPluginData());
        if (!cachedData) return MS::kFailure;

        MFnPluginData outputDataFn;
        outputDataFn.create(VoxelShapeGeometryData::id);
        VoxelShapeGeometryData* outputVoxelShapeData = static_cast<VoxelShapeGeometryData*>(outputDataFn.data());
        outputVoxelShapeData->smartCopy(cachedData);
        dataBlock.outputValue(aOutputGeom).set(outputVoxelShapeData);

        return MS::kSuccess;
    }

    MStatus computeWorldGeom(const MPlug& plug, MDataBlock& dataBlock) {
        MStatus status;
        computeOutputGeom(plug, dataBlock);

        VoxelShapeGeometryData* outputData = static_cast<VoxelShapeGeometryData*>(dataBlock.outputValue(aOutputGeom).asPluginData());
        if (!outputData) return MS::kFailure;

        MFnPluginData worldDataFn;
        worldDataFn.create(VoxelShapeGeometryData::id);
        VoxelShapeGeometryData* worldVoxelShapeData = static_cast<VoxelShapeGeometryData*>(worldDataFn.data());
        worldVoxelShapeData->smartCopy(outputData);
        worldVoxelShapeData->setMatrix(getWorldMatrix(dataBlock, 0));

        int arrayIndex = plug.logicalIndex(&status);
        MArrayDataHandle worldHandle = dataBlock.outputArrayValue(aWorldGeom, &status);
        MArrayDataBuilder builder = worldHandle.builder(&status);
        MDataHandle outHandle = builder.addElement(arrayIndex, &status);
        outHandle.set(worldVoxelShapeData);
        worldHandle.set(builder);
        dataBlock.setClean(plug);

        return MS::kSuccess;
    }

private:
    

};