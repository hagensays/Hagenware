#pragma once

#include <windows.h>

namespace WindowPlacement {

using TriggerEnabledCallback = void (*)(bool enabled);

bool Initialize(HINSTANCE instance, TriggerEnabledCallback set_trigger_enabled);
void Show();
void Hide();
void Shutdown();

} // namespace WindowPlacement
