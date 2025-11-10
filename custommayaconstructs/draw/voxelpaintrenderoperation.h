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
#include <maya/MDrawContext.h>
#include "../../constants.h"
#include "../../resource.h"
#include <maya/MMatrixArray.h>
#include <algorithm>
#include <array>
using namespace MHWRender;
using std::unique_ptr;
using std::make_unique;

class VoxelPaintRenderOperation : public MUserRenderOperation {
public:
    inline static const MString paintOutputRenderTargetName = "voxelPaintOutputTarget";
    inline static const MString paintDepthRenderTargetName = "voxelPaintDepthTarget";

    VoxelPaintRenderOperation(const MString& name) : MUserRenderOperation(name) {
        mOperationType = kUserDefined;
        mInputTargetNames.append(paintDepthRenderTargetName);
        mInputTargetNames.append(paintOutputRenderTargetName);
        mOutputTargetNames.append(paintDepthRenderTargetName);
        mOutputTargetNames.append(paintOutputRenderTargetName);

        MRenderTargetDescription desc;
        desc.setName(paintOutputRenderTargetName);
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
        cubeVb = DirectX::createReadOnlyBuffer<float>(cubeVertices, D3D11_BIND_VERTEX_BUFFER, true);

        std::vector<unsigned int> cubeIndices(cubeFacesFlattened.begin(), cubeFacesFlattened.end());
        cubeIb = DirectX::createReadOnlyBuffer<unsigned int>(cubeIndices, D3D11_BIND_INDEX_BUFFER, true);
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

    MRenderTarget* const* targetOverrideList(unsigned int &listSize) override {
        renderTargets[0] = getInputTarget(paintOutputRenderTargetName);
        renderTargets[1] = getInputTarget(paintDepthRenderTargetName);
        listSize = 2;
        return renderTargets;
    }

    /**
     * Draw the voxel cage to an offscreen render target, using the paint brush as a scissor.
     * Voxels will write their instance IDs to the color target to be read back later / indicate selection.
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

        // No need to set render targets; maya has done it for us (by virtue of the virtual methods this class overrides)
        paintSelectionShader->bind(drawContext);
        float paintPos[2] = { static_cast<float>(paintPosX), static_cast<float>(paintPosY) };
        paintSelectionShader->setParameter(PAINT_POSITION, paintPos);
        paintSelectionShader->setParameter(PAINT_RADIUS, paintRadius);
        paintSelectionShader->updateParameters(drawContext);
        paintSelectionShader->activatePass(drawContext, 0);

        UINT stride = sizeof(float) * 3;
        UINT offset = 0;
        context->IASetVertexBuffers(0, 1, cubeVb.GetAddressOf(), &stride, &offset);
        context->IASetIndexBuffer(cubeIb.Get(), DXGI_FORMAT_R32_UINT, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShaderResources(0, 1, instanceTransformSRV.GetAddressOf());

        context->DrawIndexedInstanced(static_cast<UINT>(cubeFacesFlattened.size()), instanceCount, 0, 0, 0);

        // Unbind resources / restore rasterizer state
        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        context->VSSetShaderResources(0, 1, nullSRV);
        paintSelectionShader->unbind(drawContext);
        stateManager->setRasterizerState(prevRS);
        context->RSSetScissorRects(prevRectCount, prevScissorRects.data());

        return MStatus::kSuccess;
    }

    /**
     * Tell Maya to override the standard depth and color targets with our own offscreen depth target.
     * It will allocate and manage them for us.
     */
    bool getInputTargetDescription(const MString& name, MRenderTargetDescription& description) override {
        MRenderer::theRenderer()->outputTargetSize(outputTargetWidth, outputTargetHeight);

        if (name == paintDepthRenderTargetName) {
            description = renderTargetDescriptions[1];
            description.setWidth(outputTargetWidth);
            description.setHeight(outputTargetHeight);
            return true;
        }

        if (name == paintOutputRenderTargetName) {
            description = renderTargetDescriptions[0];
            description.setWidth(outputTargetWidth);
            description.setHeight(outputTargetHeight);
            return true;
        }

        return false;
    }

    void createInstanceTransformArray(const MMatrixArray& voxelInstanceTransforms) {
        instanceCount = static_cast<unsigned int>(voxelInstanceTransforms.length());

        if (instanceCount == 0) {
            instanceTransformSRV.Reset();
            instanceTransformBuffer.Reset();
            return;
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
        instanceTransformSRV = DirectX::createSRV(instanceTransformBuffer, false, instanceCount);
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

    ComPtr<ID3D11Buffer> instanceTransformBuffer;
    ComPtr<ID3D11ShaderResourceView> instanceTransformSRV;

    // Cube geometry resources
    ComPtr<ID3D11Buffer> cubeVb;
    ComPtr<ID3D11Buffer> cubeIb;

    unsigned int instanceCount = 0;
};