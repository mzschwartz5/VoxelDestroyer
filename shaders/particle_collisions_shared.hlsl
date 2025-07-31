cbuffer ParticleCollisionsConstantBuffer : register(b0)
{
    float inverseCellSize;
    uint hashGridSize;
    uint numParticles;    // typically the same as hashGridSize, but to be more robust, bind separately.
    float particleRadius; // TODO: when there are multiple meshes, this will vary and should be accessed via buffer 
};

int getParticleCellHash(int gridPosX, int gridPosY, int gridPosZ) {
    int hash = (gridPosX * 92837111) ^ (gridPosY * 689287499) ^ (gridPosZ * 283923481);
    return abs(hash) % hashGridSize;
}