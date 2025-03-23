#pragma once
#include "glm/glm.hpp"
#include <vector>

struct Particle
{
    glm::vec3 position;
    glm::vec3 velocity;
    float mass;
};

class PBD
{
public:
    PBD() = default;
    PBD(const std::vector<glm::vec3>& positions);
    ~PBD() = default;
    const std::vector<Particle>& simulateStep();

private:
    std::vector<Particle> particles;
};