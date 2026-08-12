#pragma once

#include <windows.h>

namespace Trigger {

bool Start(HWND target_window, UINT trigger_message);
void SetEnabled(bool enabled);
void Stop();

} // namespace Trigger
