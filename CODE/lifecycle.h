#pragma once

#include <windows.h>

namespace Lifecycle {

void Initialize(HWND host_window);
void BeginActivity();
void EndActivity();
void RequestRetire();
bool IsRetiring();
void Shutdown();

} // namespace Lifecycle
