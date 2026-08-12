# AGENTS.md

This file is the authoritative rule set for Hagenware.

## Stack

- Windows only; x64 only unless explicitly approved otherwise.
- C++ + raw Win32 only.
- No frameworks.
- No third-party runtime dependencies without explicit approval.
- Assume only a clean supported Windows installation and Windows system components are available.
- Release builds should use `/MT`.
- Prefer one self-contained `.exe`.
- Use Windows facilities when Windows already provides the functionality.
- All source code and project files belong in `CODE/`.

## Code

Keep the codebase simple, native, lean, and easy for another AI agent to understand.

A lean codebase does not mean minimizing lines of code. Use as many lines as needed for clear, maintainable, explicit code while avoiding unnecessary code, abstractions, dependencies, metaprogramming, generated architecture, and unrelated refactors.

- Preserve the current minimalist visual aesthetic. Do not redesign, embellish, or add visual complexity unless explicitly requested.
- Prefer local, explicit state; avoid hidden globals, unnecessary singletons, and complicated shared mutable state.
- Use Unicode Win32 APIs (`...W`) consistently.
- Prefer event-driven Win32 messages, notifications, waits, and callbacks over polling.
- Idle means idle: avoid unnecessary threads, timers, polling, CPU use, disk access, and network activity.
- Run as a normal user. Do not require elevation unless the requested feature fundamentally needs it.
- No telemetry, update checks, or unsolicited network access unless explicitly requested.
- Do not create registry keys, AppData files, configs, caches, logs, databases, or other persistent state unless the feature needs them.
- Do not add speculative features or architecture. New services, updaters, IPC, persistence layers, networking, or similar subsystems require a concrete need.
- Prefer Windows facilities before custom implementations, but do not contort clear code merely to reduce line count.
- Fail cleanly: do not corrupt state, leak resources, or leave orphaned processes when an operation fails.
- Do not guess compatibility requirements. Windows support targets must be explicit; do not add compatibility hacks for unspecified or unsupported versions.
- Keep each version focused and reviewable; do not bundle unrelated refactors with a requested change.

## Development Speed

Development speed is one of the biggest wins of the current setup.

The speed comes from several choices lining up:

- tiny native C++ codebase
- raw Win32, no framework restore/build step
- no package manager
- no dependency graph
- no generated project layers
- one x64 Release build
- very small CI surface
- focused version branches
- small PRs
- automatic release packaging

Preserve development speed as a design objective. Avoid architecture or build choices that materially slow down iteration unless they buy something genuinely important.

## Version Workflow

Every release uses a branch named exactly:

`vX.Y.Z`

For every upgrade:

1. Start from current `main`.
2. Create `vX.Y.Z`.
3. Implement and test the change.
4. Push the branch.
5. Open a PR into `main`.
6. CI must pass before merge.
7. Never bypass or weaken CI.
8. Auto-merge may merge once CI passes.

Never develop a release directly on `main`.

## Release

After a successful merge into `main`, the release workflow must:

- create/tag `vX.Y.Z`
- build Release x64
- publish a GitHub Release
- attach `Hagenware.exe`
- attach `Hagenware Code.zip`
- include short release notes describing what changed

`Hagenware Code.zip` must contain the source code from `CODE/`.

A version is not released until the workflow succeeds.
