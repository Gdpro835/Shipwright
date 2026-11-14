#ifndef SOH_MOVESET_API_H
#define SOH_MOVESET_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SHIP_MOVESET_ABI_VERSION 1u
#define SHIP_MOVESET_AGE_CHILD (1u << 0)
#define SHIP_MOVESET_AGE_ADULT (1u << 1)
#define SHIP_MOVESET_AGE_BOTH (SHIP_MOVESET_AGE_CHILD | SHIP_MOVESET_AGE_ADULT)
#define SHIP_MOVESET_BUTTON_A 0x8000u
#define SHIP_MOVESET_BUTTON_B 0x4000u
#define SHIP_MOVESET_BUTTON_Z 0x2000u
#define SHIP_MOVESET_BUTTON_START 0x1000u
#define SHIP_MOVESET_BUTTON_D_UP 0x0800u
#define SHIP_MOVESET_BUTTON_D_DOWN 0x0400u
#define SHIP_MOVESET_BUTTON_D_LEFT 0x0200u
#define SHIP_MOVESET_BUTTON_D_RIGHT 0x0100u
#define SHIP_MOVESET_BUTTON_L 0x0020u
#define SHIP_MOVESET_BUTTON_R 0x0010u
#define SHIP_MOVESET_BUTTON_C_UP 0x0008u
#define SHIP_MOVESET_BUTTON_C_DOWN 0x0004u
#define SHIP_MOVESET_BUTTON_C_LEFT 0x0002u
#define SHIP_MOVESET_BUTTON_C_RIGHT 0x0001u
typedef enum ShipMovesetResultFlags {
    SHIP_MOVESET_RESULT_CONSUME_ACTION = 1u << 0,
    SHIP_MOVESET_RESULT_SET_LINEAR_VELOCITY = 1u << 1,
    SHIP_MOVESET_RESULT_SET_VERTICAL_VELOCITY = 1u << 2,
    SHIP_MOVESET_RESULT_PLAY_ANIMATION = 1u << 3,
    SHIP_MOVESET_RESULT_LOOP_ANIMATION = 1u << 4,
    SHIP_MOVESET_RESULT_PLAY_SOUND = 1u << 5,
    SHIP_MOVESET_RESULT_SPAWN_EFFECT = 1u << 6,
    SHIP_MOVESET_RESULT_SET_HITBOX_SCALE = 1u << 7,
} ShipMovesetResultFlags;
typedef enum ShipMovesetEffectType { SHIP_MOVESET_EFFECT_DUST = 1, SHIP_MOVESET_EFFECT_SPARK = 2 } ShipMovesetEffectType;
typedef struct ShipMovesetPlayerState {
    uint32_t structSize, age, stateFlags1, stateFlags2;
    int32_t heldItemAction, currentBoots, meleeWeaponState;
    uint32_t isGrounded;
    float position[3], velocity[3], linearVelocity;
    int16_t yaw;
    uint16_t reserved;
    void* playState;
    void* player;
} ShipMovesetPlayerState;
typedef struct ShipMovesetInputState {
    uint32_t structSize;
    uint16_t buttonsHeld, buttonsPressed;
    int16_t stickX, stickY;
    uint16_t reserved;
} ShipMovesetInputState;
typedef struct ShipMovesetActionResult {
    uint32_t structSize, flags;
    float linearVelocity, verticalVelocity, hitboxScale, animationSpeed;
    const char* animationResource;
    uint16_t soundId, reserved;
    uint32_t effectType;
    float effectScale;
} ShipMovesetActionResult;
typedef struct ShipMovesetHostApi {
    uint32_t abiVersion, structSize;
    void (*playAnimation)(void*, void*, const char*, float, uint32_t);
    void (*playSound)(void*, uint16_t);
    void (*spawnEffect)(void*, void*, uint32_t, float);
    void (*setMeleeHitboxScale)(float);
} ShipMovesetHostApi;
typedef int32_t (*ShipMovesetInitializeFn)(const ShipMovesetHostApi*, const char*);
typedef int32_t (*ShipMovesetOnPlayerActionFn)(const ShipMovesetPlayerState*, const ShipMovesetInputState*, ShipMovesetActionResult*);
typedef void (*ShipMovesetShutdownFn)(void);
#ifdef __cplusplus
}
#endif
#endif
