#pragma once

#include <windows.h>

namespace Trigger {

bool Start(HWND target_window, UINT trigger_message);
void Stop();

} // namespace Trigger
