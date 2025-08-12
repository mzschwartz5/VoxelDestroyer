cbuffer ParticleCollisionsConstantBuffer : register(b0)
{
    float inverseCellSize;
    uint hashGridSize;
    uint numParticles;    // typically the same as hashGridSize, but to be more robust, bind separately.
    uint padding;
};

int getParticleCellHash(int gridPosX, int gridPosY, int gridPosZ) {
    int hash = (gridPosX * 92837111) ^ (gridPosY * 689287499) ^ (gridPosZ * 283923481);
    return abs(hash) % hashGridSize;
}