#pragma once

#include <windows.h>

namespace Grid {

bool Initialize(HINSTANCE instance);
void Show();
void Hide();
void DismissForPassThrough();
bool IsVisible();
void Shutdown();

} // namespace Grid
