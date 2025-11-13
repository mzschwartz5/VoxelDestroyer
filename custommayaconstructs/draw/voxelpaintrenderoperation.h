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
#include <maya/MMatrixArray.h>
#include <algorithm>
#include <array>
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

        void* shaderData = nullptr;
        DWORD size = Utils::loadResourceFile(DirectX::getPluginInstance(), IDR_SHADER15, L"SHADER", &shaderData);

        MShaderCompileMacro macros[] = {
            {"PAINT_SELECTION_TECHNIQUE_NAME", PAINT_SELECTION_TECHNIQUE_NAME},
            {"PAINT_POSITION", PAINT_POSITION},
            {"PAINT_RADIUS", PAINT_RADIUS}
        };
        paintSelectionShader = MRenderer::theRenderer()->getShaderManager()->getEffectsBufferShader(
            shaderData, size, PAINT_SELECTION_TECHNIQUE_NAME, macros, ARRAYSIZE(macros)
        );

        std::vector<float> cubeVertices(cubeCornersFlattened.begin(), cubeCornersFlattened.end());
        cubeVb = DirectX::createReadOnlyBuffer<float>(cubeVertices, D3D11_BIND_VERTEX_BUFFER, DirectX::BufferFormat::RAW);

        std::vector<unsigned int> cubeIndices(cubeFacesFlattened.begin(), cubeFacesFlattened.end());
        cubeIb = DirectX::createReadOnlyBuffer<unsigned int>(cubeIndices, D3D11_BIND_INDEX_BUFFER, DirectX::BufferFormat::RAW);
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
    };

    /**
     * Draw the voxel cage to an offscreen render target, using the paint brush as a scissor.
     * In the first pass, IDs get drawn to the offscreen target where the brush intersects voxels.
     * In the second pass, we draw the voxels to the standard targets with their modified paint values.
     * 
     * Note: Maya will bind our input render targets for us, but our use case is complex so we just have to do it ourselves.
     */
    MStatus execute(const MDrawContext& drawContext) override {
        if (!paintSelectionShader || !instanceTransformSRV || instanceCount == 0) {
            return MStatus::kSuccess;
        }

        // Store off the current scissor rects and rasterizer state, so we can change and restore them later
        ID3D11DeviceContext* context = DirectX::getContext();
        MStateManager* stateManager = drawContext.getStateManager();
        const MRasterizerState* prevRS = stateManager->getRasterizerState();
        UINT prevRectCount = 0;
        context->RSGetScissorRects(&prevRectCount, nullptr);
        std::vector<D3D11_RECT> prevScissorRects(prevRectCount);
        if (prevRectCount) context->RSGetScissorRects(&prevRectCount, prevScissorRects.data());

        // Set our scissor rect and rasterizer state
        context->RSSetScissorRects(1, &scissor);
        stateManager->setRasterizerState(scissorRasterState);

        // Bind the offscreen ID render targets
        const MRenderTarget* paintColorTarget = getInputTarget(paintColorRenderTargetName);
        const MRenderTarget* paintDepthTarget = getInputTarget(paintDepthRenderTargetName);
        ID3D11RenderTargetView* paintColorRTV = static_cast<ID3D11RenderTargetView*>(paintColorTarget->resourceHandle());
        ID3D11DepthStencilView* paintDepthDSV = static_cast<ID3D11DepthStencilView*>(paintDepthTarget->resourceHandle());
        context->OMSetRenderTargets(1, &paintColorRTV, paintDepthDSV);

        paintSelectionShader->bind(drawContext);
        float paintPos[2] = { static_cast<float>(paintPosX), static_cast<float>(paintPosY) };
        paintSelectionShader->setParameter(PAINT_POSITION, paintPos);
        paintSelectionShader->setParameter(PAINT_RADIUS, paintRadius);
        paintSelectionShader->updateParameters(drawContext);
        paintSelectionShader->activatePass(drawContext, 0);

        UINT stride = sizeof(float) * 3;
        UINT offset = 0;
        ID3D11ShaderResourceView* srvs[] = {
            instanceTransformSRV.Get(),
            visibleToGlobalVoxelSRV.Get()
        };
        context->IASetVertexBuffers(0, 1, cubeVb.GetAddressOf(), &stride, &offset);
        context->IASetIndexBuffer(cubeIb.Get(), DXGI_FORMAT_R32_UINT, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

        context->DrawIndexedInstanced(static_cast<UINT>(cubeFacesFlattened.size()), instanceCount, 0, 0, 0);

        // Unbind resources / restore rasterizer state
        ID3D11ShaderResourceView* nullSRV[] = { nullptr, nullptr };
        context->VSSetShaderResources(0, ARRAYSIZE(nullSRV), nullSRV);
        paintSelectionShader->unbind(drawContext);
        stateManager->setRasterizerState(prevRS);
        context->RSSetScissorRects(prevRectCount, prevScissorRects.data());

        return MStatus::kSuccess;
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
        const ComPtr<ID3D11UnorderedAccessView>& voxelPaintUAV,
        const ComPtr<ID3D11ShaderResourceView>& voxelPaintSRV
    ) {
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

        // Create a copy of the voxel paint buffer based on its UAV, passing an empty vector so we don't have to first map back data.
        // It's okay that it's empty, we just need the buffer size and flags to match. Also, it's really half-floats, but uint16_t has the same size.
        const std::vector<uint16_t> emptyPaintData(numVoxels, 0);
        voxelPaintBufferB = DirectX::createBufferFromViewTemplate(voxelPaintUAV, emptyPaintData);
        voxelPaintViews = PingPongView(
            voxelPaintSRV,
            DirectX::createSRVFromTemplate(voxelPaintSRV, voxelPaintBufferB), 
            voxelPaintUAV,
            DirectX::createUAVFromTemplate(voxelPaintUAV, voxelPaintBufferB)
        );

        const std::vector<uint32_t> emptyIDData(numVoxels, 0);
        voxelIDBufferA = DirectX::createReadWriteBuffer(emptyIDData);
        voxelIDBufferB = DirectX::createReadWriteBuffer(emptyIDData);
        voxelIDViews = PingPongView(
            DirectX::createSRV(voxelIDBufferA),
            DirectX::createSRV(voxelIDBufferB),
            DirectX::createUAV(voxelIDBufferA),
            DirectX::createUAV(voxelIDBufferB)
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

    void updatePaintToolRadius(float newPaintRadius) {
        paintRadius = newPaintRadius;
    }

private:

    MRenderTargetDescription renderTargetDescriptions[2];
    MRenderTarget* renderTargets[2] = { nullptr, nullptr };
    MShaderInstance* paintSelectionShader = nullptr;
    const MRasterizerState* scissorRasterState = nullptr;
    D3D11_RECT scissor = { 0, 0, 0, 0 };
    float paintRadius;
    int paintPosX;;
    int paintPosY;
    unsigned int outputTargetWidth = 0;
    unsigned int outputTargetHeight = 0;

    // Alllll the buffers and views we need for painting
    ComPtr<ID3D11Buffer> instanceTransformBuffer;
    ComPtr<ID3D11ShaderResourceView> instanceTransformSRV;
    ComPtr<ID3D11Buffer> visibleToGlobalVoxelBuffer;
    ComPtr<ID3D11ShaderResourceView> visibleToGlobalVoxelSRV;
    ComPtr<ID3D11Buffer> voxelPaintBufferB; // The original is owned by the VoxelShape being painted
    PingPongView voxelPaintViews;
    ComPtr<ID3D11Buffer> voxelIDBufferA;
    ComPtr<ID3D11Buffer> voxelIDBufferB;
    PingPongView voxelIDViews;

    // Cube geometry resources
    ComPtr<ID3D11Buffer> cubeVb;
    ComPtr<ID3D11Buffer> cubeIb;

    unsigned int instanceCount = 0;
};