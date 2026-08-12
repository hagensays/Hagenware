#include "trigger.h"

namespace {
enum class ModifierTrigger {
    None,
    Shift,
    Control,
};

HHOOK g_hook = nullptr;
HWND g_targetWindow = nullptr;
UINT g_shiftTriggerMessage = 0;
UINT g_controlTriggerMessage = 0;
ModifierTrigger g_candidate = ModifierTrigger::None;
DWORD g_candidateScanCode = 0;
ModifierTrigger g_suppressed = ModifierTrigger::None;

bool IsShiftKey(DWORD virtual_key) {
    return virtual_key == VK_SHIFT || virtual_key == VK_LSHIFT || virtual_key == VK_RSHIFT;
}

bool IsControlKey(DWORD virtual_key) {
    return virtual_key == VK_CONTROL || virtual_key == VK_LCONTROL || virtual_key == VK_RCONTROL;
}

ModifierTrigger ModifierForKey(DWORD virtual_key) {
    if (IsShiftKey(virtual_key)) {
        return ModifierTrigger::Shift;
    }
    if (IsControlKey(virtual_key)) {
        return ModifierTrigger::Control;
    }
    return ModifierTrigger::None;
}

void CancelCandidate() {
    g_candidate = ModifierTrigger::None;
    g_candidateScanCode = 0;
}

void PostTrigger(ModifierTrigger trigger) {
    if (g_targetWindow == nullptr) {
        return;
    }

    UINT message = 0;
    if (trigger == ModifierTrigger::Shift) {
        message = g_shiftTriggerMessage;
    } else if (trigger == ModifierTrigger::Control) {
        message = g_controlTriggerMessage;
    }

    if (message != 0) {
        PostMessageW(g_targetWindow, message, 0, 0);
    }
}

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        const bool key_down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
        const bool key_up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;
        const ModifierTrigger modifier = ModifierForKey(key->vkCode);

        if (g_suppressed != ModifierTrigger::None) {
            if (key_up && modifier == g_suppressed) {
                g_suppressed = ModifierTrigger::None;
            }
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }

        if (key_down) {
            if (g_candidate == ModifierTrigger::None) {
                if (modifier != ModifierTrigger::None) {
                    g_candidate = modifier;
                    g_candidateScanCode = key->scanCode;
                }
            } else if (modifier != g_candidate || key->scanCode != g_candidateScanCode) {
                CancelCandidate();
            }
        } else if (key_up && g_candidate != ModifierTrigger::None) {
            if (modifier == g_candidate && key->scanCode == g_candidateScanCode) {
                const ModifierTrigger completed = g_candidate;
                CancelCandidate();
                PostTrigger(completed);
            } else {
                CancelCandidate();
            }
        }
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}
} // namespace

namespace Trigger {

bool Start(HWND target_window, UINT shift_trigger_message, UINT control_trigger_message) {
    Stop();

    g_targetWindow = target_window;
    g_shiftTriggerMessage = shift_trigger_message;
    g_controlTriggerMessage = control_trigger_message;
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleW(nullptr), 0);

    if (g_hook == nullptr) {
        g_targetWindow = nullptr;
        g_shiftTriggerMessage = 0;
        g_controlTriggerMessage = 0;
        return false;
    }

    return true;
}

void SuppressModifierUntilRelease(DWORD virtual_key) {
    const ModifierTrigger modifier = ModifierForKey(virtual_key);
    if (modifier == ModifierTrigger::None) {
        return;
    }

    CancelCandidate();
    g_suppressed = modifier;
}

void Stop() {
    if (g_hook != nullptr) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }

    g_targetWindow = nullptr;
    g_shiftTriggerMessage = 0;
    g_controlTriggerMessage = 0;
    CancelCandidate();
    g_suppressed = ModifierTrigger::None;
}

} // namespace Trigger
