#pragma once

#include <maya/MPxGPUDeformer.h>
#include <maya/MStatus.h>
#include <maya/MGPUDeformerRegistry.h>
#include "directx/directx.h"
#include <clew/clew.h>
#include <maya/MOpenCLInfo.h>
#include "constants.h"
#include "../resource.h"
#include <thread>

extern std::thread::id g_mainThreadId;
const MString KERNEL_ID = "VoxelTransformVertices";
const MString KERNEL_ENTRY_POINT = "transformVertices";

class VoxelDeformerGPUNode : public MPxGPUDeformer {
public:
    VoxelDeformerGPUNode() {}

    ~VoxelDeformerGPUNode() override = default;

    static MGPUDeformerRegistrationInfo* getGPUDeformerInfo();

    DeformerStatus evaluate(
        MDataBlock& block,
        const MEvaluationNode& evaluationNode,
        const MPlug& outputPlug,
        const MPlugArray& inputPlugs,
        const MGPUDeformerData& inputData,
        MGPUDeformerData& outputData
    ) override {

        // According to the docs, D3D/OpenCL interop must be executed from the main thread to 
        // provide access to the D3D11 device and context.
        if (std::this_thread::get_id() != g_mainThreadId) {
            return MPxGPUDeformer::kDeformerRetryMainThread;
        }

        // Only support a single input plug
        if (inputPlugs.length() != 1)
            return MPxGPUDeformer::kDeformerFailure;

        const MPlug& inputPlug = inputPlugs[0];
        const MGPUDeformerBuffer inputPositions = inputData.getBuffer(MPxGPUDeformer::sPositionsName(), inputPlug);
        MGPUDeformerBuffer outputPositions = createOutputBuffer(inputPositions);

        if (!inputPositions.isValid() || !outputPositions.isValid()) return MPxGPUDeformer::kDeformerFailure;

        // Acquire D3D11 interop input buffers
        MAutoCLEvent acquireEvent;
        cl_mem buffers[] = { m_particlePositionsBuffer.get(), m_vertStartIdsBuffer.get(), m_numVerticesBuffer.get(), m_localRestPositionsBuffer.get() };

        // Check if the buffers are valid
        if (m_particlePositionsBuffer.isNull() || m_vertStartIdsBuffer.isNull() || m_numVerticesBuffer.isNull() || m_localRestPositionsBuffer.isNull()) {
            MGlobal::displayError("One or more D3D11 interop buffers are not initialized.");
            return MPxGPUDeformer::kDeformerFailure;
        }

        clEnqueueAcquireD3D11Objects(MOpenCLInfo::getMayaDefaultOpenCLCommandQueue(), ARRAYSIZE(buffers), buffers, 0, NULL, acquireEvent.getReferenceForAssignment());

        // Set kernel parameters
        unsigned int parameterId = 0;
        cl_int err = CL_SUCCESS;
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)m_particlePositionsBuffer.getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)m_vertStartIdsBuffer.getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)m_numVerticesBuffer.getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)m_localRestPositionsBuffer.getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)outputPositions.buffer().getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);

        // Run the kernel
        MAutoCLEvent kernelFinishedEvent;
        cl_event acquireEventHandle = acquireEvent.get();
        err = clEnqueueNDRangeKernel(MOpenCLInfo::getMayaDefaultOpenCLCommandQueue(), mKernel.get(), 1, NULL, &m_globalWorkSize, &m_localWorkSize, 1, &acquireEventHandle, kernelFinishedEvent.getReferenceForAssignment());
        MGlobal::displayInfo("OpenCL kernel launched successfully.");

        // Release D3D11 interop input buffers
        cl_event kernelFinishedEventHandle = kernelFinishedEvent.get();
        clEnqueueReleaseD3D11Objects(MOpenCLInfo::getMayaDefaultOpenCLCommandQueue(), ARRAYSIZE(buffers), buffers, 1, &kernelFinishedEventHandle, NULL);

        outputPositions.setBufferReadyEvent(kernelFinishedEvent);
        MOpenCLInfo::checkCLErrorStatus(err);
        if ( err != CL_SUCCESS ) return MPxGPUDeformer::kDeformerFailure;

        outputData.setBuffer(outputPositions);
        return MPxGPUDeformer::kDeformerSuccess;
    }

    void terminate() override {
        MOpenCLInfo::releaseOpenCLKernel(mKernel);
        mKernel.reset();
    }

    // We need to the OpenCL context to be valid, so this cannot be done in the constructor.
    // Instead, call this after registering the node.
    static bool compileKernel() {
        // Use utils loadResourceFile to get kernel as string
        void* kernelData = nullptr;
        DWORD kernelSize = Utils::loadResourceFile(DirectX::getPluginInstance(), IDR_SHADER1, L"SHADER", &kernelData);
    
        if (kernelSize == 0) {
            MGlobal::displayError("Failed to load OpenCL kernel resource.");
            return false;
        }
    
        mKernel = MOpenCLInfo::getOpenCLKernelFromString(
            MString(static_cast<char*>(kernelData), kernelSize),
            KERNEL_ID,
            KERNEL_ENTRY_POINT
        );
    
        return !mKernel.isNull();
    }

    // TODO: for now, this is static. When we handle multiple objects, each with their own deformer node,
    // this will need to be an instance method.
    static void initializeExternalKernelArgs(
        const int numVoxels,
        ID3D11Buffer* particlePositions,
        ID3D11Buffer* vertStartIds,
        ID3D11Buffer* numVerts,
        ID3D11Buffer* localRestPositionsBuffer
    ) {
        m_globalWorkSize = numVoxels;
        MAutoCLMem::NoRef dummy;
        cl_int err = CL_SUCCESS;

        cl_mem particlePositionsMem = clCreateFromD3D11Buffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_WRITE,
            particlePositions,
            &err
        );

        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create particlePositionsBuffer from D3D11 buffer: ") + MString(clewErrorString(err)));
            return;
        }
        m_particlePositionsBuffer = MAutoCLMem(particlePositionsMem, dummy);

        cl_mem vertStartIdsMem = clCreateFromD3D11Buffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_ONLY,
            vertStartIds,
            &err
        );
        
        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create vertStartIdsBuffer from D3D11 buffer: ") + MString(clewErrorString(err)));
            return;
        }
        m_vertStartIdsBuffer = MAutoCLMem(vertStartIdsMem, dummy);

        cl_mem numVerticesMem = clCreateFromD3D11Buffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_ONLY,
            numVerts,
            &err
        );
        
        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create numVertsBuffer from D3D11 buffer: ") + MString(clewErrorString(err)));
            return;
        }
        m_numVerticesBuffer = MAutoCLMem(numVerticesMem, dummy);

        cl_mem localRestPositionsMem = clCreateFromD3D11Buffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_ONLY,
            localRestPositionsBuffer,
            &err
        );
        
        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create localRestPositionsBuffer from D3D11 buffer: ") + MString(clewErrorString(err)));
            return;
        }
        m_localRestPositionsBuffer = MAutoCLMem(localRestPositionsMem, dummy);
    }

private:
    inline static MAutoCLKernel mKernel;
    inline static MAutoCLMem m_particlePositionsBuffer;
    inline static MAutoCLMem m_vertStartIdsBuffer;
    inline static MAutoCLMem m_numVerticesBuffer;
    inline static MAutoCLMem m_localRestPositionsBuffer;
    inline static size_t m_globalWorkSize;
    inline static size_t m_localWorkSize = TRANSFORM_VERTICES_THREADS;
};

class VoxelDeformerGPUNodeInfo : public MGPUDeformerRegistrationInfo {
public:
    VoxelDeformerGPUNodeInfo() {};
    ~VoxelDeformerGPUNodeInfo() override = default;

    MPxGPUDeformer* createGPUDeformer() override {
        return new VoxelDeformerGPUNode();
    }

    bool validateNodeInGraph(MDataBlock& block, const MEvaluationNode&, const MPlug& plug, MStringArray* messages) override {
        return true;
    }

    bool validateNodeValues(MDataBlock& block, const MEvaluationNode&, const MPlug& plug, MStringArray* messages) override {
        return true;
    }
};

// Has to be outside of the class because of dependency on the definition of the info class.
inline MGPUDeformerRegistrationInfo* VoxelDeformerGPUNode::getGPUDeformerInfo() {
    static VoxelDeformerGPUNodeInfo info;
    return &info;
}