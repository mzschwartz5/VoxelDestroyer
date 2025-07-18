static const float eps = 1e-8f;
static const float oneThird = 1.0f / 3.0f;

// TODO: args are reverse order form vgs.hlsl
// Make them consistent and refactor into a common header
float3 project(float3 onto, float3 v)
{
    float denom = dot(onto, onto);
    if (abs(denom) < eps) denom = eps;
    return onto * (dot(v, onto) / denom);
}

float3 safeNormal(float3 u, int axis) {
    float3 normal = u;
    float len = length(u);
    if (len < eps) {
        normal[axis] = eps;
        len = eps;
    }

    return normal / len;
}

bool doVGSIterations(
    inout float3 pos[8],
    inout float w[8],
    float particleRadius,
    float voxelRestVolume,
    float iterCount,
    float relaxation,
    float edgeUniformity
) {
    for (int iter = 0; iter < iterCount; iter++)
    {
        float3 center = 0.125 * (pos[0] + pos[1] + pos[2] + pos[3] + pos[4] + pos[5] + pos[6] + pos[7]);

        // Calculate basis vectors (average of edges for each axis)
        float3 v0 = (pos[1] - pos[0]) + (pos[3] - pos[2]) + (pos[5] - pos[4]) + (pos[7] - pos[6]);
        float3 v1 = (pos[2] - pos[0]) + (pos[3] - pos[1]) + (pos[6] - pos[4]) + (pos[7] - pos[5]);
        float3 v2 = (pos[4] - pos[0]) + (pos[5] - pos[1]) + (pos[6] - pos[2]) + (pos[7] - pos[3]);

        // Apply relaxed Gram-Schmidt orthonormalization
        float3 u0 = v0 - relaxation * (project(v1, v0) + project(v2, v0));
        float3 u1 = v1 - relaxation * (project(v0, v1) + project(v2, v1));
        float3 u2 = v2 - relaxation * (project(v0, v2) + project(v1, v2));

        // Normalize and scale
        u0 = safeNormal(u0, 0) * ((1.0f - edgeUniformity) * particleRadius + (edgeUniformity * (length(v0) + eps) * 0.5f));
        u1 = safeNormal(u1, 1) * ((1.0f - edgeUniformity) * particleRadius + (edgeUniformity * (length(v1) + eps) * 0.5f));
        u2 = safeNormal(u2, 2) * ((1.0f - edgeUniformity) * particleRadius + (edgeUniformity * (length(v2) + eps) * 0.5f));

        // Check for flipping
        float volume = dot(cross(u0, u1), u2);
        if (volume <= 0.0f) return false;

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