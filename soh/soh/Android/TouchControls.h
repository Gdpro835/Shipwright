#pragma once

#ifdef __ANDROID__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
int32_t SohAndroid_TouchControls_GetModernStick(int8_t* stickX, int8_t* stickY);
void SohAndroid_TouchControls_SetModernMovesetActive(int32_t isModern);
#ifdef __cplusplus
}

#include <ship/window/gui/GuiWindow.h>

namespace SohAndroid {
namespace TouchControls {
void Initialize();
void Draw();
void RequestMenuToggle();
} // namespace TouchControls

class TouchControlsWindow final : public Ship::GuiWindow {
  public:
    TouchControlsWindow();
    void Draw() override;

  protected:
    void InitElement() override;
    void UpdateElement() override;
    void DrawElement() override;
};
} // namespace SohAndroid
#endif // __cplusplus

#endif // __ANDROID__
