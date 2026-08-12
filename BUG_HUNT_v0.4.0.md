# Hagenware v0.4.0 Bug Hunt

## Goal

v0.4.0 is a stabilization release. The focus is reliability of the bare-Shift Deck trigger, the bare-Ctrl Grid trigger, surface dismissal/focus handoff, and recent supporting features. No new user-facing feature was intentionally added.

## Audit method

The codebase was reviewed in repeated passes rather than as one diff review:

1. **Input-state pass** — bare modifier detection, keyboard/mouse hook ordering, repeated input, suppression, queued messages.
2. **Surface-lifecycle pass** — Deck/Grid show, hide, dismissal, focus return, mouse pass-through, DWM thumbnail cleanup.
3. **Supporting-system pass** — screenshot indicator/capture, version handoff/lifecycle, shutdown/error paths, CI/release versioning.
4. **Regression-history pass** — reviewed the changes that introduced cross-surface input routing and the v0.3.5/v0.3.6 visibility-race attempt/revert.
5. **Post-fix pass** — re-read the rewritten input paths and corrected issues found in the stabilization patch itself before CI.

## Root cause of the unreliable Shift / Ctrl feel

The pre-0.4 input model depended on coordination between **two low-level keyboard hooks** and an **out-of-context WinEvent SHOW/HIDE hook**. The visibility callback installed and removed temporary surface input hooks after Windows queued the accessibility event.

That meant correctness depended on callback timing and hook ordering during rapid show/hide/reopen sequences. The previous v0.3.5 patch attempted to filter stale visibility callbacks, but the underlying architecture still depended on queued visibility events and the user reported no improvement.

v0.4 removes that timing dependency instead of adding another delay, debounce, or visibility check.

## Defects fixed

| Area | Defect | v0.4 fix |
| --- | --- | --- |
| Trigger architecture | Two keyboard hooks plus queued SHOW/HIDE events could disagree about which surface owned input. | One global keyboard hook now owns modifier state. Deck/Grid register and unregister active-surface state synchronously in their own Show/Hide paths. |
| Mouse + modifier | Shift-click / Ctrl-click could still count as a bare modifier because mouse activity never cancelled the trigger candidate. | A temporary mouse guard exists only while a modifier gesture or trigger dispatch is pending. Mouse buttons/wheel cancel the gesture. |
| Pre-held input | A bare-modifier candidate could begin even if another key or mouse button was already held before Shift/Ctrl was pressed. | Candidate creation now checks tracked key state plus current asynchronous key/button state before accepting the modifier as bare. |
| Repeated modifier state | Modifier auto-repeat and side/generic modifier aliases could produce ambiguous state. | Physical scan code/extended state and explicit key-down tracking are used; generic/left/right Shift and Ctrl aliases are treated as the same current modifier where appropriate. |
| Missed release recovery | A missed modifier release could leave suppression/candidate state stale until unrelated input happened. | On subsequent keyboard activity the state self-checks against the real asynchronous modifier state and clears stale suppression/candidates. |
| Queued trigger messages | `PostMessage` could leave an old Shift/Ctrl trigger queued after newer keyboard or mouse input, causing a delayed or wrong surface to appear. | Posted triggers carry a generation token. New relevant input invalidates the token; the host ignores stale trigger messages. |
| Rapid Deck/Grid crossing | Rapid Shift/Ctrl sequences could queue both surface requests and allow Grid to see a Hagenware surface as foreground. | Host coordination dismisses the other surface before opening the requested one, and Grid now rejects all current-process windows as placement targets. |
| Surface input lifecycle | Active-surface mouse/keyboard ownership was started/stopped by queued accessibility events. | Surface registration is direct and synchronous; the correctness-critical WinEvent hook is gone. |
| Deck pass-through latency | DWM thumbnail unregister/entry cleanup ran synchronously during pass-through dismissal, including paths entered from a low-level input callback. | Deck hides and restores focus first, then defers thumbnail cleanup to its normal message queue; reopening Deck first drains any pending cleanup safely. |
| Print Screen | Deck consumed `PrtSc` key-down but allowed the release half to continue through the system. | Deck now consumes both press and release while using Print Screen for Hagenware capture. |
| Screenshot badge lifecycle | The detached screenshot badge used another queued Deck SHOW/HIDE WinEvent observer and could inherit stale visibility ordering. | Deck now shows/hides the badge directly in its synchronous lifecycle. |
| Message-loop error handling | `GetMessageW == -1` was treated like a normal quit and returned whatever happened to be in `wParam`. | The main loop distinguishes normal quit from API failure and returns an error code on failure. |
| Version handoff safety | CI verified `Version::kNumber` against the release branch but not the numeric fields used to calculate `kPacked` for instance handoff. | CI now requires `kMajor.kMinor.kPatch` to match `kNumber`, preventing silent handoff-order drift. |

## Deliberately unchanged

- Deck MRU ordering, numbered 1–9 shortcuts, DWM preview layout, mouse-card activation, and arrow/Space navigation.
- Grid's 3x3 placement geometry and numpad/click actions.
- Screenshot file format/location and whole-virtual-desktop capture.
- Graceful version handoff protocol and idle/busy lifecycle model.
- Minimalist visuals and the existing visible host window.
- No timers, debounce delays, polling loops, background services, persistence, telemetry, networking, or third-party dependencies were added.

## Resource and failure-path review

The audit rechecked DWM thumbnail unregister paths, GDI bitmap/DC ownership in screenshot capture, window-class/window teardown, WinEvent unhooking, low-level hook teardown, process/mutex handles in version handoff, and lifecycle busy-count symmetry. No additional concrete leak or corruption bug was found that justified unrelated refactoring in this release.

The screenshot capture itself remains posted off the low-level keyboard hook and runs on the normal UI message path, so BMP capture/file I/O does not execute inside the keyboard-hook callback.

## Validation matrix

Automated CI for the release branch must still pass the existing `/W4 /WX /O2 /MT` x64 build and System32-only runtime dependency gate. v0.4 also adds the numeric/display version consistency check described above.

Interactive Windows behavior cannot be physically exercised by GitHub's compile/dependency CI, so the intended desktop stress matrix is:

- repeatedly tap Shift and repeatedly tap Ctrl;
- rapidly alternate Shift → Ctrl → Shift and verify only the latest valid gesture wins;
- hold a normal key, then tap Shift/Ctrl — no Hagenware surface should open;
- Shift-click and Ctrl-click in normal applications — no Hagenware surface should open;
- hold a mouse button, tap Shift/Ctrl — no Hagenware surface should open;
- use Shift/Ctrl as normal modifiers with another keyboard key — no bare-trigger activation;
- open Deck/Grid and press an unrelated key — surface closes and the key reaches the original app;
- open Deck/Grid and click outside — surface closes and the click reaches the target;
- click inside Deck/Grid — normal Hagenware interaction remains active;
- Deck arrows, Space, top-row 1–9 and card clicks continue to work;
- Grid numpad 1–9 and cell clicks continue to work;
- Deck `PrtSc` creates one screenshot beside the EXE without invoking normal Print Screen behavior;
- launch v0.4.0 over a running older Hagenware and verify graceful takeover;
- deliberately spam Shift/Ctrl for an extended period and verify the trigger state does not become stuck.

## Expected result

The main behavioral change should be negative: Hagenware should interfere **less**. Shift and Ctrl should trigger only when they are genuinely used alone, rapid input should not resurrect stale surfaces, and Deck/Grid ownership should no longer depend on delayed visibility callbacks.
