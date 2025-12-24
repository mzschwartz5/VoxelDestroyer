#include "common.hlsl"
#include "constants.hlsli"

StructuredBuffer<float3> originalVertPositions : register(t0);
StructuredBuffer<float3> originalVertNormals : register(t1);
StructuredBuffer<Particle> originalParticles : register(t2);
StructuredBuffer<Particle> particles : register(t3);
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

    float3 v0 = particles[particleStartIdx + 0].position;
    float3 v1 = particles[particleStartIdx + 1].position;
    float3 v2 = particles[particleStartIdx + 2].position;
    float3 v3 = particles[particleStartIdx + 3].position;
    float3 v4 = particles[particleStartIdx + 4].position;
    float3 v5 = particles[particleStartIdx + 5].position;
    float3 v6 = particles[particleStartIdx + 6].position;
    float3 v7 = particles[particleStartIdx + 7].position;

    // Note: originalParticles contains only one reference particle per voxel, thus the index into it
    // does not need to be multiplied by 8 to account for the 8 particles per voxel.
    Particle p0_orig = originalParticles[voxelId];
    float r = unpackHalf2x16(p0_orig.radiusAndInvMass).x;
    float3x3 gridRotInv3x3 = (float3x3)gridRotationInverse;
    float3 restLocal = mul(gridRotInv3x3, originalVertPositions[gId.x] - (p0_orig.position - float3(r, r, r)));
    float3 uvw = restLocal / (4.0f * r);
    float u = uvw.x; float v = uvw.y; float w = uvw.z;

    float3 deformedPos =
        v0 * (1 - u) * (1 - v) * (1 - w) +
        v1 * u * (1 - v) * (1 - w) +
        v2 * (1 - u) * v * (1 - w) +
        v3 * u * v * (1 - w) +
        v4 * (1 - u) * (1 - v) * w +
        v5 * u * (1 - v) * w +
        v6 * (1 - u) * v * w +
        v7 * u * v * w;

    uint outOffset = gId.x * 3;
    outVertPositions[outOffset + 0] = deformedPos.x;
    outVertPositions[outOffset + 1] = deformedPos.y;
    outVertPositions[outOffset + 2] = deformedPos.z;

    // Deform normal
    float3 dP_du =
        (v1 - v0) * (1.0f - v) * (1.0f - w) +
        (v3 - v2) * (v)        * (1.0f - w) +
        (v5 - v4) * (1.0f - v) * (w) +
        (v7 - v6) * (v)        * (w);

    float3 dP_dv =
        (v2 - v0) * (1.0f - u) * (1.0f - w) +
        (v3 - v1) * (u)        * (1.0f - w) +
        (v6 - v4) * (1.0f - u) * (w) +
        (v7 - v5) * (u)        * (w);

    float3 dP_dw =
        (v4 - v0) * (1.0f - u) * (1.0f - v) +
        (v5 - v1) * (u)        * (1.0f - v) +
        (v6 - v2) * (1.0f - u) * (v) +
        (v7 - v3) * (u)        * (v);

    float3x3 deformMatrix = transpose(inverseFromRows(dP_du, dP_dv, dP_dw));

    float3 normal = originalVertNormals[gId.x];
    normal = mul(gridRotInv3x3, normal);
    normal = normalize(mul(deformMatrix, normal));

    outVertNormals[outOffset + 0] = normal.x;
    outVertNormals[outOffset + 1] = normal.y;
    outVertNormals[outOffset + 2] = normal.z;
}