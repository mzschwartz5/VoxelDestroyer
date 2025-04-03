#pragma once
#include "glm/glm.hpp"
#include <vector>
#include <array>

struct Particle
{
    glm::vec3 position;
    glm::vec3 oldPosition;
    glm::vec3 velocity;
    float w = 0.0f; // inverse mass
};

struct Tetrahedron
{
    std::array<int, 4> indices;
    float restVolume;
};

struct Edge
{
    std::array<int, 2> indices;
    float restLength;
};

class PBD
{
public:
    PBD() = default;
    PBD(const std::vector<glm::vec3>& positions, const std::vector<std::array<int, 4>>& tetrahedra, const std::vector<std::array<int, 2>>& edges);
    ~PBD() = default;
    const std::vector<Particle>& simulateStep();
    void simulateSubstep();

	std::vector<Particle> getParticles() const { return particles; }

private:
    std::vector<Particle> particles;
    std::vector<Tetrahedron> tetrahedra;
    std::vector<Edge> edges;
    int substeps = 10;
    float timeStep;

    float getTetVolume(const Tetrahedron& tet) const;
    float getEdgeLength(const Edge& edge) const;

    // Constraint solvers
    void solveGroundCollision();
    void solveDistanceConstraint();
    void solveVolumeConstraint();
};