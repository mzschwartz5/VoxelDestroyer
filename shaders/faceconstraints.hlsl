
// Face constraint structure
struct FaceConstraint {
    int voxelOneIdx;
    int voxelTwoIdx;
    float tensionLimit;
    float compressionLimit;
};

cbuffer VoxelSimBuffer : register(b0)
{
    float RELAXATION;
    float BETA;
    float PARTICLE_RADIUS;
    float VOXEL_REST_VOLUME;
    float ITER_COUNT;
    float AXIS;
    float FTF_RELAXATION;
    float FTF_BETA;
};

RWStructuredBuffer<float4> positions : register(u0);
RWStructuredBuffer<FaceConstraint> xConstraints : register(u1);
RWStructuredBuffer<FaceConstraint> yConstraints : register(u2);
RWStructuredBuffer<FaceConstraint> zConstraints : register(u3);
StructuredBuffer<float> weights : register(t0);

float3 project(float3 u, float3 v)
{
    const float eps = 1e-12f;
    return dot(v, u) / (dot(u, u) + eps) * u;
}

void breakConstraint(int constraintIdx) {
    if (AXIS == 0) {
        xConstraints[constraintIdx].voxelOneIdx = -1;
        xConstraints[constraintIdx].voxelTwoIdx = -1;
    }
    else if (AXIS == 1) {
        yConstraints[constraintIdx].voxelOneIdx = -1;
        yConstraints[constraintIdx].voxelTwoIdx = -1;
    }
    else if (AXIS == 2) {
        zConstraints[constraintIdx].voxelOneIdx = -1;
        zConstraints[constraintIdx].voxelTwoIdx = -1;
    }
}

[numthreads(VGS_THREADS, 1, 1)]
void main(
    uint3 globalThreadId : SV_DispatchThreadID,
    uint3 groupId : SV_GroupID,
    uint3 localThreadId : SV_GroupThreadID
)
{
    uint constraintIdx = globalThreadId.x;

    // Get the constraint data from the buffer
    FaceConstraint constraint;
	if (AXIS == 0) {
        constraint = xConstraints[constraintIdx];
	}
	else if (AXIS == 1) {
		constraint = yConstraints[constraintIdx];
	}
	else {
		constraint = zConstraints[constraintIdx];
	}
    
    int voxelOneIdx = constraint.voxelOneIdx;
    int voxelTwoIdx = constraint.voxelTwoIdx;

    if (voxelOneIdx == -1 || voxelTwoIdx == -1)
    {
        return;
    }

    uint voxelOneStartIdx = voxelOneIdx << 3;
    uint voxelTwoStartIdx = voxelTwoIdx << 3;

    // Arrays to store face indices
    uint faceOneIndices[4];
    uint faceTwoIndices[4];

    // Define face indices based on axis
    if (AXIS == 0) // x-axis
    {
        faceOneIndices[0] = 1; faceOneIndices[1] = 3; faceOneIndices[2] = 5; faceOneIndices[3] = 7;
        faceTwoIndices[0] = 0; faceTwoIndices[1] = 2; faceTwoIndices[2] = 4; faceTwoIndices[3] = 6;
    }
    else if (AXIS == 1) // y-axis
    {
        faceOneIndices[0] = 2; faceOneIndices[1] = 3; faceOneIndices[2] = 6; faceOneIndices[3] = 7;
        faceTwoIndices[0] = 0; faceTwoIndices[1] = 1; faceTwoIndices[2] = 4; faceTwoIndices[3] = 5;
    }
    else // z-axis
    {
        faceOneIndices[0] = 4; faceOneIndices[1] = 5; faceOneIndices[2] = 6; faceOneIndices[3] = 7;
        faceTwoIndices[0] = 0; faceTwoIndices[1] = 1; faceTwoIndices[2] = 2; faceTwoIndices[3] = 3;
    }

    // Calculate voxel centers
    float3 v1Center = (
        positions[voxelOneStartIdx + 0].xyz +
        positions[voxelOneStartIdx + 1].xyz +
        positions[voxelOneStartIdx + 2].xyz +
        positions[voxelOneStartIdx + 3].xyz +
        positions[voxelOneStartIdx + 4].xyz +
        positions[voxelOneStartIdx + 5].xyz +
        positions[voxelOneStartIdx + 6].xyz +
        positions[voxelOneStartIdx + 7].xyz
        ) * 0.125f;

    float3 v2Center = (
        positions[voxelTwoStartIdx + 0].xyz +
        positions[voxelTwoStartIdx + 1].xyz +
        positions[voxelTwoStartIdx + 2].xyz +
        positions[voxelTwoStartIdx + 3].xyz +
        positions[voxelTwoStartIdx + 4].xyz +
        positions[voxelTwoStartIdx + 5].xyz +
        positions[voxelTwoStartIdx + 6].xyz +
        positions[voxelTwoStartIdx + 7].xyz
        ) * 0.125f;

    // Calculate midpoint positions
    float3 v1MidPositions[8];
    float3 v2MidPositions[8];

    for (int i = 0; i < 8; i++)
    {
        v1MidPositions[i] = (positions[voxelOneStartIdx + i].xyz + v1Center) * 0.5f;
        v2MidPositions[i] = (positions[voxelTwoStartIdx + i].xyz + v2Center) * 0.5f;
    }

    // Get face corners and weights
    float3 faceOne[4];
    float3 faceTwo[4];
    float faceOneW[4];
    float faceTwoW[4];

    for (int i = 0; i < 4; i++)
    {
        faceOne[i] = v1MidPositions[faceOneIndices[i]];
        faceTwo[i] = v2MidPositions[faceTwoIndices[i]];
        faceOneW[i] = weights[voxelOneStartIdx + faceOneIndices[i]];
        faceTwoW[i] = weights[voxelTwoStartIdx + faceTwoIndices[i]];
    }

    // Check if constraint should be broken due to tension/compression
    bool breakConstraint = false;
    for (int i = 0; i < 4; i++)
    {
        float3 u = faceTwo[i] - faceOne[i];
        float L = length(u);
        float strain = (L - 2.0f * PARTICLE_RADIUS) / (2.0f * PARTICLE_RADIUS);

        // Assuming tension/compression limits, adjust based on your actual values
        float tensionLimit = constraint.tensionLimit;
        float compressionLimit = constraint.compressionLimit;

        if (strain > tensionLimit || strain < compressionLimit)
        {
            breakConstraint = true;
            break;
        }
    }

    if (breakConstraint)
    {
        //THIS IS BROKEN
        //breakConstraint(constraintIdx);
        return;
    }

    // Calculate midpoint face center
    float3 centerOfVoxels = float3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; i++)
    {
        centerOfVoxels += faceOne[i] + faceTwo[i];
    }
    centerOfVoxels *= 0.125f;

    // Edge vectors for shape preservation
    float3 dp[3];

    // Check if either voxel is static
    float sumW1 = faceOneW[0] + faceOneW[1] + faceOneW[2] + faceOneW[3];
    float sumW2 = faceTwoW[0] + faceTwoW[1] + faceTwoW[2] + faceTwoW[3];

    if (sumW1 == 0.0f || sumW2 == 0.0f)
    {
        // Handle static voxel case
        float3 u1, u2, u0;

        if (sumW1 == 0.0f)
        {
            // Voxel 1 is static
            u1 = (faceOne[1] - faceOne[0]) + (faceOne[3] - faceOne[2]);
            u2 = (faceOne[2] - faceOne[0]) + (faceOne[3] - faceOne[1]);
        }
        else
        {
            // Voxel 2 is static
            u1 = (faceTwo[1] - faceTwo[0]) + (faceTwo[3] - faceTwo[2]);
            u2 = (faceTwo[2] - faceTwo[0]) + (faceTwo[3] - faceTwo[1]);
        }

        // Calculate normal vector based on axis
        if (AXIS == 0 || AXIS == 2)
        {
            u0 = cross(u1, u2);
        }
        else
        {
            u0 = cross(u2, u1);
        }

        // Normalize and scale by radius
        dp[0] = normalize(u1) * PARTICLE_RADIUS;
        dp[1] = normalize(u2) * PARTICLE_RADIUS;
        dp[2] = normalize(u0) * PARTICLE_RADIUS;

        // Check if volume is negative
        float V = dot(cross(dp[0], dp[1]), dp[2]);
        if (V < 0.0f)
        {
            // Break constraint due to volume inversion
            // THIS IS BROKEN
            //breakConstraint(constraintIdx);
            return;
        }

        // Save original midpoint positions
        float3 origFaceOne[4];
        float3 origFaceTwo[4];
        for (int i = 0; i < 4; i++)
        {
            origFaceOne[i] = faceOne[i];
            origFaceTwo[i] = faceTwo[i];
        }

        // Update midpoint positions
        if (faceOneW[0] != 0.0f) faceOne[0] = centerOfVoxels - dp[0] - dp[1] - dp[2];
        if (faceOneW[1] != 0.0f) faceOne[1] = centerOfVoxels + dp[0] - dp[1] - dp[2];
        if (faceOneW[2] != 0.0f) faceOne[2] = centerOfVoxels - dp[0] + dp[1] - dp[2];
        if (faceOneW[3] != 0.0f) faceOne[3] = centerOfVoxels + dp[0] + dp[1] - dp[2];
        if (faceTwoW[0] != 0.0f) faceTwo[0] = centerOfVoxels - dp[0] - dp[1] + dp[2];
        if (faceTwoW[1] != 0.0f) faceTwo[1] = centerOfVoxels + dp[0] - dp[1] + dp[2];
        if (faceTwoW[2] != 0.0f) faceTwo[2] = centerOfVoxels - dp[0] + dp[1] + dp[2];
        if (faceTwoW[3] != 0.0f) faceTwo[3] = centerOfVoxels + dp[0] + dp[1] + dp[2];

        // Apply delta from midpoint positions back to particle positions
        for (int i = 0; i < 4; i++)
        {
            if (faceOneW[i] != 0.0f)
            {
                float3 delta = faceOne[i] - origFaceOne[i];
                positions[voxelOneStartIdx + faceOneIndices[i]].xyz += delta;
            }

            if (faceTwoW[i] != 0.0f)
            {
                float3 delta = faceTwo[i] - origFaceTwo[i];
                positions[voxelTwoStartIdx + faceTwoIndices[i]].xyz += delta;
            }
        }
    }
    else
    {
        // No static voxels - apply iterative shape preservation

        for (int iter = 0; iter < ITER_COUNT; iter++)
        {
            // Calculate edge vectors
            dp[0] = (faceTwo[0] - faceOne[0]) + (faceTwo[1] - faceOne[1]) +
                (faceTwo[2] - faceOne[2]) + (faceTwo[3] - faceOne[3]);

            dp[1] = (faceOne[1] - faceOne[0]) + (faceOne[3] - faceOne[2]) +
                (faceTwo[1] - faceTwo[0]) + (faceTwo[3] - faceTwo[2]);

            dp[2] = (faceOne[2] - faceOne[0]) + (faceOne[3] - faceOne[1]) +
                (faceTwo[2] - faceTwo[0]) + (faceTwo[3] - faceTwo[1]);

            // Recalculate center
            centerOfVoxels = float3(0.0f, 0.0f, 0.0f);
            for (int i = 0; i < 4; i++)
            {
                centerOfVoxels += faceOne[i] + faceTwo[i];
            }
            centerOfVoxels *= 0.125f;

            // Apply orthogonalization
            float3 u0 = dp[0] - FTF_RELAXATION * (project(dp[1], dp[0]) + project(dp[2], dp[0]));
            float3 u1 = dp[1] - FTF_RELAXATION * (project(dp[0], dp[1]) + project(dp[2], dp[1]));
            float3 u2 = dp[2] - FTF_RELAXATION * (project(dp[0], dp[2]) + project(dp[1], dp[2]));

            // Check for flipping
            float V = dot(cross(u0, u1), u2);
            if (AXIS == 1) V = -V; // Hack from GPU code

            if (V < 0.0f)
            {
                // Break constraint due to flipping
                //breakConstraint(constraintIdx);
                return;
            }

            // Calculate normalized and scaled edge vectors
            float3 lenu = float3(length(u0), length(u1), length(u2)) + float3(1e-12f, 1e-12f, 1e-12f);
            float3 lenp = float3(length(dp[0]), length(dp[1]), length(dp[2])) + float3(1e-12f, 1e-12f, 1e-12f);

            float r_v = pow(PARTICLE_RADIUS * PARTICLE_RADIUS * PARTICLE_RADIUS / (lenp.x * lenp.y * lenp.z), 0.3333f);

            // Scale change in position based on beta
            dp[0] = u0 / lenu.x * lerp(PARTICLE_RADIUS, lenp.x * r_v, FTF_BETA);
            dp[1] = u1 / lenu.y * lerp(PARTICLE_RADIUS, lenp.y * r_v, FTF_BETA);
            dp[2] = u2 / lenu.z * lerp(PARTICLE_RADIUS, lenp.z * r_v, FTF_BETA);

            // Save original midpoint positions
            float3 origFaceOne[4];
            float3 origFaceTwo[4];
            for (int i = 0; i < 4; i++)
            {
                origFaceOne[i] = faceOne[i];
                origFaceTwo[i] = faceTwo[i];
            }

            // Update midpoint positions
            if (faceOneW[0] != 0.0f) faceOne[0] = centerOfVoxels - dp[0] - dp[1] - dp[2];
            if (faceOneW[1] != 0.0f) faceOne[1] = centerOfVoxels + dp[0] - dp[1] - dp[2];
            if (faceOneW[2] != 0.0f) faceOne[2] = centerOfVoxels - dp[0] + dp[1] - dp[2];
            if (faceOneW[3] != 0.0f) faceOne[3] = centerOfVoxels + dp[0] + dp[1] - dp[2];
            if (faceTwoW[0] != 0.0f) faceTwo[0] = centerOfVoxels - dp[0] - dp[1] + dp[2];
            if (faceTwoW[1] != 0.0f) faceTwo[1] = centerOfVoxels + dp[0] - dp[1] + dp[2];
            if (faceTwoW[2] != 0.0f) faceTwo[2] = centerOfVoxels - dp[0] + dp[1] + dp[2];
            if (faceTwoW[3] != 0.0f) faceTwo[3] = centerOfVoxels + dp[0] + dp[1] + dp[2];

            // Apply delta from midpoint positions back to particle positions
            for (int i = 0; i < 4; i++)
            {
                if (faceOneW[i] != 0.0f)
                {
                    float3 delta = faceOne[i] - origFaceOne[i];
                    positions[voxelOneStartIdx + faceOneIndices[i]].xyz += delta;
                }

                if (faceTwoW[i] != 0.0f)
                {
                    float3 delta = faceTwo[i] - origFaceTwo[i];
                    positions[voxelTwoStartIdx + faceTwoIndices[i]].xyz += delta;
                }
            }
        }
    }
}