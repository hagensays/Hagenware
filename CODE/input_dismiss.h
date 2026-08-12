#pragma once

#include <windows.h>

namespace InputDismiss {

using SuppressModifierCallback = void (*)(DWORD virtual_key);

bool Start(SuppressModifierCallback suppress_modifier_until_release);
void Stop();

} // namespace InputDismiss
