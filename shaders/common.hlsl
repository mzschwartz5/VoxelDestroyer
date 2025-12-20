static const float eps = 1e-8f;

float2 unpackHalf2x16(float packed)
{
    uint lo = asuint(packed) & 0xFFFF;
    uint hi = (asuint(packed) >> 16) & 0xFFFF;
    return float2(f16tof32(lo), f16tof32(hi));
}

float updateMass(float radiusMassPacked, float newMass) {
    uint lo = asuint(radiusMassPacked) & 0xFFFFu;
    uint hiBits = (f32tof16(newMass) & 0xFFFFu) << 16;
    return asfloat(hiBits | lo);
}

bool massIsInfinite(float4 pos) {
    return unpackHalf2x16(pos.w).y == 0;
}