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
#include <maya/MEventMessage.h>
#include <maya/MCommandMessage.h>
#include <maya/MCallbackIdArray.h>
#include <functional>

using namespace MHWRender;
using std::unique_ptr;
using std::make_unique;

struct RenderItemInfo {
    MIndexBufferDescriptor indexDesc;
    MShaderInstance* shaderInstance;
    MString renderItemName;
};

class SelectionCustomData : public MUserData {
public:
    SelectionCustomData(std::function<void(int)> onHover)
        : MUserData(), hoverCallback(std::move(onHover))
    {}
    ~SelectionCustomData() override = default;

    std::function<void(int)> hoverCallback;
};

/**
 * This converter is registered with the render item that writes to the selection buffer.
 * Generally, component converters are for converting index buffer indices to components, but in this case
 * we're just using it as an intersection machine for getting which voxels were clicked or hovered.
 */
class VoxelSubSceneComponentConverter : public MPxComponentConverter {
public:
    static MPxComponentConverter* creator() {
        return new VoxelSubSceneComponentConverter();
    }

    VoxelSubSceneComponentConverter() = default;
    ~VoxelSubSceneComponentConverter() override = default;

    void addIntersection(MIntersection& intersection) override {
        // Instance IDs are 1-based, so subtract 1 to get a 0-based index.
        int instanceID = intersection.instanceID() - 1;
        if (instanceID < 0) return;

        MFnSingleIndexedComponent fnFaceComp;
        fnFaceComp.setObject(componentObj);

        // Hijack this face component to store the voxel instance ID rather than a face index.
        fnFaceComp.addElement(instanceID);
        customData->hoverCallback(instanceID);
    }

    MSelectionMask selectionMask() const override {
        return MSelectionMask::kSelectMeshFaces;
    }

    void initialize(const MRenderItem& renderItem) override {
        componentObj = fnComp.create(MFn::kMeshPolygonComponent);
        customData = static_cast<SelectionCustomData*>(renderItem.getCustomData().get());
    }

    MObject component() override {
        return componentObj;
    }

private:
    SelectionCustomData* customData = nullptr;
    MObject componentObj = MObject::kNullObj;
    MFnSingleIndexedComponent fnComp;
};

class VoxelSubSceneOverride : public MPxSubSceneOverride {
private:
    VoxelShape* voxelShape;
    MObject voxelShapeObj;

    enum class ShowHideStateChange {
        None,
        HideSelected,
        ShowAll,
        ShowSelected
    } showHideStateChange = ShowHideStateChange::None;
    
    bool shouldUpdate = true;
    bool selectionChanged = false;
    bool hoveredVoxelChanged = false;
    MCallbackIdArray callbackIds;
    MMatrixArray selectedVoxelMatrices;
    MMatrixArray hoveredVoxelMatrices; // Will only ever have 0 or 1 matrix in it.
    MMatrixArray visibleVoxelMatrices; // When hiding pieces of the mesh, also need to hide the selected voxels.
    MObjectArray visibleVoxelFaceComponents; 
    std::unordered_set<uint> voxelsToHide;
    std::unordered_map<MString, std::vector<uint>, Utils::MStringHash, Utils::MStringEq> hiddenIndices;         // hidden face indices per render item
    std::unordered_map<MString, std::vector<uint>, Utils::MStringHash, Utils::MStringEq> recentlyHiddenIndices; // the most recent faces to be hidden (again mapped by render item)

    inline static const MString voxelSelectedHighlightItemName = "VoxelSelectedHighlightItem";
    inline static const MString voxelPreviewSelectionHighlightItemName = "VoxelPreviewSelectionHighlightItem";
    inline static const MString voxelWireframeRenderItemName = "VoxelWireframeRenderItem";
    inline static const MString voxelSelectionRenderItemName = "VoxelSelectionItem";

    ComPtr<ID3D11Buffer> positionsBuffer;
    ComPtr<ID3D11UnorderedAccessView> positionsUAV;

    ComPtr<ID3D11Buffer> normalsBuffer;
    ComPtr<ID3D11UnorderedAccessView> normalsUAV;

    // The deform shader also needs the original vertex positions and normals to do its transformations
    ComPtr<ID3D11Buffer> originalPositionsBuffer;
    ComPtr<ID3D11ShaderResourceView> originalPositionsSRV;

    ComPtr<ID3D11Buffer> originalNormalsBuffer;
    ComPtr<ID3D11ShaderResourceView> originalNormalsSRV;

    // These are just stored to persist the buffers. Subscene owns any geometry buffers it creates.
    std::vector<unique_ptr<MVertexBuffer>> meshVertexBuffers;
    std::vector<unique_ptr<MIndexBuffer>> meshIndexBuffers;
    std::vector<uint> allMeshIndices; // Mesh vertex indices, _not_ split per render item but rather for the entire mesh.
    std::unordered_set<MUint64> meshRenderItemIDs;

    unique_ptr<MVertexBuffer> voxelVertexBuffer;
    std::unordered_map<MGeometry::Primitive, unique_ptr<MIndexBuffer>> voxelIndexBuffers;

    VoxelSubSceneOverride(const MObject& obj)
    : MPxSubSceneOverride(obj), voxelShapeObj(obj) {
        MFnDependencyNode dn(obj);
        voxelShape = static_cast<VoxelShape*>(dn.userNode());

        MCallbackId callbackId = MEventMessage::addEventCallback("SelectionChanged", onSelectionChanged, this, nullptr);
        callbackIds.append(callbackId);

        callbackId = MCommandMessage::addProcCallback(onShowHideStateChange, this, nullptr);
        callbackIds.append(callbackId);
    }

    static void onSelectionChanged(void* clientData) {
        VoxelSubSceneOverride* subscene = static_cast<VoxelSubSceneOverride*>(clientData);
        
        // Collect the voxel instances that are selected
        const MObjectArray& activeComponents = subscene->voxelShape->activeComponents();
        const MMatrixArray& voxelMatrices = subscene->visibleVoxelMatrices;
        subscene->selectedVoxelMatrices.clear();
        subscene->hoveredVoxelMatrices.clear();

        for (const MObject& comp : activeComponents) {
            MFnSingleIndexedComponent fnComp(comp);
            for (int i = 0; i < fnComp.elementCount(); ++i) {
                int voxelInstanceId = fnComp.element(i);
                subscene->selectedVoxelMatrices.append(voxelMatrices[voxelInstanceId]);
            }
        }

        subscene->shouldUpdate = true;
        subscene->selectionChanged = true;
        // changing selection invalidates toggling hidden faces
        subscene->hiddenIndices.merge(subscene->recentlyHiddenIndices);
        subscene->recentlyHiddenIndices.clear();
    }

    /**
     * Surprisingly, neither MpxSurfaceShape nor MPxSubsceneOverride provide any mechanism for hooking into hiding components.
     * To handle this, we just have to listen for commands that contain "hide" or "showHidden", etc.
     */
    static void onShowHideStateChange(const MString& procName, unsigned int procId, bool isProcEntry, unsigned int type, void* clientData) {
        // Only need to run this callback once (but it's invoked on entry and exit of the procedure)
        if (!isProcEntry) return;
        
        bool toggleHideCommand = (procName.indexW("toggleVisibilityAndKeepSelection") != -1);
        bool hideCommand = (procName.indexW("hide") != -1);
        bool showHiddenCommand = (procName.indexW("showHidden") != -1);
        if (!toggleHideCommand && !hideCommand && !showHiddenCommand) return;
        
        VoxelSubSceneOverride* subscene = static_cast<VoxelSubSceneOverride*>(clientData);
        const MObjectArray& activeComponents = subscene->voxelShape->activeComponents();
        int numSelectedVoxels = activeComponents.length();
        subscene->shouldUpdate = true;

        if (hideCommand) {
            subscene->showHideStateChange = ShowHideStateChange::HideSelected;
        }
        else if (showHiddenCommand) {
            subscene->showHideStateChange = ShowHideStateChange::ShowAll;
        }
        else if (toggleHideCommand) {
            subscene->showHideStateChange = (subscene->recentlyHiddenIndices.size() > 0) ? ShowHideStateChange::ShowSelected : ShowHideStateChange::HideSelected;
        }

        if (subscene->showHideStateChange != ShowHideStateChange::HideSelected) return;
        subscene->hiddenIndices.merge(subscene->recentlyHiddenIndices);
        subscene->recentlyHiddenIndices.clear();

        for (const MObject& comp : activeComponents) {
            MFnSingleIndexedComponent voxelComponent(comp);
            for (int i = 0; i < voxelComponent.elementCount(); ++i) {
                int voxelInstanceId = voxelComponent.element(i);

                subscene->voxelsToHide.insert(voxelInstanceId);
            }
        }
    }

    void onHoveredVoxelChange(int hoveredVoxelInstanceId) {
        hoveredVoxelMatrices.clear();
        if (hoveredVoxelInstanceId < 0 || hoveredVoxelInstanceId >= (int)visibleVoxelMatrices.length()) return;

        hoveredVoxelMatrices.append(visibleVoxelMatrices[hoveredVoxelInstanceId]);

        shouldUpdate = true;
        hoveredVoxelChanged = true;
    }

    /**
     * Given a list of voxels to hide (from which we can get the contained mesh indices to hide), iterate over each mesh render item and remove the corresponding indices from its index buffer.
     * Unfortunately, there's no faster way to do this, but it's not terribly slow if we use a set for the indices to hide.
     */
    void hideSelectedMeshFaces(MSubSceneContainer& container) {
        MSubSceneContainer::Iterator* it = container.getIterator();
        it->reset();
        MRenderItem* item = nullptr;

        // Convert voxelsToHide to a set of face indices to hide
        std::unordered_set<uint> indicesToHide;
        MFnSingleIndexedComponent faceComponent;

        for (uint voxelInstanceId : voxelsToHide) {
            faceComponent.setObject(visibleVoxelFaceComponents[voxelInstanceId]);

            for (int j = 0; j < faceComponent.elementCount(); ++j) {
                int faceIdx = faceComponent.element(j);
    
                indicesToHide.insert(allMeshIndices[faceIdx * 3 + 0]);
                indicesToHide.insert(allMeshIndices[faceIdx * 3 + 1]);
                indicesToHide.insert(allMeshIndices[faceIdx * 3 + 2]);
            }
        }

        // Now go through each (mesh) render item and remove those indices from its index buffer.
        while ((item = it->next()) != nullptr) {
            if (meshRenderItemIDs.find(item->InternalObjectId()) == meshRenderItemIDs.end()) continue;
            const MString& itemName = item->name();

            MIndexBuffer* indexBuffer = item->geometry()->indexBuffer(0);
            uint32_t* indices = static_cast<uint32_t*>(indexBuffer->map());
            std::vector<uint32_t> newIndices(indexBuffer->size(), 0);
            
            for (unsigned int i = 0; i < indexBuffer->size(); ++i) {
                // Didn't find this index in the set of indices to hide, so keep it.
                if (indicesToHide.find(indices[i]) == indicesToHide.end()) {
                    newIndices[i] = indices[i];
                    continue;
                };

                recentlyHiddenIndices[itemName].push_back(indices[i]);
            }
            
            indexBuffer->unmap();

            if (newIndices.size() > 0) {
                indexBuffer->update(newIndices.data(), 0, static_cast<unsigned int>(newIndices.size()), false);
            }
        }
        
        it->destroy();
    }

    /**
     * Create new instanced transform arrays for the voxel render items, excluding any hidden voxels.
     */
    void hideSelectedVoxels(MSubSceneContainer& container) {
        MMatrixArray oldVisibleVoxelMatrices = visibleVoxelMatrices;
        MObjectArray oldVisibleVoxelFaceComponents = visibleVoxelFaceComponents;
        visibleVoxelMatrices.clear();
        visibleVoxelFaceComponents.clear();

        // First of all, the selection highlight render items should show 0 voxels now, so use the cleared array.
        updateVoxelRenderItem(container, voxelSelectedHighlightItemName, visibleVoxelMatrices);
        updateVoxelRenderItem(container, voxelPreviewSelectionHighlightItemName, visibleVoxelMatrices);

        // Filter the voxel matrices array and voxel face components to exclude any hidden voxels.
        for (uint i = 0; i < oldVisibleVoxelMatrices.length(); ++i) {
            if (voxelsToHide.find(i) != voxelsToHide.end()) continue;
            
            visibleVoxelMatrices.append(oldVisibleVoxelMatrices[i]);
            visibleVoxelFaceComponents.append(oldVisibleVoxelFaceComponents[i]);
        }

        updateVoxelRenderItem(container, voxelWireframeRenderItemName, visibleVoxelMatrices);
        updateVoxelRenderItem(container, voxelSelectionRenderItemName, visibleVoxelMatrices);
    }

    void updateVoxelRenderItem(MSubSceneContainer& container, const MString& itemName, const MMatrixArray& voxelMatrices) {
        MRenderItem* item = container.find(itemName);
        setInstanceTransformArray(*item, voxelMatrices);
        item->enable(voxelMatrices.length() > 0);
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

    void createMeshVertexBuffer(const MVertexBufferDescriptor& vbDesc, const MGeometryExtractor& extractor, uint vertexCount, MVertexBufferArray& vertexBufferArray) {
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

    MIndexBuffer* createMeshIndexBuffer(const RenderItemInfo& itemInfo, const MGeometryExtractor& extractor) {
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

        meshRenderItemIDs.insert(renderItem->InternalObjectId());
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

    unsigned int getAllMeshIndices(
        const MGeometryExtractor& extractor
    ) {
        // No face component arg --> whole mesh
        MIndexBufferDescriptor indexDesc(MIndexBufferDescriptor::kTriangle, MString(), MGeometry::kTriangles, 0);
        const unsigned int primitiveCount = extractor.primitiveCount(indexDesc);
        allMeshIndices.resize(primitiveCount * 3);
        extractor.populateIndexBuffer(allMeshIndices.data(), primitiveCount * 3, indexDesc);

        return extractor.vertexCount(); 
    }

    void updateSelectionGranularity(
		const MDagPath& path,
		MSelectionContext& selectionContext) 
    {
        selectionContext.setSelectionLevel(MHWRender::MSelectionContext::kComponent);    
    }

    void createVoxelWireframeRenderItem(MSubSceneContainer& container) {
        MRenderItem* renderItem = MRenderItem::Create(voxelWireframeRenderItemName, MRenderItem::DecorationItem, MGeometry::kLines);
        MShaderInstance* shader = MRenderer::theRenderer()->getShaderManager()->getStockShader(MShaderManager::k3dSolidShader);
        const float solidColor[] = {0.0f, 1.0f, 0.25f, 1.0f};
        shader->setParameter("solidColor", solidColor);

        renderItem->setDrawMode(static_cast<MGeometry::DrawMode>(MGeometry::kWireframe | MGeometry::kShaded | MGeometry::kTextured));
        renderItem->depthPriority(MRenderItem::sActiveWireDepthPriority);
        renderItem->setWantConsolidation(true);
        renderItem->setHideOnPlayback(true);
        renderItem->setShader(shader);
        container.add(renderItem);

        setVoxelGeometryForRenderItem(*renderItem, MGeometry::kLines);

        const MMatrixArray& voxelInstanceTransforms = voxelShape->getVoxels().get()->modelMatrices;
        setInstanceTransformArray(*renderItem, voxelInstanceTransforms);
    }

    void createVoxelSelectionRenderItem(MSubSceneContainer& container) {
        MRenderItem* renderItem = MRenderItem::Create(voxelSelectionRenderItemName, MRenderItem::DecorationItem, MGeometry::kTriangles);
        MShaderInstance* shader = MRenderer::theRenderer()->getShaderManager()->getStockShader(MShaderManager::k3dDefaultMaterialShader);
        MSharedPtr<MUserData> customData(new SelectionCustomData(
            std::bind(&VoxelSubSceneOverride::onHoveredVoxelChange, this, std::placeholders::_1)
        ));

        MSelectionMask selMask;
        selMask.addMask(MSelectionMask::kSelectMeshFaces);
        selMask.addMask(MSelectionMask::kSelectMeshes);

        renderItem->setDrawMode(static_cast<MGeometry::DrawMode>(MGeometry::kSelectionOnly));
        renderItem->setSelectionMask(selMask);
        renderItem->depthPriority(MRenderItem::sSelectionDepthPriority);
        renderItem->setWantConsolidation(true);
        renderItem->setHideOnPlayback(true);
        renderItem->setShader(shader);
        renderItem->setCustomData(customData);
        container.add(renderItem);

        setVoxelGeometryForRenderItem(*renderItem, MGeometry::kTriangles);

        const MMatrixArray& voxelInstanceTransforms = voxelShape->getVoxels().get()->modelMatrices;
        setInstanceTransformArray(*renderItem, voxelInstanceTransforms);
    }

    void createVoxelSelectedHighlightRenderItem(MSubSceneContainer& container, const MString& renderItemName, const std::array<float, 4>& color) {
        MRenderItem* renderItem = MRenderItem::Create(renderItemName, MRenderItem::DecorationItem, MGeometry::kTriangles);
        MShaderInstance* shader = MRenderer::theRenderer()->getShaderManager()->getStockShader(MShaderManager::k3dSolidShader);
        shader->setParameter("solidColor", color.data());

        renderItem->setDrawMode(static_cast<MGeometry::DrawMode>(MGeometry::kWireframe | MGeometry::kShaded | MGeometry::kTextured));
        renderItem->depthPriority(MRenderItem::sSelectionDepthPriority);
        renderItem->setWantConsolidation(false);
        renderItem->setHideOnPlayback(true);
        renderItem->setShader(shader);
        renderItem->enable(false);
        container.add(renderItem);

        setVoxelGeometryForRenderItem(*renderItem, MGeometry::kTriangles);
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

            createMeshVertexBuffer(vbDesc, extractor, vertexCount, vertexBufferArray);
        }

        // Create an index buffer + render item for each shading set of the original mesh (which corresponds to an indexing requirement)
        // Use an effectively infinite bounding box because the voxel shape can deform and shatter.
        double bound = 1e10;
        const MBoundingBox bounds(MPoint(-bound, -bound, -bound), MPoint(bound, bound, bound));
        for (const RenderItemInfo& itemInfo : renderItemInfos) {
            MIndexBuffer* rawIndexBuffer = createMeshIndexBuffer(itemInfo, extractor);
            if (!rawIndexBuffer) continue;

            MRenderItem* renderItem = createSingleMeshRenderItem(container, itemInfo);
            setGeometryForRenderItem(*renderItem, vertexBufferArray, *rawIndexBuffer, &bounds);
        }

        // The voxel shape needs the whole mesh's vertex indices to tag each vertex with the voxel it belongs to.
        // It's important to do the tagging using the vertex buffer that MGeometryExtractor provides.
        unsigned int numVertices = getAllMeshIndices(extractor);
                
        voxelShape->initializeDeformVerticesCompute(
            allMeshIndices,
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

        MEventMessage::removeCallbacks(callbackIds);
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
            visibleVoxelMatrices = voxelShape->getVoxels().get()->modelMatrices;
            visibleVoxelFaceComponents = voxelShape->getVoxels().get()->faceComponents;

            // The render items for the actual, voxelized mesh.
            createMeshRenderItems(container);
            // Geometry buffers for a simple unit cube, reused for all voxel render items.
            createVoxelGeometryBuffers();
            // The visible wireframe render item
            createVoxelWireframeRenderItem(container);
            // Invisible item, only gets drawn to the selection buffer to enable selection
            createVoxelSelectionRenderItem(container);
            // Shows highlights for selected voxels
            createVoxelSelectedHighlightRenderItem(container, voxelSelectedHighlightItemName, {0.0f, 1.0f, 0.25f, 0.5f});
            // Shows highlight for hovered voxel
            createVoxelSelectedHighlightRenderItem(container, voxelPreviewSelectionHighlightItemName, {1.0f, 1.0f, 0.0f, 0.5f});
        }
        
        if (selectionChanged) {
            updateVoxelRenderItem(container, voxelSelectedHighlightItemName, selectedVoxelMatrices);
            selectionChanged = false;
        }

        if (hoveredVoxelChanged) {
            updateVoxelRenderItem(container, voxelPreviewSelectionHighlightItemName, hoveredVoxelMatrices);
            hoveredVoxelChanged = false;
        }

        switch (showHideStateChange)
        {
        case ShowHideStateChange::None:
            break;
        case ShowHideStateChange::ShowAll:
            break;
        case ShowHideStateChange::ShowSelected:
            break;
        case ShowHideStateChange::HideSelected:
            hideSelectedMeshFaces(container);
            hideSelectedVoxels(container);
            voxelsToHide.clear();
            break;
        }

        showHideStateChange = ShowHideStateChange::None;

        shouldUpdate = false;
    }
};
