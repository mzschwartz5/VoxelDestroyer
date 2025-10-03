#pragma once

#include <maya/MPxSubSceneOverride.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMesh.h>
#include <maya/MShaderManager.h>
#include "voxelshape.h"
#include "../../cube.h"
#include <maya/MGeometryRequirements.h>
#include <maya/MGeometryExtractor.h>
#include <maya/MPxComponentConverter.h>
#include <memory>
#include <maya/MUserData.h>
#include <maya/MSharedPtr.h>
using namespace MHWRender;
using std::unique_ptr;
using std::make_unique;

struct RenderItemInfo {
    MIndexBufferDescriptor indexDesc;
    MShaderInstance* shaderInstance;
    MString renderItemName;
};

class VoxelFaceUserData : public MUserData {
public:
    VoxelFaceUserData(const MObjectArray& voxelFaces)
        : MUserData()
        , voxelFaces(voxelFaces)
    {}
    ~VoxelFaceUserData() override = default;

    MObjectArray voxelFaces;
};

class VoxelSubSceneComponentConverter : public MPxComponentConverter {
public:
    static MPxComponentConverter* creator() {
        return new VoxelSubSceneComponentConverter();
    }

    VoxelSubSceneComponentConverter() = default;
    ~VoxelSubSceneComponentConverter() override = default;

    void addIntersection(MIntersection& intersection) override {
        int instanceID = intersection.instanceID();
        if (instanceID < 0 || instanceID >= (int)voxelFaces.length()) return;

        MObject voxelFaceComponentObject = voxelFaces[instanceID];
        MFnSingleIndexedComponent fnFaceComp;
        fnFaceComp.setObject(voxelFaceComponentObject);
        int elemCount = fnFaceComp.elementCount();
        MGlobal::displayInfo("Adding " + MString() + elemCount + " elements from voxel face component");

        for (int i = 0; i < elemCount; ++i) {
            int elem = fnFaceComp.element(i);
            fnComp.addElement(elem);
        }
    }

    MSelectionMask selectionMask() const override {
        return MSelectionMask::kSelectMeshFaces;
    }

    void initialize(const MRenderItem& renderItem) override {
        componentObj = fnComp.create(MFn::kMeshPolygonComponent);
        MSharedPtr<MUserData> ud = renderItem.getCustomData();
        if (!ud) return;
        VoxelFaceUserData* faceData = static_cast<VoxelFaceUserData*>(ud.get());
        voxelFaces = faceData->voxelFaces;
    }

    MObject component() override {
        return componentObj;
    }

private:
    MObjectArray voxelFaces;
    MObject componentObj = MObject::kNullObj;
    MFnSingleIndexedComponent fnComp;
};

class VoxelSubSceneOverride : public MPxSubSceneOverride {
private:
    VoxelShape* voxelShape;
    MObject voxelShapeObj;

    bool shouldUpdate = true;

    ComPtr<ID3D11Buffer> positionsBuffer;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;

    ComPtr<ID3D11Buffer> normalsBuffer;
    ComPtr<ID3D11UnorderedAccessView> normalsUAV;

    // The deform shader also needs the original vertex positions and normals to do its transformations
    ComPtr<ID3D11Buffer> originalPositionsBuffer;
    ComPtr<ID3D11ShaderResourceView> originalPositionsSRV;

    ComPtr<ID3D11Buffer> originalNormalsBuffer;
    ComPtr<ID3D11ShaderResourceView> originalNormalsSRV;

    std::vector<unique_ptr<MVertexBuffer>> meshVertexBuffers;
    std::vector<unique_ptr<MIndexBuffer>> meshIndexBuffers;

    unique_ptr<MVertexBuffer> voxelVertexBuffer;
    std::unordered_map<MGeometry::Primitive, unique_ptr<MIndexBuffer>> voxelIndexBuffers;

    VoxelSubSceneOverride(const MObject& obj)
    : MPxSubSceneOverride(obj), voxelShapeObj(obj) {
        MFnDependencyNode dn(obj);
        voxelShape = static_cast<VoxelShape*>(dn.userNode());
    }

    MShaderInstance* getVertexBufferDescriptorsForShader(const MObject& shaderNode, const MDagPath& geomDagPath, MVertexBufferDescriptorList& vertexBufferDescriptors) {
        MRenderer* renderer = MRenderer::theRenderer();
        if (!renderer) return nullptr;

        const MShaderManager* shaderManager = renderer->getShaderManager();
        if (!shaderManager) return nullptr;

        MShaderInstance* shaderInstance = shaderManager->getShaderFromNode(shaderNode, geomDagPath);
        if (!shaderInstance) return nullptr;

        shaderInstance->requiredVertexBuffers(vertexBufferDescriptors);

        return shaderInstance;
    }

    MObject getShaderNodeFromShadingSet(const MObject& shadingSet) {
        MFnDependencyNode fnShadingSet(shadingSet);
        MPlug shaderPlug = fnShadingSet.findPlug("surfaceShader", true);
        MPlugArray conns;
        if (shaderPlug.isNull() || !shaderPlug.connectedTo(conns, true, false) || conns.length() == 0) return MObject::kNullObj;
        return conns[0].node(); // API returns a plug array but there can only be one shader connected.
    }

    MObjectArray getShadingSetFaceComponents(const MObjectArray& shadingSets, const MIntArray& faceIdxToShader) {
        MObjectArray shadingSetFaceComponents;
        shadingSetFaceComponents.setLength(shadingSets.length());
        MFnSingleIndexedComponent fnFaceComponent;

        for (uint i = 0; i < shadingSets.length(); ++i) {
            shadingSetFaceComponents[i] = fnFaceComponent.create(MFn::kMeshPolygonComponent);
        }

        for (uint i = 0; i < faceIdxToShader.length(); ++i) {
            int shadingSetIdx = faceIdxToShader[i];
            if (shadingSetIdx < 0 || shadingSetIdx >= (int)shadingSets.length()) continue;
            
            fnFaceComponent.setObject(shadingSetFaceComponents[shadingSetIdx]);
            fnFaceComponent.addElement(i);
        }

        return shadingSetFaceComponents;
    }

    void buildGeometryRequirements(
        const MObjectArray& shadingSets, 
        const MObjectArray& shadingSetFaceComponents,
        const MDagPath& originalGeomPath,
        MGeometryRequirements& geomReqs,
        std::vector<RenderItemInfo>& renderItemInfos
    ) {
        MFnSingleIndexedComponent fnFaceComponent;
        MFnMesh originalMeshFn(originalGeomPath.node());

        // TODO: may need to support multiple UV sets in future.
        MString uvSet;
        originalMeshFn.getCurrentUVSetName(uvSet);
        const bool haveUVs = (uvSet.length() > 0) && (originalMeshFn.numUVs(uvSet) > 0);
        
        std::unordered_set<MGeometry::Semantic> existingVBRequirements;
        for (uint i = 0; i < shadingSets.length(); ++i) {
            fnFaceComponent.setObject(shadingSetFaceComponents[i]);
            if (fnFaceComponent.elementCount() == 0) continue;

            MObject shaderNode = getShaderNodeFromShadingSet(shadingSets[i]);
            if (shaderNode.isNull()) continue;

            MVertexBufferDescriptorList vbDescList;
            MShaderInstance* shaderInstance = getVertexBufferDescriptorsForShader(shaderNode, originalGeomPath, vbDescList);
            if (!shaderInstance) continue;

            for (int j = 0; j < vbDescList.length(); ++j) {
                MVertexBufferDescriptor vbDesc;
                if (!vbDescList.getDescriptor(j, vbDesc)) continue;
                
                if (existingVBRequirements.find(vbDesc.semantic()) != existingVBRequirements.end()) continue;
                existingVBRequirements.insert(vbDesc.semantic());

                if (vbDesc.semantic() == MGeometry::kTexture && !haveUVs) continue;
                geomReqs.addVertexRequirement(vbDesc);
            }

            MIndexBufferDescriptor indexDesc(
                MIndexBufferDescriptor::kTriangle,
                MString(), // unused for kTriangle
                MGeometry::kTriangles,
                0, // unused for kTriangle
                shadingSetFaceComponents[i]
            );

            geomReqs.addIndexingRequirement(indexDesc);

            renderItemInfos.push_back({indexDesc, shaderInstance, 
                "voxelRenderItem_" + MFnDependencyNode(shadingSets[i]).name()});
        }

    }

    void createVertexBuffer(const MVertexBufferDescriptor& vbDesc, const MGeometryExtractor& extractor, uint vertexCount, MVertexBufferArray& vertexBufferArray) {
        auto vertexBuffer = make_unique<MVertexBuffer>(vbDesc);
        const MGeometry::Semantic semantic = vbDesc.semantic();
        
        // Position and normal buffers need to be created with flags for binding (write-ably) to a DX11 compute shader (for the deform vertex compute step).
        // So create them as DX11 buffers with the unordered access flag, then pass the underlying resource handle to the Maya MVertexBuffer.
        if (semantic == MGeometry::kPosition || semantic == MGeometry::kNormal) {
            ComPtr<ID3D11Buffer>& buffer = (semantic == MGeometry::kPosition) ? positionsBuffer : normalsBuffer;
            
            // Create the buffer - must be a raw buffer because Maya doesn't seem to accept structured buffers 
            // for binding as vertex buffers.
            std::vector<float> data(vertexCount * vbDesc.dimension(), 0.0f);
            extractor.populateVertexBuffer(data.data(), vertexCount, vbDesc);
            buffer = DirectX::createReadWriteBuffer(data, D3D11_BIND_VERTEX_BUFFER, true);
            vertexBuffer->resourceHandle(buffer.Get(), data.size());

            ComPtr<ID3D11UnorderedAccessView>& uav = (semantic == MGeometry::kPosition) ? positionsUAV : normalsUAV;
            uav = DirectX::createUAV(buffer, true);

            // Also need to create a buffer with the original positions/normals for the deform shader to read from
            ComPtr<ID3D11Buffer>& originalBuffer = (semantic == MGeometry::kPosition) ? originalPositionsBuffer : originalNormalsBuffer;
            originalBuffer = DirectX::createReadOnlyBuffer(data, 0, false, sizeof(float) * vbDesc.dimension());
            
            ComPtr<ID3D11ShaderResourceView>& originalSRV = (semantic == MGeometry::kPosition) ? originalPositionsSRV : originalNormalsSRV;
            originalSRV = DirectX::createSRV(originalBuffer);
        }
        else {
            void* data = vertexBuffer->acquire(vertexCount, true);
            extractor.populateVertexBuffer(data, vertexCount, vbDesc);
            vertexBuffer->commit(data);
        }
        
        vertexBufferArray.addBuffer(vbDesc.name(), vertexBuffer.get());
        meshVertexBuffers.push_back(std::move(vertexBuffer));
    }

    MIndexBuffer* createIndexBuffer(const RenderItemInfo& itemInfo, const MGeometryExtractor& extractor) {
        unsigned int numTriangles = extractor.primitiveCount(itemInfo.indexDesc);
        if (numTriangles == 0) return nullptr;
    
        auto indexBuffer = make_unique<MIndexBuffer>(MGeometry::kUnsignedInt32);
        void* indexData = indexBuffer->acquire(3 * numTriangles, true);

        extractor.populateIndexBuffer(indexData, 3 * numTriangles, itemInfo.indexDesc);
        indexBuffer->commit(indexData);

        MIndexBuffer* rawIndexBuffer = indexBuffer.get();
        meshIndexBuffers.push_back(std::move(indexBuffer));
        
        return rawIndexBuffer;
    }


    MRenderItem* createSingleMeshRenderItem(MSubSceneContainer& container, const RenderItemInfo& itemInfo) {
        MRenderItem* renderItem = container.find(itemInfo.renderItemName);
        if (renderItem) return renderItem;

        renderItem = MRenderItem::Create(itemInfo.renderItemName, MRenderItem::MaterialSceneItem, MGeometry::kTriangles);
        renderItem->setDrawMode(static_cast<MGeometry::DrawMode>(MGeometry::kShaded | MGeometry::kTextured));
        renderItem->setWantConsolidation(true);
        renderItem->setShader(itemInfo.shaderInstance);
        container.add(renderItem);
        
        releaseShaderInstance(itemInfo.shaderInstance);
        return renderItem;
    }

    void releaseShaderInstance(MShaderInstance* shaderInstance) {
        MRenderer* renderer = MRenderer::theRenderer();
        if (!renderer) return;

        const MShaderManager* shaderManager = renderer->getShaderManager();
        if (!shaderManager) return;

        shaderManager->releaseShader(shaderInstance);
    }

    unsigned int getAllMeshVertices(
        const MGeometryExtractor& extractor,
        std::vector<uint>& vertexIndices
    ) {
        // No face component arg --> whole mesh
        MIndexBufferDescriptor indexDesc(MIndexBufferDescriptor::kTriangle, MString(), MGeometry::kTriangles, 0);
        const unsigned int primitiveCount = extractor.primitiveCount(indexDesc);
        vertexIndices.resize(primitiveCount * 3);
        extractor.populateIndexBuffer(vertexIndices.data(), primitiveCount * 3, indexDesc);

        return extractor.vertexCount(); 
    }

    void updateSelectionGranularity(
		const MDagPath& path,
		MSelectionContext& selectionContext) 
    {
        selectionContext.setSelectionLevel(MHWRender::MSelectionContext::kComponent);    
    }

    void createVoxelWireframeRenderItem(MSubSceneContainer& container) {
        MRenderItem* voxelRenderItem = MRenderItem::Create("VoxelRenderItem", MRenderItem::DecorationItem, MGeometry::kLines);
        MShaderInstance* voxelShader = MRenderer::theRenderer()->getShaderManager()->getStockShader(MShaderManager::k3dSolidShader);
        const float solidColor[] = {0.0f, 1.0f, 0.25f, 1.0f};
        voxelShader->setParameter("solidColor", solidColor);

        voxelRenderItem->setDrawMode(static_cast<MGeometry::DrawMode>(MGeometry::kWireframe | MGeometry::kShaded | MGeometry::kTextured));
        voxelRenderItem->depthPriority(MRenderItem::sActiveWireDepthPriority);
        voxelRenderItem->setWantConsolidation(true);
        voxelRenderItem->setHideOnPlayback(true);
        voxelRenderItem->setShader(voxelShader);
        container.add(voxelRenderItem);

        setVoxelGeometryForRenderItem(*voxelRenderItem, MGeometry::kLines);

        const MMatrixArray& voxelInstanceTransforms = voxelShape->getVoxels().get()->modelMatrices;
        setInstanceTransformArray(*voxelRenderItem, voxelInstanceTransforms);
    }

    void createVoxelSelectionRenderItem(MSubSceneContainer& container) {
        MRenderItem* voxelRenderItem = MRenderItem::Create("VoxelSelectionItem", MRenderItem::DecorationItem, MGeometry::kTriangles);
        MShaderInstance* voxelShader = MRenderer::theRenderer()->getShaderManager()->getStockShader(MShaderManager::k3dDefaultMaterialShader);
        const MObjectArray& voxelFaces = voxelShape->getVoxels().get()->faceComponents;
        MSharedPtr<MUserData> faceData(new VoxelFaceUserData(voxelFaces));

        MSelectionMask selMask;
        selMask.addMask(MSelectionMask::kSelectMeshFaces);
        selMask.addMask(MSelectionMask::kSelectMeshes);

        voxelRenderItem->setDrawMode(static_cast<MGeometry::DrawMode>(MGeometry::kSelectionOnly));
        voxelRenderItem->setSelectionMask(selMask);
        voxelRenderItem->depthPriority(MRenderItem::sSelectionDepthPriority);
        voxelRenderItem->setWantConsolidation(true);
        voxelRenderItem->setHideOnPlayback(true);
        voxelRenderItem->setShader(voxelShader);
        voxelRenderItem->setCustomData(faceData);
        container.add(voxelRenderItem);

        setVoxelGeometryForRenderItem(*voxelRenderItem, MGeometry::kTriangles);

        const MMatrixArray& voxelInstanceTransforms = voxelShape->getVoxels().get()->modelMatrices;
        setInstanceTransformArray(*voxelRenderItem, voxelInstanceTransforms);
    }

    void createVoxelGeometryBuffers() {
        MVertexBufferDescriptor posDesc("", MGeometry::kPosition, MGeometry::kFloat, 3);
        auto posVB = make_unique<MVertexBuffer>(posDesc);
        float* posData = static_cast<float*>(posVB->acquire(8, true));
        std::copy(cubeCornersFlattened.begin(), cubeCornersFlattened.end(), posData);
        posVB->commit(posData);
        voxelVertexBuffer = std::move(posVB);

        auto makeIndexBuffer = [&](MGeometry::Primitive prim, const auto& src) {
            auto buf = make_unique<MIndexBuffer>(MGeometry::kUnsignedInt32);
            uint32_t* data = static_cast<uint32_t*>(buf->acquire(static_cast<uint>(src.size()), true));
            for (size_t i = 0; i < src.size(); ++i) {
                data[i] = static_cast<uint32_t>(src[i]);
            }
            buf->commit(data);
            voxelIndexBuffers[prim] = std::move(buf);
        };

        makeIndexBuffer(MGeometry::kTriangles, cubeFacesFlattened);
        makeIndexBuffer(MGeometry::kLines, cubeEdgesFlattened);
        makeIndexBuffer(MGeometry::kPoints, cubeCornersFlattened);
    }

    void setVoxelGeometryForRenderItem(
        MRenderItem& renderItem,
        MGeometry::Primitive primitiveType
    ) {
        MVertexBufferArray vbArray;
        vbArray.addBuffer("", voxelVertexBuffer.get());
        const MBoundingBox bounds(MPoint(-0.5, -0.5, -0.5), MPoint(0.5, 0.5, 0.5));
        setGeometryForRenderItem(renderItem, vbArray, *voxelIndexBuffers[primitiveType].get(), &bounds);
    }

    /**
     * Creates the actual, visible, voxelized mesh render items (multiple, possibly, if the original, unvoxelized mesh has multiple shaders / face sets).
     */
    void createMeshRenderItems(MSubSceneContainer& container) {
        MStatus status;
        meshVertexBuffers.clear();
        meshIndexBuffers.clear();

        const MDagPath originalGeomPath = voxelShape->pathToOriginalGeometry();
        MFnMesh originalMeshFn(originalGeomPath.node());
        if (originalMeshFn.numVertices() == 0) return;
        
        // Get all shaders from the original mesh. It will tell us the required vertex buffers,
        // and its mapping of faces to shaders will tell us how to create index buffers and render items.
        MObjectArray shadingSets;
        MIntArray faceIdxToShader;
        status = originalMeshFn.getConnectedShaders(originalGeomPath.instanceNumber(), shadingSets, faceIdxToShader);
        if (status != MStatus::kSuccess) return;
        MObjectArray shadingSetFaceComponents = getShadingSetFaceComponents(shadingSets, faceIdxToShader);

        // Extract the geometry requirements (vertex and index buffer descriptors) from the shaders.
        // Then use MGeometryExtractor to extract the vertex and index buffers from the original mesh.
        MGeometryRequirements geomReqs;
        std::vector<RenderItemInfo> renderItemInfos;
        renderItemInfos.reserve(shadingSets.length());
        buildGeometryRequirements(shadingSets, shadingSetFaceComponents, originalGeomPath, geomReqs, renderItemInfos);
        MGeometryExtractor extractor(geomReqs, originalGeomPath, kPolyGeom_Normal, &status);
        if (status != MStatus::kSuccess) return;

        MVertexBufferArray vertexBufferArray;        
        const unsigned int vertexCount = extractor.vertexCount(); 
        const MVertexBufferDescriptorList& vbDescList = geomReqs.vertexRequirements();
        for (int i = 0; i < vbDescList.length(); ++i) {
            MVertexBufferDescriptor vbDesc;
            if (!vbDescList.getDescriptor(i, vbDesc)) continue;

            createVertexBuffer(vbDesc, extractor, vertexCount, vertexBufferArray);
        }

        // Create an index buffer + render item for each shading set of the original mesh (which corresponds to an indexing requirement)
        // Use an effectively infinite bounding box because the voxel shape can deform and shatter.
        double bound = 1e10;
        const MBoundingBox bounds(MPoint(-bound, -bound, -bound), MPoint(bound, bound, bound));
        for (const RenderItemInfo& itemInfo : renderItemInfos) {
            MIndexBuffer* rawIndexBuffer = createIndexBuffer(itemInfo, extractor);
            if (!rawIndexBuffer) continue;

            MRenderItem* renderItem = createSingleMeshRenderItem(container, itemInfo);
            setGeometryForRenderItem(*renderItem, vertexBufferArray, *rawIndexBuffer, &bounds);
        }

        // The voxel shape needs the whole mesh's vertex indices to tag each vertex with the voxel it belongs to.
        // It's important to do the tagging using the vertex buffer that MGeometryExtractor provides.
        std::vector<uint> vertexIndices;
        unsigned int numVertices = getAllMeshVertices(extractor, vertexIndices);
                
        voxelShape->initializeDeformVerticesCompute(
            vertexIndices,
            numVertices,
            positionsUAV,
            normalsUAV,
            originalPositionsSRV, 
            originalNormalsSRV
        );
    }

public:
    inline static MString drawDbClassification = "drawdb/subscene/voxelSubsceneOverride";
    inline static MString drawRegistrantId = "VoxelSubSceneOverridePlugin";

    static MPxSubSceneOverride* creator(const MObject& obj) 
    {
        return new VoxelSubSceneOverride(obj);
    }

    /**
     * Overriding this to tell Maya that any instance of a render item that gets selected still belongs
     * to the same original shape node.
     */
    bool getInstancedSelectionPath(
        const MRenderItem& renderItem, 
        const MIntersection& intersection, 
        MDagPath& dagPath) const override
    {
        if (!voxelShape) return false;
        MFnDagNode fnDag(voxelShapeObj);
        fnDag.getPath(dagPath);
        return true;
    }

    ~VoxelSubSceneOverride() override {
        if (positionsBuffer) {
            DirectX::notifyMayaOfMemoryUsage(positionsBuffer);
            positionsBuffer.Reset();
        }

        if (normalsBuffer) {
            DirectX::notifyMayaOfMemoryUsage(normalsBuffer);
            normalsBuffer.Reset();
        }

        if (originalPositionsBuffer) {
            DirectX::notifyMayaOfMemoryUsage(originalPositionsBuffer);
            originalPositionsBuffer.Reset();
        }

        if (originalNormalsBuffer) {
            DirectX::notifyMayaOfMemoryUsage(originalNormalsBuffer);
            originalNormalsBuffer.Reset();
        }
    }
    
    DrawAPI supportedDrawAPIs() const override {
        return kDirectX11;
    }

    bool requiresUpdate(
        const MSubSceneContainer& container,
        const MFrameContext& frameContext) const override
    {
        return shouldUpdate;
    }

    /**
     * This method is responsible for populating the MSubSceneContainer with render items. In our case, we want the our custom VoxelShape
     * to have the same geometry, topology, and shading as the original mesh it deforms. To do so, we use the shading sets of the original mesh
     * to tell us what geometry requirements we need to extract and recreate here.
     */
    void update(MSubSceneContainer& container, const MFrameContext& frameContext) override
    {
        if (!voxelShape) return;
        
        if (container.count() <= 0) {
            // The render items for the actual, voxelized mesh.
            createMeshRenderItems(container);

            createVoxelGeometryBuffers();
            // The visible wireframe render item
            createVoxelWireframeRenderItem(container);
            // Invisible item, only gets drawn to the selection buffer
            createVoxelSelectionRenderItem(container);
        }
    }
};
