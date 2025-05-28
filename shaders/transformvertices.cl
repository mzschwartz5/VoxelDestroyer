__kernel void transformVertices(
    __global const float4* particles,
    __global const uint* vertStartIds,
    __global const uint* numVerts,
    __global const float4* localRestPositions,
    __global float4* transformedPositions
) {
    uint globalThreadId = get_global_id(0);
    uint groupId = get_group_id(0);
    uint localThreadId = get_local_id(0);
    uint localSize = get_local_size(0);

    uint v0_idx = groupId << 3;
    float4 v0 = particles[v0_idx];
    float4 v1 = particles[v0_idx + 1];
    float4 v2 = particles[v0_idx + 2];
    float4 v4 = particles[v0_idx + 4];

    float4 e0 = normalize(v1 - v0);
    float4 e1 = normalize(v2 - v0);
    float4 e2 = normalize(v4 - v0);

    uint vertexStartIdx = vertStartIds[groupId];
    uint numVertsInVoxel = numVerts[groupId];
    uint numVertsPerThread = (numVertsInVoxel + localSize - 1) / localSize;

    for (uint i = 0; i < numVertsPerThread; i++) {
        uint vertexIdx = vertexStartIdx + localThreadId + (i * localSize);
        if (vertexIdx >= vertexStartIdx + numVertsInVoxel) {
            continue;
        }

        float4 localRestPosition = localRestPositions[vertexIdx];
        float4 transformedPos = v0 + (localRestPosition.x * e0) + (localRestPosition.y * e1) + (localRestPosition.z * e2);
        transformedPositions[vertexIdx] = (float4)(transformedPos.x, transformedPos.y, transformedPos.z, 1.0f);
    }
}