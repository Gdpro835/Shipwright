#include "soh/Android/TouchControls.h"

#ifdef __ANDROID__

#include <SDL2/SDL.h>
#include <imgui.h>
#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/mapping/keyboard/KeyboardScancodes.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/cvar_prefixes.h"

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
} // namespace SohGui

namespace SohAndroid::TouchControls {
namespace {
enum class Control {
    Menu,
    Toggle,
    Analog,
    A,
    B,
    Z,
    Start,
    L,
    R,
    CUp,
    CDown,
    CLeft,
    CRight,
    DUp,
    DDown,
    DLeft,
    DRight,
    Custom,
};

using TouchId = std::pair<SDL_TouchID, SDL_FingerID>;

struct FingerState {
    float x = 0.0f;
    float y = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;
    bool down = false;
    bool justPressed = false;
};

struct ButtonVisual {
    std::string id;
    Control control;
    std::string label;
    ImVec2 center;
    float radius;
    ImU32 color;
    Ship::KbScancode key = Ship::LUS_KB_UNKNOWN;
};

struct Layout {
    ImVec2 size;
    ButtonVisual analog;
    std::vector<ButtonVisual> buttons;
};

struct LayoutOverride {
    float x = 0.5f;
    float y = 0.5f;
    float size = 0.05f;
};

struct CustomButton {
    uint32_t id = 0;
    std::string label = "Button";
    Ship::KbScancode key = Ship::LUS_KB_SPACE;
    float x = 0.5f;
    float y = 0.5f;
    float size = 0.05f;
};

struct KeyOption {
    Ship::KbScancode key;
    const char* label;
};

constexpr KeyOption kKeyOptions[] = {
    { Ship::LUS_KB_ESCAPE, "Escape" },
    { Ship::LUS_KB_1, "1" },
    { Ship::LUS_KB_2, "2" },
    { Ship::LUS_KB_3, "3" },
    { Ship::LUS_KB_4, "4" },
    { Ship::LUS_KB_5, "5" },
    { Ship::LUS_KB_6, "6" },
    { Ship::LUS_KB_7, "7" },
    { Ship::LUS_KB_8, "8" },
    { Ship::LUS_KB_9, "9" },
    { Ship::LUS_KB_0, "0" },
    { Ship::LUS_KB_BACKSPACE, "Backspace" },
    { Ship::LUS_KB_TAB, "Tab" },
    { Ship::LUS_KB_Q, "Q" },
    { Ship::LUS_KB_W, "W" },
    { Ship::LUS_KB_E, "E" },
    { Ship::LUS_KB_R, "R" },
    { Ship::LUS_KB_T, "T" },
    { Ship::LUS_KB_Y, "Y" },
    { Ship::LUS_KB_U, "U" },
    { Ship::LUS_KB_I, "I" },
    { Ship::LUS_KB_O, "O" },
    { Ship::LUS_KB_P, "P" },
    { Ship::LUS_KB_ENTER, "Enter" },
    { Ship::LUS_KB_CONTROL, "Ctrl" },
    { Ship::LUS_KB_A, "A" },
    { Ship::LUS_KB_S, "S" },
    { Ship::LUS_KB_D, "D" },
    { Ship::LUS_KB_F, "F" },
    { Ship::LUS_KB_G, "G" },
    { Ship::LUS_KB_H, "H" },
    { Ship::LUS_KB_J, "J" },
    { Ship::LUS_KB_K, "K" },
    { Ship::LUS_KB_L, "L" },
    { Ship::LUS_KB_SHIFT, "Shift" },
    { Ship::LUS_KB_Z, "Z" },
    { Ship::LUS_KB_X, "X" },
    { Ship::LUS_KB_C, "C" },
    { Ship::LUS_KB_V, "V" },
    { Ship::LUS_KB_B, "B" },
    { Ship::LUS_KB_N, "N" },
    { Ship::LUS_KB_M, "M" },
    { Ship::LUS_KB_SPACE, "Space" },
    { Ship::LUS_KB_F1, "F1" },
    { Ship::LUS_KB_F2, "F2" },
    { Ship::LUS_KB_F3, "F3" },
    { Ship::LUS_KB_F4, "F4" },
    { Ship::LUS_KB_F5, "F5" },
    { Ship::LUS_KB_F6, "F6" },
    { Ship::LUS_KB_F7, "F7" },
    { Ship::LUS_KB_F8, "F8" },
    { Ship::LUS_KB_F9, "F9" },
    { Ship::LUS_KB_F10, "F10" },
    { Ship::LUS_KB_F11, "F11" },
    { Ship::LUS_KB_F12, "F12" },
    { Ship::LUS_KB_ARROWKEY_UP, "Arrow Up" },
    { Ship::LUS_KB_ARROWKEY_LEFT, "Arrow Left" },
    { Ship::LUS_KB_ARROWKEY_RIGHT, "Arrow Right" },
    { Ship::LUS_KB_ARROWKEY_DOWN, "Arrow Down" },
};

std::mutex gTouchMutex;
std::map<TouchId, FingerState> gTouches;
std::map<TouchId, std::string> gAssignments;
std::set<Ship::KbScancode> gPressedKeys;
std::map<std::string, LayoutOverride> gLayoutOverrides;
std::vector<CustomButton> gCustomButtons;
std::atomic<bool> gMenuToggleRequested = false;
bool gInitialized = false;
bool gControlsVisible = true;
bool gEscapeReleasePending = false;
bool gResetTouchesPending = false;
bool gLayoutDragDirty = false;
ImVec2 gLayoutDragOffset(0.0f, 0.0f);
uint32_t gNextCustomButtonId = 1;
std::string gSelectedLayoutButton;

constexpr const char* kTouchControlsSettingsCVar = CVAR_GENERAL("AndroidTouchControls.Layout");
constexpr const char* kTouchControlsSettingsWindow = "Android Touch Controls";
constexpr float kMinimumButtonSize = 0.025f;
constexpr float kMaximumButtonSize = 0.20f;

const KeyOption* FindKeyOption(Ship::KbScancode key) {
    for (const KeyOption& option : kKeyOptions) {
        if (option.key == key) {
            return &option;
        }
    }
    return nullptr;
}

const char* GetKeyLabel(Ship::KbScancode key) {
    const KeyOption* option = FindKeyOption(key);
    return option == nullptr ? "Unknown" : option->label;
}

std::string CustomButtonId(uint32_t id) {
    return "custom_" + std::to_string(id);
}

CustomButton* FindCustomButton(const std::string& id) {
    const auto button = std::find_if(gCustomButtons.begin(), gCustomButtons.end(),
                                     [&](const CustomButton& candidate) { return CustomButtonId(candidate.id) == id; });
    return button == gCustomButtons.end() ? nullptr : &*button;
}

float ReadJsonFloat(const nlohmann::json& object, const char* key, float defaultValue, float minimum, float maximum) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_number()) {
        return defaultValue;
    }
    const float value = object[key].get<float>();
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : defaultValue;
}

void LimitButtonLabel(std::string& label) {
    size_t offset = 0;
    size_t characters = 0;
    while (offset < label.size() && characters < 12) {
        const unsigned char first = static_cast<unsigned char>(label[offset]);
        size_t sequenceLength = 1;
        if ((first & 0xE0) == 0xC0) {
            sequenceLength = 2;
        } else if ((first & 0xF0) == 0xE0) {
            sequenceLength = 3;
        } else if ((first & 0xF8) == 0xF0) {
            sequenceLength = 4;
        }
        if (offset + sequenceLength > label.size()) {
            break;
        }
        offset += sequenceLength;
        ++characters;
    }
    label.resize(offset);
}

void SaveSettings() {
    nlohmann::json settings;
    settings["version"] = 1;
    settings["controlsVisible"] = gControlsVisible;
    settings["layout"] = nlohmann::json::object();
    for (const auto& [id, layoutOverride] : gLayoutOverrides) {
        settings["layout"][id] = { { "x", layoutOverride.x },
                                   { "y", layoutOverride.y },
                                   { "size", layoutOverride.size } };
    }
    settings["customButtons"] = nlohmann::json::array();
    for (const CustomButton& button : gCustomButtons) {
        settings["customButtons"].push_back({ { "id", button.id },
                                              { "label", button.label },
                                              { "key", static_cast<int>(button.key) },
                                              { "x", button.x },
                                              { "y", button.y },
                                              { "size", button.size } });
    }
    const std::string serialized = settings.dump();
    CVarSetString(kTouchControlsSettingsCVar, serialized.c_str());
    auto context = Ship::Context::GetRawInstance();
    if (context != nullptr && context->GetWindow() != nullptr && context->GetWindow()->GetGui() != nullptr) {
        context->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

void LoadSettings() {
    gLayoutOverrides.clear();
    gCustomButtons.clear();
    gNextCustomButtonId = 1;
    const char* serialized = CVarGetString(kTouchControlsSettingsCVar, "");
    if (serialized == nullptr || serialized[0] == '\0') {
        return;
    }
    const nlohmann::json settings = nlohmann::json::parse(serialized, nullptr, false);
    if (settings.is_discarded() || !settings.is_object()) {
        return;
    }
    const auto visible = settings.find("controlsVisible");
    if (visible != settings.end() && visible->is_boolean()) {
        gControlsVisible = visible->get<bool>();
    }
    const auto layout = settings.find("layout");
    if (layout != settings.end() && layout->is_object()) {
        for (auto it = layout->begin(); it != layout->end(); ++it) {
            if (!it.value().is_object()) {
                continue;
            }
            LayoutOverride override;
            override.size = ReadJsonFloat(it.value(), "size", 0.05f, kMinimumButtonSize, kMaximumButtonSize);
            const float margin = std::min(0.49f, override.size + 0.01f);
            override.x = ReadJsonFloat(it.value(), "x", 0.5f, margin, 1.0f - margin);
            override.y = ReadJsonFloat(it.value(), "y", 0.5f, margin, 1.0f - margin);
            gLayoutOverrides[it.key()] = override;
        }
    }
    const auto customButtons = settings.find("customButtons");
    if (customButtons == settings.end() || !customButtons->is_array()) {
        return;
    }
    std::set<uint32_t> usedIds;
    for (const auto& entry : *customButtons) {
        if (!entry.is_object()) {
            continue;
        }
        const auto id = entry.find("id");
        if (id == entry.end() || (!id->is_number_unsigned() && !id->is_number_integer())) {
            continue;
        }
        uint64_t idValue = 0;
        if (id->is_number_unsigned()) {
            idValue = id->get<uint64_t>();
        } else {
            const int64_t signedId = id->get<int64_t>();
            if (signedId <= 0) {
                continue;
            }
            idValue = static_cast<uint64_t>(signedId);
        }
        if (idValue == 0 || idValue >= std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        CustomButton button;
        button.id = static_cast<uint32_t>(idValue);
        if (!usedIds.insert(button.id).second) {
            continue;
        }
        if (entry.contains("label") && entry["label"].is_string()) {
            button.label = entry["label"].get<std::string>();
        }
        LimitButtonLabel(button.label);
        const auto keyValue = entry.find("key");
        if (keyValue != entry.end() && (keyValue->is_number_unsigned() || keyValue->is_number_integer())) {
            int64_t keyNumber = -1;
            if (keyValue->is_number_unsigned()) {
                const uint64_t unsignedKey = keyValue->get<uint64_t>();
                if (unsignedKey < static_cast<uint64_t>(Ship::LUS_KB_MAX)) {
                    keyNumber = static_cast<int64_t>(unsignedKey);
                }
            } else {
                keyNumber = keyValue->get<int64_t>();
            }
            if (keyNumber >= 0 && keyNumber < Ship::LUS_KB_MAX) {
                const auto key = static_cast<Ship::KbScancode>(keyNumber);
                if (FindKeyOption(key) != nullptr) {
                    button.key = key;
                }
            }
        }
        button.size = ReadJsonFloat(entry, "size", button.size, kMinimumButtonSize, kMaximumButtonSize);
        const float margin = std::min(0.49f, button.size + 0.01f);
        button.x = ReadJsonFloat(entry, "x", button.x, margin, 1.0f - margin);
        button.y = ReadJsonFloat(entry, "y", button.y, margin, 1.0f - margin);
        gNextCustomButtonId = std::max(gNextCustomButtonId, button.id + 1);
        gCustomButtons.push_back(std::move(button));
    }
}

int SDLCALL OnSdlEvent(void*, SDL_Event* event) {
    if (event == nullptr) {
        return 1;
    }

    std::lock_guard<std::mutex> lock(gTouchMutex);
    switch (event->type) {
        case SDL_FINGERDOWN: {
            const TouchId id{ event->tfinger.touchId, event->tfinger.fingerId };
            FingerState& finger = gTouches[id];
            finger.x = finger.startX = event->tfinger.x;
            finger.y = finger.startY = event->tfinger.y;
            finger.down = true;
            finger.justPressed = true;
            break;
        }
        case SDL_FINGERMOTION: {
            const TouchId id{ event->tfinger.touchId, event->tfinger.fingerId };
            FingerState& finger = gTouches[id];
            if (!finger.down && !finger.justPressed) {
                finger.startX = event->tfinger.x;
                finger.startY = event->tfinger.y;
                finger.justPressed = true;
            }
            finger.x = event->tfinger.x;
            finger.y = event->tfinger.y;
            finger.down = true;
            break;
        }
        case SDL_FINGERUP: {
            const TouchId id{ event->tfinger.touchId, event->tfinger.fingerId };
            auto finger = gTouches.find(id);
            if (finger != gTouches.end()) {
                finger->second.x = event->tfinger.x;
                finger->second.y = event->tfinger.y;
                finger->second.down = false;
            }
            break;
        }
        case SDL_WINDOWEVENT:
            if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                gTouches.clear();
                gResetTouchesPending = true;
            }
            break;
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_TERMINATING:
            gTouches.clear();
            gResetTouchesPending = true;
            break;
        default:
            break;
    }
    return 1;
}

Layout MakeDefaultLayout(ImVec2 size) {
    const float shortSide = std::max(1.0f, std::min(size.x, size.y));
    const float buttonRadius = std::clamp(shortSide * 0.055f, 24.0f, 58.0f);
    const float smallRadius = std::clamp(shortSide * 0.038f, 18.0f, 34.0f);
    const ImVec2 analogCenter(size.x * 0.17f, size.y * 0.78f);
    const float analogRadius = std::clamp(shortSide * 0.15f, 60.0f, 142.0f);
    const float dpadOffset = buttonRadius * 1.8f;
    const float cOffset = buttonRadius * 1.82f;
    const float cX = size.x * 0.86f;
    const float cY = size.y * 0.42f;
    const ImU32 neutral = IM_COL32(14, 23, 37, 145);

    return {
        size,
        ButtonVisual{ "analog", Control::Analog, "", analogCenter, analogRadius, IM_COL32(14, 23, 37, 125) },
        { ButtonVisual{ "menu", Control::Menu, "M", ImVec2(size.x * 0.055f, size.y * 0.08f), smallRadius, neutral },
          ButtonVisual{ "toggle", Control::Toggle, "T", ImVec2(size.x * 0.135f, size.y * 0.08f), smallRadius, neutral },
          ButtonVisual{ "l", Control::L, "L", ImVec2(size.x * 0.075f, size.y * 0.20f), buttonRadius * 0.86f, neutral },
          ButtonVisual{ "r", Control::R, "R", ImVec2(size.x * 0.925f, size.y * 0.20f), buttonRadius * 0.86f, neutral },
          ButtonVisual{ "a", Control::A, "A", ImVec2(size.x * 0.86f, size.y * 0.77f), buttonRadius * 1.16f,
                        IM_COL32(48, 155, 90, 170) },
          ButtonVisual{ "b", Control::B, "B", ImVec2(size.x * 0.72f, size.y * 0.87f), buttonRadius,
                        IM_COL32(59, 101, 184, 170) },
          ButtonVisual{ "z", Control::Z, "Z", ImVec2(size.x * 0.59f, size.y * 0.67f), buttonRadius * 0.92f,
                        IM_COL32(154, 83, 71, 170) },
          ButtonVisual{ "start", Control::Start, "START", ImVec2(size.x * 0.49f, size.y * 0.92f), buttonRadius * 0.78f,
                        IM_COL32(104, 111, 126, 175) },
          ButtonVisual{ "c_up", Control::CUp, "C^", ImVec2(cX, cY - cOffset), buttonRadius * 0.75f,
                        IM_COL32(166, 143, 54, 175) },
          ButtonVisual{ "c_down", Control::CDown, "Cv", ImVec2(cX, cY + cOffset), buttonRadius * 0.75f,
                        IM_COL32(166, 143, 54, 175) },
          ButtonVisual{ "c_left", Control::CLeft, "C<", ImVec2(cX - cOffset, cY), buttonRadius * 0.75f,
                        IM_COL32(166, 143, 54, 175) },
          ButtonVisual{ "c_right", Control::CRight, "C>", ImVec2(cX + cOffset, cY), buttonRadius * 0.75f,
                        IM_COL32(166, 143, 54, 175) },
          ButtonVisual{ "d_up", Control::DUp, "^", ImVec2(size.x * 0.16f, size.y * 0.42f - dpadOffset),
                        buttonRadius * 0.72f, neutral },
          ButtonVisual{ "d_down", Control::DDown, "v", ImVec2(size.x * 0.16f, size.y * 0.42f + dpadOffset),
                        buttonRadius * 0.72f, neutral },
          ButtonVisual{ "d_left", Control::DLeft, "<", ImVec2(size.x * 0.16f - dpadOffset, size.y * 0.42f),
                        buttonRadius * 0.72f, neutral },
          ButtonVisual{ "d_right", Control::DRight, ">", ImVec2(size.x * 0.16f + dpadOffset, size.y * 0.42f),
                        buttonRadius * 0.72f, neutral } }
    };
}

void ApplyLayoutOverride(ButtonVisual& button, ImVec2 size, float shortSide) {
    const auto setting = gLayoutOverrides.find(button.id);
    if (setting != gLayoutOverrides.end()) {
        button.center = ImVec2(setting->second.x * size.x, setting->second.y * size.y);
        button.radius = setting->second.size * shortSide;
    }
}

void InitializeDefaultLayoutOverrides() {
    const ImVec2 referenceSize(1000.0f, 600.0f);
    const Layout defaults = MakeDefaultLayout(referenceSize);
    for (const ButtonVisual& button : defaults.buttons) {
        gLayoutOverrides.try_emplace(button.id,
                                     LayoutOverride{ button.center.x / referenceSize.x,
                                                     button.center.y / referenceSize.y, button.radius / 600.0f });
    }
    gLayoutOverrides.try_emplace(defaults.analog.id, LayoutOverride{ defaults.analog.center.x / referenceSize.x,
                                                                     defaults.analog.center.y / referenceSize.y,
                                                                     defaults.analog.radius / 600.0f });
}

Layout MakeLayout(ImVec2 size) {
    Layout layout = MakeDefaultLayout(size);
    const float shortSide = std::max(1.0f, std::min(size.x, size.y));
    ApplyLayoutOverride(layout.analog, size, shortSide);
    for (ButtonVisual& button : layout.buttons) {
        ApplyLayoutOverride(button, size, shortSide);
    }
    for (const CustomButton& custom : gCustomButtons) {
        layout.buttons.push_back({ CustomButtonId(custom.id), Control::Custom, custom.label,
                                   ImVec2(custom.x * size.x, custom.y * size.y), custom.size * shortSide,
                                   IM_COL32(112, 92, 170, 175), custom.key });
    }
    return layout;
}

const ButtonVisual* FindButton(const Layout& layout, const std::string& id) {
    if (layout.analog.id == id) {
        return &layout.analog;
    }
    const auto button = std::find_if(layout.buttons.begin(), layout.buttons.end(),
                                     [&](const ButtonVisual& candidate) { return candidate.id == id; });
    return button == layout.buttons.end() ? nullptr : &*button;
}

float DistanceSquared(ImVec2 left, ImVec2 right) {
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    return x * x + y * y;
}

const ButtonVisual* HitTest(ImVec2 position, const Layout& layout, bool controlsVisible) {
    // Keep the menu and visibility toggle available when the controls are hidden.
    for (const ButtonVisual& button : layout.buttons) {
        const float radius = button.radius * 1.45f;
        if ((button.control == Control::Menu || button.control == Control::Toggle) &&
            DistanceSquared(position, button.center) <= radius * radius) {
            return &button;
        }
    }
    if (!controlsVisible) {
        return nullptr;
    }

    const ButtonVisual* nearest = nullptr;
    float nearestDistance = std::numeric_limits<float>::max();
    for (const ButtonVisual& button : layout.buttons) {
        if (button.control == Control::Menu || button.control == Control::Toggle) {
            continue;
        }
        const float radius = button.radius * 1.25f;
        const float distance = DistanceSquared(position, button.center);
        if (distance <= radius * radius && distance < nearestDistance) {
            nearest = &button;
            nearestDistance = distance;
        }
    }
    if (nearest != nullptr) {
        return nearest;
    }
    const float analogHitRadius = layout.analog.radius * 1.42f;
    return DistanceSquared(position, layout.analog.center) <= analogHitRadius * analogHitRadius ? &layout.analog
                                                                                                : nullptr;
}

void AddAnalogKeys(std::set<Ship::KbScancode>& keys, ImVec2 position, const ButtonVisual& analog) {
    const float dx = position.x - analog.center.x;
    const float dy = position.y - analog.center.y;
    const float deadZone = analog.radius * 0.27f;
    if (dx < -deadZone) {
        keys.insert(Ship::LUS_KB_A);
    } else if (dx > deadZone) {
        keys.insert(Ship::LUS_KB_D);
    }
    if (dy < -deadZone) {
        keys.insert(Ship::LUS_KB_W);
    } else if (dy > deadZone) {
        keys.insert(Ship::LUS_KB_S);
    }
}

void AddMappedKey(std::set<Ship::KbScancode>& keys, const ButtonVisual& button, ImVec2 position, const Layout& layout) {
    switch (button.control) {
        case Control::Analog:
            AddAnalogKeys(keys, position, layout.analog);
            break;
        case Control::A:
            keys.insert(Ship::LUS_KB_X);
            break;
        case Control::B:
            keys.insert(Ship::LUS_KB_C);
            break;
        case Control::Z:
            keys.insert(Ship::LUS_KB_Z);
            break;
        case Control::Start:
            keys.insert(Ship::LUS_KB_SPACE);
            break;
        case Control::L:
            keys.insert(Ship::LUS_KB_E);
            break;
        case Control::R:
            keys.insert(Ship::LUS_KB_R);
            break;
        case Control::CUp:
            keys.insert(Ship::LUS_KB_ARROWKEY_UP);
            break;
        case Control::CDown:
            keys.insert(Ship::LUS_KB_ARROWKEY_DOWN);
            break;
        case Control::CLeft:
            keys.insert(Ship::LUS_KB_ARROWKEY_LEFT);
            break;
        case Control::CRight:
            keys.insert(Ship::LUS_KB_ARROWKEY_RIGHT);
            break;
        case Control::DUp:
            keys.insert(Ship::LUS_KB_T);
            break;
        case Control::DDown:
            keys.insert(Ship::LUS_KB_G);
            break;
        case Control::DLeft:
            keys.insert(Ship::LUS_KB_F);
            break;
        case Control::DRight:
            keys.insert(Ship::LUS_KB_H);
            break;
        case Control::Custom:
            if (FindKeyOption(button.key) != nullptr) {
                keys.insert(button.key);
            }
            break;
        default:
            break;
    }
}

void QueueEscapeEvent(bool pressed) {
    SDL_Event event{};
    event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = 0;
    event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    event.key.keysym.sym = SDLK_ESCAPE;
    SDL_PushEvent(&event);
}

void DrawButton(ImDrawList* drawList, const ButtonVisual& button, bool pressed, bool controlsVisible) {
    const ImU32 fill = pressed ? IM_COL32(70, 157, 232, 215) : button.color;
    drawList->AddCircleFilled(button.center, button.radius, fill, 32);
    drawList->AddCircle(button.center, button.radius, IM_COL32(255, 255, 255, pressed ? 235 : 130), 32, 2.0f);

    if (!button.label.empty()) {
        const char* label = button.label.c_str();
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        const ImVec2 labelPosition(button.center.x - labelSize.x * 0.5f, button.center.y - labelSize.y * 0.5f);
        drawList->AddText(ImVec2(labelPosition.x + 1.0f, labelPosition.y + 1.0f), IM_COL32(0, 0, 0, 220), label);
        drawList->AddText(labelPosition, IM_COL32(255, 255, 255, 245), label);
    }

    if (button.control == Control::Menu || button.control == Control::Toggle) {
        const char* caption = button.control == Control::Menu ? "MENU" : (controlsVisible ? "TOUCH ON" : "TOUCH OFF");
        const ImVec2 captionPosition(button.center.x - button.radius * 0.35f, button.center.y + button.radius + 2.0f);
        drawList->AddText(captionPosition, IM_COL32(255, 255, 255, 205), caption);
    }
}

void SetButtonPosition(const std::string& id, float x, float y) {
    if (CustomButton* custom = FindCustomButton(id)) {
        const float margin = std::min(0.49f, custom->size + 0.01f);
        custom->x = std::clamp(x, margin, 1.0f - margin);
        custom->y = std::clamp(y, margin, 1.0f - margin);
        return;
    }
    LayoutOverride& layoutOverride = gLayoutOverrides[id];
    const float margin = std::min(0.49f, layoutOverride.size + 0.01f);
    layoutOverride.x = std::clamp(x, margin, 1.0f - margin);
    layoutOverride.y = std::clamp(y, margin, 1.0f - margin);
}

void SetButtonSize(const std::string& id, float requestedSize) {
    const float size = std::clamp(requestedSize, kMinimumButtonSize, kMaximumButtonSize);
    const float margin = std::min(0.49f, size + 0.01f);
    if (CustomButton* custom = FindCustomButton(id)) {
        custom->size = size;
        custom->x = std::clamp(custom->x, margin, 1.0f - margin);
        custom->y = std::clamp(custom->y, margin, 1.0f - margin);
        return;
    }
    LayoutOverride& layoutOverride = gLayoutOverrides[id];
    layoutOverride.size = size;
    layoutOverride.x = std::clamp(layoutOverride.x, margin, 1.0f - margin);
    layoutOverride.y = std::clamp(layoutOverride.y, margin, 1.0f - margin);
}

void ResetBuiltInLayout() {
    gLayoutOverrides.clear();
    InitializeDefaultLayoutOverrides();
    gSelectedLayoutButton.clear();
    SaveSettings();
}

void AddCustomButton() {
    CustomButton button;
    button.id = gNextCustomButtonId++;
    button.label = "Button " + std::to_string(button.id);
    const uint32_t index = button.id - 1;
    button.x = 0.46f + static_cast<float>(index % 4) * 0.035f;
    button.y = 0.50f + static_cast<float>((index / 4) % 5) * 0.045f;
    gCustomButtons.push_back(button);
    gSelectedLayoutButton = CustomButtonId(button.id);
    SaveSettings();
}

const ButtonVisual* FindEditableButton(const Layout& layout, ImVec2 position) {
    const ButtonVisual* nearest = nullptr;
    float nearestDistance = std::numeric_limits<float>::max();
    auto consider = [&](const ButtonVisual& button) {
        const float distance = DistanceSquared(position, button.center);
        const float radius = std::max(button.radius * 1.4f, 18.0f);
        if (distance <= radius * radius && distance < nearestDistance) {
            nearest = &button;
            nearestDistance = distance;
        }
    };
    consider(layout.analog);
    for (const ButtonVisual& button : layout.buttons) {
        consider(button);
    }
    return nearest;
}

void DrawLayoutPreview() {
    if (!ImGui::BeginChild("AndroidTouchLayoutPreview", ImVec2(0.0f, 260.0f), true)) {
        ImGui::EndChild();
        return;
    }
    const ImVec2 areaSize = ImGui::GetContentRegionAvail();
    if (areaSize.x <= 0.0f || areaSize.y <= 0.0f) {
        ImGui::EndChild();
        return;
    }
    ImGui::InvisibleButton("AndroidTouchLayoutCanvas", areaSize);
    const bool clicked = ImGui::IsItemClicked(0);
    const bool active = ImGui::IsItemActive();
    const ImVec2 areaMin = ImGui::GetItemRectMin();
    const ImVec2 areaMax = ImGui::GetItemRectMax();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float aspect = std::clamp(display.y > 0.0f ? display.x / display.y : 16.0f / 9.0f, 0.5f, 3.0f);
    ImVec2 previewSize = areaSize;
    if (previewSize.x / previewSize.y > aspect) {
        previewSize.x = previewSize.y * aspect;
    } else {
        previewSize.y = previewSize.x / aspect;
    }
    const ImVec2 offset((areaSize.x - previewSize.x) * 0.5f, (areaSize.y - previewSize.y) * 0.5f);
    const ImVec2 previewMin(areaMin.x + offset.x, areaMin.y + offset.y);
    const ImVec2 previewMax(previewMin.x + previewSize.x, previewMin.y + previewSize.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(areaMin, areaMax, IM_COL32(13, 17, 24, 255), 6.0f);
    drawList->AddRectFilled(previewMin, previewMax, IM_COL32(20, 27, 37, 255), 6.0f);
    drawList->AddRect(previewMin, previewMax, IM_COL32(120, 140, 165, 200), 6.0f, 0, 1.5f);
    for (int i = 1; i < 4; ++i) {
        const float x = previewMin.x + previewSize.x * static_cast<float>(i) / 4.0f;
        const float y = previewMin.y + previewSize.y * static_cast<float>(i) / 4.0f;
        drawList->AddLine(ImVec2(x, previewMin.y), ImVec2(x, previewMax.y), IM_COL32(255, 255, 255, 16));
        drawList->AddLine(ImVec2(previewMin.x, y), ImVec2(previewMax.x, y), IM_COL32(255, 255, 255, 16));
    }

    Layout layout = MakeLayout(previewSize);
    if (clicked) {
        const ImVec2 local(ImGui::GetIO().MousePos.x - previewMin.x, ImGui::GetIO().MousePos.y - previewMin.y);
        const ButtonVisual* hit =
            ImGui::IsMouseHoveringRect(previewMin, previewMax) ? FindEditableButton(layout, local) : nullptr;
        gSelectedLayoutButton = hit == nullptr ? std::string() : hit->id;
        gLayoutDragOffset =
            hit == nullptr ? ImVec2(0.0f, 0.0f) : ImVec2(hit->center.x - local.x, hit->center.y - local.y);
    }
    if (active && !gSelectedLayoutButton.empty() && ImGui::IsMouseDown(0)) {
        const ImVec2 local(ImGui::GetIO().MousePos.x - previewMin.x, ImGui::GetIO().MousePos.y - previewMin.y);
        const float x = (local.x + gLayoutDragOffset.x) / previewSize.x;
        const float y = (local.y + gLayoutDragOffset.y) / previewSize.y;
        const ButtonVisual* current = FindButton(layout, gSelectedLayoutButton);
        if (current != nullptr && (std::abs(current->center.x / previewSize.x - x) > 0.0001f ||
                                   std::abs(current->center.y / previewSize.y - y) > 0.0001f)) {
            SetButtonPosition(gSelectedLayoutButton, x, y);
            gLayoutDragDirty = true;
            layout = MakeLayout(previewSize);
        }
    }
    auto offsetButton = [&](ButtonVisual button) {
        button.center.x += previewMin.x;
        button.center.y += previewMin.y;
        return button;
    };
    const ButtonVisual analog = offsetButton(layout.analog);
    drawList->AddCircleFilled(analog.center, analog.radius, analog.color, 40);
    drawList->AddCircle(analog.center, analog.radius,
                        analog.id == gSelectedLayoutButton ? IM_COL32(100, 190, 255, 255)
                                                           : IM_COL32(255, 255, 255, 105),
                        40, analog.id == gSelectedLayoutButton ? 3.0f : 2.0f);
    drawList->AddText(ImVec2(analog.center.x - 24.0f, analog.center.y - 8.0f), IM_COL32(235, 240, 248, 210), "Stick");
    for (const ButtonVisual& button : layout.buttons) {
        DrawButton(drawList, offsetButton(button), button.id == gSelectedLayoutButton, gControlsVisible);
    }
    if (gLayoutDragDirty && ImGui::IsMouseReleased(0)) {
        SaveSettings();
        gLayoutDragDirty = false;
    }
    ImGui::EndChild();
}

void DrawTouchControlsSettings() {
    ImGui::TextWrapped("Drag controls in the preview; select one to adjust its size.");
    if (ImGui::Button("Reset built-in layout")) {
        ResetBuiltInLayout();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show touch controls", &gControlsVisible)) {
        SaveSettings();
    }
    DrawLayoutPreview();
    const Layout layout = MakeLayout(ImVec2(1000.0f, 600.0f));
    const ButtonVisual* selected = FindButton(layout, gSelectedLayoutButton);
    if (selected != nullptr) {
        float size = selected->radius / std::min(layout.size.x, layout.size.y);
        const std::string label = "Selected button size##" + selected->id;
        if (ImGui::SliderFloat(label.c_str(), &size, kMinimumButtonSize, kMaximumButtonSize, "%.3f")) {
            SetButtonSize(selected->id, size);
            SaveSettings();
        }
    }

    ImGui::SeparatorText("Custom buttons");
    ImGui::TextWrapped("A custom button sends its selected keyboard key while pressed.");
    if (ImGui::Button("Add custom button")) {
        AddCustomButton();
    }
    for (size_t i = 0; i < gCustomButtons.size();) {
        CustomButton& button = gCustomButtons[i];
        bool remove = false;
        ImGui::PushID(static_cast<int>(button.id));
        ImGui::Separator();
        ImGui::Text("Button %u", button.id);
        ImGui::SameLine();
        remove = ImGui::SmallButton("Delete");
        if (UIWidgets::InputString("Label", &button.label)) {
            LimitButtonLabel(button.label);
            SaveSettings();
        }
        if (ImGui::BeginCombo("Keyboard key", GetKeyLabel(button.key))) {
            for (const KeyOption& option : kKeyOptions) {
                const bool selectedKey = option.key == button.key;
                if (ImGui::Selectable(option.label, selectedKey)) {
                    button.key = option.key;
                    SaveSettings();
                }
                if (selectedKey) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        if (remove) {
            if (gSelectedLayoutButton == CustomButtonId(button.id)) {
                gSelectedLayoutButton.clear();
            }
            gCustomButtons.erase(gCustomButtons.begin() + static_cast<std::ptrdiff_t>(i));
            SaveSettings();
            continue;
        }
        ++i;
    }
}

class TouchControlsSettingsWindow final : public Ship::GuiWindow {
  public:
    TouchControlsSettingsWindow()
        : Ship::GuiWindow(CVAR_WINDOW("AndroidTouchControls"), kTouchControlsSettingsWindow, ImVec2(900.0f, 720.0f)) {
    }

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
        DrawTouchControlsSettings();
    }
};

void RegisterTouchControlsMenu() {
    auto context = Ship::Context::GetRawInstance();
    if (context == nullptr || context->GetWindow() == nullptr || context->GetWindow()->GetGui() == nullptr ||
        SohGui::mSohMenu == nullptr) {
        return;
    }
    context->GetWindow()->GetGui()->AddGuiWindow(std::make_shared<TouchControlsSettingsWindow>());
    WidgetPath path = { "Settings", "Controls", SECTION_COLUMN_2 };
    SohGui::mSohMenu->AddWidget(path, "Android Touch Controls", WIDGET_WINDOW_BUTTON)
        .CVar(CVAR_WINDOW("AndroidTouchControls"))
        .RaceDisable(false)
        .WindowName(kTouchControlsSettingsWindow)
        .HideInSearch(true)
        .Options(UIWidgets::WindowButtonOptions()
                     .Tooltip("Create Android touch buttons, choose their keys, and arrange the layout.")
                     .EmbedWindow(false));
}

static RegisterMenuInitFunc touchControlsMenuInit(RegisterTouchControlsMenu);
} // namespace

void Initialize() {
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_DelEventWatch(OnSdlEvent, nullptr);
    SDL_AddEventWatch(OnSdlEvent, nullptr);
    {
        std::lock_guard<std::mutex> lock(gTouchMutex);
        gTouches.clear();
        gResetTouchesPending = false;
    }
    gAssignments.clear();
    gPressedKeys.clear();
    gEscapeReleasePending = false;
    gMenuToggleRequested.store(false, std::memory_order_relaxed);
    gControlsVisible = true;
    gLayoutDragDirty = false;
    gSelectedLayoutButton.clear();
    LoadSettings();
    InitializeDefaultLayoutOverrides();
    gInitialized = true;
}

void RequestMenuToggle() {
    gMenuToggleRequested.store(true, std::memory_order_relaxed);
}

void Draw() {
    if (!gInitialized || ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f) {
        return;
    }
    std::vector<std::pair<TouchId, FingerState>> fingers;
    bool resetTouches = false;
    {
        std::lock_guard<std::mutex> lock(gTouchMutex);
        resetTouches = gResetTouchesPending;
        gResetTouchesPending = false;
        for (auto finger = gTouches.begin(); finger != gTouches.end();) {
            fingers.emplace_back(finger->first, finger->second);
            if (finger->second.down) {
                finger->second.justPressed = false;
                ++finger;
            } else {
                finger = gTouches.erase(finger);
            }
        }
    }
    if (resetTouches) {
        gAssignments.clear();
    }

    auto context = Ship::Context::GetRawInstance();
    if (context == nullptr || context->GetWindow() == nullptr || context->GetWindow()->GetGui() == nullptr) {
        return;
    }
    const auto gui = context->GetWindow()->GetGui();
    const Layout layout = MakeLayout(displaySize);
    const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const auto settingsWindow = gui->GetGuiWindow(kTouchControlsSettingsWindow);
    bool uiOpen =
        gui->GetMenuOrMenubarVisible() || popupOpen || (settingsWindow != nullptr && settingsWindow->IsVisible());
    if (uiOpen) {
        // Let SDL's touch-to-mouse events navigate ImGui without assigning those fingers to game controls.
        gAssignments.clear();
        fingers.clear();
    }

    std::set<Ship::KbScancode> desiredKeys;
    std::set<std::string> activeButtons;
    bool analogActive = false;
    ImVec2 analogPosition = layout.analog.center;

    // Keep a synthesized Escape press across frames when a popup needs dismissing.
    if (gEscapeReleasePending) {
        QueueEscapeEvent(false);
        gEscapeReleasePending = false;
    }
    bool toggleMenu = gMenuToggleRequested.exchange(false, std::memory_order_relaxed);

    for (const auto& [id, finger] : fingers) {
        auto assignment = gAssignments.find(id);
        if (assignment == gAssignments.end() && (finger.down || finger.justPressed)) {
            const ImVec2 startPosition(finger.startX * displaySize.x, finger.startY * displaySize.y);
            const ButtonVisual* hit = HitTest(startPosition, layout, gControlsVisible);
            if (hit != nullptr) {
                assignment = gAssignments.emplace(id, hit->id).first;
            }
        }
        if (assignment == gAssignments.end()) {
            continue;
        }

        const ButtonVisual* button = FindButton(layout, assignment->second);
        if (button == nullptr) {
            gAssignments.erase(assignment);
            continue;
        }
        const Control control = button->control;
        if (finger.justPressed && control == Control::Menu) {
            toggleMenu = true;
        } else if (finger.justPressed && control == Control::Toggle) {
            gControlsVisible = !gControlsVisible;
            SaveSettings();
        }

        const ImVec2 position(finger.x * displaySize.x, finger.y * displaySize.y);
        const ImVec2 mappedPosition =
            finger.down ? position : ImVec2(finger.startX * displaySize.x, finger.startY * displaySize.y);
        const bool visibleControl = gControlsVisible || control == Control::Menu || control == Control::Toggle;
        if (!uiOpen && visibleControl && (finger.down || finger.justPressed)) {
            AddMappedKey(desiredKeys, *button, mappedPosition, layout);
            activeButtons.insert(button->id);
            if (control == Control::Analog) {
                analogActive = true;
                analogPosition = mappedPosition;
            }
        }

        if (!finger.down) {
            gAssignments.erase(assignment);
        }
    }

    if (toggleMenu) {
        if (popupOpen) {
            // Let Escape dismiss modal popups before changing the underlying menu state.
            QueueEscapeEvent(true);
            gEscapeReleasePending = true;
        } else if (const auto menu = gui->GetMenu(); menu != nullptr) {
            // Toggle directly on the render thread instead of routing this touch through a
            // synthetic SDL key event before it reaches the ImGui menu handler.
            menu->ToggleVisibility();
            context->GetWindow()->GetMouseStateManager()->UpdateMouseCapture();
        } else {
            QueueEscapeEvent(true);
            gEscapeReleasePending = true;
        }
        uiOpen = gui->GetMenuOrMenubarVisible() ||
                 ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
                 (settingsWindow != nullptr && settingsWindow->IsVisible());
    }

    if (uiOpen) {
        desiredKeys.clear();
        activeButtons.clear();
        analogActive = false;
    }

    const auto controlDeck = context->GetControlDeck();
    if (controlDeck != nullptr) {
        for (const Ship::KbScancode key : gPressedKeys) {
            if (!desiredKeys.contains(key)) {
                controlDeck->ProcessKeyboardEvent(Ship::LUS_KB_EVENT_KEY_UP, key);
            }
        }
        for (const Ship::KbScancode key : desiredKeys) {
            if (!gPressedKeys.contains(key)) {
                controlDeck->ProcessKeyboardEvent(Ship::LUS_KB_EVENT_KEY_DOWN, key);
            }
        }
    }
    gPressedKeys = desiredKeys;

    if (uiOpen) {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (drawList == nullptr) {
        return;
    }

    for (const ButtonVisual& button : layout.buttons) {
        const bool isUtilityButton = button.control == Control::Menu || button.control == Control::Toggle;
        if (!gControlsVisible && !isUtilityButton) {
            continue;
        }
        DrawButton(drawList, button, activeButtons.contains(button.id), gControlsVisible);
    }

    if (gControlsVisible) {
        const ImVec2 analogCenter = layout.analog.center;
        const float analogRadius = layout.analog.radius;
        drawList->AddCircleFilled(analogCenter, analogRadius, layout.analog.color, 48);
        drawList->AddCircle(analogCenter, analogRadius, IM_COL32(255, 255, 255, 115), 48, 2.0f);
        ImVec2 knobPosition = analogCenter;
        if (analogActive) {
            float dx = analogPosition.x - analogCenter.x;
            float dy = analogPosition.y - analogCenter.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float maxDistance = analogRadius * 0.78f;
            if (distance > maxDistance && distance > 0.0f) {
                dx *= maxDistance / distance;
                dy *= maxDistance / distance;
            }
            knobPosition = ImVec2(analogCenter.x + dx, analogCenter.y + dy);
        }
        drawList->AddCircleFilled(knobPosition, analogRadius * 0.39f,
                                  analogActive ? IM_COL32(70, 157, 232, 220) : IM_COL32(235, 240, 248, 145), 36);
        drawList->AddCircle(knobPosition, analogRadius * 0.39f, IM_COL32(255, 255, 255, 185), 36, 2.0f);
    }
}
} // namespace SohAndroid::TouchControls

namespace SohAndroid {
TouchControlsWindow::TouchControlsWindow() : Ship::GuiWindow("", true, "AndroidTouchControls") {
}

void TouchControlsWindow::Draw() {
    if (IsVisible()) {
        TouchControls::Draw();
    }
}

void TouchControlsWindow::InitElement() {
}

void TouchControlsWindow::UpdateElement() {
}

void TouchControlsWindow::DrawElement() {
}
} // namespace SohAndroid

#endif // __ANDROID__
