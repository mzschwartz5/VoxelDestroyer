#pragma once
#include <array>

/**
 * The 8 corners of a unit cube centered at the origin.
 * (Ordered in Morton-code order according to https://dl.acm.org/doi/pdf/10.1145/3677388.3696322)
 */
inline std::array<std::array<float, 3>, 8> cubeCorners = {{
    {{-0.5f, -0.5f, -0.5f}},
    {{ 0.5f, -0.5f, -0.5f}},
    {{-0.5f,  0.5f, -0.5f}},
    {{ 0.5f,  0.5f, -0.5f}},
    {{-0.5f, -0.5f,  0.5f}},
    {{ 0.5f, -0.5f,  0.5f}},
    {{-0.5f,  0.5f,  0.5f}},
    {{ 0.5f,  0.5f,  0.5f}}
}};

inline std::array<std::array<int, 2>, 12> cubeEdges = {{
    {{0, 1}}, {{1, 3}}, {{3, 2}}, {{2, 0}}, // bottom face
    {{4, 5}}, {{5, 7}}, {{7, 6}}, {{6, 4}}, // top face
    {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}}  // vertical edges
}};

inline std::array<std::array<int, 3>, 12> cubeFaces = {
    std::array<int, 3>{0, 4, 6}, std::array<int, 3>{0, 6, 2}, // Bottom
    std::array<int, 3>{1, 3, 7}, std::array<int, 3>{1, 7, 5}, // Top
    std::array<int, 3>{0, 1, 5}, std::array<int, 3>{0, 5, 4}, // Front
    std::array<int, 3>{4, 5, 7}, std::array<int, 3>{4, 7, 6}, // Right
    std::array<int, 3>{6, 7, 3}, std::array<int, 3>{6, 3, 2}, // Back
    std::array<int, 3>{2, 3, 1}, std::array<int, 3>{2, 1, 0}  // Left
};

inline constexpr std::array<float, 24> cubeCornersFlattened = {{
    -0.5f, -0.5f, -0.5f,
     0.5f, -0.5f, -0.5f,
    -0.5f,  0.5f, -0.5f,
     0.5f,  0.5f, -0.5f,
    -0.5f, -0.5f,  0.5f,
     0.5f, -0.5f,  0.5f,
    -0.5f,  0.5f,  0.5f,
     0.5f,  0.5f,  0.5f
}};

inline constexpr std::array<uint32_t, 24> cubeEdgesFlattened = {{
    0,1, 1,3, 3,2, 2,0,
    4,5, 5,7, 7,6, 6,4,
    0,4, 1,5, 2,6, 3,7
}};

inline constexpr std::array<uint32_t, 36> cubeFacesFlattened = {{
    0,4,6, 0,6,2, 1,3,7, 1,7,5,
    0,1,5, 0,5,4, 4,5,7, 4,7,6,
    6,7,3, 6,3,2, 2,3,1, 2,1,0
}};