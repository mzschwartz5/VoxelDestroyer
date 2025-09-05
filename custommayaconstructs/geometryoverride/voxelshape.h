#pragma once
#include <maya/MPxSurfaceShape.h>
#include <maya/MFnMeshData.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnPluginData.h>
#include <maya/MArrayDataBuilder.h>

class VoxelShape : public MPxSurfaceShape {
    
public:
    inline static MTypeId id = { 0x0012A3B4 };
    inline static MString typeName = "VoxelShape";
    inline static MString drawDbClassification = "drawdb/subscene/voxelSubsceneOverride/voxelshape";
    // Attributes (required to work with deformers)
    inline static MObject aInputGeom; 
    inline static MObject aOutputGeom;
    inline static MObject aWorldGeom;
    inline static MObject aCachedGeom;
    
    static void* creator() { return new VoxelShape(); }
    
    static MStatus initialize() {
        MStatus status;
        MFnTypedAttribute tAttr;
        aInputGeom = tAttr.create("inMesh", "in", MFnData::kMesh, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(false);
        tAttr.setReadable(false);
        tAttr.setWritable(true);
        status = addAttribute(aInputGeom);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aOutputGeom = tAttr.create("outMesh", "out", MFnData::kMesh, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(false);
        tAttr.setWritable(false);
        tAttr.setReadable(true);
        status = addAttribute(aOutputGeom);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aWorldGeom = tAttr.create("worldMesh", "wmesh", MFnData::kMesh, MObject::kNullObj, &status);
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

        aCachedGeom = tAttr.create("cachedMesh", "cm", MFnData::kMesh, MObject::kNullObj, &status);
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
   
    bool isBounded() const override { return true; }

    MBoundingBox boundingBox() const override {
        MDagPath srcDagPath = pathToOriginalGeometry();
        if (!srcDagPath.isValid()) return MBoundingBox();

        return MFnDagNode(srcDagPath).boundingBox();
    }

    MDagPath pathToOriginalGeometry() const {
        MPlug inPlug(thisMObject(), aInputGeom);
        if (inPlug.isNull()) return MDagPath();

        MPlugArray sources;
        if (!inPlug.connectedTo(sources, true, false) || sources.length() <= 0) return MDagPath();

        const MPlug& srcPlug = sources[0];
        MObject srcNode = srcPlug.node();
        if (srcNode.isNull() || !srcNode.hasFn(MFn::kMesh)) return MDagPath();

        MDagPath srcDagPath;
        MStatus status = MDagPath::getAPathTo(srcNode, srcDagPath);
        if (status != MS::kSuccess) return MDagPath();

        return srcDagPath;
    }

    MObject localShapeInAttr() const override { return aInputGeom; }
    MObject localShapeOutAttr() const override { return aOutputGeom; }
    MObject worldShapeOutAttr() const override { return aWorldGeom; }
    MObject cachedShapeAttr() const override { return aCachedGeom; }
    
    MObject geometryData() const override { 
        const MDagPath srcDagPath = pathToOriginalGeometry();
        if (!srcDagPath.isValid()) return MObject::kNullObj;

        MFnMesh fnMesh(srcDagPath);
        return fnMesh.object();
    }

    bool acceptsGeometryIterator(bool writeable = true) override {
        return false;
    }

    bool acceptsGeometryIterator(MObject& object, bool writeable = true, bool forReadOnly = false) override {
        return false;
    }

    // Need to override this method for the shape to be usable with deformers, but since our attributes of are type kMesh 
    // (rather than a custom MpxGeometryData type), we don't actually need to supply an iterator.
    MPxGeometryIterator* geometryIteratorSetup(MObjectArray& componentList, MObject& component, bool forReadOnly = false ) {
        return nullptr; 
    }

    bool excludeAsPluginShape() const {
        // Always display this shape in the outliner, even when plugin shapes are excluded.
        return false;
    }

    MObject createFullVertexGroup() const override {
        MFnSingleIndexedComponent fnComponent;
        MObject fullComponent = fnComponent.create( MFn::kMeshVertComponent );
        MObject geomData = geometryData();
        if (geomData.isNull()) return MObject::kNullObj;

        int numVertices = MFnMesh(geomData).numVertices();
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
        MObject inputGeomObj = dataBlock.inputValue(aInputGeom).data();
        if (inputGeomObj.isNull()) return MS::kFailure;
        MFnMesh fnMesh(inputGeomObj);

        MFnMeshData dataFn;
        MObject cachedGeomObj = dataFn.create();
        fnMesh.copy(inputGeomObj, cachedGeomObj);
        dataBlock.outputValue(aCachedGeom).set(cachedGeomObj);

        return MS::kSuccess;
    }

    MStatus computeOutputGeom(const MPlug& plug, MDataBlock& dataBlock) {
        if (computeCachedGeom(plug, dataBlock) != MS::kSuccess) return MS::kFailure;

        MObject cachedData = dataBlock.outputValue(aCachedGeom).data();
        if (cachedData.isNull()) return MS::kFailure;

        MFnMesh fnMesh(cachedData);
        MFnMeshData dataFn;
        MObject outputGeomObj = dataFn.create();
        fnMesh.copy(cachedData, outputGeomObj);
        dataBlock.outputValue(aOutputGeom).set(outputGeomObj);

        return MS::kSuccess;
    }

    MStatus computeWorldGeom(const MPlug& plug, MDataBlock& dataBlock) {
        MStatus status;
        computeOutputGeom(plug, dataBlock);

        MObject outputData = dataBlock.outputValue(aOutputGeom).data();
        if (outputData.isNull()) return MS::kFailure;

        MFnMesh fnMesh(outputData);
        MFnMeshData dataFn;
        MObject worldGeomObj = dataFn.create();
        fnMesh.copy(outputData, worldGeomObj);

        int arrayIndex = plug.logicalIndex(&status);
        MArrayDataHandle worldHandle = dataBlock.outputArrayValue(aWorldGeom, &status);
        MArrayDataBuilder builder = worldHandle.builder(&status);
        MDataHandle outHandle = builder.addElement(arrayIndex, &status);
        outHandle.set(worldGeomObj);
        worldHandle.set(builder);
        dataBlock.setClean(plug);

        return MS::kSuccess;
    }

private:
    

};