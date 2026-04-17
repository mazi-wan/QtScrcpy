# Design: Panel Default Open & Per-Tile Hotkey Fix

**Date:** 2026-04-17
**Branch:** local/dev

---

## Overview

Two independent bug fixes:

1. **Panel always open on startup** — The left control panel must be open every time the app launches, regardless of any previously persisted state.
2. **Hotkeys work per-focused tile** — Keyboard shortcuts (e.g. `Ctrl+P`) must fire for the currently focused device tile when multiple tiles are open in the same Dialog window.

---

## Fix 1 — Panel Always Open on Startup

### Problem

`Dialog` constructor reads `Config::getPanelOpen()` to restore the last-used panel state. The stored default is `false`, so the panel starts closed on first run and stays closed if the user never opened it. Even after the user opens the panel, a future run may restore it to closed if they closed it last.

### Decision

Panel always starts open. The persisted value is never read on startup.

### Change

**File:** `QtScrcpy/ui/dialog.cpp`

Remove the conditional `if (Config::getInstance().getPanelOpen()) { ... }` block that restores panel state on startup. Replace it with unconditional open-state initialization:

- Set `m_panelOpen = true`
- Move panel container to `(0, 0)`
- Set toggle button text to `"◀"`
- Position toggle button at `(panelW, height() / 2 - 15)`

The toggle handler (`setPanelOpen` calls) is unchanged — users can still close the panel at runtime and the state is still written to config (for future use), but it is never read at startup.

### Scope

- `QtScrcpy/ui/dialog.cpp` — constructor only (~5 lines changed)
- No changes to `config.cpp`, `config.h`, or any other file

---

## Fix 2 — Per-Tile Hotkeys with Multiple Tiles

### Problem

`VideoForm::installShortcut()` creates ~26 `QShortcut` objects with the default `Qt::WindowShortcut` context. When multiple `VideoForm` tiles are embedded in the same top-level `Dialog` window, each tile registers identical key sequences (e.g. `Ctrl+P`) in the same window context. Qt detects the ambiguity and fires the `activatedAmbiguously` signal instead of `activated` — so no shortcut handler runs.

Raw key events (`keyPressEvent`) still reach the focused widget because they bypass the shortcut system entirely, which is why typing and mouse events continue to work.

### Decision

Change all `QShortcut` instances in `VideoForm::installShortcut()` to use `Qt::WidgetWithChildrenShortcut` context. A shortcut with this context only activates when its parent `VideoForm` (or any of its child widgets, such as the OpenGL video renderer) currently holds keyboard focus. With each tile having distinct focus ownership, there is no ambiguity.

### Change

**File:** `QtScrcpy/ui/videoform.cpp`, `installShortcut()` (lines 190–365)

Every `QShortcut` construction changes from:

```cpp
shortcut = new QShortcut(QKeySequence("Ctrl+p"), this);
```

to:

```cpp
shortcut = new QShortcut(QKeySequence("Ctrl+p"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
```

This applies to all ~26 shortcuts in the method. No other files require changes.

### Scope

- `QtScrcpy/ui/videoform.cpp` — `installShortcut()` only (~26 constructor calls updated)
- No changes to focus policy, `keyPressEvent`, `GroupController`, or `DeviceTile`

---

## Non-Goals

- No changes to how panel state is persisted (writes still happen on toggle)
- No changes to multi-device `GroupController` sync logic
- No changes to detached/pop-out window behavior

---

## Testing

**Fix 1:**
- Launch app fresh (no prior config) → panel must be open
- Close panel, restart app → panel must be open again (not restored to closed)

**Fix 2:**
- Connect 2+ devices, open tiles side-by-side
- Click on one tile to focus it
- Press `Ctrl+P` → only that device's screen should toggle power
- Click a different tile, press `Ctrl+P` → only that device responds
- Single device (regression): shortcuts must still work normally
