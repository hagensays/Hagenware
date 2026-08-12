# IDEAS.md

This file contains non-authoritative design ideas for Hagenware.

`AGENTS.md` remains the authoritative repository and development rulebook. Nothing in this file should be treated as an implementation requirement unless explicitly requested.

## Floating Surface Direction

A possible long-term direction is for Hagenware to behave more like a lightweight background host with temporary floating surfaces than a traditional application built around one persistent main window.

- The current bare-Shift gesture is the entry point, but it should remain replaceable.
- The trigger should invoke Hagenware rather than being baked into any specific tool or surface.
- Hagenware can decide which temporary surface to show after a trigger.
- Individual surfaces should be separate, lightweight Win32 window/widget-like entities rather than pages inside a large permanent application shell.
- Surfaces should appear only when needed and disappear cleanly when the interaction is finished.
- The current minimalist white surface with black borders is a good visual baseline for these floating entities.
- `Deck` is the current Alt+Tab-style window-switching surface.
- `Grid` is the current Ctrl-triggered window-sizing and placement surface.
- Possible future surfaces could include a launcher, command palette, contextual tools, or other small widgets.
- Avoid introducing a giant persistent shell, nested-page architecture, or other application chrome unless a concrete feature eventually needs it.
- If a visible main Hagenware window stops serving a useful purpose, it could eventually be removed in favor of only the minimal hidden/message-host plumbing actually required by the system.

## Separation of Responsibilities

Keep the interaction conceptually simple:

`trigger -> Hagenware/surface coordinator -> selected surface`

The trigger mechanism should not know what UI it launches, and individual surfaces should not know which shortcut or gesture invoked them. This keeps it easy to replace bare Shift or Ctrl later without rewriting Deck, Grid, or future widgets.

Use modularity where it improves clarity or replaceability, but do not create layers or modules merely to match this diagram.

## Status

These are planning ideas, not committed architecture. Implement them incrementally only when a real requested feature benefits from them.
