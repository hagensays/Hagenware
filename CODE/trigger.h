#pragma once

#include <windows.h>

namespace Trigger {

bool Start(HWND target_window, UINT shift_trigger_message, UINT control_trigger_message);
void SetEnabled(bool enabled);
void Stop();

} // namespace Trigger
