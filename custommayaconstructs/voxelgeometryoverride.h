#pragma once

#include <maya/MGlobal.h>
#include <maya/MViewport2Renderer.h>
#include <maya/MPxGeometryOverride.h>
#include <maya/MFnMesh.h>
#include <maya/MFloatArray.h>
#include <maya/MFloatPointArray.h>
#include <maya/MFloatVectorArray.h>
#include <maya/MUintArray.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MGeometryRequirements.h>
#include <maya/MHWGeometry.h>

using namespace MHWRender;

struct VoxelGeometry {
    MFloatVectorArray  m_Positions;
    MFloatVectorArray m_Normals;
    MFloatArray m_TextureCoords; // Interleaved U and V coordinates
    MUintArray m_Indices;
};

/**
 * Populate the geometry buffers of the associated DAG node manually.
 * By doing so, we get access to the MVertexBuffer which we can then bind to our compute shaders.
 */
class VoxelGeometryOverride : public MPxGeometryOverride {
public:
    static MPxGeometryOverride* creator(const MObject& obj)
    {
        // TODO: return nullptr for unsupported objects (maybe create an attribute to check for support)
        // so that not all mesh shapes are overridden
        return new VoxelGeometryOverride(obj);
    }

    /**
     * According to the docs, we should not access the DG in populateGeometry. Instead, we need to cache any data required. Thus, in this constructor,
     * we get all positions, normals, and UVs. In theory, we should update these whenever updateDG is invoked, but this isn't supported anyway because
     * the rest of the simulation does not support modifying the mesh via normal Maya operations.
     */
    VoxelGeometryOverride(const MObject& obj)
        : MPxGeometryOverride(obj), obj(obj)
    {
        MStatus status;
        MFnMesh meshFn(obj, &status);
        CHECK_MSTATUS(status);
    
        // Positions
        MFloatPointArray points;
        status = meshFn.getPoints(points, MSpace::kObject);
        CHECK_MSTATUS(status);

        // Only need x, y, z coordinates
        m_voxelGeometry.m_Positions.setLength(points.length());
        for (unsigned int i = 0; i < points.length(); ++i) {
            m_voxelGeometry.m_Positions[i].x = points[i].x;
            m_voxelGeometry.m_Positions[i].y = points[i].y;
            m_voxelGeometry.m_Positions[i].z = points[i].z;
        }
    
        // Normals
        status = meshFn.getNormals(m_voxelGeometry.m_Normals, MSpace::kObject);
        CHECK_MSTATUS(status);

        // Texture coordinates (UVs)
        MFloatArray uArray, vArray;
        status = meshFn.getUVs(uArray, vArray);
        CHECK_MSTATUS(status);

        m_voxelGeometry.m_TextureCoords.setLength(uArray.length() * 2);
        for (unsigned int i = 0; i < uArray.length(); ++i) {
            m_voxelGeometry.m_TextureCoords[i * 2] = uArray[i];
            m_voxelGeometry.m_TextureCoords[i * 2 + 1] = vArray[i];
        }

        // Indices (triangulated)
        m_voxelGeometry.m_Indices.clear();
        MItMeshPolygon itPoly(obj, &status);
        if (!status) {
            return;
        }
        for (; !itPoly.isDone(); itPoly.next()) {
            int numTris;
            itPoly.numTriangles(numTris);
            for (int t = 0; t < numTris; ++t) {
                MPointArray triPoints;
                MIntArray triVertexList;
                itPoly.getTriangle(t, triPoints, triVertexList, MSpace::kObject);
                for (unsigned int idx = 0; idx < triVertexList.length(); ++idx) {
                    m_voxelGeometry.m_Indices.append(triVertexList[idx]);
                }
            }
        }
    }

    ~VoxelGeometryOverride() override {}

    void updateDG() override
    {

    }

    void updateRenderItems(const MDagPath& path,
		                   MRenderItemList& list) override
    {
        // No-op. No need for updating / creating / modifying any render items.
    }

    // Use the cached geometry data to populate the geometry buffers.
    // For now, we only support positions, normals, and UVs. Shaders that require other attributes will not work.
    void populateGeometry(const MGeometryRequirements& requirements,
                          const MRenderItemList& renderItems,
                          MGeometry& data) override
    {
        const MVertexBufferDescriptorList&  vertexBufferDescriptorList = requirements.vertexRequirements();
        for (int i = 0; i < vertexBufferDescriptorList.length(); i++)
        {
            MVertexBufferDescriptor desc{};
            if (!vertexBufferDescriptorList.getDescriptor(i, desc)) continue;

            switch (desc.semantic())
            {
                case MGeometry::kPosition:
                {
                    MGlobal::displayInfo(MString("VoxelGeometryOverride: Populating position buffer with ") + m_voxelGeometry.m_Positions.length() + " positions.");
                    MVertexBuffer* positionBuffer = data.createVertexBuffer(desc);
                    if (!positionBuffer) continue;
                    
                    void* buffer = positionBuffer->acquire(m_voxelGeometry.m_Positions.length(), true);
                    if (!buffer) continue;

                    const std::size_t bufferSizeInByte = m_voxelGeometry.m_Positions.length() * sizeof(float) * 3;
                    memcpy(buffer, &m_voxelGeometry.m_Positions[0], bufferSizeInByte);
                    positionBuffer->commit(buffer); // Transfer from CPU to GPU memory.
                    break;
                }
                case MGeometry::kNormal:
                {
                    MVertexBuffer* normalBuffer = data.createVertexBuffer(desc);
                    if (!normalBuffer) continue;

                    void* buffer = normalBuffer->acquire(m_voxelGeometry.m_Normals.length(), true);
                    if (!buffer) continue;

                    const std::size_t bufferSizeInByte = m_voxelGeometry.m_Normals.length() * sizeof(float) * 3;
                    memcpy(buffer, &m_voxelGeometry.m_Normals[0], bufferSizeInByte);
                    normalBuffer->commit(buffer); // Transfer from CPU to GPU memory.
                    break;
                }
                case MGeometry::kTexture:
                {
                    MVertexBuffer* texCoordBuffer = data.createVertexBuffer(desc);
                    if (!texCoordBuffer) continue;

                    void* buffer = texCoordBuffer->acquire(m_voxelGeometry.m_TextureCoords.length() / 2, true);
                    if (!buffer) continue;

                    const std::size_t bufferSizeInByte = m_voxelGeometry.m_TextureCoords.length() * sizeof(float);
                    memcpy(buffer, &m_voxelGeometry.m_TextureCoords[0], bufferSizeInByte);
                    texCoordBuffer->commit(buffer); // Transfer from CPU to GPU memory.
                    break;
                }
                default:
                    MGlobal::displayInfo(MString("VoxelGeometryOverride: Unsupported vertex buffer descriptor: ") + MGeometry::semanticString(desc.semantic()));
                    break;
            }
        }

        // Update indexing data for all appropriate render items
        const int numItems = renderItems.length();
        for (int i = 0; i < numItems; i++)
        {
            const MRenderItem* item = renderItems.itemAt(i);
            if (!item) continue;

            // For now, only support triangles.
            if (item->primitive() != MGeometry::kTriangles) continue;

            MIndexBuffer* indexBuffer = data.createIndexBuffer(MGeometry::kUnsignedInt32);
            if (!indexBuffer) continue;

            void* buffer = indexBuffer->acquire(m_voxelGeometry.m_Indices.length(), true);
            if (!buffer) continue;

            MGlobal::displayInfo(MString("VoxelGeometryOverride: Populating index buffer with ") + m_voxelGeometry.m_Indices.length() + " indices.");
            const std::size_t bufferSizeInByte = m_voxelGeometry.m_Indices.length() * sizeof(unsigned int);
            memcpy(buffer, &m_voxelGeometry.m_Indices[0], bufferSizeInByte);
            indexBuffer->commit(buffer);                 // Transfer from CPU to GPU memory.
            item->associateWithIndexBuffer(indexBuffer); // Associate index buffer with render item
        }
    }

    DrawAPI supportedDrawAPIs() const override {
        return kDirectX11;
    }

    void cleanUp() override
    {
        // Not needed: called after every populateGeometry, to clear cached data from updateDG, which we're not using.
    }

private:
    MObject obj {};
    VoxelGeometry m_voxelGeometry {};
};