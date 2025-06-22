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
        // provide safe access to the D3D11 / OpenCL device and context.
        if (std::this_thread::get_id() != g_mainThreadId) return MPxGPUDeformer::kDeformerRetryMainThread;

        // Only support a single input geometry
        if (inputPlugs.length() != 1) {
            MGlobal::displayError("VoxelDeformerGPUNode only supports a single input geometry.");
            return MPxGPUDeformer::kDeformerFailure;
        }

        MAutoCLEventList kernelWaitOnEvents;
        const MPlug& inputPlug = inputPlugs[0];
        const MGPUDeformerBuffer inputPositions = inputData.getBuffer(MPxGPUDeformer::sPositionsName(), inputPlug);
        MGPUDeformerBuffer outputPositions = createOutputBuffer(inputPositions);
        kernelWaitOnEvents.add(inputPositions.bufferReadyEvent());

        if (!inputPositions.isValid() || !outputPositions.isValid()) return MPxGPUDeformer::kDeformerFailure;

        // Acquire D3D11 interop buffers
        cl_int err = CL_SUCCESS;
        MAutoCLEvent acquireEvent;
        cl_mem buffers[] = { m_particlePositionsBuffer.get() };
        err = clEnqueueAcquireD3D11Objects(MOpenCLInfo::getMayaDefaultOpenCLCommandQueue(), ARRAYSIZE(buffers), buffers, 0, NULL, acquireEvent.getReferenceForAssignment());
        kernelWaitOnEvents.add(acquireEvent);
        MOpenCLInfo::checkCLErrorStatus(err);

        // Set kernel parameters
        unsigned int parameterId = 0;
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)m_particlePositionsBuffer.getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)m_vertStartIdsBuffer.getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)m_numVerticesBuffer.getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)m_originalParticlePositionsBuffer.getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)inputPositions.buffer().getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)outputPositions.buffer().getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);

        // Run the kernel
        MAutoCLEvent kernelFinishedEvent;
        err = clEnqueueNDRangeKernel(MOpenCLInfo::getMayaDefaultOpenCLCommandQueue(), mKernel.get(), 1, NULL, &m_globalWorkSize, &m_localWorkSize, kernelWaitOnEvents.size(), kernelWaitOnEvents.array(), kernelFinishedEvent.getReferenceForAssignment());
        MOpenCLInfo::checkCLErrorStatus(err);

        if ( err != CL_SUCCESS ) {
            MGlobal::displayError(MString("Failed to run OpenCL kernel: ") + MString(clewErrorString(err)));
            return MPxGPUDeformer::kDeformerFailure;
        }

        // Release D3D11 interop input buffers
        cl_event kernelFinishedEventHandle = kernelFinishedEvent.get();
        err = clEnqueueReleaseD3D11Objects(MOpenCLInfo::getMayaDefaultOpenCLCommandQueue(), ARRAYSIZE(buffers), buffers, 1, &kernelFinishedEventHandle, NULL);
        MOpenCLInfo::checkCLErrorStatus(err);

        outputPositions.setBufferReadyEvent(kernelFinishedEvent);

        outputData.setBuffer(outputPositions);
        return MPxGPUDeformer::kDeformerSuccess;
    }

    void terminate() override {

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
    // this will need to be an instance method. MAutoCLMem will then take care of releasing the buffers when the node is deleted.
    static void initializeExternalKernelArgs(
        const int numVoxels,
        ID3D11Buffer* particlePositions,
        const std::vector<glm::vec4>& particlePositionsCPU, // to store the original particle positions, as an OpenCL-only buffer
        const std::vector<uint>& vertStartIds,
        const std::vector<uint>& numVerts
    ) {
        numberVoxels = numVoxels;
        m_globalWorkSize = numberVoxels * m_localWorkSize;
        cl_int err = CL_SUCCESS;

        cl_mem particlePositionsMem = clCreateFromD3D11Buffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_ONLY,
            particlePositions,
            &err
        );

        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create particlePositionsBuffer from D3D11 buffer: ") + MString(clewErrorString(err)));
            return;
        }
        m_particlePositionsBuffer.attach(particlePositionsMem);

        // Store a copy of the original particle positions for the kernel to use
        // TODO: we technically only need the original positions of one particle per voxel. Stride by 8?
        m_originalParticlePositionsBufferSize = sizeof(glm::vec4) * particlePositionsCPU.size();
        cl_mem originalParticlePositionsMem = clCreateBuffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            m_originalParticlePositionsBufferSize,
            (void*)particlePositionsCPU.data(),
            &err
        );

        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create originalParticlePositionsBuffer: ") + MString(clewErrorString(err)));
            return;
        }
        MHWRender::MRenderer::theRenderer()->holdGPUMemory(m_originalParticlePositionsBufferSize); // Helps Maya track and manage GPU memory usage
        m_originalParticlePositionsBuffer.attach(originalParticlePositionsMem);

        m_vertStartIdsBufferSize = sizeof(uint) * vertStartIds.size();
        cl_mem vertStartIdsMem = clCreateBuffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            m_vertStartIdsBufferSize,
            (void*)vertStartIds.data(),
            &err
        );

        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create vertStartIdsBuffer: ") + MString(clewErrorString(err)));
            return;
        }
        MHWRender::MRenderer::theRenderer()->holdGPUMemory(m_vertStartIdsBufferSize); // Helps Maya track and manage GPU memory usage
        m_vertStartIdsBuffer.attach(vertStartIdsMem);

        m_numVerticesBufferSize = sizeof(uint) * numVerts.size();
        cl_mem numVerticesMem = clCreateBuffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            m_numVerticesBufferSize,
            (void*)numVerts.data(),
            &err
        );

        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create numVerticesBuffer: ") + MString(clewErrorString(err)));
            return;
        }
        MHWRender::MRenderer::theRenderer()->holdGPUMemory(m_numVerticesBufferSize); // Helps Maya track and manage GPU memory usage
        m_numVerticesBuffer.attach(numVerticesMem);
    }

    static void tearDown() {
        MOpenCLInfo::releaseOpenCLKernel(mKernel);
        mKernel.reset();

        // These only need to be reset because they're static. Once we have support for multiple meshes+deformers, 
        // we'll use terminate() instead, and MAutoCLMem will automatically release buffers.
        m_particlePositionsBuffer.reset();
        m_vertStartIdsBuffer.reset();
        m_numVerticesBuffer.reset();
        m_originalParticlePositionsBuffer.reset();
        // Still need to do this part though, in terminate():
        MHWRender::MRenderer::theRenderer()->releaseGPUMemory(m_originalParticlePositionsBufferSize);
        MHWRender::MRenderer::theRenderer()->releaseGPUMemory(m_vertStartIdsBufferSize);
        MHWRender::MRenderer::theRenderer()->releaseGPUMemory(m_numVerticesBufferSize);
    }

private:
    inline static MAutoCLKernel mKernel;
    inline static unsigned int numberVoxels = 0;
    inline static MAutoCLMem m_particlePositionsBuffer;
    inline static MAutoCLMem m_vertStartIdsBuffer;
    inline static MAutoCLMem m_numVerticesBuffer;
    inline static MAutoCLMem m_originalParticlePositionsBuffer;
    inline static size_t m_vertStartIdsBufferSize = 0;
    inline static size_t m_numVerticesBufferSize = 0;
    inline static size_t m_originalParticlePositionsBufferSize = 0;
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