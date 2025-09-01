#pragma once

#include "voxelshape.h"
#include <maya/MPxGeometryOverride.h>
#include <maya/MTypeId.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMesh.h>
#include <maya/MFnMeshData.h>
#include <maya/MGeometryRequirements.h>
#include <maya/MHWGeometry.h>
#include <maya/MFloatPointArray.h>
#include <maya/MFloatVectorArray.h>
#include <maya/MShaderManager.h>
using namespace MHWRender;

class VoxelGeometryOverride : public MPxGeometryOverride {
public:
    inline static MString drawRegistryId = "VoxelDestroyerPlugin";

    static MPxGeometryOverride* creator(const MObject& obj) {
        return new VoxelGeometryOverride(obj);
    }
    
    ~VoxelGeometryOverride() override {
        voxelShape = nullptr;
    };

    DrawAPI supportedDrawAPIs() const override {
        // We're reliant on DX11 VP2 for this whole algorithm.
        return kDirectX11;
    }

    void updateRenderItems(const MDagPath& path, MRenderItemList& renderItems) override {
        if (!path.isValid()) return;

        MRenderer* renderer = MRenderer::theRenderer();
        if (!renderer) return;
        
        const MShaderManager* shaderManager = renderer->getShaderManager();
        if (!shaderManager) return;
        
        auto renderItemIndex = renderItems.indexOf(shadedRenderItemName);
        // Render item already exists
        if (renderItemIndex >= 0) return;
        
        MRenderItem* shadedRenderItem = MRenderItem::Create(
            shadedRenderItemName,
            MRenderItem::MaterialSceneItem,
            MGeometry::kTriangles
        );
        shadedRenderItem->setDrawMode((MGeometry::DrawMode)(MGeometry::kShaded | MGeometry::kTextured));
        
        shadedRenderItem->enable(true);
        // Get an instance of a 3dSolidShader from the shader manager.
        // The shader tells the graphics hardware how to draw the geometry. 
        // The MShaderInstance is a reference to a shader along with the values for the shader parameters.
        MShaderInstance* shader = shaderManager->getStockShader(MShaderManager::k3dSolidShader);
        if (!shader) return;

        const float blueColor[] = { 0.0f, 0.0f, 1.0f, 1.0f };
        shader->setParameter("solidColor", blueColor);
        shadedRenderItem->setShader(shader);
        shaderManager->releaseShader(shader);
        renderItems.append(shadedRenderItem);
    }

    // Any information from the DG must be queried and cached here. It is invalid and may cause instability to do so later.
    void updateDG() override {
        if (!voxelShape) return;
        MGlobal::displayInfo("Updating DG in VoxelGeometryOverride");

        MObject srcMesh = voxelShape->geometryData();
        MFnMesh fnMesh(srcMesh);

        MFnMeshData fnMeshData;
        voxelMeshData = fnMeshData.create();
        fnMesh.copy(srcMesh, voxelMeshData);
    }

    void populateGeometry(		
        const MGeometryRequirements& requirements,
		const MRenderItemList& renderItems,
		MGeometry& data
    ) override {
        if (!voxelShape) return;
        MStatus status;
        MFnMesh fnMesh(voxelMeshData);
        MGlobal::displayInfo(MString("Populating geometry"));

        // Populate vertex buffers
        // TODO: generalize to a function.
        const MVertexBufferDescriptorList& vertexBufferDescriptorList = requirements.vertexRequirements();
        for (int i = 0; i < vertexBufferDescriptorList.length(); ++i) {
            MVertexBufferDescriptor desc{};
            if (!vertexBufferDescriptorList.getDescriptor(i, desc)) continue;

            switch (desc.semantic())
            {
            case MGeometry::kPosition:
            {
                MVertexBuffer* positionBuffer = data.createVertexBuffer(desc);
                if (!positionBuffer) continue;

                void* buffer = positionBuffer->acquire(fnMesh.numVertices(), true);
                if (!buffer) continue;

                memcpy(buffer, fnMesh.getRawPoints(&status), fnMesh.numVertices() * desc.dimension() * sizeof(float));
                positionBuffer->commit(buffer);

            }
            break;
            case MGeometry::kNormal:
            {
                MVertexBuffer* normalBuffer = data.createVertexBuffer(desc);
                if (!normalBuffer) continue;

                void* buffer = normalBuffer->acquire(fnMesh.numNormals(), true);
                if (!buffer) continue;

                memcpy(buffer, fnMesh.getRawNormals(&status), fnMesh.numNormals() * desc.dimension() * sizeof(float));
                normalBuffer->commit(buffer);
            }
            break;
            case MGeometry::kTexture:
            {
                MVertexBuffer* texCoordBuffer = data.createVertexBuffer(desc);
                if (!texCoordBuffer) continue;

                void* buffer = texCoordBuffer->acquire(fnMesh.numUVs(), true);
                if (!buffer) continue;

                memcpy(buffer, fnMesh.getRawUVs(&status), fnMesh.numUVs() * desc.dimension() * sizeof(float));
                texCoordBuffer->commit(buffer);
            }
            break;
            }
        }

        // Populate index buffers
        const int numItems = renderItems.length();
        for (int i = 0; i < numItems; ++i) {
            const MRenderItem* item = renderItems.itemAt(i);
            if (!item) continue;
            if (item->primitive() != MGeometry::kTriangles) continue; // Only triangles supported

            MIndexBuffer* indexBuffer = data.createIndexBuffer(MGeometry::kUnsignedInt32);
            if (!indexBuffer) continue;

            MIntArray triCounts, triVertices;
            fnMesh.getTriangles(triCounts, triVertices);

            void* buffer = indexBuffer->acquire(triVertices.length(), true);
            if (!buffer) continue;

            uint32_t* indexBufferData = static_cast<uint32_t*>(buffer);
            for (unsigned int j = 0; j < static_cast<unsigned int>(triVertices.length()); ++j) {
                indexBufferData[j] = static_cast<uint32_t>(triVertices[j]);
            }
            
            indexBuffer->commit(buffer);
            item->associateWithIndexBuffer(indexBuffer);
        }
    }

    void cleanUp() override {
        voxelMeshData = MObject::kNullObj;
    }

    bool supportsEvaluationManagerParallelUpdate() const override {
        return true;
    }

    bool isIndexingDirty(const MRenderItem& item) override {
        return false;
    }

    bool isStreamDirty(const MVertexBufferDescriptor& desc) override {
        return false;
    }

    bool requiresGeometryUpdate() const override {
        return false;
    }

    bool requiresUpdateRenderItems(const MDagPath& path) const override {
        return false;
    }

private:
    inline static MString shadedRenderItemName = "voxelGeometryRenderItem";
    VoxelShape* voxelShape;
    MObject voxelMeshData;

    VoxelGeometryOverride(const MObject& obj) : MPxGeometryOverride(obj) {
        MFnDependencyNode fnNode(obj);
        voxelShape = static_cast<VoxelShape*>(fnNode.userNode());
    }
};