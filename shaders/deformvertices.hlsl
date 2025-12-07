#include "common.hlsl"

StructuredBuffer<float3> originalVertPositions : register(t0);
StructuredBuffer<float3> originalVertNormals : register(t1);
StructuredBuffer<float4> originalParticlePositions : register(t2);
StructuredBuffer<float4> particlePositions : register(t3);
StructuredBuffer<uint> vertexVoxelIds : register(t4);

// The bind flags Maya uses prevents us from using structured buffers and requires an R32_FLOAT format for UAVs.
RWBuffer<float > outVertPositions : register(u0);
RWBuffer<float> outVertNormals : register(u1);

cbuffer DeformConstants : register(b0)
{
    // The rest frame of each voxel may be rotated if the voxel grid is rotated. We need to account for that in the deformation.
    float4x4 gridRotationInverse;
    uint vertexCount;
    uint padding0;
    uint padding1;
    uint padding2;
};

float3x3 inverseFromRows(float3 r0, float3 r1, float3 r2)
{
    float3 c0 = cross(r1, r2);
    float3 c1 = cross(r2, r0);
    float3 c2 = cross(r0, r1);
    float det = dot(r0, c0);

    // Fallback for degenerate cases (amounts to returning original, undeformed normal)
    if (abs(det) < 1e-8) return (float3x3)1.0;
    float invDet = 1.0 / det;
    return float3x3(c0 * invDet, c1 * invDet, c2 * invDet);
}


/**
 * Each thread represents a vertex. Based on the voxel the vertex belongs to, we deform the vertex
 * according to the positions of the particles in that voxel, compared to their original positions.
 * Similarly, we deform the normals.
 * 
 * A couple notes:
 * 1. Vertices and normals may be duplicated in the input buffers if a vertex is shared between triangles
 *    and has different normals or UVs for each triangle. This is fine, we just do a little redundant work.
 * 2. Maya's GPU deformer API abstracts this duplicate vertex concern away - you only deform the logical vertices.
 *    However, the deformer API gives *no* way to control normals. It calculates them automatically. For most workflows,
 *    that's probably fine. The discontinuities between destructible voxels, however, make Maya's auto-calculated normals look bad.
 */
[numthreads(DEFORM_VERTICES_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) 
{
    if (gId.x >= vertexCount) return;

    uint voxelId = vertexVoxelIds[gId.x];
    uint particleStartIdx = voxelId << 3;

    float3 v0 = particlePositions[particleStartIdx + 0].xyz;
    float3 v1 = particlePositions[particleStartIdx + 1].xyz;
    float3 v2 = particlePositions[particleStartIdx + 2].xyz;
    float3 v4 = particlePositions[particleStartIdx + 4].xyz;

    // Note: originalParticles contains only one reference particle per voxel, thus the index into it
    // does not need to be multiplied by 8 to account for the 8 particles per voxel.
    float4 v0_orig = originalParticlePositions[voxelId];
    float v0_orig_radius = unpackHalf2x16(v0_orig.w).x;
    float voxelRestLengthInv = 1.0 / (2.0 * v0_orig_radius); // All particles in a voxel have the same radius.

    // The deformed basis of the voxel (not normalized, but scaled by voxelRestLengthInv)
    float3 e0 = (v1 - v0) * voxelRestLengthInv;
    float3 e1 = (v2 - v0) * voxelRestLengthInv;
    float3 e2 = (v4 - v0) * voxelRestLengthInv;

    // Deform position
    float3 restPosition = mul((float3x3)gridRotationInverse, originalVertPositions[gId.x] - v0_orig.xyz);
    restPosition = v0 + restPosition.x * e0
                      + restPosition.y * e1
                      + restPosition.z * e2;
    uint outOffset = gId.x * 3;
    outVertPositions[outOffset + 0] = restPosition.x;
    outVertPositions[outOffset + 1] = restPosition.y;
    outVertPositions[outOffset + 2] = restPosition.z;

    // Deform normal
    float3x3 deformMatrix = transpose(inverseFromRows(e0, e1, e2));
    float3 normal = originalVertNormals[gId.x];
    normal = normalize(mul(deformMatrix, normal));

    outVertNormals[outOffset + 0] = normal.x;
    outVertNormals[outOffset + 1] = normal.y;
    outVertNormals[outOffset + 2] = normal.z;
}