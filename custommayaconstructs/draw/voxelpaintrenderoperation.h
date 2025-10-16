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

        void* shaderData = nullptr;
        DWORD size = Utils::loadResourceFile(DirectX::getPluginInstance(), IDR_SHADER15, L"SHADER", &shaderData);

        MShaderCompileMacro macros[] = {{"PAINT_SELECTION_TECHNIQUE_NAME", PAINT_SELECTION_TECHNIQUE_NAME}};
        paintSelectionShader = MRenderer::theRenderer()->getShaderManager()->getEffectsBufferShader(
            shaderData, size, PAINT_SELECTION_TECHNIQUE_NAME, macros, 1
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
    };

    MRenderTarget* const* targetOverrideList(unsigned int &listSize) override {
        renderTargets[0] = getInputTarget(paintOutputRenderTargetName);
        renderTargets[1] = getInputTarget(paintDepthRenderTargetName);
        listSize = 2;
        return renderTargets;
    }

    MStatus execute(const MDrawContext& drawContext) override {
        if (!paintSelectionShader || !instanceTransformSRV || instanceCount == 0) {
            return MStatus::kSuccess;
        }

        // No need to set render targets; maya has done it for us (by virtue of the virtual methods we overrode)
        paintSelectionShader->bind(drawContext);
        paintSelectionShader->updateParameters(drawContext);
        paintSelectionShader->activatePass(drawContext, 0);

        UINT stride = sizeof(float) * 3;
        UINT offset = 0;
        ID3D11DeviceContext* context = DirectX::getContext();
        context->IASetVertexBuffers(0, 1, cubeVb.GetAddressOf(), &stride, &offset);
        context->IASetIndexBuffer(cubeIb.Get(), DXGI_FORMAT_R32_UINT, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShaderResources(0, 1, instanceTransformSRV.GetAddressOf());

        context->DrawIndexedInstanced(static_cast<UINT>(cubeFacesFlattened.size()), instanceCount, 0, 0, 0);

        // Unbind resources
        ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
        context->VSSetShaderResources(0, 1, nullSRV);
        paintSelectionShader->unbind(drawContext);

        return MStatus::kSuccess;
    }

    /**
     * Tell Maya to override the standard depth and color targets with our own offscreen depth target.
     * It will allocate and manage them for us.
     */
    bool getInputTargetDescription(const MString& name, MRenderTargetDescription& description) override {
        unsigned int width, height;
        MRenderer::theRenderer()->outputTargetSize(width, height);

        if (name == paintDepthRenderTargetName) {
            description = renderTargetDescriptions[1];
            description.setWidth(width);
            description.setHeight(height);
            return true;
        }

        if (name == paintOutputRenderTargetName) {
            description = renderTargetDescriptions[0];
            description.setWidth(width);
            description.setHeight(height);
            return true;
        }

        return false;
    }

    void createInstanceTransformArray(const MMatrixArray& voxelInstanceTransforms) {
        struct Float4x4 { float m[16]; };
        instanceCount = static_cast<unsigned int>(voxelInstanceTransforms.length());

        if (instanceCount == 0) {
            instanceTransformSRV.Reset();
            instanceTransformBuffer.Reset();
            return;
        }

        // Flatten MMatrixArray into a std::vector of Float4x4
        std::vector<Float4x4> gpuMats(instanceCount);
        for (unsigned int i = 0; i < instanceCount; ++i) {
            const MMatrix& M = voxelInstanceTransforms[i];
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    gpuMats[i].m[c * 4 + r] = static_cast<float>(M(r, c));
                }
            }
        }

        instanceTransformBuffer = DirectX::createReadOnlyBuffer<Float4x4>(gpuMats);
        instanceTransformSRV = DirectX::createSRV(instanceTransformBuffer, false, instanceCount);
    }

    // TODO: necessary?
    bool requiresResetDeviceStates() const override { return true; }

private:

    MRenderTargetDescription renderTargetDescriptions[2];
    MRenderTarget* renderTargets[2] = { nullptr, nullptr };
    MShaderInstance* paintSelectionShader = nullptr;
    ComPtr<ID3D11Buffer> instanceTransformBuffer;
    ComPtr<ID3D11ShaderResourceView> instanceTransformSRV;

    // Cube geometry resources
    ComPtr<ID3D11Buffer> cubeVb;
    ComPtr<ID3D11Buffer> cubeIb;

    unsigned int instanceCount = 0;
};