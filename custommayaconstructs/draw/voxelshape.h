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
#include <maya/MAttributeSpecArray.h>
#include <maya/MAttributeSpec.h>
#include <maya/MAttributeIndex.h>
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
#include "../commands/changevoxeleditmodecommand.h"
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
    
    bool excludeAsPluginShape() const override {
        // Always display this shape in the outliner, even when plugin shapes are excluded.
        return false;
    }
    
    MSharedPtr<Voxels> getVoxels() {
        Utils::PluginData<VoxelData> voxelData(thisMObject(), aVoxelData);
        return voxelData.get()->getVoxels();
    }

    MSelectionMask getShapeSelectionMask() const override {
        return MSelectionMask::kSelectMeshes;
    }

    MSelectionMask getComponentSelectionMask() const override {
        MSelectionMask mask;
        mask.addMask(MSelectionMask::kSelectMeshFaces);
        mask.addMask(MSelectionMask::kSelectMeshVerts);
        return mask;
    }

    /**
     * Invoked by the subscene override after it has created geometry buffers to fulfill shader requirements.
     * In addition to the GPU resources it passes in, we need to pull CPU-side data from this node's connected plugs and
     * upload them to the GPU (done in the constructor of DeformVerticesCompute).
     */
    void initializeDeformVerticesCompute(
        const std::vector<uint>& vertexIndices,
        const unsigned int numVertices,
        const ComPtr<ID3D11UnorderedAccessView>& positionsUAV,
        const ComPtr<ID3D11UnorderedAccessView>& normalsUAV,
        const ComPtr<ID3D11ShaderResourceView>& originalPositionsSRV,
        const ComPtr<ID3D11ShaderResourceView>& originalNormalsSRV
    ) {

        std::vector<uint> vertexVoxelIds = getVoxelIdsForVertices(vertexIndices, numVertices, getVoxels());

        Utils::PluginData<ParticleData> particleData(thisMObject(), aParticleData);
        Utils::PluginData<D3D11Data> particleSRVData(thisMObject(), aParticleSRV);
        const ParticleDataContainer& particleDataContainer = particleData.get()->getData();
        const MDagPath originalGeomPath = pathToOriginalGeometry();

        deformVerticesCompute = DeformVerticesCompute(
            particleDataContainer.numParticles,
            numVertices,
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

    const ComPtr<ID3D11ShaderResourceView>& getFaceTensionPaintSRV() {
        if (!faceTensionPaintSRV) {
            allocatePaintResources();
        }
        return faceTensionPaintSRV;
    }

    const ComPtr<ID3D11UnorderedAccessView>& getFaceTensionPaintUAV() {
        if (!faceTensionPaintUAV) {
            allocatePaintResources();
        }
        return faceTensionPaintUAV;
    }

private:
    bool isInitialized = false;
    bool isParticleSRVPlugDirty = false;
    MCallbackIdArray callbackIds;
    DeformVerticesCompute deformVerticesCompute;
    // Holds the face-to-face tension weight values of each voxel face, for use with the Voxel Paint tool.
    ComPtr<ID3D11Buffer> faceTensionPaintBuffer; 
    ComPtr<ID3D11ShaderResourceView> faceTensionPaintSRV;
    ComPtr<ID3D11UnorderedAccessView> faceTensionPaintUAV;
    
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

    /**
     * Associate each vertex in the buffer created by the subscene override with a voxel ID it belongs to.
     * We do this by iterating over the faces indices of each voxel face component, using them to access the index buffer of the whole mesh,
     * and tagging the vertices of each face with the voxel ID.
     * 
     * Note that this makes implicit assumptions about the order of face indices from MGeometryExtractor.
     * 
     * We do this now, instead of in the voxelizer, because the subscene override is the ultimate source of truth on the order of vertices in the GPU buffers.
     * Supporting split normals, UV seams, etc. requires duplicating vertices. So we have to do this step after the subscene override has created the final vertex buffers.
     */
    std::vector<uint> getVoxelIdsForVertices(
        const std::vector<uint>& vertexIndices,
        const unsigned int numVertices,
        const MSharedPtr<Voxels>& voxels
    ) const {
        std::vector<uint> vertexVoxelIds(numVertices, UINT_MAX);
        const MObjectArray& faceComponents = voxels->faceComponents;
        const std::vector<uint32_t>& mortonCodes = voxels->mortonCodes;
        const std::unordered_map<uint32_t, uint32_t>& mortonCodesToSortedIdx = voxels->mortonCodesToSortedIdx;

        MFnSingleIndexedComponent fnFaceComponent;
        for (int i = 0; i < voxels->numOccupied; ++i) {
            int voxelIndex = mortonCodesToSortedIdx.at(mortonCodes[i]);
            MObject faceComponent = faceComponents[i];
            fnFaceComponent.setObject(faceComponent);

            for (int j = 0; j < fnFaceComponent.elementCount(); ++j) {
                int faceIndex = fnFaceComponent.element(j);

                for (int k = 0; k < 3; ++k) {
                    uint vertexIndex = vertexIndices[3 * faceIndex + k];
                    vertexVoxelIds[vertexIndex] = voxelIndex;
                }
            }
        }

        return vertexVoxelIds;
    }

    void allocatePaintResources() {
        MSharedPtr<Voxels> voxels = getVoxels();
        if (!voxels) return;

        const int numVoxels = voxels->numOccupied;
        
        // Face tension paint values start at 0. Use uint16_t to get the size right, but it will really be half-floats in the shader.
        // Need to use a typed buffer to get half-float support.
        int elementCount = numVoxels * 6; // 6 faces per voxel
        const std::vector<uint16_t> emptyFaceTensionData(elementCount, 0); // 6 faces per voxel
        faceTensionPaintBuffer = DirectX::createReadWriteBuffer(emptyFaceTensionData, 0, DirectX::BufferFormat::TYPED);
        faceTensionPaintSRV = DirectX::createSRV(faceTensionPaintBuffer, elementCount, 0, DirectX::BufferFormat::TYPED, DXGI_FORMAT_R16_FLOAT);
        faceTensionPaintUAV = DirectX::createUAV(faceTensionPaintBuffer, elementCount, 0, DirectX::BufferFormat::TYPED, DXGI_FORMAT_R16_FLOAT);
    }

};