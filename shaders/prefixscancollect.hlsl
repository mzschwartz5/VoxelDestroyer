// Note that on subscans (scanning the partial sums of a previous scan), this first UAV will actually
// be a view into the previous scan's partial sums buffer. For readability, it's written as if it's always the primary invocation.
RWStructuredBuffer<uint> collisionCellParticleCounts : register(u0);
StructuredBuffer<uint> partialSums : register(t0);

[numthreads(PREFIX_SCAN_THREADS, 1, 1)]
void main(uint3 globalId : SV_DispatchThreadID, uint3 groupId : SV_GroupID)
{
    // No out-of-bounds check needed. (This also saves us having to send a constant buffer with the number of elements on each dispatch.)

    // Divide by two because this pass processes one element per thread,
    // while the scan step processed two elements per thread.
    // Subtract one because the partial sums are scanned inclusively. 
    int partialSumsIdx = (groupId.x / 2) - 1;
    if (partialSumsIdx < 0) return; // same as adding 0, which is what these threads would do if we had scanned the partial sums exclusively
    collisionCellParticleCounts[globalId.x] += partialSums[partialSumsIdx];
}