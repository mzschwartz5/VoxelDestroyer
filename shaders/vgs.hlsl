
RWStructuredBuffer<float4> positions : register(u0);
StructuredBuffer<float> weights : register(t0);

static const float eps = 1e-8f;
static const float oneThird = 1.0f / 3.0f;

cbuffer VoxelSimBuffer : register(b0)
{
    float RELAXATION;
    float BETA;
    float PARTICLE_RADIUS;
    float VOXEL_REST_VOLUME;
    float ITER_COUNT;
    float PADDING_0;
    float PADDING_1;
    float PADDING_2;
};

float3 project(float3 v, float3 onto)
{
    float denom = dot(onto, onto);
    if (abs(denom) < eps) denom = eps;
    return onto * (dot(v, onto) / denom);
}

// Normalization that avoids division by zero, and also allows for the added epsilon
// to be along a specific axis, in case multiple basis vectors are 0-length (so that they aren't degenerate).
float3 safeNormal(float3 u, int axis) {
    float3 normal = u;
    float len = length(u);
    if (len < eps) {
        normal[axis] = eps;
        len = eps;
    }

    return normal / len;
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
        // TODO: only load positions once at start and write once at end. Each iteration should read and write to a local array.
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

        // Calculate basis vectors (average of edges for each axis)
        float4 v0 = ((p1 - p0) + (p3 - p2) + (p5 - p4) + (p7 - p6)) * 0.25f;
        float4 v1 = ((p2 - p0) + (p3 - p1) + (p6 - p4) + (p7 - p5)) * 0.25f;
        float4 v2 = ((p4 - p0) + (p5 - p1) + (p6 - p2) + (p7 - p3)) * 0.25f;

        // Apply relaxed Gram-Schmidt orthonormalization
        float3 u0 = v0.xyz - RELAXATION * (project(v0.xyz, v1.xyz) + project(v0.xyz, v2.xyz));
        float3 u1 = v1.xyz - RELAXATION * (project(v1.xyz, v2.xyz) + project(v1.xyz, v0.xyz)); 
        float3 u2 = v2.xyz - RELAXATION * (project(v2.xyz, v0.xyz) + project(v2.xyz, v1.xyz)); 

        // Normalize and scale
        u0 = safeNormal(u0, 0) * ((1.0f - BETA) * PARTICLE_RADIUS + (BETA * (length(v0.xyz) + eps) * 0.5f));
        u1 = safeNormal(u1, 1) * ((1.0f - BETA) * PARTICLE_RADIUS + (BETA * (length(v1.xyz) + eps) * 0.5f));
        u2 = safeNormal(u2, 2) * ((1.0f - BETA) * PARTICLE_RADIUS + (BETA * (length(v2.xyz) + eps) * 0.5f));

        // Volume preservation
        float volume = dot(cross(u0, u1), u2);
        // Per mcgraw et al., if the voxel has been inverted (negative volume), flip the shortest edge to correct it.
        if (volume < 0.0f) {
            volume = -volume;
            float len0 = length(u0);
            float len1 = length(u1);
            float len2 = length(u2);
            float minLen = min(len0, min(len1, len2));

            if (len0 == minLen) {
                u0 = -u0;
            } else if (len1 == minLen) {
                u1 = -u1;
            } else if (len2 == minLen) {
                u2 = -u2;
            }
        }

        // If the volume is zero, that means the voxel bases are degenerate. Trying to preserve volume will result in NaNs.
        // However, the safe normalization and other epsilon adjusments above will yield slight adjustments so that, next iteration, the bases are not degenerate.
        // So, for this iteration, simply skip volume preservation by setting the voxel's volume to its rest volume.
        if (volume == 0) volume = VOXEL_REST_VOLUME;

        float mult = 0.5f * pow((VOXEL_REST_VOLUME / volume), oneThird);
        u0 *= mult;
        u1 *= mult;
        u2 *= mult;

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