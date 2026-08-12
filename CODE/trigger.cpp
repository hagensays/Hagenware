#include "trigger.h"

#include <array>

#include "input_dismiss.h"

namespace {
enum class ModifierTrigger {
    None,
    Shift,
    Control,
};

HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseGuardHook = nullptr;
HWND g_targetWindow = nullptr;
UINT g_shiftTriggerMessage = 0;
UINT g_controlTriggerMessage = 0;

ModifierTrigger g_candidate = ModifierTrigger::None;
DWORD g_candidateVirtualKey = 0;
DWORD g_candidateScanCode = 0;
bool g_candidateExtended = false;

ModifierTrigger g_suppressed = ModifierTrigger::None;
DWORD g_suppressedVirtualKey = 0;
DWORD g_suppressedScanCode = 0;
bool g_suppressedExtended = false;

UINT g_pendingMessage = 0;
UINT g_pendingToken = 0;
UINT g_nextToken = 0;

std::array<bool, 256> g_keyDown{};

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

bool IsExtendedKey(const KBDLLHOOKSTRUCT& key) {
    return (key.flags & LLKHF_EXTENDED) != 0;
}

bool IsMouseCancellationInput(WPARAM message) {
    return message == WM_LBUTTONDOWN ||
        message == WM_LBUTTONUP ||
        message == WM_RBUTTONDOWN ||
        message == WM_RBUTTONUP ||
        message == WM_MBUTTONDOWN ||
        message == WM_MBUTTONUP ||
        message == WM_XBUTTONDOWN ||
        message == WM_XBUTTONUP ||
        message == WM_MOUSEWHEEL ||
        message == WM_MOUSEHWHEEL;
}

bool IsTrackedKey(DWORD virtual_key) {
    return virtual_key < static_cast<DWORD>(g_keyDown.size());
}

bool WasKeyDown(DWORD virtual_key) {
    return IsTrackedKey(virtual_key) && g_keyDown[static_cast<size_t>(virtual_key)];
}

void SetKeyDown(DWORD virtual_key, bool down) {
    if (IsTrackedKey(virtual_key)) {
        g_keyDown[static_cast<size_t>(virtual_key)] = down;
    }
}

bool IsCurrentModifierAlias(DWORD query_key, DWORD current_key) {
    if (query_key == current_key) {
        return true;
    }
    if (IsShiftKey(current_key) && IsShiftKey(query_key)) {
        return true;
    }
    if (IsControlKey(current_key) && IsControlKey(query_key)) {
        return true;
    }
    return false;
}

bool AnyOtherInputDown(DWORD current_key) {
    for (DWORD virtual_key = 1; virtual_key < static_cast<DWORD>(g_keyDown.size()); ++virtual_key) {
        if (IsCurrentModifierAlias(virtual_key, current_key)) {
            continue;
        }

        if (g_keyDown[static_cast<size_t>(virtual_key)] ||
            (GetAsyncKeyState(static_cast<int>(virtual_key)) & 0x8000) != 0) {
            return true;
        }
    }

    return false;
}

bool MatchesCandidate(const KBDLLHOOKSTRUCT& key) {
    return g_candidate != ModifierTrigger::None &&
        ModifierForKey(key.vkCode) == g_candidate &&
        key.scanCode == g_candidateScanCode &&
        IsExtendedKey(key) == g_candidateExtended;
}

bool MatchesSuppressed(const KBDLLHOOKSTRUCT& key) {
    return g_suppressed != ModifierTrigger::None &&
        ModifierForKey(key.vkCode) == g_suppressed &&
        key.scanCode == g_suppressedScanCode &&
        IsExtendedKey(key) == g_suppressedExtended;
}

void ClearCandidateFields() {
    g_candidate = ModifierTrigger::None;
    g_candidateVirtualKey = 0;
    g_candidateScanCode = 0;
    g_candidateExtended = false;
}

void ClearSuppressed() {
    g_suppressed = ModifierTrigger::None;
    g_suppressedVirtualKey = 0;
    g_suppressedScanCode = 0;
    g_suppressedExtended = false;
}

bool MouseGuardNeeded() {
    return g_candidate != ModifierTrigger::None || g_pendingToken != 0;
}

LRESULT CALLBACK MouseGuardProc(int code, WPARAM wparam, LPARAM lparam);

bool EnsureMouseGuard() {
    if (g_mouseGuardHook != nullptr) {
        return true;
    }

    g_mouseGuardHook = SetWindowsHookExW(
        WH_MOUSE_LL,
        MouseGuardProc,
        GetModuleHandleW(nullptr),
        0);
    return g_mouseGuardHook != nullptr;
}

void StopMouseGuardIfIdle() {
    if (!MouseGuardNeeded() && g_mouseGuardHook != nullptr) {
        UnhookWindowsHookEx(g_mouseGuardHook);
        g_mouseGuardHook = nullptr;
    }
}

void CancelCandidate() {
    ClearCandidateFields();
    StopMouseGuardIfIdle();
}

void InvalidatePendingTrigger() {
    g_pendingMessage = 0;
    g_pendingToken = 0;
    StopMouseGuardIfIdle();
}

void CancelCandidateAndPending() {
    ClearCandidateFields();
    g_pendingMessage = 0;
    g_pendingToken = 0;
    StopMouseGuardIfIdle();
}

LRESULT CALLBACK MouseGuardProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && IsMouseCancellationInput(wparam)) {
        CancelCandidateAndPending();
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}

UINT MessageForTrigger(ModifierTrigger trigger) {
    if (trigger == ModifierTrigger::Shift) {
        return g_shiftTriggerMessage;
    }
    if (trigger == ModifierTrigger::Control) {
        return g_controlTriggerMessage;
    }
    return 0;
}

void PostTrigger(ModifierTrigger trigger) {
    const UINT message = MessageForTrigger(trigger);
    if (g_targetWindow == nullptr || message == 0) {
        StopMouseGuardIfIdle();
        return;
    }

    ++g_nextToken;
    if (g_nextToken == 0) {
        ++g_nextToken;
    }

    g_pendingMessage = message;
    g_pendingToken = g_nextToken;
    EnsureMouseGuard();

    if (PostMessageW(
            g_targetWindow,
            message,
            static_cast<WPARAM>(g_pendingToken),
            0) == FALSE) {
        InvalidatePendingTrigger();
    }
}

bool BeginCandidate(const KBDLLHOOKSTRUCT& key, ModifierTrigger modifier) {
    if (modifier == ModifierTrigger::None || AnyOtherInputDown(key.vkCode)) {
        return false;
    }

    g_candidate = modifier;
    g_candidateVirtualKey = key.vkCode;
    g_candidateScanCode = key.scanCode;
    g_candidateExtended = IsExtendedKey(key);

    if (!EnsureMouseGuard()) {
        ClearCandidateFields();
        return false;
    }

    return true;
}

void SuppressCurrentModifier(const KBDLLHOOKSTRUCT& key) {
    CancelCandidate();
    g_suppressed = ModifierForKey(key.vkCode);
    g_suppressedVirtualKey = key.vkCode;
    g_suppressedScanCode = key.scanCode;
    g_suppressedExtended = IsExtendedKey(key);
}

void RecoverSuppressionIfReleaseWasMissed(const KBDLLHOOKSTRUCT& key, bool key_up) {
    if (g_suppressed == ModifierTrigger::None) {
        return;
    }

    if (key_up && MatchesSuppressed(key)) {
        return;
    }

    if ((GetAsyncKeyState(static_cast<int>(g_suppressedVirtualKey)) & 0x8000) == 0) {
        ClearSuppressed();
    }
}

void RecoverCandidateIfReleaseWasMissed(const KBDLLHOOKSTRUCT& key, bool key_up) {
    if (g_candidate == ModifierTrigger::None) {
        return;
    }

    if (key_up && MatchesCandidate(key)) {
        return;
    }

    if ((GetAsyncKeyState(static_cast<int>(g_candidateVirtualKey)) & 0x8000) == 0) {
        CancelCandidate();
    }
}

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code != HC_ACTION) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
    const bool key_down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
    const bool key_up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;
    if (!key_down && !key_up) {
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    RecoverSuppressionIfReleaseWasMissed(*key, key_up);
    RecoverCandidateIfReleaseWasMissed(*key, key_up);

    if (key_down) {
        InvalidatePendingTrigger();
    }

    const InputDismiss::KeyboardAction surface_action =
        InputDismiss::HandleKeyboard(key->vkCode, key_down, key_up);

    if (surface_action == InputDismiss::KeyboardAction::SuppressTrigger && key_down) {
        SuppressCurrentModifier(*key);
        SetKeyDown(key->vkCode, true);
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    if (g_suppressed != ModifierTrigger::None) {
        if (key_up && MatchesSuppressed(*key)) {
            ClearSuppressed();
        }
        SetKeyDown(key->vkCode, key_down);
        return surface_action == InputDismiss::KeyboardAction::Consume
            ? 1
            : CallNextHookEx(nullptr, code, wparam, lparam);
    }

    const ModifierTrigger modifier = ModifierForKey(key->vkCode);
    const bool was_down = WasKeyDown(key->vkCode);

    if (key_down) {
        if (!was_down) {
            if (g_candidate == ModifierTrigger::None) {
                BeginCandidate(*key, modifier);
            } else if (!MatchesCandidate(*key)) {
                CancelCandidate();
            }
        }
        SetKeyDown(key->vkCode, true);
    } else {
        if (g_candidate != ModifierTrigger::None) {
            if (MatchesCandidate(*key)) {
                const ModifierTrigger completed = g_candidate;
                ClearCandidateFields();
                PostTrigger(completed);
            } else {
                CancelCandidate();
            }
        }
        SetKeyDown(key->vkCode, false);
    }

    if (surface_action == InputDismiss::KeyboardAction::Consume) {
        return 1;
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}
} // namespace

namespace Trigger {

bool Start(HWND target_window, UINT shift_trigger_message, UINT control_trigger_message) {
    Stop();

    if (target_window == nullptr || shift_trigger_message == 0 || control_trigger_message == 0) {
        return false;
    }

    g_targetWindow = target_window;
    g_shiftTriggerMessage = shift_trigger_message;
    g_controlTriggerMessage = control_trigger_message;
    g_keyDown.fill(false);

    g_keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        KeyboardHookProc,
        GetModuleHandleW(nullptr),
        0);

    if (g_keyboardHook == nullptr) {
        g_targetWindow = nullptr;
        g_shiftTriggerMessage = 0;
        g_controlTriggerMessage = 0;
        return false;
    }

    return true;
}

bool AcceptPostedTrigger(UINT message, WPARAM token) {
    if (message == 0 ||
        token == 0 ||
        message != g_pendingMessage ||
        static_cast<UINT>(token) != g_pendingToken) {
        return false;
    }

    g_pendingMessage = 0;
    g_pendingToken = 0;
    StopMouseGuardIfIdle();
    return true;
}

void Stop() {
    if (g_mouseGuardHook != nullptr) {
        UnhookWindowsHookEx(g_mouseGuardHook);
        g_mouseGuardHook = nullptr;
    }

    if (g_keyboardHook != nullptr) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }

    g_targetWindow = nullptr;
    g_shiftTriggerMessage = 0;
    g_controlTriggerMessage = 0;
    ClearCandidateFields();
    ClearSuppressed();
    g_pendingMessage = 0;
    g_pendingToken = 0;
    g_nextToken = 0;
    g_keyDown.fill(false);
}

} // namespace Trigger
