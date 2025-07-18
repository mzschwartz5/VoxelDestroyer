static const float eps = 1e-8f;
static const float oneThird = 1.0f / 3.0f;

// Face constraint structure
struct FaceConstraint {
    int voxelAIdx;
    int voxelBIdx;
    float tensionLimit;
    float compressionLimit;
};

cbuffer VoxelSimBuffer : register(b0)
{
    float RELAXATION;
    float BETA;
    float PARTICLE_RADIUS;
    float VOXEL_REST_VOLUME;
    float ITER_COUNT;
    float FTF_RELAXATION;
    float FTF_BETA;
    float padding;
};

cbuffer FaceIndicesBuffer : register(b1)
{
    uint4 faceAParticles;
    uint4 faceBParticles;
};

RWStructuredBuffer<float4> positions : register(u0);
RWStructuredBuffer<FaceConstraint> faceConstraints : register(u1);
RWStructuredBuffer<uint> isSurfaceVoxel : register(u2);
StructuredBuffer<float> weights : register(t0);

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

void breakConstraint(int constraintIdx, int voxelAIdx, int voxelBIdx) {
    isSurfaceVoxel[voxelAIdx] = 1;
    isSurfaceVoxel[voxelBIdx] = 1;

    faceConstraints[constraintIdx].voxelAIdx = -1;
    faceConstraints[constraintIdx].voxelBIdx = -1;
}

/**
* Solves face constraints for a pair of voxels using the VGS method.
* One thread = one face constraint. 
*/
[numthreads(VGS_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint3 localThreadId : SV_GroupThreadID
)
{
    uint constraintIdx = globalThreadId.x;
    FaceConstraint constraint;
    constraint = faceConstraints[constraintIdx];
    
    // A face constraint deals with two voxels, which we'll refer to as A and B throughout this shader.
    int voxelAIdx = constraint.voxelAIdx;
    int voxelBIdx = constraint.voxelBIdx;

    // Face constraint is already broken.
    if (voxelAIdx == -1 || voxelBIdx == -1) return;

    uint voxelAParticlesIdx = voxelAIdx << 3;
    uint voxelBParticlesIdx = voxelBIdx << 3;

    float3 pos[8];
    float w[8];
    for (int i = 0; i < 4; ++i) {
        // Note: this is NOT a mistake. The face indices of the opposite voxel tell us where to index
        // the particles of the first voxel. Each face*Particles array tells us which particles to use from each voxel,
        // but we leverage the order within each array to avoid axis-specific control flow.
        pos[faceBParticles[i]] = positions[voxelAParticlesIdx + faceAParticles[i]].xyz;
        pos[faceAParticles[i]] = positions[voxelBParticlesIdx + faceBParticles[i]].xyz;

        // Check if the constraint between these two voxels should be broken due to tension/compression
        float3 edgeLength = length(pos[faceAParticles[i]] - pos[faceBParticles[i]]);
        float strain = (edgeLength - 2.0f * PARTICLE_RADIUS) / (2.0f * PARTICLE_RADIUS);
        if (strain > constraint.tensionLimit || strain < constraint.compressionLimit) {
            breakConstraint(constraintIdx, voxelAIdx, voxelBIdx);
            return;
        }

        w[faceBParticles[i]] = weights[voxelAParticlesIdx + faceAParticles[i]];
        w[faceAParticles[i]] = weights[voxelBParticlesIdx + faceBParticles[i]];
    }

    // Now we do VGS iterations on the imaginary "voxel" formed by the particles of the two voxels' faces.
    for (int iter = 0; iter < ITER_COUNT; iter++)
    {
        float3 center = 0.125 * (pos[0] + pos[1] + pos[2] + pos[3] + pos[4] + pos[5] + pos[6] + pos[7]);

        // Calculate basis vectors (average of edges for each axis)
        float3 v0 = (pos[1] - pos[0]) + (pos[3] - pos[2]) + (pos[5] - pos[4]) + (pos[7] - pos[6]);
        float3 v1 = (pos[2] - pos[0]) + (pos[3] - pos[1]) + (pos[6] - pos[4]) + (pos[7] - pos[5]);
        float3 v2 = (pos[4] - pos[0]) + (pos[5] - pos[1]) + (pos[6] - pos[2]) + (pos[7] - pos[3]);

        // Apply relaxed Gram-Schmidt orthonormalization
        float3 u0 = v0 - FTF_RELAXATION * (project(v1, v0) + project(v2, v0));
        float3 u1 = v1 - FTF_RELAXATION * (project(v0, v1) + project(v2, v1));
        float3 u2 = v2 - FTF_RELAXATION * (project(v0, v2) + project(v1, v2));

        // Normalize and scale
        u0 = safeNormal(u0, 0) * ((1.0f - FTF_BETA) * PARTICLE_RADIUS + (FTF_BETA * (length(v0) + eps) * 0.5f));
        u1 = safeNormal(u1, 1) * ((1.0f - FTF_BETA) * PARTICLE_RADIUS + (FTF_BETA * (length(v1) + eps) * 0.5f));
        u2 = safeNormal(u2, 2) * ((1.0f - FTF_BETA) * PARTICLE_RADIUS + (FTF_BETA * (length(v2) + eps) * 0.5f));

        // Check for flipping
        float volume = dot(cross(u0, u1), u2);
        if (volume <= 0.0f)
        {
            breakConstraint(constraintIdx, voxelAIdx, voxelBIdx);
            return;
        }

        float mult = 0.5f * pow((VOXEL_REST_VOLUME / volume), oneThird);
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

    // Write back the updated positions to global memory
    for (int i = 0; i < 4; ++i) {
        // Again, the mixing of A and B is actually not a mistake. It's a result of how we defined the face indices,
        // taking advantage of the ordering to be able to write one shader for all axes with no branching.
        if (w[i] != 0.0f) {
            positions[voxelAParticlesIdx + faceAParticles[i]] = float4(pos[faceBParticles[i]], 1.0f);
        }
        if (w[i] != 0.0f) {
            positions[voxelBParticlesIdx + faceBParticles[i]] = float4(pos[faceAParticles[i]], 1.0f);
        }
    }
}