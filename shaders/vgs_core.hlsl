#include "common.hlsl"
#include "constants.hlsli"

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

float calcMassNormalization(
    Particle particles[8],
    float compliance
) {
    float maxInvMass = 0.0f;
    [unroll] for (uint i = 0; i < 8; ++i) {
        maxInvMass = max(maxInvMass, particleInverseMass(particles[i]));
    }
    maxInvMass = max(maxInvMass, eps);
    return 1.0f / (maxInvMass + compliance);
}

void doVGSIterations(
    inout Particle particles[8],
    VGSConstants vgsConstants,
    bool bailOnInverted
) {
    float relaxation = vgsConstants.relaxation;
    float edgeUniformity = vgsConstants.edgeUniformity;
    float voxelRestVolume = vgsConstants.voxelRestVolume;
    float particleRadius = vgsConstants.particleRadius;
    uint iterCount = vgsConstants.iterCount;
    float massNormalization = calcMassNormalization(particles, vgsConstants.compliance);

    for (uint iter = 0; iter < iterCount; iter++)
    {
        // Calculate basis vectors (average of edges for each axis)
        float3 v0 = 0.25 * ((particles[1].position - particles[0].position) + (particles[3].position - particles[2].position) + (particles[5].position - particles[4].position) + (particles[7].position - particles[6].position));
        float3 v1 = 0.25 * ((particles[2].position - particles[0].position) + (particles[3].position - particles[1].position) + (particles[6].position - particles[4].position) + (particles[7].position - particles[5].position));
        float3 v2 = 0.25 * ((particles[4].position - particles[0].position) + (particles[5].position - particles[1].position) + (particles[6].position - particles[2].position) + (particles[7].position - particles[3].position));
        if (dot(cross(v0, v1), v2) == 0.0f) {
            v2 = normalize(cross(v0, v1)) * particleRadius;
        }
        
        // Apply relaxed Gram-Schmidt orthonormalization
        float3 u0 = v0 - relaxation * (safeProject(v0, v1) + safeProject(v0, v2));
        float3 u1 = v1 - relaxation * (safeProject(v1, v0) + safeProject(v1, v2));
        float3 u2 = v2 - relaxation * (safeProject(v2, v0) + safeProject(v2, v1));

        // Normalize and scale
        u0 = safeNormal(u0, u1, u2) * (edgeUniformity * particleRadius + ((1.0f - edgeUniformity) * safeLength(v0) * 0.5f));
        u1 = safeNormal(u1, u2, u0) * (edgeUniformity * particleRadius + ((1.0f - edgeUniformity) * safeLength(v1) * 0.5f));
        u2 = safeNormal(u2, u0, u1) * (edgeUniformity * particleRadius + ((1.0f - edgeUniformity) * safeLength(v2) * 0.5f));

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

        float3 center = 0.125f * (particles[0].position + particles[1].position + particles[2].position + particles[3].position +
                                  particles[4].position + particles[5].position + particles[6].position + particles[7].position);

        // Lerp between current positions and goal positions weighted by inverse mass (relative to max inverse mass)
        // NOTE: this seems to produce the right effect, but I fear it introduces compliance / increases time to converge, because a single iteration 
        // won't preserve a voxel's COM. It might be better to compute the COM shift after applying the position updates, and then apply a COM correction step.
        particles[0].position = lerp(particles[0].position, center - u0 - u1 - u2, particleInverseMass(particles[0]) * massNormalization);
        particles[1].position = lerp(particles[1].position, center + u0 - u1 - u2, particleInverseMass(particles[1]) * massNormalization);
        particles[2].position = lerp(particles[2].position, center - u0 + u1 - u2, particleInverseMass(particles[2]) * massNormalization);
        particles[3].position = lerp(particles[3].position, center + u0 + u1 - u2, particleInverseMass(particles[3]) * massNormalization);
        particles[4].position = lerp(particles[4].position, center - u0 - u1 + u2, particleInverseMass(particles[4]) * massNormalization);
        particles[5].position = lerp(particles[5].position, center + u0 - u1 + u2, particleInverseMass(particles[5]) * massNormalization);
        particles[6].position = lerp(particles[6].position, center - u0 + u1 + u2, particleInverseMass(particles[6]) * massNormalization);
        particles[7].position = lerp(particles[7].position, center + u0 + u1 + u2, particleInverseMass(particles[7]) * massNormalization);
    }
}