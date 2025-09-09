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
#include "../particledata.h"
#include "../d3d11data.h"
#include "../voxeldata.h"
#include "../../directx/compute/deformverticescompute.h"
#include <d3d11.h>
#include <wrl/client.h>
#include "directx/directx.h"
#include "../../utils.h"
using Microsoft::WRL::ComPtr;

class VoxelShape : public MPxSurfaceShape {
    
public:
    inline static MTypeId id = { 0x0012A3B4 };
    inline static MString typeName = "VoxelShape";
    inline static MString drawDbClassification = "drawdb/subscene/voxelSubsceneOverride/voxelshape";
    
    inline static MObject aInputGeom;
    inline static MObject aParticleSRV;
    inline static MObject aParticleData;
    inline static MObject aVoxelData;
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

        aVoxelData = tAttr.create("voxelData", "vxd", VoxelData::id, MObject::kNullObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        tAttr.setStorable(false);
        tAttr.setWritable(true);
        tAttr.setReadable(false);
        status = addAttribute(aVoxelData);
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
   
    static MObject createVoxelShapeNode(const MDagPath& voxelTransformDagPath, const MObject& pbdNodeObj) {
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

        MPlug pbdVoxelDataPlug = pbdNode.findPlug(PBD::aVoxelDataOut, false);
        MPlug voxelDataPlug = dstDep.findPlug(aVoxelData, false);
        dgMod.connect(pbdVoxelDataPlug, voxelDataPlug); 

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
    
    bool excludeAsPluginShape() const {
        // Always display this shape in the outliner, even when plugin shapes are excluded.
        return false;
    }

    /**
     * Associate each vertex in the buffer created by the subscene override with a voxel ID it belongs to.
     * This is done by computing the centroid of each triangle and seeing which voxel it falls into. All vertices of that triangle get tagged with that voxel ID.
     * 
     * We do this now, instead of in the voxelizer, because the subscene override is the ultimate source of truth on the order of vertices in the GPU buffers.
     * Aside from possible internal Maya reasons, supporting split normals, UV seams, etc. requires duplicating vertices. So we have to do this step after the subscene override has created the final vertex buffers.
     */
    std::vector<uint> getVoxelIdsForVertices(
        const std::vector<uint>& vertexIndices,
        const std::vector<float>& vertexPositions,
        const VoxelizationGrid& voxelizationGrid,
        const Voxels& voxels
    ) const {

        // TODO: THIS APPROACH ONLY WORKS IF WE'RE CLIPPING TRIANGLES
        // (The idea of assuming each triangle is fully contained within a single voxel breaks down if we don't clip triangles to voxel boundaries.)

        std::vector<uint> vertexVoxelIds(vertexPositions.size(), 0);
        double voxelSize = voxelizationGrid.gridEdgeLength / voxelizationGrid.voxelsPerEdge;
        MPoint gridMin = voxelizationGrid.gridCenter - MVector(voxelizationGrid.gridEdgeLength / 2, voxelizationGrid.gridEdgeLength / 2, voxelizationGrid.gridEdgeLength / 2);
        const std::unordered_map<uint32_t, uint32_t>& voxelMortonCodeToIndex = voxels.mortonCodesToSortedIdx;

        for (size_t i = 0; i < vertexIndices.size(); i += 3) {
            uint idx0 = vertexIndices[i];
            uint idx1 = vertexIndices[i + 1];
            uint idx2 = vertexIndices[i + 2];

            MPoint v0(vertexPositions[idx0 * 3], vertexPositions[idx0 * 3 + 1], vertexPositions[idx0 * 3 + 2]);
            MPoint v1(vertexPositions[idx1 * 3], vertexPositions[idx1 * 3 + 1], vertexPositions[idx1 * 3 + 2]);
            MPoint v2(vertexPositions[idx2 * 3], vertexPositions[idx2 * 3 + 1], vertexPositions[idx2 * 3 + 2]);

            MPoint centroid = (v0 + v1 + v2) / 3.0;

            int voxelX = static_cast<int>(floor((centroid.x - gridMin.x) / voxelSize));
            int voxelY = static_cast<int>(floor((centroid.y - gridMin.y) / voxelSize));
            int voxelZ = static_cast<int>(floor((centroid.z - gridMin.z) / voxelSize));

            uint voxelMortonCode = Utils::toMortonCode(voxelX, voxelY, voxelZ);
            uint voxelId = voxelMortonCodeToIndex.at(voxelMortonCode);

            // Tag all three vertices of this triangle with the same voxel ID
            // Note that this will overwrite previous assignments if a vertex is shared between triangles, but that's fine - we just need some voxel ID for each vertex.
            // A triangle can't be shared between voxels, by construction, so this is well-defined.
            vertexVoxelIds[idx0] = voxelId;
            vertexVoxelIds[idx1] = voxelId;
            vertexVoxelIds[idx2] = voxelId;
        }

        return vertexVoxelIds;
    }

    /**
     * Invoked by the subscene override after it has created geometry buffers to fulfill shader requirements.
     * In addition to the GPU resources it passes in, we need to pull CPU-side data from this nodes connected plugs and
     * upload them to the GPU (done in the constructor of DeformVerticesCompute).
     */
    void initializeDeformVerticesCompute(
        const std::vector<uint>& vertexIndices,
        const std::vector<float>& vertexPositions,
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

        MObject particleSRVObj;
        MPlug particleSRVPlug(thisMObject(), aParticleSRV);
        particleSRVPlug.getValue(particleSRVObj);
        MFnPluginData d3d11DataFn(particleSRVObj);
        D3D11Data* particleSRVData = static_cast<D3D11Data*>(d3d11DataFn.data());

        MObject voxelDataObj;
        MPlug voxelDataPlug(thisMObject(), aVoxelData);
        voxelDataPlug.getValue(voxelDataObj);
        MFnPluginData voxelDataFn(voxelDataObj);
        VoxelData* voxelData = static_cast<VoxelData*>(voxelDataFn.data());

        std::vector<uint> vertexVoxelIds = getVoxelIdsForVertices(
            vertexIndices, 
            vertexPositions,
            voxelData->getVoxelizationGrid(),
            voxelData->getVoxels()
        );

        deformVerticesCompute = DeformVerticesCompute(
            particleDataContainer.numParticles,
            vertexPositions.size() / 3,
            particleDataContainer.particlePositionsCPU,
            vertexVoxelIds,
            positionsUAV,
            normalsUAV,
            originalPositionsSRV,
            originalNormalsSRV,
            particleSRVData->getSRV()
        );

        isInitialized = true;
    }

private:
    bool isInitialized = false;
    DeformVerticesCompute deformVerticesCompute;
    MCallbackIdArray callbackIds;
    
    VoxelShape() = default;
    ~VoxelShape() override = default;

    MStatus VoxelShape::compute(const MPlug& plug, MDataBlock& dataBlock) override {
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
        D3D11Data* particleSRVData = static_cast<D3D11Data*>(MFnPluginData(particleSRVObj).data());

        VoxelShape* shapeNode = static_cast<VoxelShape*>(clientData);
        shapeNode->deformVerticesCompute.setParticlePositionsSRV(particleSRVData->getSRV());
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