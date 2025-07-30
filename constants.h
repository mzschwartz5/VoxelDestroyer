#define TRANSFORM_VERTICES_THREADS 256
#define VGS_THREADS 256
#define BUILD_COLLISION_GRID_THREADS 256
#define BUILD_COLLISION_PARTICLE_THREADS 256
#define SOLVE_COLLISION_THREADS 128        // CAREFUL: this directly affects the amount of shared memory available to each collision cell.
#define THREADS_PER_COLLISION_CELL 2       // CAREFUL: this directly affects the amount of shared memory available to each collision cell.
#define THREADS_PER_COLLISION_CELL_INV (1.0f / (float)(THREADS_PER_COLLISION_CELL))
#define PREFIX_SCAN_THREADS 512  // This MUST be a power of two (many assumptions in the scan code rely on this).