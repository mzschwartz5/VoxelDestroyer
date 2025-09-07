#pragma once
#include <maya/MPxSurfaceShape.h>
#include <maya/MFnMeshData.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnPluginData.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MDagModifier.h>
#include <maya/MDGModifier.h>
#include <maya/MCallbackIdArray.h>
#include <maya/MNodeMessage.h>
#include "../../voxelizer.h"
#include "../../pbd.h"
#include "../deformerdata.h"
#include "../particledata.h"
#include "../d3d11data.h"
#include "../../directx/compute/deformverticescompute.h"
#include <d3d11.h>
#include <wrl/client.h>
#include "directx/directx.h"
using Microsoft::WRL::ComPtr;

class VoxelShape : public MPxSurfaceShape {
    
public:
    inline static MTypeId id = { 0x0012A3B4 };
    inline static MString typeName = "VoxelShape";
    inline static MString drawDbClassification = "drawdb/subscene/voxelSubsceneOverride/voxelshape";
    
    inline static MObject aInputGeom;
    inline static MObject aDeformerData;
    inline static MObject aParticleSRV;
    inline static MObject aParticleData;
    inline static MObject aTrigger;

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

        // Vertex buffer offsets (TODO: better name for data / attribute)
        aDeformerData = tAttr.create("deformerData", "ddt", DeformerData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(true); // YES storable - we want this data to persist with save/load
        tAttr.setWritable(false); // set by voxelizer directly, not written by connection. (TODO: for now...)
        tAttr.setReadable(false);
        status = addAttribute(aDeformerData);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // Contains the particle positions (on the CPU) and a few other things not used by this node.
        aParticleData = tAttr.create("particleData", "pdt", ParticleData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(false); // NOT storable - just for initialization
        tAttr.setWritable(true);
        tAttr.setReadable(false); 
        status = addAttribute(aParticleData);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        aParticleSRV = tAttr.create("particleSRV", "psrv", D3D11Data::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(false);
        tAttr.setWritable(true);
        tAttr.setReadable(false);
        status = addAttribute(aParticleSRV);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // This is the output of the PBD sim node, which is just used to trigger evaluation of the deformer.
        MFnNumericAttribute nAttr;
        aTrigger = nAttr.create("trigger", "trg", MFnNumericData::kBoolean, false, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        nAttr.setStorable(false);
        nAttr.setWritable(true);
        nAttr.setReadable(false);
        status = addAttribute(aTrigger);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        return MS::kSuccess;
    }
   
    static MObject createVoxelShapeNode(Voxels& voxels, const MDagPath& voxelTransformDagPath, const MObject& pbdNodeObj) {
        MStatus status;
        MObject voxelTransform = voxelTransformDagPath.node();
        MDagPath voxelMeshDagPath = voxelTransformDagPath;
        status = voxelMeshDagPath.extendToShape();

        // Create the new shape under the existing transform
        MDagModifier dagMod;
        MObject newShapeObj = dagMod.createNode(typeName, voxelTransform);

        // Relegate the old shape to an intermediate object
        MFnDagNode oldShapeDagNode(voxelMeshDagPath);
        oldShapeDagNode.setIntermediateObject(true);

        // Connect the old shape's geometry to the new shape as its input
        MFnDependencyNode srcDep(voxelMeshDagPath.node(), &status);
        MPlug srcOutMesh = srcDep.findPlug("outMesh", true, &status);

        MFnDependencyNode dstDep(newShapeObj, &status);
        MPlug dstInMesh = dstDep.findPlug(aInputGeom, false, &status);

        dagMod.connect(srcOutMesh, dstInMesh);
        dagMod.doIt();

        // Directly set voxel data on the shape for vertex deformation (TODO: this will be set by connection in the future...)
        MFnPluginData pluginDataFn;
        MObject deformerDataObj = pluginDataFn.create(DeformerData::id, &status);
        DeformerData* deformerData = static_cast<DeformerData*>(pluginDataFn.data(&status));
        deformerData->setVertexStartIdx(std::move(voxels.vertStartIdx));

        MPlug deformerDataPlug = dstDep.findPlug(aDeformerData, false, &status);
        deformerDataPlug.setValue(deformerDataObj);

        // Connect the PBD node outputs to the shape's inputs
        MDGModifier dgMod;
        MFnDependencyNode pbdNode(pbdNodeObj);
        
        MPlug pbdTriggerPlug = pbdNode.findPlug(PBD::aTriggerOut, false);
        MPlug triggerPlug = dstDep.findPlug(aTrigger, false);
        dgMod.connect(pbdTriggerPlug, triggerPlug);

        MPlug pbdParticleDataPlug = pbdNode.findPlug(PBD::aParticleData, false);
        MPlug particleDataPlug = dstDep.findPlug(aParticleData, false);
        dgMod.connect(pbdParticleDataPlug, particleDataPlug);

        MPlug pbdParticleSRVPlug = pbdNode.findPlug(PBD::aParticleSRV, false);
        MPlug particleSRVPlug = dstDep.findPlug(aParticleSRV, false);
        dgMod.connect(pbdParticleSRVPlug, particleSRVPlug);

        dgMod.doIt();

        return newShapeObj;
    }

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
    
    MObject geometryData() const override { 
        const MDagPath srcDagPath = pathToOriginalGeometry();
        if (!srcDagPath.isValid()) return MObject::kNullObj;

        MFnMesh fnMesh(srcDagPath);
        return fnMesh.object();
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

    /**
     * Invoked by the subscene override after it has created geometry buffers to fulfill shader requirements.
     * In addition to the GPU resources it passes in, we need to pull CPU-side data from this nodes connected plugs and
     * upload them to the GPU (done in the constructor of DeformVerticesCompute).
     */
    void initializeDeformVerticesCompute(
        int vertexCount,
        const ComPtr<ID3D11UnorderedAccessView>& positionsUAV,
        const ComPtr<ID3D11UnorderedAccessView>& normalsUAV,
        const ComPtr<ID3D11ShaderResourceView>& originalPositionsSRV,
        const ComPtr<ID3D11ShaderResourceView>& originalNormalsSRV
    ) {
        MObject particleDataObj;
        MPlug particleDataPlug(thisMObject(), aParticleData);
        particleDataPlug.getValue(particleDataObj);
        MFnPluginData particleDataFn(particleDataObj);
        ParticleData* particleData = static_cast<ParticleData*>(particleDataFn.data());
        const ParticleDataContainer& particleDataContainer = particleData->getData();

        MObject deformerDataObj;
        MPlug deformerDataPlug(thisMObject(), aDeformerData);
        deformerDataPlug.getValue(deformerDataObj);
        MFnPluginData deformerDataFn(deformerDataObj);
        DeformerData* deformerData = static_cast<DeformerData*>(deformerDataFn.data());

        MObject particleSRVObj;
        MPlug particleSRVPlug(thisMObject(), aParticleSRV);
        particleSRVPlug.getValue(particleSRVObj);
        MFnPluginData d3d11DataFn(particleSRVObj);
        D3D11Data* d3d11Data = static_cast<D3D11Data*>(d3d11DataFn.data());

        deformVerticesCompute = DeformVerticesCompute(
            particleDataContainer.numParticles,
            vertexCount,
            deformerData->getVertexStartIdx(),
            particleDataContainer.particlePositionsCPU,
            positionsUAV,
            normalsUAV,
            originalPositionsSRV,
            originalNormalsSRV,
            d3d11Data->getSRV()
        );

        isInitialized = true;
    }

private:
    bool isInitialized = false;
    DeformVerticesCompute deformVerticesCompute;
    MCallbackIdArray callbackIds;
    
    VoxelShape() = default;
    ~VoxelShape() override = default;

    MStatus VoxelShape::compute(const MPlug& plug, MDataBlock& dataBlock) {
        if (!isInitialized) return MS::kSuccess;
        if (plug != aTrigger) return MS::kUnknownParameter;
        
        deformVerticesCompute.dispatch();

        return MS::kSuccess;
    }

    // TODO: I'm not actually sure if this will run when the other plug value changes... need to test which message is emitted.
    static void onParticleSRVChange(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData) {
        if (!(msg & MNodeMessage::kOtherPlugSet)) return;
        if (plug != aParticleSRV) return;

        MObject particleSRVObj = plug.asMObject();
        D3D11Data* d3d11Data = static_cast<D3D11Data*>(MFnPluginData(particleSRVObj).data());

        VoxelShape* shapeNode = static_cast<VoxelShape*>(clientData);
        shapeNode->deformVerticesCompute.setParticleSRV(d3d11Data->getSRV());
    }

    void postConstructor() override {
        MPxSurfaceShape::postConstructor();
        setRenderable(true); 

        MStatus status;
        MCallbackId callbackId = MNodeMessage::addAttributeChangedCallback(thisMObject(), onParticleSRVChange, this, &status); 
        callbackIds.append(callbackId);

        callbackId = MNodeMessage::addNodePreRemovalCallback(thisMObject(), [](MObject& node, void* clientData) {
            VoxelShape* shapeNode = static_cast<VoxelShape*>(clientData);
            MMessage::removeCallbacks(shapeNode->callbackIds);
        }, this, &status);

        callbackIds.append(callbackId);
    }


};