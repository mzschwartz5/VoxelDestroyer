// Constants
#define RELAXATION 0.25f
#define BETA 0.8f
#define PARTICLE_RADIUS 0.125f
#define VOXEL_REST_VOLUME 0.125f
#define ITER_COUNT 3

RWStructuredBuffer<float4> positions : register(u0);
StructuredBuffer<float> weights : register(t0);

float3 project(float3 v, float3 onto)
{
    return onto * (dot(v, onto) / dot(onto, onto));
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

    // Each thread in the group will contribute to the constraint solving
    for (uint i = 0; i < ITER_COUNT; i++)
    {
        // Load positions
        float4 p0 = positions[start_idx];
        float4 p1 = positions[start_idx + 1];
        float4 p2 = positions[start_idx + 2];
        float4 p3 = positions[start_idx + 3];
        float4 p4 = positions[start_idx + 4];
        float4 p5 = positions[start_idx + 5];
        float4 p6 = positions[start_idx + 6];
        float4 p7 = positions[start_idx + 7];

        // Calculate centroid
        float4 center = p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7;
        center *= 0.125f;

        // Calculate basis vectors
        float4 v0 = ((p1 - p0) + (p3 - p2) + (p5 - p4) + (p7 - p6)) * 0.25f;
        float4 v1 = ((p2 - p0) + (p3 - p1) + (p6 - p4) + (p7 - p5)) * 0.25f;
        float4 v2 = ((p4 - p0) + (p5 - p1) + (p6 - p2) + (p7 - p3)) * 0.25f;

        // Apply Gram-Schmidt orthogonalization - CORRECTED to match CPU implementation
        float3 u0 = v0.xyz - RELAXATION * (project(v0.xyz, v1.xyz) + project(v0.xyz, v2.xyz));
        float3 u1 = v1.xyz - RELAXATION * (project(v1.xyz, v2.xyz) + project(v1.xyz, v0.xyz)); // Changed from u0 to v0.xyz
        float3 u2 = v2.xyz - RELAXATION * (project(v2.xyz, v0.xyz) + project(v2.xyz, v1.xyz)); // Changed from u0/u1 to v0.xyz/v1.xyz

        // Normalize and scale
        u0 = normalize(u0) * ((1.0f - BETA) * PARTICLE_RADIUS + (BETA * length(v0.xyz) * 0.5f));
        u1 = normalize(u1) * ((1.0f - BETA) * PARTICLE_RADIUS + (BETA * length(v1.xyz) * 0.5f));
        u2 = normalize(u2) * ((1.0f - BETA) * PARTICLE_RADIUS + (BETA * length(v2.xyz) * 0.5f));

        // Volume preservation
        float volume = dot(cross(u0, u1), u2);
        float mult = 0.5f * pow((VOXEL_REST_VOLUME / volume), 1.0f / 3.0f);

        u0 *= mult;
        u1 *= mult;
        u2 *= mult;

        // Update positions based on weights - CORRECTED to match CPU implementation
        if (weights[start_idx] != 0.0f) {
            positions[start_idx] = center - float4(u0, 0.0f) - float4(u1, 0.0f) - float4(u2, 0.0f);
        }
        if (weights[start_idx + 1] != 0.0f) {
            positions[start_idx + 1] = center + float4(u0, 0.0f) - float4(u1, 0.0f) - float4(u2, 0.0f);
        }
        if (weights[start_idx + 2] != 0.0f) {
            positions[start_idx + 2] = center - float4(u0, 0.0f) + float4(u1, 0.0f) - float4(u2, 0.0f);
        }
        if (weights[start_idx + 3] != 0.0f) {
            positions[start_idx + 3] = center + float4(u0, 0.0f) + float4(u1, 0.0f) - float4(u2, 0.0f);
        }
        if (weights[start_idx + 4] != 0.0f) {
            positions[start_idx + 4] = center - float4(u0, 0.0f) - float4(u1, 0.0f) + float4(u2, 0.0f);
        }
        if (weights[start_idx + 5] != 0.0f) {
            positions[start_idx + 5] = center + float4(u0, 0.0f) - float4(u1, 0.0f) + float4(u2, 0.0f);
        }
        if (weights[start_idx + 6] != 0.0f) {
            positions[start_idx + 6] = center - float4(u0, 0.0f) + float4(u1, 0.0f) + float4(u2, 0.0f);
        }
        if (weights[start_idx + 7] != 0.0f)
        {
            positions[start_idx + 7] = center + float4(u0, 0.0f) + float4(u1, 0.0f) + float4(u2, 0.0f);
        }
    }
}