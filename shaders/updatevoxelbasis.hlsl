
struct VoxelBasis {
    float4 e0;
    float4 e1;
    float4 e2;
};

StructuredBuffer<float4> particles : register(t0); 
RWStructuredBuffer<VoxelBasis> voxelBases : register(u0); 

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
}