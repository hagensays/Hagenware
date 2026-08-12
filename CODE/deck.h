#pragma once

#include <windows.h>

namespace Deck {

bool Initialize(HINSTANCE instance);
void Show();
void Hide();
void DismissForPassThrough();
void Shutdown();

} // namespace Deck
