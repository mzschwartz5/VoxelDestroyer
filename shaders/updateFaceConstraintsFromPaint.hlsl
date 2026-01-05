#include "common.hlsl"
#include "constants.hlsli"
#include "faceconstraints_shared.hlsl"

RWBuffer<float> paintDeltas : register(u4);
RWBuffer<float> paintValues : register(u5);

// This entry point is for updating the face constraints based on paint values.
// One thread per face constraint (over three dispatches, one per axis - just because that's how the face constraint data is stored).
// This runs every time a brush stroke ends - that way, we can use the computed delta to know which faces were updated, and can 
// update the neighboring face in the constraint pair. (And it also means the simulation values are always in sync).
[numthreads(VGS_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID
)
{
    uint constraintIdx = globalThreadId.x;
    if (constraintIdx >= numConstraints) return;

    int voxelAIdx = faceConstraintsIndices[constraintIdx * 2];
    int voxelBIdx = faceConstraintsIndices[constraintIdx * 2 + 1];
    if (voxelAIdx == -1 || voxelBIdx == -1) return;

    int paintValueAIdx = voxelAIdx * 6 + faceAId;
    int paintValueBIdx = voxelBIdx * 6 + faceBId;

    // In theory, in a given brush stroke, the user can only paint one side of a face constraint. (If camera-based painting is off,
    // they can get both sides, but they would be the same value). So we take the first non-zero value (if either is non-zero), and set the constraint limits
    // based on that. And also mirror to the other paint delta in the pair so both sides are visually painted consistently.
    float paintDeltaA = paintDeltas[paintValueAIdx];
    float paintDeltaB = paintDeltas[paintValueBIdx];
    if (abs(paintDeltaA) < eps && abs(paintDeltaB) < eps) return;

    // Branch-free selection of the paint delta to use (pick B if non-zero, else A)
    float useB = abs(sign(paintDeltaB));
    float selectedDelta = paintDeltaA + useB * (paintDeltaB - paintDeltaA);
    int selectedIdx = paintValueAIdx + (int)(useB) * (paintValueBIdx - paintValueAIdx);
    float selectedPaintValue = paintValues[selectedIdx];

    if (paintDeltaA != paintDeltaB) {
        // Note: on undo/redos, the deltas are always the same because this block was already run before the deltas were first recorded.
        int nonSelectedIdx = paintValueBIdx + (int)(useB) * (paintValueAIdx - paintValueBIdx);
        paintValues[nonSelectedIdx] = selectedPaintValue;
        paintDeltas[nonSelectedIdx] = selectedDelta;
    }

    // For now, same paint value for both compression and tension
    float limit;
    if (selectedPaintValue < 0) {
        limit = FLT_MAX;
    } else {
        limit = lerp(constraintLow, constraintHigh, selectedPaintValue);
    }

    faceConstraintsLimits[constraintIdx * 2] = limit;
    faceConstraintsLimits[constraintIdx * 2 + 1] = -limit;
}