#pragma once

namespace Utils {

// From: https://github.com/liamdon/fast-morton/blob/main/src/3d/mb/decode.ts
const uint32_t magicBitsMask3DDecode[] = {
    0x00000000, 0x000003ff, 0x000300ff, 0x0300f00f, 0x30c30c3, 0x9249249
};

uint32_t morton3DGetThirdBits(uint32_t coord) {
    uint32_t x = coord & magicBitsMask3DDecode[5];
    x = (x ^ (x >> 2)) & magicBitsMask3DDecode[4];
    x = (x ^ (x >> 4)) & magicBitsMask3DDecode[3];
    x = (x ^ (x >> 8)) & magicBitsMask3DDecode[2];
    x = (x ^ (x >> 16)) & magicBitsMask3DDecode[1];
    return x;
}

uint32_t toMortonCode(uint32_t x, uint32_t y, uint32_t z) {
    auto spreadBits = [](uint32_t value) -> uint32_t {
        value = (value | (value << 16)) & 0x030000FF;
        value = (value | (value << 8)) & 0x0300F00F;
        value = (value | (value << 4)) & 0x030C30C3;
        value = (value | (value << 2)) & 0x09249249;
        return value;
    };

    uint32_t xBits = spreadBits(x);
    uint32_t yBits = spreadBits(y) << 1;
    uint32_t zBits = spreadBits(z) << 2;

    return xBits | yBits | zBits;
}

void fromMortonCode(uint32_t mortonCode, uint32_t& x, uint32_t& y, uint32_t& z) {
    auto compactBits = [](uint32_t value) -> uint32_t {
        value &= 0x09249249;
        value = (value ^ (value >> 2)) & 0x030C30C3;
        value = (value ^ (value >> 4)) & 0x0300F00F;
        value = (value ^ (value >> 8)) & 0x030000FF;
        value = (value ^ (value >> 16)) & 0x000003FF;
        return value;
    };

    x = compactBits(mortonCode);
    y = compactBits(mortonCode >> 1);
    z = compactBits(mortonCode >> 2);
}

}