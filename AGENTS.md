# AGENTS.md

## Stack

- C++ + raw Win32 only.
- No frameworks.
- No third-party runtime dependencies without explicit approval.
- Assume only Windows system components are available.
- Release builds should use `/MT`.
- Prefer a single self-contained `.exe`.
- All source code and project files belong in `CODE/`.

## Code

Keep it simple, native, and easy for another AI agent to understand.

Avoid unnecessary abstractions, dependencies, metaprogramming, generated architecture, and unrelated refactors.

Use Windows APIs when Windows already provides the functionality.

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
