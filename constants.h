#define DEFORM_VERTICES_THREADS 256
#define VGS_THREADS 256
#define BUILD_COLLISION_GRID_THREADS 256
#define BUILD_COLLISION_PARTICLE_THREADS 256
#define SOLVE_COLLISION_THREADS 32        // CAREFUL: this directly affects the amount of shared memory available to each collision cell.
#define PREFIX_SCAN_THREADS 512  // This MUST be a power of two (many assumptions in the scan code rely on this).
#define MAX_COLLIDERS 256
#define PAINT_SELECTION_TECHNIQUE_NAME "PaintSelection"
#define PAINT_POSITION "PAINT_POSITION"
#define PAINT_RADIUS "PAINT_RADIUS"
#define PAINT_VALUE "PAINT_VALUE"
#define PAINT_MODE "PAINT_MODE"
#define LOW_COLOR "LOW_COLOR"
#define HIGH_COLOR "HIGH_COLOR"
#define COMPONENT_MASK "COMPONENT_MASK"
#define PARTICLE_RADIUS "PARTICLE_RADIUS"