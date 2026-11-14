#pragma once
#include "soh/MovesetAPI.h"
#include "z64.h"
#ifdef __cplusplus
#include <map>
namespace MovesetManager {
void Initialize();
void Shutdown();
const std::map<int32_t, const char*>& GetAvailableMovesetOptions(uint32_t ageMask);
} // namespace MovesetManager
extern "C" {
#endif
int32_t MovesetManager_IsModern(const Player* player);
int32_t MovesetManager_HandlePlayerAction(PlayState* play, Player* player, Input* input);
void MovesetManager_EndPlayerFrame(PlayState* play, Player* player);
void MovesetManager_CancelPlayerAction(Player* player);
float MovesetManager_GetMeleeHitboxScale(const Player* player);
int32_t MovesetManager_GetDashCooldown(const Player* player);
void MovesetManager_SetDashCooldown(Player* player, int32_t frames);
void MovesetManager_SpawnEffect(PlayState* play, Player* player, uint32_t effectType, float scale);
#ifdef __cplusplus
}
#endif
