RWStructuredBuffer<uint> collisionCellParticleCounts : register(u0);

[numthreads(PREFIX_SCAN_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID)
{
}