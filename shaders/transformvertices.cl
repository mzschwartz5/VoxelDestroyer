/*
* Each workgroup represents a voxel. Each thread in the workgroup handles 
* (numVerts / TRANFORM_VERTICES_THREADS) vertices from the voxel and transforms them based on the deformed voxel's basis.
*/
__kernel void transformVertices(
    const uint numVoxels,
    const uint totalVerts,
    const int particleBufferOffset,
    __global const float4* particles,
    __global const uint* vertStartIds,
    __global const float4* originalParticles,
    __global const float* originalVertPositions, // inputPositions from deformer input
    __global float* transformedPositions         // outputPositions from deformer output
) {
    uint groupId = get_group_id(0);
    uint localThreadId = get_local_id(0);
    uint localSize = get_local_size(0);

    uint v0_idx = groupId << 3;
    float4 v0 = particles[particleBufferOffset + v0_idx];
    float4 v1 = particles[particleBufferOffset + v0_idx + 1];
    float4 v2 = particles[particleBufferOffset + v0_idx + 2];
    float4 v4 = particles[particleBufferOffset + v0_idx + 4];

    // Note: originalParticles contains only one reference particle per voxel, thus the index into it
    // does not need to be multiplied by 8 to account for the 8 particles per voxel.
    float4 v0_orig = originalParticles[groupId]; 

    float3 e0 = normalize(v1 - v0).xyz;
    float3 e1 = normalize(v2 - v0).xyz;
    float3 e2 = normalize(v4 - v0).xyz;

    uint vertexStartIdx = vertStartIds[groupId];
    uint vertexEndIdx = (groupId + 1 < numVoxels) ? vertStartIds[groupId + 1] : totalVerts;
    uint numVertsPerThread = (vertexEndIdx - vertexStartIdx + localSize - 1) / localSize;

    for (uint i = 0; i < numVertsPerThread; i++) {
        uint vertexIdx = vertexStartIdx + localThreadId + (i * localSize);
        if (vertexIdx >= vertexEndIdx) {
            continue;
        }

        vertexIdx *= 3; // Vertex positions are stored as flat arrays of floats, 3 floats per vertex.
        float3 restPosition = (float3)(originalVertPositions[vertexIdx + 0], originalVertPositions[vertexIdx + 1], originalVertPositions[vertexIdx + 2]) 
                            - v0_orig.xyz;
        float3 transformedPos = v0.xyz + (restPosition.x * e0) + (restPosition.y * e1) + (restPosition.z * e2);

        transformedPositions[vertexIdx + 0] = transformedPos.x;
        transformedPositions[vertexIdx + 1] = transformedPos.y;
        transformedPositions[vertexIdx + 2] = transformedPos.z;
    }
}