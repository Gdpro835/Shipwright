#include "MovesetManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>
#include <nlohmann/json.hpp>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <spdlog/spdlog.h>

#include "soh/ResourceManagerHelpers.h"
#include "soh/cvar_prefixes.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
}

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(_WIN32)
#include <Windows.h>
#elif !defined(__SWITCH__) && !defined(__WIIU__) && !defined(__IOS__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#include <dlfcn.h>
#endif

namespace MovesetManager {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr int32_t kVanilla = 0;
constexpr int32_t kModern = 1;
constexpr float kMinHitboxScale = 0.1f;
constexpr float kMaxHitboxScale = 4.0f;
constexpr size_t kMaxAnimationPathLength = 512;
#if defined(__SWITCH__) || defined(__WIIU__) || defined(__IOS__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#define SHIP_MOVESET_HAS_NATIVE_PLUGINS 0
#else
#define SHIP_MOVESET_HAS_NATIVE_PLUGINS 1
#endif

struct Package {
    int32_t optionId = 0;
    std::string id;
    std::string name;
    fs::path library;
    fs::path archive;
    uint32_t ages = SHIP_MOVESET_AGE_BOTH;
};
struct Runtime {
    Player* player = nullptr;
    uint32_t lastFrame = UINT32_MAX;
    int32_t dashCooldown = 0;
    float hitboxScale = 1.0f;
    bool framePrepared = false;
    bool consumeAction = false;
    ShipMovesetActionResult result{};
    std::string animation;
};
#if SHIP_MOVESET_HAS_NATIVE_PLUGINS
#if defined(_WIN32)
using Library = HMODULE;
#else
using Library = void*;
#endif
struct LoadedPlugin {
    Library library = nullptr;
    ShipMovesetOnPlayerActionFn action = nullptr;
    ShipMovesetShutdownFn shutdown = nullptr;
};
std::map<int32_t, LoadedPlugin> sLoaded;
std::set<int32_t> sFailed;
#endif
std::map<int32_t, Package> sPackages;
std::map<int32_t, const char*> sAdultOptions, sChildOptions;
std::list<std::string> sLabels;
std::set<fs::path> sMountedArchives;
Runtime sRuntime;
bool sScanned = false;
bool sInitialized = false;

bool IsValidId(const std::string& id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}
bool IsWithin(const fs::path& root, const fs::path& file) {
    const fs::path relative = file.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
bool ResolveFile(const fs::path& package, const std::string& relative, fs::path& result) {
    if (relative.empty() || fs::path(relative).is_absolute())
        return false;
    std::error_code ec;
    const fs::path canonicalPackage = fs::canonical(package, ec);
    if (ec)
        return false;
    const fs::path candidate = fs::canonical(package / relative, ec);
    if (ec || !IsWithin(canonicalPackage, candidate) || !fs::is_regular_file(candidate))
        return false;
    result = candidate;
    return true;
}
std::string PlatformKey() {
#if defined(_WIN32)
    return "windows";
#elif defined(__ANDROID__)
#if defined(__aarch64__)
    return "android-arm64";
#elif defined(__arm__)
    return "android-arm32";
#elif defined(__x86_64__)
    return "android-x86_64";
#elif defined(__i386__)
    return "android-x86";
#else
    return "android";
#endif
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "default";
#endif
}
std::string LibraryPath(const Json& manifest) {
    if (manifest.contains("libraries") && manifest["libraries"].is_object()) {
        const Json& libraries = manifest["libraries"];
        const std::string key = PlatformKey();
        if (libraries.contains(key) && libraries[key].is_string())
            return libraries[key].get<std::string>();
        if (libraries.contains("default") && libraries["default"].is_string())
            return libraries["default"].get<std::string>();
    }
    if (manifest.contains("library") && manifest["library"].is_string())
        return manifest["library"].get<std::string>();
    return {};
}
uint32_t AgeMask(const Json& manifest) {
    if (!manifest.contains("ages"))
        return SHIP_MOVESET_AGE_BOTH;
    if (!manifest["ages"].is_array())
        return 0;
    uint32_t mask = 0;
    for (const Json& age : manifest["ages"]) {
        if (!age.is_string())
            continue;
        const std::string value = age.get<std::string>();
        if (value == "adult")
            mask |= SHIP_MOVESET_AGE_ADULT;
        if (value == "child")
            mask |= SHIP_MOVESET_AGE_CHILD;
    }
    return mask;
}
int32_t StableId(const std::string& id, const std::set<int32_t>& used) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : id) {
        hash ^= c;
        hash *= 16777619u;
    }
    int32_t value = static_cast<int32_t>(hash & 0x7fffffffu);
    if (value < 2)
        value += 2;
    while (used.contains(value))
        value = value == INT32_MAX ? 2 : value + 1;
    return value;
}
fs::path MovesetRoot() {
    const fs::path userRoot = fs::path(Ship::Context::GetPathRelativeToAppDirectory("mods", "soh")) / "movesets";
    std::error_code ec;
    fs::create_directories(userRoot, ec);
    if (!ec && fs::is_directory(userRoot, ec) && !ec)
        return userRoot;

    ec.clear();
    const std::string bundleMods = Ship::Context::GetPathRelativeToAppBundle("mods");
    if (!bundleMods.empty()) {
        const fs::path bundleRoot = fs::path(bundleMods) / "movesets";
        if (fs::is_directory(bundleRoot, ec) && !ec)
            return bundleRoot;
    }
    return userRoot;
}
void ScanPackages() {
    sPackages.clear();
    sAdultOptions.clear();
    sChildOptions.clear();
    sLabels.clear();
    sLabels.emplace_back("Vanilla");
    sAdultOptions.emplace(kVanilla, sLabels.back().c_str());
    sChildOptions.emplace(kVanilla, sLabels.back().c_str());
    sLabels.emplace_back("Modern");
    sAdultOptions.emplace(kModern, sLabels.back().c_str());
    sChildOptions.emplace(kModern, sLabels.back().c_str());
#if SHIP_MOVESET_HAS_NATIVE_PLUGINS
    std::error_code ec;
    const fs::path root = MovesetRoot();
    fs::create_directories(root, ec);
    if (ec || !fs::exists(root, ec) || ec) {
        sScanned = true;
        return;
    }
    const fs::path canonicalRoot = fs::canonical(root, ec);
    if (ec) {
        sScanned = true;
        return;
    }
    std::vector<fs::path> directories;
    for (fs::directory_iterator it(canonicalRoot, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code sec;
        if (it->is_directory(sec) && !sec)
            directories.push_back(it->path());
    }
    std::sort(directories.begin(), directories.end());
    std::set<int32_t> used = { kVanilla, kModern };
    std::set<std::string> seen;
    for (const fs::path& directory : directories) {
        try {
            fs::path manifestPath;
            if (!ResolveFile(directory, "manifest.json", manifestPath))
                continue;
            std::ifstream stream(manifestPath);
            if (!stream)
                continue;
            const Json manifest = Json::parse(stream);
            if (!manifest.is_object() || !manifest.contains("id") || !manifest["id"].is_string())
                continue;
            Package package;
            package.id = manifest["id"].get<std::string>();
            if (!IsValidId(package.id))
                continue;
            package.name = manifest.value("name", package.id);
            if (package.name.empty() || package.name.size() > 96)
                package.name = package.id;
            const fs::path packageRoot = fs::canonical(directory);
            if (!IsWithin(canonicalRoot, packageRoot))
                continue;
            package.ages = AgeMask(manifest);
            if (package.ages == 0 || !ResolveFile(packageRoot, LibraryPath(manifest), package.library))
                continue;
            if (manifest.contains("archive") && manifest["archive"].is_string()) {
                const std::string archive = manifest["archive"].get<std::string>();
                if (!archive.empty() && ResolveFile(packageRoot, archive, package.archive)) {
                    std::string extension = package.archive.extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (extension != ".otr" && extension != ".o2r")
                        package.archive.clear();
                }
            }
            if (!seen.insert(package.id).second)
                continue;
            package.optionId = StableId(package.id, used);
            used.insert(package.optionId);
            sLabels.emplace_back(package.name + " (Plugin)");
            if (package.ages & SHIP_MOVESET_AGE_ADULT)
                sAdultOptions.emplace(package.optionId, sLabels.back().c_str());
            if (package.ages & SHIP_MOVESET_AGE_CHILD)
                sChildOptions.emplace(package.optionId, sLabels.back().c_str());
            sPackages.emplace(package.optionId, std::move(package));
        } catch (const std::exception& e) {
            SPDLOG_WARN("Could not read moveset package '{}': {}", directory.generic_string(), e.what());
        }
    }
#else
    (void)MovesetRoot;
#endif
    sScanned = true;
}
bool IsAdult(const Player*) {
    return gSaveContext.linkAge == LINK_AGE_ADULT;
}
int32_t Selection(const Player* player) {
    return CVarGetInteger(IsAdult(player) ? CVAR_ENHANCEMENT("AdultMoveset") : CVAR_ENHANCEMENT("ChildMoveset"),
                          kVanilla);
}
#if SHIP_MOVESET_HAS_NATIVE_PLUGINS
Library OpenLibrary(const fs::path& path) {
#if defined(_WIN32)
    return LoadLibraryW(path.wstring().c_str());
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}
void CloseLibrary(Library library) {
    if (!library)
        return;
#if defined(_WIN32)
    FreeLibrary(library);
#else
    dlclose(library);
#endif
}
void* Symbol(Library library, const char* name) {
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress(library, name);
#if defined(_M_IX86) || defined(__i386__)
    if (!symbol) {
        const std::string decorated = std::string("_") + name;
        symbol = GetProcAddress(library, decorated.c_str());
    }
#endif
    return reinterpret_cast<void*>(symbol);
#else
    return dlsym(library, name);
#endif
}
void MountArchive(const Package& package) {
    if (package.archive.empty() || sMountedArchives.contains(package.archive))
        return;
    try {
        Ship::Context* ctx = Ship::Context::GetRawInstance();
        if (!ctx || !ctx->GetResourceManager() || !ctx->GetResourceManager()->GetArchiveManager())
            return;
        if (ctx->GetResourceManager()->GetArchiveManager()->AddArchive(package.archive.generic_string()))
            sMountedArchives.insert(package.archive);
        else
            SPDLOG_WARN("Could not add archive for moveset '{}'", package.id);
    } catch (const std::exception& e) { SPDLOG_WARN("Could not mount moveset archive '{}': {}", package.id, e.what()); }
}
size_t AnimationLength(const char* path) {
    size_t n = 0;
    while (n <= kMaxAnimationPathLength && path[n] != '\0')
        ++n;
    return n;
}
void HostAnimation(void* play, void* player, const char* path, float speed, uint32_t loop) {
    if (!sRuntime.framePrepared || sRuntime.player != player || !play || !path)
        return;
    const size_t n = AnimationLength(path);
    if (n == 0 || n > kMaxAnimationPathLength)
        return;
    sRuntime.animation.assign(path, n);
    sRuntime.result.animationResource = sRuntime.animation.c_str();
    sRuntime.result.animationSpeed = std::isfinite(speed) && speed != 0.0f ? speed : 1.0f;
    sRuntime.result.flags |= SHIP_MOVESET_RESULT_PLAY_ANIMATION;
    if (loop)
        sRuntime.result.flags |= SHIP_MOVESET_RESULT_LOOP_ANIMATION;
    else
        sRuntime.result.flags &= ~SHIP_MOVESET_RESULT_LOOP_ANIMATION;
}
void HostSound(void* player, uint16_t sound) {
    if (player)
        Player_PlaySfx(&static_cast<Player*>(player)->actor, sound);
}
void HostEffect(void* play, void* player, uint32_t type, float scale);
void HostHitbox(float scale) {
    if (sRuntime.framePrepared && std::isfinite(scale))
        sRuntime.hitboxScale = std::clamp(scale, kMinHitboxScale, kMaxHitboxScale);
}
const ShipMovesetHostApi sHostApi = {
    SHIP_MOVESET_ABI_VERSION, sizeof(ShipMovesetHostApi), HostAnimation, HostSound, HostEffect, HostHitbox
};
LoadedPlugin* LoadPlugin(const Package& package) {
    if (sFailed.contains(package.optionId))
        return nullptr;
    auto found = sLoaded.find(package.optionId);
    if (found != sLoaded.end())
        return &found->second;
    Library library = OpenLibrary(package.library);
    if (!library) {
#if defined(_WIN32)
        SPDLOG_WARN("Could not load moveset '{}' (Win32 error {})", package.id, GetLastError());
#else
        const char* error = dlerror();
        SPDLOG_WARN("Could not load moveset '{}': {}", package.id, error ? error : "unknown error");
#endif
        sFailed.insert(package.optionId);
        return nullptr;
    }
    auto init = reinterpret_cast<ShipMovesetInitializeFn>(Symbol(library, "ShipMoveset_Initialize"));
    auto action = reinterpret_cast<ShipMovesetOnPlayerActionFn>(Symbol(library, "ShipMoveset_OnPlayerAction"));
    auto shutdown = reinterpret_cast<ShipMovesetShutdownFn>(Symbol(library, "ShipMoveset_Shutdown"));
    if (!init || !action) {
        CloseLibrary(library);
        sFailed.insert(package.optionId);
        return nullptr;
    }
    int32_t ok = 0;
    try {
        ok = init(&sHostApi, package.id.c_str());
    } catch (...) { SPDLOG_ERROR("Moveset '{}' threw during initialization", package.id); }
    if (!ok) {
        if (shutdown) {
            try {
                shutdown();
            } catch (...) {}
        }
        CloseLibrary(library);
        sFailed.insert(package.optionId);
        return nullptr;
    }
    MountArchive(package);
    return &sLoaded.emplace(package.optionId, LoadedPlugin{ library, action, shutdown }).first->second;
}
#endif
void ResetResult() {
    sRuntime.framePrepared = false;
    sRuntime.consumeAction = false;
    sRuntime.hitboxScale = 1.0f;
    sRuntime.animation.clear();
    sRuntime.result = {};
    sRuntime.result.structSize = sizeof(ShipMovesetActionResult);
    sRuntime.result.hitboxScale = sRuntime.result.animationSpeed = sRuntime.result.effectScale = 1.0f;
}
void BeginFrame(PlayState* play, Player* player, Input* input) {
    if (sRuntime.player != player) {
        sRuntime = {};
        sRuntime.player = player;
        sRuntime.lastFrame = UINT32_MAX;
    }
    ResetResult();
    const uint32_t frame = static_cast<uint32_t>(play->gameplayFrames);
    if (sRuntime.lastFrame != frame) {
        if (sRuntime.dashCooldown > 0)
            --sRuntime.dashCooldown;
        sRuntime.lastFrame = frame;
    }
    sRuntime.framePrepared = true;
    const int32_t selection = Selection(player);
    if (selection < 2)
        return;
#if SHIP_MOVESET_HAS_NATIVE_PLUGINS
    auto it = sPackages.find(selection);
    if (it == sPackages.end())
        return;
    const uint32_t age = IsAdult(player) ? SHIP_MOVESET_AGE_ADULT : SHIP_MOVESET_AGE_CHILD;
    const Package& package = it->second;
    if (!(package.ages & age))
        return;
    LoadedPlugin* plugin = LoadPlugin(package);
    if (!plugin)
        return;
    ShipMovesetPlayerState state{};
    state.structSize = sizeof(state);
    state.age = age;
    state.stateFlags1 = player->stateFlags1;
    state.stateFlags2 = player->stateFlags2;
    state.heldItemAction = player->heldItemAction;
    state.currentBoots = player->currentBoots;
    state.meleeWeaponState = player->meleeWeaponState;
    state.isGrounded = (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) != 0;
    state.position[0] = player->actor.world.pos.x;
    state.position[1] = player->actor.world.pos.y;
    state.position[2] = player->actor.world.pos.z;
    state.velocity[0] = player->actor.velocity.x;
    state.velocity[1] = player->actor.velocity.y;
    state.velocity[2] = player->actor.velocity.z;
    state.linearVelocity = player->linearVelocity;
    state.yaw = player->yaw;
    state.playState = play;
    state.player = player;
    ShipMovesetInputState buttons{};
    buttons.structSize = sizeof(buttons);
    if (input) {
        buttons.buttonsHeld = input->cur.button;
        buttons.buttonsPressed = input->press.button;
        buttons.stickX = input->rel.stick_x;
        buttons.stickY = input->rel.stick_y;
    }
    int32_t consumed = 0;
    try {
        consumed = plugin->action(&state, &buttons, &sRuntime.result);
    } catch (...) {
        sFailed.insert(selection);
        sRuntime.result.flags = 0;
        sRuntime.hitboxScale = 1.0f;
        return;
    }
    if ((sRuntime.result.flags & SHIP_MOVESET_RESULT_SET_HITBOX_SCALE) && std::isfinite(sRuntime.result.hitboxScale))
        sRuntime.hitboxScale = std::clamp(sRuntime.result.hitboxScale, kMinHitboxScale, kMaxHitboxScale);
    if ((sRuntime.result.flags & SHIP_MOVESET_RESULT_PLAY_ANIMATION) && sRuntime.result.animationResource) {
        const size_t n = AnimationLength(sRuntime.result.animationResource);
        if (n == 0 || n > kMaxAnimationPathLength)
            sRuntime.result.flags &= ~SHIP_MOVESET_RESULT_PLAY_ANIMATION;
        else if (sRuntime.result.animationResource != sRuntime.animation.c_str())
            sRuntime.animation.assign(sRuntime.result.animationResource, n);
    }
    sRuntime.consumeAction = consumed != 0 || (sRuntime.result.flags & SHIP_MOVESET_RESULT_CONSUME_ACTION);
#else
    (void)input;
#endif
}
void ApplyAnimation(PlayState* play, Player* player) {
    if (sRuntime.animation.empty())
        return;
    AnimationHeaderCommon* animation = ResourceMgr_LoadAnimByName(sRuntime.animation.c_str());
    if (!animation) {
        SPDLOG_WARN("Moveset animation '{}' not found", sRuntime.animation);
        return;
    }
    const bool loop = (sRuntime.result.flags & SHIP_MOVESET_RESULT_LOOP_ANIMATION) != 0;
    const float speed = std::isfinite(sRuntime.result.animationSpeed) && sRuntime.result.animationSpeed != 0.0f
                            ? sRuntime.result.animationSpeed
                            : 1.0f;
    LinkAnimation_Change(play, &player->skelAnime, reinterpret_cast<LinkAnimationHeader*>(animation), speed, 0.0f,
                         static_cast<float>(Animation_GetLastFrame(animation)), loop ? ANIMMODE_LOOP : ANIMMODE_ONCE,
                         0.0f);
}
void ApplyResult(PlayState* play, Player* player) {
    if (!sRuntime.framePrepared || sRuntime.player != player)
        return;
    const uint32_t flags = sRuntime.result.flags;
    if ((flags & SHIP_MOVESET_RESULT_SET_LINEAR_VELOCITY) && std::isfinite(sRuntime.result.linearVelocity))
        player->linearVelocity = sRuntime.result.linearVelocity;
    if ((flags & SHIP_MOVESET_RESULT_SET_VERTICAL_VELOCITY) && std::isfinite(sRuntime.result.verticalVelocity))
        player->actor.velocity.y = sRuntime.result.verticalVelocity;
    if (flags & SHIP_MOVESET_RESULT_SET_HITBOX_SCALE)
        sRuntime.hitboxScale = std::clamp(sRuntime.hitboxScale, kMinHitboxScale, kMaxHitboxScale);
    if (flags & SHIP_MOVESET_RESULT_PLAY_ANIMATION)
        ApplyAnimation(play, player);
    if (flags & SHIP_MOVESET_RESULT_PLAY_SOUND)
        Player_PlaySfx(&player->actor, sRuntime.result.soundId);
    if (flags & SHIP_MOVESET_RESULT_SPAWN_EFFECT)
        MovesetManager_SpawnEffect(play, player, sRuntime.result.effectType, sRuntime.result.effectScale);
}
void SpawnEffect(PlayState* play, Player* player, uint32_t type, float scale) {
    if (!play || !player || !std::isfinite(scale))
        return;
    scale = std::clamp(scale, 0.1f, 4.0f);
    Vec3f pos = player->actor.world.pos;
    pos.y += 4.0f;
    Vec3f zero = { 0.0f, 0.0f, 0.0f };
    Color_RGBA8 prim = { 255, 255, 255, 255 }, env = { 80, 140, 255, 255 };
    if (type == SHIP_MOVESET_EFFECT_DUST) {
        prim = { 180, 145, 105, 255 };
        env = { 105, 80, 55, 255 };
        EffectSsDust_Spawn(play, 0, &pos, &zero, &zero, &prim, &env,
                           static_cast<s16>(std::clamp(100.0f * scale, 20.0f, 500.0f)), 5, 10, 0);
    } else if (type == SHIP_MOVESET_EFFECT_SPARK) {
        EffectSsKiraKira_SpawnDispersed(play, &pos, &zero, &zero, &prim, &env,
                                        static_cast<s16>(std::clamp(500.0f * scale, 50.0f, 2000.0f)), -10);
    }
}
#if SHIP_MOVESET_HAS_NATIVE_PLUGINS
void HostEffect(void* play, void* player, uint32_t type, float scale) {
    SpawnEffect(static_cast<PlayState*>(play), static_cast<Player*>(player), type, scale);
}
#endif
} // namespace
void Initialize() {
    if (sInitialized)
        return;
    if (!sScanned)
        ScanPackages();
    const int32_t adult = CVarGetInteger(CVAR_ENHANCEMENT("AdultMoveset"), kVanilla);
    if (!sAdultOptions.contains(adult))
        CVarSetInteger(CVAR_ENHANCEMENT("AdultMoveset"), kVanilla);
    const int32_t child = CVarGetInteger(CVAR_ENHANCEMENT("ChildMoveset"), kVanilla);
    if (!sChildOptions.contains(child))
        CVarSetInteger(CVAR_ENHANCEMENT("ChildMoveset"), kVanilla);
    sInitialized = true;
}
void Shutdown() {
#if SHIP_MOVESET_HAS_NATIVE_PLUGINS
    for (auto& [id, plugin] : sLoaded) {
        if (plugin.shutdown) {
            try {
                plugin.shutdown();
            } catch (...) { SPDLOG_WARN("Moveset {} shutdown threw", id); }
        }
        CloseLibrary(plugin.library);
    }
    sLoaded.clear();
    sFailed.clear();
#endif
    sRuntime = {};
    sMountedArchives.clear();
    sScanned = false;
    sInitialized = false;
}
const std::map<int32_t, const char*>& GetAvailableMovesetOptions(uint32_t ageMask) {
    if (!sScanned)
        ScanPackages();
    return ageMask == SHIP_MOVESET_AGE_CHILD ? sChildOptions : sAdultOptions;
}
} // namespace MovesetManager
extern "C" int32_t MovesetManager_IsModern(const Player* player) {
    return MovesetManager::Selection(player) == 1;
}
extern "C" int32_t MovesetManager_HandlePlayerAction(PlayState* play, Player* player, Input* input) {
    if (!play || !player)
        return 0;
    MovesetManager::BeginFrame(play, player, input);
    return MovesetManager::sRuntime.consumeAction ? 1 : 0;
}
extern "C" void MovesetManager_EndPlayerFrame(PlayState* play, Player* player) {
    if (play && player)
        MovesetManager::ApplyResult(play, player);
}
extern "C" void MovesetManager_CancelPlayerAction(Player* player) {
    if (MovesetManager::sRuntime.player == player)
        MovesetManager::ResetResult();
}
extern "C" float MovesetManager_GetMeleeHitboxScale(const Player* player) {
    if (MovesetManager::sRuntime.player != player || !std::isfinite(MovesetManager::sRuntime.hitboxScale))
        return 1.0f;
    return MovesetManager::sRuntime.hitboxScale;
}
extern "C" int32_t MovesetManager_GetDashCooldown(const Player* player) {
    return MovesetManager::sRuntime.player == player ? MovesetManager::sRuntime.dashCooldown : 0;
}
extern "C" void MovesetManager_SetDashCooldown(Player* player, int32_t frames) {
    if (MovesetManager::sRuntime.player == player)
        MovesetManager::sRuntime.dashCooldown = std::max(0, frames);
}
extern "C" void MovesetManager_SpawnEffect(PlayState* play, Player* player, uint32_t effectType, float scale) {
    MovesetManager::SpawnEffect(play, player, effectType, scale);
}
