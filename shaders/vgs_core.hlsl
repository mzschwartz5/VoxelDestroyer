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

bool doVGSIterations(
    inout float3 pos[8],
    float w[8],
    float particleRadius,
    float voxelRestVolume,
    float iterCount,
    float relaxation,
    float edgeUniformity,
    bool bailOnInverted
) {
    for (int iter = 0; iter < iterCount; iter++)
    {
        float3 center = 0.125 * (pos[0] + pos[1] + pos[2] + pos[3] + pos[4] + pos[5] + pos[6] + pos[7]);

        // Calculate basis vectors (average of edges for each axis)
        float3 v0 = 0.25 * ((pos[1] - pos[0]) + (pos[3] - pos[2]) + (pos[5] - pos[4]) + (pos[7] - pos[6]));
        float3 v1 = 0.25 * ((pos[2] - pos[0]) + (pos[3] - pos[1]) + (pos[6] - pos[4]) + (pos[7] - pos[5]));
        float3 v2 = 0.25 * ((pos[4] - pos[0]) + (pos[5] - pos[1]) + (pos[6] - pos[2]) + (pos[7] - pos[3]));
        
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
        // If the volume is zero, that means the voxel bases are degenerate. Trying to preserve volume will result in NaNs.
        // However, the safe normalization and other epsilon adjusments above will yield slight adjustments so that, next iteration, the bases are not degenerate.
        // So, for this iteration, simply skip volume preservation by setting the voxel's volume to its rest volume.
        if (volume == 0.0f) volume = voxelRestVolume;

        if (volume < 0.0f) {
            if (bailOnInverted) return false;
            volume = -volume;

            // Per mcgraw et al., if the voxel has been inverted (negative volume), flip the shortest edge to correct it.
            // Only for VGS within voxels, not for face-to-face VGS (thus the option to bail).
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

        // Volume preservation
        float mult = 0.5f * pow((voxelRestVolume / volume), oneThird);
        u0 *= mult;
        u1 *= mult;
        u2 *= mult;

        if (w[0] != 0.0f) {
            pos[0] = center - u0 - u1 - u2;
        }
        if (w[1] != 0.0f) {
            pos[1] = center + u0 - u1 - u2;
        }
        if (w[2] != 0.0f) {
            pos[2] = center - u0 + u1 - u2;
        }
        if (w[3] != 0.0f) {
            pos[3] = center + u0 + u1 - u2;
        }
        if (w[4] != 0.0f) {
            pos[4] = center - u0 - u1 + u2;
        }
        if (w[5] != 0.0f) {
            pos[5] = center + u0 - u1 + u2;
        }
        if (w[6] != 0.0f) {
            pos[6] = center - u0 + u1 + u2;
        }
        if (w[7] != 0.0f) {
            pos[7] = center + u0 + u1 + u2;
        }
    }

    return true;
}