float2 unpackHalf2x16(float packed)
{
    uint lo = asuint(packed) & 0xFFFF;
    uint hi = (asuint(packed) >> 16) & 0xFFFF;
    return float2(f16tof32(lo), f16tof32(hi));
}

bool massIsInfinite(float4 pos) {
    return unpackHalf2x16(pos.w).y == 0;
}