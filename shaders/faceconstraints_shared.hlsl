cbuffer VGSConstantsBuffer : register(b0)
{
    VGSConstants vgsConstants;
};

cbuffer FaceConstraintsCB : register(b1)
{
    uint4 faceAParticles; // Which particles of voxel A are involved in the face constraint
    uint4 faceBParticles; // Which particles of voxel B are involved in the face constraint
    uint numConstraints;
    int faceAId;          // Which face index this constraint corresponds to on voxel A (only used for paint value lookup)
    int faceBId;          // Which face index this constraint corresponds to on voxel B (only used for paint value lookup)
    float constraintLow;
    float constraintHigh;
    int padding0;
    int padding1;
    int padding2;
};

RWStructuredBuffer<Particle> particles : register(u0);
RWStructuredBuffer<int> faceConstraintsIndices : register(u1);
RWStructuredBuffer<float> faceConstraintsLimits : register(u2);
RWStructuredBuffer<uint> isSurfaceVoxel : register(u3);

static const float FLT_MAX = asfloat(0x7f7fffff);