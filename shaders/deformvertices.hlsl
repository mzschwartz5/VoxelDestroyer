
StructuredBuffer<float3> originalVertPositions : register(t0);
StructuredBuffer<float3> originalVertNormals : register(t1);
StructuredBuffer<float4> originalParticlePositions : register(t2);
StructuredBuffer<float4> particlePositions : register(t3);
StructuredBuffer<uint> vertexVoxelIds : register(t4);

// Maya doesn't support structured buffers for these (which get bound to Maya's IA stage).
// Logically, they store float3s per vertex.
RWByteAddressBuffer outVertPositions : register(u0);
RWByteAddressBuffer outVertNormals : register(u1);

cbuffer DeformConstants : register(b0)
{
    uint vertexCount;
    uint padding0;
    uint padding1;
    uint padding2;
};

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
    float3 v0_orig = originalParticlePositions[voxelId + 0].xyz;
    float3 v1_orig = originalParticlePositions[voxelId + 1].xyz;
    float3 v2_orig = originalParticlePositions[voxelId + 2].xyz;
    float3 v3_orig = originalParticlePositions[voxelId + 4].xyz;

    float3 e0 = normalize(v1 - v0);
    float3 e1 = normalize(v2 - v0);
    float3 e2 = normalize(v4 - v0);

    float3 restPosition = originalVertPositions[gId.x] - v0_orig;
    float3 deformedPos = v0 + restPosition.x * e0
                            + restPosition.y * e1
                            + restPosition.z * e2;

    uint outOffset = gId.x * 12; // 3 floats * 4 bytes each
    outVertPositions.Store3(outOffset, deformedPos);

    // TODO: normals
}