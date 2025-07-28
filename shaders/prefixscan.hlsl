// Note that on subscans (scanning the partial sums of a previous scan), this first UAV will actually
// be a view into the previous scan's partial sums buffer. For readability, it's written as if it's always the primary invocation.
RWStructuredBuffer<uint> collisionCellParticleCounts : register(u0);
RWStructuredBuffer<uint> partialSums : register(u1);

#define LOG_NUM_BANKS 5
#define CONFLICT_FREE_OFFSET(threadIdx) ((threadIdx) >> LOG_NUM_BANKS)

// Two elements per thread, PREFIX_SCAN_THREADS threads per workgroup, four bytes per uint
// And then we add overflow for the conflict-free offset
groupshared int s_particleCounts[(2 * PREFIX_SCAN_THREADS + CONFLICT_FREE_OFFSET(2 * PREFIX_SCAN_THREADS - 1)) * 4];

[numthreads(PREFIX_SCAN_THREADS, 1, 1)]
void main(uint3 globalId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    // No out-of-bounds checks are necessary here by construction.
    // Also saves us from needing to send a new constant buffer with the number of elements each dispatch.

    // Pull array elements into shared memory
    uint originalLeftValue = collisionCellParticleCounts[2 * globalId.x];
    s_particleCounts[2 * groupThreadId.x + CONFLICT_FREE_OFFSET(2 * groupThreadId.x)] = originalLeftValue;

    uint originalRightValue =  collisionCellParticleCounts[2 * globalId.x + 1];
    s_particleCounts[(2 * groupThreadId.x + 1) + CONFLICT_FREE_OFFSET(2 * groupThreadId.x + 1)] = originalRightValue;

    // Upsweep phase (structured for optimal thread-level parallelism)
    int numLevels = 0;
    for (int stride = PREFIX_SCAN_THREADS; stride > 0; stride >>= 1) {
        numLevels++;
        GroupMemoryBarrierWithGroupSync();
        if (groupThreadId.x < stride) {
            s_particleCounts[groupThreadId.x + stride] = s_particleCounts[groupThreadId.x];
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // Save off the last entry of s_particleCounts to a sums buffer. If the buffer we're scanning is
    // too large to scan in one workgroup (so, basically always), we need to do a collect step where
    // we add the partial sums back to the input buffer.
    // Also, use this opportunity to zero out the last entry of s_particleCounts for the next phase (downsweep).
    if (groupThreadId.x == 0) {
        int lastEntryIdx = 2 * PREFIX_SCAN_THREADS - 1;
        partialSums[groupId.x] = s_particleCounts[lastEntryIdx + CONFLICT_FREE_OFFSET(lastEntryIdx)];
        s_particleCounts[lastEntryIdx + CONFLICT_FREE_OFFSET(lastEntryIdx)] = 0;
    }

    // Downsweep phase (as far as I know, this can't be restructured for good thread-level parallelism)
    for (int stride = numLevels - 1; stride >= 1; --stride)
    {
        GroupMemoryBarrierWithGroupSync();

        int twoToTheStridePlusOne = 1 << (stride + 1);
        int twoToTheStride = 1 << stride;
        int rightChildIdx = groupThreadId.x * twoToTheStridePlusOne + twoToTheStridePlusOne - 1;
        int leftChildIdx = groupThreadId.x * twoToTheStridePlusOne + twoToTheStride - 1;

        if (rightChildIdx < 2 * PREFIX_SCAN_THREADS) {
            int leftVal = s_particleCounts[leftChildIdx + CONFLICT_FREE_OFFSET(leftChildIdx)];
            s_particleCounts[leftChildIdx + CONFLICT_FREE_OFFSET(leftChildIdx)]
                = s_particleCounts[rightChildIdx + CONFLICT_FREE_OFFSET(rightChildIdx)];

            s_particleCounts[rightChildIdx + CONFLICT_FREE_OFFSET(rightChildIdx)] += leftVal;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // On the last iteration, stride = 0, write to global memory.
    // By adding back the original values, we convert the scan into an inclusive one.
    collisionCellParticleCounts[2 * globalId.x]
        = s_particleCounts[2 * groupThreadId.x + 1 + CONFLICT_FREE_OFFSET(2 * groupThreadId.x + 1)]
        + originalLeftValue;

    collisionCellParticleCounts[2 * globalId.x + 1]
        = s_particleCounts[2 * groupThreadId.x + CONFLICT_FREE_OFFSET(2 * groupThreadId.x)]
        + s_particleCounts[(2 * groupThreadId.x + 1) + CONFLICT_FREE_OFFSET(2 * groupThreadId.x + 1)]
        + originalRightValue;
}