#include "common.hlsl"
#include "prevgs_shared.hlsl"

// Used for updating particle (inverse) masses based on paint values
// Users paint particle _mass_, so we also need to convert to inverse mass here, 
// treating negative paint values (indicating infinite mass) as 0 inverse mass (the convention for infinite mass in the simulation)
// One thread per particle.
[numthreads(VGS_THREADS, 1, 1)]
void main(uint3 gId : SV_DispatchThreadID) {
    if (gId.x >= preVgsConstants.numParticles) return;

    float paintDelta = paintDeltas[gId.x];
    if (abs(paintDelta) < eps) return;

    float paintValue = paintValues[gId.x];
    float packedRadiusAndInvMass = particles[gId.x].radiusAndInvMass;

    if (paintValue < 0.0f) { 
        // Infinite mass case.
        packedRadiusAndInvMass = updateMass(packedRadiusAndInvMass, 0.0f);
    } else {
        // Note: we prevent the user from setting a mass lower limit of 0, so no need to check for division by zero here.
        float mass = lerp(preVgsConstants.massLow, preVgsConstants.massHigh, paintValue);
        float invMass = 1.0f / mass;
        packedRadiusAndInvMass = updateMass(packedRadiusAndInvMass, invMass);
    }

    particles[gId.x].radiusAndInvMass = packedRadiusAndInvMass;
    oldParticles[gId.x].radiusAndInvMass = packedRadiusAndInvMass;
}