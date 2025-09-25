#pragma once

#include <maya/MPxSubSceneOverride.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMesh.h>
#include <maya/MShaderManager.h>
#include "voxelshape.h"
#include <maya/MGeometryRequirements.h>
#include <maya/MGeometryExtractor.h>
#include <memory>
using namespace MHWRender;
using std::unique_ptr;
using std::make_unique;

struct RenderItemInfo {
    MIndexBufferDescriptor indexDesc;
    MShaderInstance* shaderInstance;
    MString renderItemName;
};

class VoxelSubSceneOverride : public MPxSubSceneOverride {
private:
    VoxelShape* voxelShape;
    
    ComPtr<ID3D11Buffer> positionsBuffer;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;

    ComPtr<ID3D11Buffer> normalsBuffer;
    ComPtr<ID3D11UnorderedAccessView> normalsUAV;

    // The deform shader also needs the original vertex positions and normals to do its transformations
    ComPtr<ID3D11Buffer> originalPositionsBuffer;
    ComPtr<ID3D11ShaderResourceView> originalPositionsSRV;

    ComPtr<ID3D11Buffer> originalNormalsBuffer;
    ComPtr<ID3D11ShaderResourceView> originalNormalsSRV;

    std::unordered_map<MGeometry::Semantic, unique_ptr<MVertexBuffer>> mayaVertexBuffers;
    std::vector<unique_ptr<MIndexBuffer>> indexBuffers;

    VoxelSubSceneOverride(const MObject& obj)
    : MPxSubSceneOverride(obj) {
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
            vertexBuffer->resourceHandle(buffer.Get(), sizeof(float) * data.size());

            ComPtr<ID3D11UnorderedAccessView>& uav = (semantic == MGeometry::kPosition) ? positionsUAV : normalsUAV;
            uav = DirectX::createUAV(buffer, true);

            // Also need to create a buffer with the original positions/normals for the deform shader to read from
            ComPtr<ID3D11Buffer>& originalBuffer = (semantic == MGeometry::kPosition) ? originalPositionsBuffer : originalNormalsBuffer;
            originalBuffer = DirectX::createReadOnlyBuffer(data);
            
            ComPtr<ID3D11ShaderResourceView>& originalSRV = (semantic == MGeometry::kPosition) ? originalPositionsSRV : originalNormalsSRV;
            originalSRV = DirectX::createSRV(originalBuffer);
        }
        else {
            void* data = vertexBuffer->acquire(vertexCount, true);
            extractor.populateVertexBuffer(data, vertexCount, vbDesc);
            vertexBuffer->commit(data);
        }
        
        vertexBufferArray.addBuffer(vbDesc.name(), vertexBuffer.get());
        mayaVertexBuffers.insert({semantic, std::move(vertexBuffer)});
    }

    MIndexBuffer* createIndexBuffer(const RenderItemInfo& itemInfo, const MGeometryExtractor& extractor) {
        unsigned int numTriangles = extractor.primitiveCount(itemInfo.indexDesc);
        if (numTriangles == 0) return nullptr;
    
        auto indexBuffer = make_unique<MIndexBuffer>(MGeometry::kUnsignedInt32);
        void* indexData = indexBuffer->acquire(3 * numTriangles, true);

        extractor.populateIndexBuffer(indexData, 3 * numTriangles, itemInfo.indexDesc);
        indexBuffer->commit(indexData);

        MIndexBuffer* rawIndexBuffer = indexBuffer.get();
        indexBuffers.push_back(std::move(indexBuffer));
        
        return rawIndexBuffer;
    }

    MRenderItem* createRenderItem(MSubSceneContainer& container, const RenderItemInfo& itemInfo) {
        MRenderItem* renderItem = container.find(itemInfo.renderItemName);
        if (renderItem) return renderItem;

        renderItem = MRenderItem::Create(itemInfo.renderItemName, MRenderItem::MaterialSceneItem, MGeometry::kTriangles);
        renderItem->setDrawMode(static_cast<MGeometry::DrawMode>(MGeometry::kShaded | MGeometry::kTextured));
        renderItem->setSelectionMask(MSelectionMask::kSelectMeshes);
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

    void getAllMeshVertices(
        const MGeometryExtractor& extractor,
        std::vector<uint>& vertexIndices,
        std::vector<float>& vertexPositions
    ) {
        MVertexBufferDescriptor posDesc("position", MGeometry::kPosition, MGeometry::kFloat, 3);
        const unsigned int vertexCount = extractor.vertexCount(); 
        vertexPositions.resize(vertexCount * 3);
        extractor.populateVertexBuffer(vertexPositions.data(), vertexCount, posDesc);

        // No face component arg --> whole mesh
        MIndexBufferDescriptor indexDesc(MIndexBufferDescriptor::kTriangle, MString(), MGeometry::kTriangles, 0);
        const unsigned int primitiveCount = extractor.primitiveCount(indexDesc);
        vertexIndices.resize(primitiveCount * 3);
        extractor.populateIndexBuffer(vertexIndices.data(), primitiveCount * 3, indexDesc);
    }

public:
    inline static MString drawDbClassification = "drawdb/subscene/voxelSubsceneOverride";
    inline static MString drawRegistrantId = "VoxelSubSceneOverridePlugin";

    static MPxSubSceneOverride* creator(const MObject& obj) 
    {
        return new VoxelSubSceneOverride(obj);
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
        return (container.count() == 0);
    }

    /**
     * This method is responsible for populating the MSubSceneContainer with render items. In our case, we want the our custom VoxelShape
     * to have the same geometry, topology, and shading as the original mesh it deforms. To do so, we use the shading sets of the original mesh
     * to tell us what geometry requirements we need to extract and recreate here.
     */
    void update(MSubSceneContainer& container, const MFrameContext& frameContext) override
    {
        if (!voxelShape) return;
        MStatus status;
        mayaVertexBuffers.clear();
        indexBuffers.clear();

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

            MRenderItem* renderItem = createRenderItem(container, itemInfo);
            setGeometryForRenderItem(*renderItem, vertexBufferArray, *rawIndexBuffer, &bounds);
        }

        // The voxel shape needs the whole mesh's vertex positions and indices to tag each vertex with the voxel it belongs to.
        // It's important to do this in the order that MGeometryExtractor provides the buffers to us.
        std::vector<uint> vertexIndices;
        std::vector<float> vertexPositions;
        getAllMeshVertices(extractor, vertexIndices, vertexPositions);

        voxelShape->initializeDeformVerticesCompute(
            vertexIndices,
            vertexPositions,
            positionsUAV,
            normalsUAV,
            originalPositionsSRV, 
            originalNormalsSRV
        );
    }
};
