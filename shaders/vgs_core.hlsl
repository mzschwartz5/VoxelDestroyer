#include "common.hlsl"

static const float eps = 1e-8f;
static const float oneThird = 1.0f / 3.0f;

float3 safeProject(float3 v, float3 onto)
{
    float denom = dot(onto, onto);
    if (denom < eps) return float3(0, 0, 0);
    return onto * (dot(v, onto) / denom);
}

// Normalizes u0 but, if its length is 0, instead returns the normalized
// cross product of u1 and u2. This only works if both u1 and u2 are non-zero.
float3 safeNormal(float3 u0, float3 u1, float3 u2) {
    float3 normal = u0;
    float len = length(u0);
    if (len < eps) {
        return normalize(cross(u1, u2));
    }

    return normal / len;
}

float safeLength(float3 v) {
    float len = length(v);
    if (len < eps) return eps;
    return len;
}

void doVGSIterations(
    inout float4 pos[8],
    float particleRadius,
    float voxelRestVolume,
    float iterCount,
    float relaxation,
    float edgeUniformity,
    bool bailOnInverted
) {
    for (int iter = 0; iter < iterCount; iter++)
    {
        float3 center = 0.125 * (pos[0].xyz + pos[1].xyz + pos[2].xyz + pos[3].xyz + pos[4].xyz + pos[5].xyz + pos[6].xyz + pos[7].xyz);

        // Calculate basis vectors (average of edges for each axis)
        float3 v0 = 0.25 * ((pos[1].xyz - pos[0].xyz) + (pos[3].xyz - pos[2].xyz) + (pos[5].xyz - pos[4].xyz) + (pos[7].xyz - pos[6].xyz));
        float3 v1 = 0.25 * ((pos[2].xyz - pos[0].xyz) + (pos[3].xyz - pos[1].xyz) + (pos[6].xyz - pos[4].xyz) + (pos[7].xyz - pos[5].xyz));
        float3 v2 = 0.25 * ((pos[4].xyz - pos[0].xyz) + (pos[5].xyz - pos[1].xyz) + (pos[6].xyz - pos[2].xyz) + (pos[7].xyz - pos[3].xyz));
        if (dot(cross(v0, v1), v2) == 0.0f) {
            v2 = normalize(cross(v0, v1)) * particleRadius;
        }
        
        // Apply relaxed Gram-Schmidt orthonormalization
        float3 u0 = v0 - relaxation * (safeProject(v0, v1) + safeProject(v0, v2));
        float3 u1 = v1 - relaxation * (safeProject(v1, v0) + safeProject(v1, v2));
        float3 u2 = v2 - relaxation * (safeProject(v2, v0) + safeProject(v2, v1));

        // Normalize and scale
        u0 = safeNormal(u0, u1, u2) * ((1.0f - edgeUniformity) * particleRadius + (edgeUniformity * safeLength(v0) * 0.5f));
        u1 = safeNormal(u1, u2, u0) * ((1.0f - edgeUniformity) * particleRadius + (edgeUniformity * safeLength(v1) * 0.5f));
        u2 = safeNormal(u2, u0, u1) * ((1.0f - edgeUniformity) * particleRadius + (edgeUniformity * safeLength(v2) * 0.5f));

        // Check for flipping
        float volume = dot(cross(u0, u1), u2);
        if (volume < 0.0f) {
            if (bailOnInverted) return;
            volume = -volume;

            // Per mcgraw et al., if the voxel has been inverted (negative volume), flip the shortest edge to correct it.
            // Only for VGS within voxels, not for face-to-face VGS (thus the option to bail).
            float len0sq = dot(u0, u0);
            float len1sq = dot(u1, u1);
            float len2sq = dot(u2, u2);

            // Branchless selection and inversion of the shortest edge.
            float m0 = step(len0sq, len1sq) * step(len0sq, len2sq);                      
            float m1 = step(len1sq, len0sq) * step(len1sq, len2sq) * (1.0f - m0);        
            float m2 = step(len2sq, len0sq) * step(len2sq, len1sq) * (1.0f - m0) * (1.0f - m1);

            u0 = lerp(u0, -u0, m0);
            u1 = lerp(u1, -u1, m1);
            u2 = lerp(u2, -u2, m2);
        }

        // Bail if volume is too small (voxel is degenerate, there's no way to know how to restore it.
        // Other constraints may restore it later).
        if (volume < eps) return;

        // Volume preservation
        float mult = 0.5f * pow(abs(voxelRestVolume / volume), oneThird); // (abs to appease FXC)
        u0 *= mult;
        u1 *= mult;
        u2 *= mult;

        if (!massIsInfinite(pos[0])) { pos[0].xyz = center - u0 - u1 - u2; }
        if (!massIsInfinite(pos[1])) { pos[1].xyz = center + u0 - u1 - u2; }
        if (!massIsInfinite(pos[2])) { pos[2].xyz = center - u0 + u1 - u2; }
        if (!massIsInfinite(pos[3])) { pos[3].xyz = center + u0 + u1 - u2; }
        if (!massIsInfinite(pos[4])) { pos[4].xyz = center - u0 - u1 + u2; }
        if (!massIsInfinite(pos[5])) { pos[5].xyz = center + u0 - u1 + u2; }
        if (!massIsInfinite(pos[6])) { pos[6].xyz = center - u0 + u1 + u2; }
        if (!massIsInfinite(pos[7])) { pos[7].xyz = center + u0 + u1 + u2; }
    }
}