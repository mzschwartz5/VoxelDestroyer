cbuffer ParticleCollisionsConstantBuffer : register(b0)
{
    float inverseCellSize;
    int hashGridSize;
    int numParticles; // typically the same as hashGridSize, but to be more robust, bind separately.
    int padding1;
};

int getParticleCellHash(float3 position) {
    int3 gridPosition = int3(floor(position * inverseCellSize));
    int hash = (gridPosition.x * 92837111) ^ (gridPosition.y * 689287499) ^ (gridPosition.z * 283923481);
    return abs(hash) % hashGridSize;
}