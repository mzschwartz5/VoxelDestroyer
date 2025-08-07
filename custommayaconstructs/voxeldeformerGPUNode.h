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
#include "voxeldeformerCPUNode.h"
#include <maya/MFnPluginData.h>

extern std::thread::id g_mainThreadId;
const MString KERNEL_ID = "VoxelTransformVertices";
const MString KERNEL_ENTRY_POINT = "transformVertices";

class VoxelDeformerGPUNode : public MPxGPUDeformer {
public:
    VoxelDeformerGPUNode() = default;
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
        if (std::this_thread::get_id() != g_mainThreadId) return kDeformerRetryMainThread;

        MStatus status = maybeInitParticleBuffers(block, evaluationNode);
        if (status != MStatus::kSuccess) {
            return kDeformerPassThrough;
        }

        status = maybeInitVertexOffsetsBuffer(block, evaluationNode);
        if (status != MStatus::kSuccess) {
            return kDeformerPassThrough;
        }

        // Only support a single input geometry
        if (inputPlugs.length() != 1) {
            MGlobal::displayError("VoxelDeformerGPUNode only supports a single input geometry.");
            return kDeformerFailure;
        }

        MAutoCLEventList kernelWaitOnEvents;
        const MPlug& inputPlug = inputPlugs[0];
        const MGPUDeformerBuffer inputPositions = inputData.getBuffer(sPositionsName(), inputPlug);
        cl_uint inputElementCount = inputPositions.elementCount();
        MGPUDeformerBuffer outputPositions = createOutputBuffer(inputPositions);
        kernelWaitOnEvents.add(inputPositions.bufferReadyEvent());

        if (!inputPositions.isValid() || !outputPositions.isValid()) return kDeformerFailure;

        // Acquire D3D11 interop buffers
        cl_int err = CL_SUCCESS;
        MAutoCLEvent acquireEvent;
        cl_mem buffers[] = { m_particlePositionsBuffer.get() };
        err = clEnqueueAcquireD3D11Objects(MOpenCLInfo::getMayaDefaultOpenCLCommandQueue(), ARRAYSIZE(buffers), buffers, 0, NULL, acquireEvent.getReferenceForAssignment());
        kernelWaitOnEvents.add(acquireEvent);
        MOpenCLInfo::checkCLErrorStatus(err);

        // Set kernel parameters
        unsigned int parameterId = 0;
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_uint), (void*)&numberVoxels);
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_uint), (void*)&inputElementCount);
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)m_particlePositionsBuffer.getReadOnlyRef());
        MOpenCLInfo::checkCLErrorStatus(err);
        err = clSetKernelArg(mKernel.get(), parameterId++, sizeof(cl_mem), (void*)m_vertStartIdsBuffer.getReadOnlyRef());
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
            return kDeformerFailure;
        }

        // Release D3D11 interop input buffers
        cl_event kernelFinishedEventHandle = kernelFinishedEvent.get();
        err = clEnqueueReleaseD3D11Objects(MOpenCLInfo::getMayaDefaultOpenCLCommandQueue(), ARRAYSIZE(buffers), buffers, 1, &kernelFinishedEventHandle, NULL);
        MOpenCLInfo::checkCLErrorStatus(err);

        outputPositions.setBufferReadyEvent(kernelFinishedEvent);

        outputData.setBuffer(outputPositions);
        return kDeformerSuccess;
    }

    // An instance override called on destruction. The GPU resources themselves are auto-released by the MAutoCLMem class.
    void terminate() override {
        MRenderer::theRenderer()->releaseGPUMemory(m_originalParticlePositionsBufferSize);
        MRenderer::theRenderer()->releaseGPUMemory(m_vertStartIdsBufferSize);
    }

    // We need the OpenCL context to be valid, so this cannot be done in the constructor.
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

    /**
     * The kernel is loaded and compiled once, statically, at plugin initialization. It's shared by all GPU deformer instances of this class.
     * Correspondingly, it's torn down once, at plugin uninitialization.
     */
    static void tearDown() {
        MOpenCLInfo::releaseOpenCLKernel(mKernel);
        mKernel.reset();
    }

    /**
     * This method (re-)initializes the particle buffers needed for the deformer, if and when
     * the particle data has changed. This occurs the first time the deformer is created (either by the user or on file startup),
     * or when the particle data has changed (i.e. a new model is voxelized -> the global particle buffer and offsets are updated).
     * 
     * Note: this method is called on every evaluation, returning early if no changes are detected. While slightly wasteful, there are literally
     * no other events / hooks / functions in the API where such initialization can be done. Even the API examples do initialization in the evaluate() method.
     */
    MStatus maybeInitParticleBuffers(
        MDataBlock& block,
        const MEvaluationNode& evaluationNode
    ) {
        // If the D3D11 interop buffer is already initialized, and the deformer data attribute is clean, exit early.
        if (m_particlePositionsBuffer.get() && !hasAttributeBeenModified(evaluationNode, VoxelDeformerCPUNode::aDeformerData)) {
            return MStatus::kSuccess;
        }

        // Get the particle data passed in from the PBD node to the CPU node (via the particledata plug).
        MStatus status;
        MDataHandle particleDataHandle = block.inputValue(VoxelDeformerCPUNode::aParticleData, &status);
        MObject particleDataObj = particleDataHandle.data();
        if (status != MStatus::kSuccess || particleDataObj.isNull()) {
            // Evaluate() may have run before the connection between nodes was made and the data set.
            return MStatus::kFailure;
        }

        MFnPluginData particleDataFn(particleDataObj, &status);
        ParticleData* particleData = static_cast<ParticleData*>(particleDataFn.data(&status));
        
        int numParticles = particleData->getData().numParticles;
        numberVoxels = numParticles / 8;
        m_globalWorkSize = numberVoxels * m_localWorkSize;
        cl_int err = CL_SUCCESS;

        cl_mem particlePositionsMem = clCreateFromD3D11Buffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_ONLY,
            particleData->getData().particlePositionsBuffer,
            &err
        );

        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create particlePositionsBuffer from D3D11 buffer: ") + MString(clewErrorString(err)));
            return MStatus::kFailure;
        }
        m_particlePositionsBuffer.attach(particlePositionsMem);

        // Only the D3D11 buffer handle needs to be reassigned when the plug changes. The other data is fixed on initialization.
        if (initialized) return MStatus::kSuccess;

        // Store a copy of the original position of a reference particle (lower left corner of each voxel) for the kernel to use
        std::vector<glm::vec4> referenceParticlePositions(numberVoxels);
        const glm::vec4* particlePositionsCPU = particleData->getData().particlePositionsCPU;

        for (int i = 0; i < numParticles; i += 8) {
            referenceParticlePositions[i / 8] = particlePositionsCPU[i];
        }

        m_originalParticlePositionsBufferSize = sizeof(glm::vec4) * numberVoxels;
        cl_mem originalParticlePositionsMem = clCreateBuffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            m_originalParticlePositionsBufferSize,
            (void*)referenceParticlePositions.data(),
            &err
        );

        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create originalParticlePositionsBuffer: ") + MString(clewErrorString(err)));
            return MStatus::kFailure;
        }
        MRenderer::theRenderer()->holdGPUMemory(m_originalParticlePositionsBufferSize); // Helps Maya track and manage GPU memory usage
        m_originalParticlePositionsBuffer.attach(originalParticlePositionsMem);

        return MStatus::kSuccess;
    }

    /**
     * Similar to maybeInitParticleBuffers(), this method initializes GPU buffers (specifically, the vertex offsets buffer).
     * However, this buffer should never change, so we can just check the initialization flag, instead of plugs.
     */
    MStatus maybeInitVertexOffsetsBuffer(
        MDataBlock& block,
        const MEvaluationNode& evaluationNode
    ) {
        if (initialized) return MStatus::kSuccess;
        
        MStatus status;
        cl_int err = CL_SUCCESS;

        MDataHandle deformerDataHandle = block.inputValue(VoxelDeformerCPUNode::aDeformerData, &status);
        MObject deformerDataObj = deformerDataHandle.data();
        if (status != MStatus::kSuccess || deformerDataObj.isNull()) {
            MGlobal::displayError("VoxelDeformerGPUNode: Deformer data is not set.");
            return MStatus::kFailure;
        }

        MFnPluginData pluginDataFn(deformerDataObj, &status);
        DeformerData* deformerData = static_cast<DeformerData*>(pluginDataFn.data(&status));
        const std::vector<uint>& vertexStartIdx = deformerData->getVertexStartIdx();

        m_vertStartIdsBufferSize = sizeof(uint) * vertexStartIdx.size();
        cl_mem vertStartIdsMem = clCreateBuffer(
            MOpenCLInfo::getOpenCLContext(),
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            m_vertStartIdsBufferSize,
            (void*)vertexStartIdx.data(),
            &err
        );

        if (err != CL_SUCCESS) {
            MGlobal::displayError(MString("Failed to create vertStartIdsBuffer: ") + MString(clewErrorString(err)));
            return MStatus::kFailure;
        }
        MRenderer::theRenderer()->holdGPUMemory(m_vertStartIdsBufferSize); // Helps Maya track and manage GPU memory usage
        m_vertStartIdsBuffer.attach(vertStartIdsMem);

        initialized = true;
        return MStatus::kSuccess;
    }

private:
    inline static MAutoCLKernel mKernel;
    unsigned int numberVoxels = 0;
    MAutoCLMem m_particlePositionsBuffer;
    MAutoCLMem m_vertStartIdsBuffer;
    MAutoCLMem m_originalParticlePositionsBuffer;
    size_t m_vertStartIdsBufferSize = 0;
    size_t m_originalParticlePositionsBufferSize = 0;
    size_t m_globalWorkSize;
    size_t m_localWorkSize = TRANSFORM_VERTICES_THREADS;
    bool initialized = false;
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