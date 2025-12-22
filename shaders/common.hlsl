#include "constants.hlsli"
static const float eps = 1e-8f;

float2 unpackHalf2x16(uint packed)
{
    uint lo = packed & 0xFFFF;
    uint hi = (packed >> 16) & 0xFFFF;
    return float2(f16tof32(lo), f16tof32(hi));
}

uint updateMass(uint radiusMassPacked, float newMass) {
    uint lo = radiusMassPacked & 0xFFFFu;
    uint hiBits = (f32tof16(newMass) & 0xFFFFu) << 16;
    return hiBits | lo;
}

bool massIsInfinite(Particle pos) {
    return unpackHalf2x16(pos.radiusAndInvMass).y == 0;
}