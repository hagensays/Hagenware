#pragma once

#include <windows.h>

namespace WindowPlacement {

bool Initialize(HINSTANCE instance);
void Show();
void Hide();
void DismissForPassThrough();
void Shutdown();

} // namespace WindowPlacement
