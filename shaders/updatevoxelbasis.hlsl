struct VoxelBasis {
    float4 e0;
    float4 e1;
    float4 e2;
};

StructuredBuffer<float4> particles : register(t0); 
RWStructuredBuffer<VoxelBasis> voxelBases : register(u0); 

[numthreads(UPDATE_VOXEL_BASES_THEADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID)
{
    uint p0Idx = gId.x << 3;

    float4 p0 = particles[p0Idx];
    float4 p1 = particles[p0Idx + 1];
    float4 p2 = particles[p0Idx + 2];
    float4 p4 = particles[p0Idx + 4];

    VoxelBasis newBasis;
    newBasis.e0 = p1 - p0;
    newBasis.e1 = p2 - p0;
    newBasis.e2 = p4 - p0;

    voxelBases[gId.x] = newBasis;
}