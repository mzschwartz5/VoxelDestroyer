#pragma once
#include <maya/MPxSurfaceShape.h>
#include <maya/MFnMeshData.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MDagModifier.h>
#include <maya/MDGModifier.h>
#include <maya/MCallbackIdArray.h>
#include <maya/MNodeMessage.h>
#include "../../voxelizer.h"
#include "../usernodes/pbdnode.h"
#include "../usernodes/voxelizernode.h"
#include "../data/particledata.h"
#include "../data/d3d11data.h"
#include "../data/voxeldata.h"
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
   
    static MObject createVoxelShapeNode(const MObject& pbdNodeObj, const MDagPath& voxelTransformDagPath) {
        MStatus status;
        MObject voxelTransform = voxelTransformDagPath.node();
        MDagPath voxelMeshDagPath = voxelTransformDagPath;
        status = voxelMeshDagPath.extendToShape();

        // Create the new shape under the existing transform
        MObject newShapeObj = Utils::createDagNode(typeName, voxelTransform);

        // Relegate the old shape to an intermediate object
        MFnDagNode oldShapeDagNode(voxelMeshDagPath);
        oldShapeDagNode.setIntermediateObject(true);

        Utils::connectPlugs(voxelMeshDagPath.node(), MString("outMesh"), newShapeObj, aInputGeom);
        Utils::connectPlugs(pbdNodeObj, PBDNode::aTriggerOut, newShapeObj, aTrigger);
        Utils::connectPlugs(pbdNodeObj, PBDNode::aParticleData, newShapeObj, aParticleData);
        Utils::connectPlugs(pbdNodeObj, PBDNode::aParticleSRV, newShapeObj, aParticleSRV);
        Utils::connectPlugs(pbdNodeObj, PBDNode::aVoxelDataOut, newShapeObj, aVoxelData);

        return newShapeObj;
    }

    /**
     * Since this shape can shatter, and grow unbounded, it doesn't really make sense to return a bounding box.
     * Note that, in the subscene override, we do need to pass in some bounds - so we use an effectively infinite bounding box there.
     */
    bool isBounded() const override { return false; }

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
        const MSharedPtr<Voxels> voxels
    ) const {
        const MDagPath originalGeomPath = pathToOriginalGeometry();
        std::vector<uint> vertexVoxelIds(vertexPositions.size() / 3, UINT_MAX);
        double voxelSize = voxelizationGrid.gridEdgeLength / voxelizationGrid.voxelsPerEdge;
        MPoint gridMin = voxelizationGrid.gridCenter - MVector(voxelizationGrid.gridEdgeLength / 2, voxelizationGrid.gridEdgeLength / 2, voxelizationGrid.gridEdgeLength / 2);
        gridMin = originalGeomPath.inclusiveMatrix().inverse() * gridMin; // Transform voxelization grid to object space, since that's where vertices are defined.
        const std::unordered_map<uint32_t, uint32_t>& voxelMortonCodeToIndex = voxels->mortonCodesToSortedIdx;
        const double episilon = 1e-4 * voxelSize;

        // TODO: this approach still doesn't work flawlessly... there are some cases where a triangle gets stretched between voxels.
        // A fail-safe method would be to store face components per-voxel in Voxelizer. Though this assumes the order of triangles in the vertexIndices buffer 
        // matches the order of triangles in the original mesh (which it should... but it's not the safest assumption).
        for (size_t i = 0; i < vertexIndices.size(); i += 3) {
            uint idx0 = vertexIndices[i];
            uint idx1 = vertexIndices[i + 1];
            uint idx2 = vertexIndices[i + 2];

            // If a vertex has been assigned a voxel ID already (as part of some other triangle),
            // use it for this triangle's vertices as well. By construction, a vertex should be owned by one voxel,
            // so if one triangle it's part of belongs to a voxel, all triangles it is part of should belong to the same voxel.
            uint voxelId = UINT_MAX;
            for (uint idx : { idx0, idx1, idx2 }) {
                if (vertexVoxelIds[idx] != UINT_MAX) { 
                    voxelId = vertexVoxelIds[idx]; 
                    break; 
                }
            }

            if (voxelId != UINT_MAX) {
                for (uint idx : { idx0, idx1, idx2 }) vertexVoxelIds[idx] = voxelId;
                continue;
            }

            MPoint v0(vertexPositions[idx0 * 3], vertexPositions[idx0 * 3 + 1], vertexPositions[idx0 * 3 + 2]);
            MPoint v1(vertexPositions[idx1 * 3], vertexPositions[idx1 * 3 + 1], vertexPositions[idx1 * 3 + 2]);
            MPoint v2(vertexPositions[idx2 * 3], vertexPositions[idx2 * 3 + 1], vertexPositions[idx2 * 3 + 2]);

            // Triangles on the boundary between voxels will have identical centroids. To identify the correct voxel, 
            // nudge the centroid back a small epsilon along the triangle normal (by winding order).
            MVector normal = ((v1 - v0) ^ (v2 - v0)).normal();
            MPoint centroid = ((v0 + v1 + v2) / 3.0) - (normal * episilon);

            int voxelX = static_cast<int>(floor((centroid.x - gridMin.x) / voxelSize));
            int voxelY = static_cast<int>(floor((centroid.y - gridMin.y) / voxelSize));
            int voxelZ = static_cast<int>(floor((centroid.z - gridMin.z) / voxelSize));

            uint voxelMortonCode = Utils::toMortonCode(voxelX, voxelY, voxelZ);
            voxelId = voxelMortonCodeToIndex.at(voxelMortonCode);

            // Tag all three vertices of this triangle with the same voxel ID
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

        Utils::PluginData<VoxelData> voxelData(thisMObject(), aVoxelData);
        std::vector<uint> vertexVoxelIds = getVoxelIdsForVertices(
            vertexIndices, 
            vertexPositions,
            voxelData.get()->getVoxelizationGrid(),
            voxelData.get()->getVoxels()
        );
        
        Utils::PluginData<ParticleData> particleData(thisMObject(), aParticleData);
        Utils::PluginData<D3D11Data> particleSRVData(thisMObject(), aParticleSRV);
        const ParticleDataContainer& particleDataContainer = particleData.get()->getData();
        const MDagPath originalGeomPath = pathToOriginalGeometry();

        deformVerticesCompute = DeformVerticesCompute(
            particleDataContainer.numParticles,
            vertexPositions.size() / 3,
            originalGeomPath.inclusiveMatrix().inverse(),
            *particleDataContainer.particlePositionsCPU,
            vertexVoxelIds,
            positionsUAV,
            normalsUAV,
            originalPositionsSRV,
            originalNormalsSRV,
            particleSRVData.get()->getSRV()
        );

        isInitialized = true;
    }

private:
    bool isInitialized = false;
    bool isParticleSRVPlugDirty = false;
    MCallbackIdArray callbackIds;
    DeformVerticesCompute deformVerticesCompute;
    
    VoxelShape() = default;
    ~VoxelShape() override = default;

    MStatus compute(const MPlug& plug, MDataBlock& dataBlock) override {
        if (!isInitialized) return MS::kSuccess;
        if (plug != aTrigger) return MS::kUnknownParameter;

        if (isParticleSRVPlugDirty) {
            // The particle SRV has changed, so we need to update the compute shader with the new one.
            MDataHandle d3d11DataHandle = dataBlock.inputValue(aParticleSRV);
            D3D11Data* particleSRVData = static_cast<D3D11Data*>(d3d11DataHandle.asPluginData());
            deformVerticesCompute.setParticlePositionsSRV(particleSRVData->getSRV());
            isParticleSRVPlugDirty = false;
        }

        deformVerticesCompute.dispatch();

        return MS::kSuccess;
    }

    MPxNode::SchedulingType schedulingType() const override {
        // Evaluated serially amongst nodes of the same type
        // Necessary because Maya provides a single threaded D3D11 device
        return MPxNode::kGloballySerial;
    }

    /**
     * Since this node has no outputs, nothing pulls new values of this plug if it gets dirty, so the plug will always have stale data.
     * Use a dirty plug callback to detect when it gets dirtied, and then pull the new value in compute().
     */
    static void onParticleSRVPlugDirty(MObject& node, MPlug& plug, void* clientData) {
        if (plug != aParticleSRV) return;
        
        VoxelShape* voxelShape = static_cast<VoxelShape*>(clientData);
        voxelShape->isParticleSRVPlugDirty = true;
    }

    void postConstructor() override {
        MPxSurfaceShape::postConstructor();
        setRenderable(true);

        MCallbackId callbackId = MNodeMessage::addNodeDirtyPlugCallback(thisMObject(), onParticleSRVPlugDirty, this);
        callbackIds.append(callbackId);

        // Effectively a destructor callback to clean up when the node is deleted
        // This is more reliable than a destructor, because Maya won't necessarily call destructors on node deletion (unless undo queue is flushed)
        callbackId = MNodeMessage::addNodePreRemovalCallback(thisMObject(), [](MObject& node, void* clientData) {
            VoxelShape* voxelShape = static_cast<VoxelShape*>(clientData);
            MMessage::removeCallbacks(voxelShape->callbackIds);
        }, this);
        callbackIds.append(callbackId);
    }

};