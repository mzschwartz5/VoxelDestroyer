#pragma once
#include <maya/MViewport2Renderer.h>
#include <maya/MRenderTargetManager.h>
#include <maya/MGlobal.h>
#include <maya/MShaderManager.h>
#include "../../utils.h"
#include "../../cube.h"
#include <maya/MHWGeometry.h>
#include <maya/MRenderUtilities.h>
#include "../../directx/directx.h"
#include "../../directx/pingpongview.h"
#include <maya/MDrawContext.h>
#include "../../constants.h"
#include "../../resource.h"
#include "../tools/voxelpaintcontext.h"
#include <maya/MMatrixArray.h>
#include <algorithm>
#include <array>
#include <maya/MConditionMessage.h>
using namespace MHWRender;

// Helpful docs: https://help.autodesk.com/view/MAYADEV/2025/ENU/?guid=Maya_DEVHELP_Viewport_2_0_API_Maya_Viewport_2_0_API_Guide_Advanced_Topics_Implement_an_MRenderOverride_html
class VoxelPaintRenderOperation : public MUserRenderOperation {
public:
    inline static const MString paintColorRenderTargetName = "voxelPaintColorTarget";
    inline static const MString paintDepthRenderTargetName = "voxelPaintDepthTarget";

    VoxelPaintRenderOperation(const MString& name) : MUserRenderOperation(name) {
        mOperationType = kUserDefined;
        // Note: Every render operation automatically includes the standard color and depth targets as inputs/outputs.
        // You can clear them explicitly, but we want to use them AND add two of our own inputs 
        // (which Maya will create for us based on the descriptions provided in getInputTargetDescription).
        mInputTargetNames.append(paintDepthRenderTargetName);
        mInputTargetNames.append(paintColorRenderTargetName);
        
        MRenderTargetDescription desc;
        desc.setName(paintColorRenderTargetName);
        desc.setRasterFormat(MHWRender::kR32_UINT);
        renderTargetDescriptions[0] = desc;

        desc.setName(paintDepthRenderTargetName);
        desc.setRasterFormat(MHWRender::kD32_FLOAT);
        renderTargetDescriptions[1] = desc;

        MRasterizerStateDesc rasterDesc;
        rasterDesc.setDefaults();
        rasterDesc.scissorEnable = true;
        scissorRasterState = MStateManager::acquireRasterizerState(rasterDesc);

        rasterDesc.setDefaults();
        rasterDesc.depthBiasIsFloat = true;
        rasterDesc.depthBias = -1e-4f;
        rasterDesc.slopeScaledDepthBias = -1.0f;
        depthBiasRasterState = MStateManager::acquireRasterizerState(rasterDesc);

        MBlendStateDesc blendDesc;
        blendDesc.setDefaults();
        MTargetBlendDesc& targetDesc = blendDesc.targetBlends[0];
        targetDesc.setDefaults();
        targetDesc.blendEnable = true;
        targetDesc.sourceBlend = MBlendState::kSourceAlpha;
        targetDesc.destinationBlend = MBlendState::kInvSourceAlpha;
        targetDesc.blendOperation = MBlendState::kAdd;
        targetDesc.alphaSourceBlend = MBlendState::kOne;
        targetDesc.alphaDestinationBlend = MBlendState::kInvSourceAlpha;
        targetDesc.alphaBlendOperation = MBlendState::kAdd;
        alphaEnabledBlendState = MStateManager::acquireBlendState(blendDesc);

        void* shaderData = nullptr;
        DWORD size = Utils::loadResourceFile(DirectX::getPluginInstance(), IDR_SHADER15, L"SHADER", &shaderData);

        MShaderCompileMacro macros[] = {
            {"PAINT_SELECTION_TECHNIQUE_NAME", PAINT_SELECTION_TECHNIQUE_NAME},
            {"PAINT_POSITION", PAINT_POSITION},
            {"PAINT_RADIUS", PAINT_RADIUS},
            {"PAINT_VALUE", PAINT_VALUE},
            {"PAINT_MODE", PAINT_MODE},
            {"LOW_COLOR", LOW_COLOR},
            {"HIGH_COLOR", HIGH_COLOR},
            {"COMPONENT_MASK", COMPONENT_MASK}
        };
        paintSelectionShader = MRenderer::theRenderer()->getShaderManager()->getEffectsBufferShader(
            shaderData, size, PAINT_SELECTION_TECHNIQUE_NAME, macros, ARRAYSIZE(macros)
        );

        std::vector<float> cubeVertices(cubeCornersFlattened.begin(), cubeCornersFlattened.end());
        cubeVb = DirectX::createReadOnlyBuffer<float>(cubeVertices, D3D11_BIND_VERTEX_BUFFER, DirectX::BufferFormat::RAW);

        std::vector<unsigned int> cubeIndices(cubeFacesFlattened.begin(), cubeFacesFlattened.end());
        cubeIb = DirectX::createReadOnlyBuffer<unsigned int>(cubeIndices, D3D11_BIND_INDEX_BUFFER, DirectX::BufferFormat::RAW);

        unsubscribeFromPaintMove = VoxelPaintContext::subscribeToMousePositionChange([this](const MousePosition& mousePos) {
            updatePaintToolPos(mousePos.x, mousePos.y);
            hasBrushMoved = true;
        });

        unsubscribeFromPaintStateChange = VoxelPaintContext::subscribeToPaintDragStateChange([this](const PaintDragState& state) {
            paintRadius = state.selectRadius;
            brushMode = state.brushMode;
            brushValue = state.brushValue;
            cameraBased = state.cameraBased;
            lowColor = state.lowColor;
            highColor = state.highColor;
            componentMask = state.componentMask;
            hasBrushMoved = state.isDragging;
            voxelIDViews.clear(DirectX::clearUintBuffer);
        });

        playbackCallbackId = MConditionMessage::addConditionCallback("playingBack", [](bool state, void* clientData) {
            static_cast<VoxelPaintRenderOperation*>(clientData)->isPlayingBack = state;
        }, this);
    }

    ~VoxelPaintRenderOperation() override {
        if (paintSelectionShader)
        {
            MRenderer::theRenderer()->getShaderManager()->releaseShader(paintSelectionShader);
            paintSelectionShader = nullptr;
        }

        if (scissorRasterState)
        {
            MStateManager::releaseRasterizerState(scissorRasterState);
            scissorRasterState = nullptr;
        }

        if (depthBiasRasterState)
        {
            MStateManager::releaseRasterizerState(depthBiasRasterState);
            depthBiasRasterState = nullptr;
        }

        if (alphaEnabledBlendState)
        {
            MStateManager::releaseBlendState(alphaEnabledBlendState);
            alphaEnabledBlendState = nullptr;
        }

        unsubscribeFromPaintMove();
        unsubscribeFromPaintStateChange();
        MConditionMessage::removeCallback(playbackCallbackId);
    };

    /**
     * Draw the voxel cage to an offscreen render target, using the paint brush as a scissor.
     * In the first pass, IDs get drawn to the offscreen target where the brush intersects voxels.
     * In the second pass, we draw the voxels to the standard targets with their modified paint values.
     * 
     * Note: Maya will bind our input render targets for us, but our use case is complex so we just have to do it ourselves.
     */
    MStatus execute(const MDrawContext& drawContext) override {
        if (isPlayingBack || !paintSelectionShader || !instanceTransformSRV || instanceCount == 0) {
            return MStatus::kSuccess;
        }

        prepareShader(drawContext);
        setInputAssemblyState();

        if (hasBrushMoved) {
            voxelPaintViews->swap(&DirectX::copyBufferToBuffer);
            voxelIDViews.swap(&DirectX::clearUintBuffer);
            executeIDPass(drawContext);
            executePaintPass(drawContext);
            hasBrushMoved = false;
        }
        else {
            executeRenderPass(drawContext);
        }

        unbindResources(drawContext);
        return MStatus::kSuccess;
    }

    void prepareShader(const MDrawContext& drawContext) {
        paintSelectionShader->bind(drawContext);
        float paintPos[2] = { static_cast<float>(paintPosX), static_cast<float>(paintPosY) };
        float lowColorArr[4] = { lowColor.r, lowColor.g, lowColor.b, lowColor.a };
        float highColorArr[4] = { highColor.r, highColor.g, highColor.b, highColor.a };
        paintSelectionShader->setParameter(PAINT_POSITION, paintPos);
        paintSelectionShader->setParameter(PAINT_RADIUS, paintRadius);
        paintSelectionShader->setParameter(PAINT_VALUE, brushValue);
        paintSelectionShader->setParameter(PAINT_MODE, static_cast<int>(brushMode));
        paintSelectionShader->setParameter(LOW_COLOR, lowColorArr);
        paintSelectionShader->setParameter(HIGH_COLOR, highColorArr);
        paintSelectionShader->setParameter(COMPONENT_MASK, componentMask);
        paintSelectionShader->updateParameters(drawContext);
    }

    void executeIDPass(const MDrawContext& drawContext) {
        if (!cameraBased) return; // ID pass only needed for camera-based painting
        paintSelectionShader->activatePass(drawContext, 0);

        // Store off the current scissor rects and rasterizer/blend state, so we can change and restore them later
        ID3D11DeviceContext* dxContext = DirectX::getContext();
        MStateManager* stateManager = drawContext.getStateManager();
        const MRasterizerState* prevRS = stateManager->getRasterizerState();
        UINT prevRectCount = 0;
        dxContext->RSGetScissorRects(&prevRectCount, nullptr);
        std::vector<D3D11_RECT> prevScissorRects(prevRectCount);
        if (prevRectCount) dxContext->RSGetScissorRects(&prevRectCount, prevScissorRects.data());

        // Set our scissor rect and rasterizer state
        dxContext->RSSetScissorRects(1, &scissor);
        stateManager->setRasterizerState(scissorRasterState);
        
        // Bind the offscreen ID targets
        const MRenderTarget* paintColorTarget = getInputTarget(paintColorRenderTargetName);
        const MRenderTarget* paintDepthTarget = getInputTarget(paintDepthRenderTargetName);
        ID3D11RenderTargetView* paintColorRTV = static_cast<ID3D11RenderTargetView*>(paintColorTarget->resourceHandle());
        ID3D11DepthStencilView* paintDepthDSV = static_cast<ID3D11DepthStencilView*>(paintDepthTarget->resourceHandle());
        dxContext->OMSetRenderTargets(1, &paintColorRTV, paintDepthDSV);

        ID3D11ShaderResourceView* vs_srvs[] = {
            instanceTransformSRV.Get(),
            visibleToGlobalVoxelSRV.Get()
        };
        dxContext->VSSetShaderResources(0, ARRAYSIZE(vs_srvs), vs_srvs);

        dxContext->DrawIndexedInstanced(static_cast<UINT>(cubeFacesFlattened.size()), instanceCount, 0, 0, 0);
        updateRenderTargetSRV(paintColorRTV); // The target from the first pass will be read as an SRV in the second pass

        // Restore scissor and rasterizer state
        dxContext->RSSetScissorRects(prevRectCount, prevScissorRects.data());
        stateManager->setRasterizerState(prevRS);
    }

    void executePaintPass(const MDrawContext& drawContext) {
        int pass = cameraBased ? 1 : 2;
        paintSelectionShader->activatePass(drawContext, pass);

        MStateManager* stateManager = drawContext.getStateManager();
        ID3D11DeviceContext* dxContext = DirectX::getContext();
        const MBlendState* prevBlendState = stateManager->getBlendState();
        const MRasterizerState* prevRasterizerState = stateManager->getRasterizerState();
        stateManager->setRasterizerState(depthBiasRasterState);
        stateManager->setBlendState(alphaEnabledBlendState);

        const MRenderTarget* mainColorTarget = getInputTarget(kColorTargetName);
        const MRenderTarget* mainDepthTarget = getInputTarget(kDepthTargetName);
        ID3D11RenderTargetView* mainColorRTV = static_cast<ID3D11RenderTargetView*>(mainColorTarget->resourceHandle());
        ID3D11DepthStencilView* mainDepthDSV = static_cast<ID3D11DepthStencilView*>(mainDepthTarget->resourceHandle());
        ID3D11UnorderedAccessView* uavs[] = {
            voxelIDViews.UAV().Get(),
            voxelPaintViews->UAV().Get()
        };
        
        // Bind the onscreen, main render and depth targets.
        dxContext->OMSetRenderTargetsAndUnorderedAccessViews(
            1, // NumRTVs
            &mainColorRTV,
            mainDepthDSV,
            1, // UAVStartSlot (starts at 1 because 0 is reserved)
            ARRAYSIZE(uavs),
            uavs,
            nullptr
        );

        ID3D11ShaderResourceView* vs_srvs[] = {
            instanceTransformSRV.Get(),
            visibleToGlobalVoxelSRV.Get()
        };

        ID3D11ShaderResourceView* ps_srvs[] = {
            voxelIDViews.SRV().Get(),
            voxelPaintViews->SRV().Get(),
            renderTargetSRV.Get()
        };

        dxContext->VSSetShaderResources(0, ARRAYSIZE(vs_srvs), vs_srvs);
        dxContext->PSSetShaderResources(2, ARRAYSIZE(ps_srvs), ps_srvs); // Starts at slot 2 because slots 0 and 1 are for VS shader resources
        dxContext->DrawIndexedInstanced(static_cast<UINT>(cubeFacesFlattened.size()), instanceCount, 0, 0, 0);

        // Restore state
        stateManager->setBlendState(prevBlendState);
        stateManager->setRasterizerState(prevRasterizerState);
    }

    // Regular rendering, no ID'ing or painting - just rendering what's already painted when
    // the user isn't actively dragging the brush.
    void executeRenderPass(const MDrawContext& drawContext) {
        paintSelectionShader->activatePass(drawContext, 3);
        ID3D11DeviceContext* dxContext = DirectX::getContext();
        MStateManager* stateManager = drawContext.getStateManager();
        const MBlendState* prevBlendState = stateManager->getBlendState();
        const MRasterizerState* prevRasterizerState = stateManager->getRasterizerState();
        stateManager->setBlendState(alphaEnabledBlendState);
        stateManager->setRasterizerState(depthBiasRasterState);

        const MRenderTarget* mainColorTarget = getInputTarget(kColorTargetName);
        const MRenderTarget* mainDepthTarget = getInputTarget(kDepthTargetName);
        ID3D11RenderTargetView* mainColorRTV = static_cast<ID3D11RenderTargetView*>(mainColorTarget->resourceHandle());
        ID3D11DepthStencilView* mainDepthDSV = static_cast<ID3D11DepthStencilView*>(mainDepthTarget->resourceHandle());

        ID3D11ShaderResourceView* vs_srvs[] = {
            instanceTransformSRV.Get(),
            visibleToGlobalVoxelSRV.Get()
        };

        // Bind the onscreen, main render and depth targets.
        dxContext->OMSetRenderTargetsAndUnorderedAccessViews(
            1, // NumRTVs
            &mainColorRTV,
            mainDepthDSV,
            2, // UAVStartSlot
            1, // NumUAVs
            voxelPaintViews->UAV().GetAddressOf(),
            nullptr
        );

        dxContext->VSSetShaderResources(0, ARRAYSIZE(vs_srvs), vs_srvs);
        dxContext->DrawIndexedInstanced(static_cast<UINT>(cubeFacesFlattened.size()), instanceCount, 0, 0, 0);

        // Restore state
        stateManager->setBlendState(prevBlendState);
        stateManager->setRasterizerState(prevRasterizerState);
    }

    void setInputAssemblyState() {
        ID3D11DeviceContext* dxContext = DirectX::getContext();
        UINT stride = sizeof(float) * 3;
        UINT offset = 0;
        dxContext->IASetVertexBuffers(0, 1, cubeVb.GetAddressOf(), &stride, &offset);
        dxContext->IASetIndexBuffer(cubeIb.Get(), DXGI_FORMAT_R32_UINT, 0);
        dxContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void unbindResources(const MDrawContext& drawContext) {
        ID3D11DeviceContext* dxContext = DirectX::getContext();
        const MRenderTarget* mainColorTarget = getInputTarget(kColorTargetName);
        const MRenderTarget* mainDepthTarget = getInputTarget(kDepthTargetName);
        ID3D11RenderTargetView* mainColorRTV = static_cast<ID3D11RenderTargetView*>(mainColorTarget->resourceHandle());
        ID3D11DepthStencilView* mainDepthDSV = static_cast<ID3D11DepthStencilView*>(mainDepthTarget->resourceHandle());

        ID3D11ShaderResourceView* nullVSSRV[] = { nullptr, nullptr };
        ID3D11ShaderResourceView* nullPSSRV[] = { nullptr, nullptr, nullptr };
        ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
        dxContext->VSSetShaderResources(0, ARRAYSIZE(nullVSSRV), nullVSSRV);
        dxContext->PSSetShaderResources(2, ARRAYSIZE(nullPSSRV), nullPSSRV);
        dxContext->OMSetRenderTargetsAndUnorderedAccessViews(
            1, &mainColorRTV, mainDepthDSV,
            1, ARRAYSIZE(nullUAVs),
            nullUAVs,
            nullptr
        );
        paintSelectionShader->unbind(drawContext);
    }

    /**
     * We told maya we needed two extra input render targets. It will call this function to find out their descriptions.
     * It can call it multiple times, say, whenever the viewport is resized.
     */
    bool getInputTargetDescription(const MString& name, MRenderTargetDescription& description) override {
        MRenderer::theRenderer()->outputTargetSize(outputTargetWidth, outputTargetHeight);

        if (name == paintDepthRenderTargetName) {
            description = renderTargetDescriptions[1];
            description.setWidth(outputTargetWidth);
            description.setHeight(outputTargetHeight);
            return true;
        }

        if (name == paintColorRenderTargetName) {
            description = renderTargetDescriptions[0];
            description.setWidth(outputTargetWidth);
            description.setHeight(outputTargetHeight);
            return true;
        }

        return false;
    }

    /**
     * Called any time the user switches into painting mode. The active voxel shape sends data to the renderer -> this paint operation.
     * The paint operation then needs to prepare GPU buffers for painting. These include:
     * 1. An instance transform buffer for all visible voxels
     * 2. The mapping of visible-to-global voxel IDs, as a buffer. This is used to translate instance IDs (visible) to global voxels IDs.
     * 3. A copy of the voxel paint value buffer (for a double buffer approach, to avoid read-write conflicts).
     * 4. A double buffer for the IDs of painted voxels (current and previous).
     */
    void prepareToPaint(
        const MMatrixArray& allVoxelMatrices, 
        const std::vector<uint32_t>& visibleVoxelIdToGlobalId,
        PingPongView& paintViews
    ) {
        voxelPaintViews = &paintViews;
        int numVoxels = static_cast<int>(allVoxelMatrices.length());
        instanceCount = static_cast<unsigned int>(visibleVoxelIdToGlobalId.size());
        if (instanceCount == 0) {
            instanceTransformSRV.Reset();
            instanceTransformBuffer.Reset();
            return;
        }
        
        MMatrixArray voxelInstanceTransforms;
        for (uint globalVoxelId : visibleVoxelIdToGlobalId) {
            voxelInstanceTransforms.append(allVoxelMatrices[globalVoxelId]);
        }

        // Flatten MMatrixArray into a std::vector of Float4x4
        std::vector<std::array<float, 16>> gpuMats(instanceCount);
        for (unsigned int i = 0; i < instanceCount; ++i) {
            const MMatrix& M = voxelInstanceTransforms[i];
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    gpuMats[i][c * 4 + r] = static_cast<float>(M(r, c));
                }
            }
        }

        instanceTransformBuffer = DirectX::createReadOnlyBuffer<std::array<float, 16>>(gpuMats);
        instanceTransformSRV = DirectX::createSRV(instanceTransformBuffer);
        visibleToGlobalVoxelBuffer = DirectX::createReadOnlyBuffer<uint32_t>(visibleVoxelIdToGlobalId);
        visibleToGlobalVoxelSRV = DirectX::createSRV(visibleToGlobalVoxelBuffer);

        // TODO: this can be a vector of uint8_t (it's just 1s and 0s)
        const std::vector<uint32_t> emptyIDData(numVoxels * 6, 0); // TODO: number of elements depends on whether painting faces or vertices
        voxelIDBufferA = DirectX::createReadWriteBuffer(emptyIDData);
        voxelIDBufferB = DirectX::createReadWriteBuffer(emptyIDData);
        voxelIDViews = PingPongView(
            DirectX::createSRV(voxelIDBufferB),
            DirectX::createSRV(voxelIDBufferA),
            DirectX::createUAV(voxelIDBufferB),
            DirectX::createUAV(voxelIDBufferA)
        );
    }

    void updatePaintToolPos(int mouseX, int mouseY) {
        paintPosX = mouseX;
        paintPosY = static_cast<int>(outputTargetHeight) - 1 - mouseY;

        int left = static_cast<int>(std::floor(paintPosX - paintRadius));
        int right = static_cast<int>(std::ceil(paintPosX + paintRadius));
        int top = static_cast<int>(std::floor(paintPosY - paintRadius));
        int bottom = static_cast<int>(std::ceil(paintPosY + paintRadius));

        scissor.left = std::max(0, left);
        scissor.right = std::min(static_cast<int>(outputTargetWidth), right);
        scissor.top = std::max(0, top);
        scissor.bottom = std::min(static_cast<int>(outputTargetHeight), bottom);
    }

private:

    MRenderTargetDescription renderTargetDescriptions[2];
    MRenderTarget* renderTargets[2] = { nullptr, nullptr };
    MShaderInstance* paintSelectionShader = nullptr;
    const MRasterizerState* scissorRasterState = nullptr;
    const MRasterizerState* depthBiasRasterState = nullptr;
    const MBlendState* alphaEnabledBlendState = nullptr;
    D3D11_RECT scissor = { 0, 0, 0, 0 };
    bool hasBrushMoved = false;
    float paintRadius = 50.0f;
    BrushMode brushMode = BrushMode::SET;
    float brushValue = 0.5f;
    bool cameraBased = true;
    MColor lowColor = MColor(1.0f, 0.0f, 0.0f, 0.0f);
    MColor highColor = MColor(1.0f, 0.0f, 0.0f, 1.0f);
    int componentMask = 0b111111; // All directions enabled by default
    int paintPosX;
    int paintPosY;
    unsigned int outputTargetWidth = 0;
    unsigned int outputTargetHeight = 0;
    MCallbackId playbackCallbackId = 0;
    bool isPlayingBack = false;

    // Alllll the buffers and views we need for painting
    ComPtr<ID3D11Buffer> instanceTransformBuffer;
    ComPtr<ID3D11ShaderResourceView> instanceTransformSRV;
    ComPtr<ID3D11Buffer> visibleToGlobalVoxelBuffer;
    ComPtr<ID3D11ShaderResourceView> visibleToGlobalVoxelSRV;
    PingPongView* voxelPaintViews;
    ComPtr<ID3D11Buffer> voxelIDBufferA;
    ComPtr<ID3D11Buffer> voxelIDBufferB;
    PingPongView voxelIDViews;
    // Used for tracking when the main render target has changed
    ComPtr<ID3D11ShaderResourceView> renderTargetSRV; 

    // Cube geometry resources
    ComPtr<ID3D11Buffer> cubeVb;
    ComPtr<ID3D11Buffer> cubeIb;

    unsigned int instanceCount = 0;

    EventBase::Unsubscribe unsubscribeFromPaintMove;
    EventBase::Unsubscribe unsubscribeFromPaintStateChange;

    void updateRenderTargetSRV(ID3D11RenderTargetView* rtv) {
        if (!rtv) return;

        ComPtr<ID3D11Resource> oldResource;
        if (renderTargetSRV) renderTargetSRV->GetResource(&oldResource);

        ComPtr<ID3D11Resource> newResource;
        rtv->GetResource(&newResource);

        // No updates
        if (newResource.Get() == oldResource.Get()) return;

        // When you pass in nullptr for the second parameter, it infers the format from the resource.
        HRESULT hr = DirectX::getDevice()->CreateShaderResourceView(newResource.Get(), nullptr, renderTargetSRV.GetAddressOf());
        if (FAILED(hr)) {
            MGlobal::displayError(MString("Failed to create shader resource view for render target. ") + Utils::HResultToString(hr).c_str());
        }

    }
};