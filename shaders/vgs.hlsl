RWStructuredBuffer<float4> positions : register(u0);
StructuredBuffer<float> weights : register(t0);

cbuffer VoxelSimBuffer : register(b0)
{
    float RELAXATION;
    float BETA;
    float PARTICLE_RADIUS;
    float VOXEL_REST_VOLUME;
    float ITER_COUNT;
    float AXIS;
    float PADDING_1;
    float PADDING_2;
};

float3 project(float3 v, float3 onto)
{
    const float eps = 1e-12f;
    return onto * (dot(v, onto) / (dot(onto, onto) + eps));
}

[numthreads(VGS_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint3 localThreadId : SV_GroupThreadID
)
{
    uint voxel_idx = globalThreadId.x;
    uint start_idx = voxel_idx << 3;

    for (uint i = 0; i < ITER_COUNT; i++)
    {
        // Load positions
        float4 p[8];
        [unroll]
        for (int j = 0; j < 8; ++j)
            p[j] = positions[start_idx + j];

        // Calculate centroid
        float4 center = float4(0, 0, 0, 0);
        [unroll]
        for (int j = 0; j < 8; ++j)
            center += p[j];
        center *= 0.125f;

        // Calculate basis vectors
        float3 v0 = ((p[1] - p[0]) + (p[3] - p[2]) + (p[5] - p[4]) + (p[7] - p[6])) * 0.25f;
        float3 v1 = ((p[2] - p[0]) + (p[3] - p[1]) + (p[6] - p[4]) + (p[7] - p[5])) * 0.25f;
        float3 v2 = ((p[4] - p[0]) + (p[5] - p[1]) + (p[6] - p[2]) + (p[7] - p[3])) * 0.25f;

        // Gram-Schmidt Orthogonalization
        float3 u0 = v0 - RELAXATION * (project(v0, v1) + project(v0, v2));
        float3 u1 = v1 - RELAXATION * (project(v1, v0) + project(v1, v2));
        float3 u2 = v2 - RELAXATION * (project(v2, v0) + project(v2, v1));

        // Safe normalization
        const float small = 1e-8f;
        float len0 = max(length(u0), small);
        float len1 = max(length(u1), small);
        float len2 = max(length(u2), small);

        u0 = u0 / len0;
        u1 = u1 / len1;
        u2 = u2 / len2;

        // Apply scaling
        u0 *= ((1.0f - BETA) * PARTICLE_RADIUS + BETA * length(v0) * 0.5f);
        u1 *= ((1.0f - BETA) * PARTICLE_RADIUS + BETA * length(v1) * 0.5f);
        u2 *= ((1.0f - BETA) * PARTICLE_RADIUS + BETA * length(v2) * 0.5f);

        // Compute volume
        float volume = dot(cross(u0, u1), u2);

        // Flip one axis if volume is negative (to prevent inversion)
        if (volume < 0.0f)
        {
            // Flip the shortest axis
            float3 lens = float3(len0, len1, len2);
            int minAxis = (lens.x <= lens.y && lens.x <= lens.z) ? 0 :
                (lens.y <= lens.x && lens.y <= lens.z) ? 1 : 2;

            if (minAxis == 0) u0 = -u0;
            else if (minAxis == 1) u1 = -u1;
            else u2 = -u2;

            volume = -volume;
        }

        // Clamp volume to avoid division by zero
        volume = max(volume, small);

        float mult = 0.5f * pow(VOXEL_REST_VOLUME / volume, 1.0f / 3.0f);

        u0 *= mult;
        u1 *= mult;
        u2 *= mult;

        // Apply updated positions
        float4 offsets[8] = {
            -float4(u0 + u1 + u2, 0.0f),
             float4(u0 - u1 - u2, 0.0f),
            -float4(u0 - u1 + u2, 0.0f),
             float4(u0 + u1 - u2, 0.0f),
            -float4(u0 + u1 - u2, 0.0f),
             float4(u0 - u1 + u2, 0.0f),
            -float4(u0 - u1 - u2, 0.0f),
             float4(u0 + u1 + u2, 0.0f)
        };

        [unroll]
        for (int j = 0; j < 8; ++j)
        {
            if (weights[start_idx + j] != 0.0f)
                positions[start_idx + j] = center + offsets[j];
        }
    }
}
