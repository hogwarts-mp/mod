#include <stdint.h>

enum class ESpawnActorCollisionHandlingMethod : uint8_t {
    /** Fall back to default settings. */
    Undefined,
    /** Actor will spawn in desired location, regardless of collisions. */
    AlwaysSpawn,
    /** Actor will try to find a nearby non-colliding location (based on shape components), but will always spawn even
       if one cannot be found. */
    AdjustIfPossibleButAlwaysSpawn,
    /** Actor will try to find a nearby non-colliding location (based on shape components), but will NOT spawn unless
       one is found. */
    AdjustIfPossibleButDontSpawnIfColliding,
    /** Actor will fail to spawn. */
    DontSpawnIfColliding,
};
