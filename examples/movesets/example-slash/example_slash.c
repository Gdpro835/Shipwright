#include "soh/MovesetAPI.h"
#include <stddef.h>
static const ShipMovesetHostApi* sHostApi = NULL;
#if defined(_WIN32)
#define SHIP_MOVESET_EXPORT __declspec(dllexport)
#else
#define SHIP_MOVESET_EXPORT __attribute__((visibility("default")))
#endif
SHIP_MOVESET_EXPORT int32_t ShipMoveset_Initialize(const ShipMovesetHostApi* hostApi, const char* packageId) {
    if (hostApi == NULL || packageId == NULL || hostApi->abiVersion != SHIP_MOVESET_ABI_VERSION ||
        hostApi->structSize < sizeof(ShipMovesetHostApi)) return 0;
    sHostApi = hostApi;
    return 1;
}
SHIP_MOVESET_EXPORT int32_t ShipMoveset_OnPlayerAction(const ShipMovesetPlayerState* player,
                                                       const ShipMovesetInputState* input,
                                                       ShipMovesetActionResult* result) {
    if (player == NULL || input == NULL || result == NULL || player->age != SHIP_MOVESET_AGE_ADULT ||
        (input->buttonsPressed & SHIP_MOVESET_BUTTON_B) == 0) return 0;
    result->flags |= SHIP_MOVESET_RESULT_SET_HITBOX_SCALE | SHIP_MOVESET_RESULT_SPAWN_EFFECT;
    result->hitboxScale = 1.15f;
    result->effectType = SHIP_MOVESET_EFFECT_SPARK;
    result->effectScale = 0.75f;
    if (sHostApi != NULL && sHostApi->setMeleeHitboxScale != NULL)
        sHostApi->setMeleeHitboxScale(result->hitboxScale);
    return 0;
}
SHIP_MOVESET_EXPORT void ShipMoveset_Shutdown(void) { sHostApi = NULL; }
