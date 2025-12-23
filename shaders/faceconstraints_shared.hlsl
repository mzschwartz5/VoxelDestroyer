struct FaceConstraint {
    int voxelAIdx;
    int voxelBIdx;
    float tensionLimit;
    float compressionLimit;
};

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
RWStructuredBuffer<FaceConstraint> faceConstraints : register(u1);
RWStructuredBuffer<uint> isSurfaceVoxel : register(u2);
RWBuffer<float> paintDeltas : register(u3);
RWBuffer<float> paintValues : register(u4);
RWStructuredBuffer<Particle> renderParticles : register(u5);
static const float FLT_MAX = asfloat(0x7f7fffff);