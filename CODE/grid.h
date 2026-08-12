#pragma once

#include <windows.h>

namespace Grid {

bool Initialize(HINSTANCE instance);
void Show();
void Hide();
void DismissForPassThrough();
void Shutdown();

} // namespace Grid
