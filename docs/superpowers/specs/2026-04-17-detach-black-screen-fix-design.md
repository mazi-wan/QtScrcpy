# Design: Fix Detach Device Screen Black Screen

**Date:** 2026-04-17  
**Branch:** local/dev  
**Status:** Approved

## Problem

When a device tile is "detached" (popped out into a standalone window), the video display goes black. Re-attaching (closing the pop-out window) works correctly.

### Root Cause

`QYUVOpenGLWidget::initializeGL()` does not reset `m_textureInited` or `m_needUpdate` when called after a GL context recreation.

**What happens on detach:**

1. `DeviceTile::detach()` calls `m_videoForm->setParent(nullptr)` to make VideoForm a top-level window
2. Qt destroys `QYUVOpenGLWidget`'s native window and its GL context along with it
3. `m_textureInited` remains `true` (stale) — it was `true` while video was streaming
4. `VideoForm::reinitVideoWidget()` calls `hide()` + `show()` on `m_videoWidget`, scheduling a repaint
5. `initializeGL()` fires on the new context — sets up shaders and VBO — but **does not touch `m_textureInited` or `m_needUpdate`**
6. `paintGL()` runs: `m_needUpdate == false` so no reinit; `m_textureInited == true` so it tries to bind old invalid texture IDs → **black screen**

### Why `reinitVideoWidget()` Alone Is Insufficient

The hide/show trick in `reinitVideoWidget()` correctly triggers context recreation (forces `initializeGL()`) but cannot fix the stale state *inside* the GL widget. The stale texture state must be reset from within `initializeGL()` itself.

## Fix

**Approach A: Reset texture state in `initializeGL()`**

Add two lines to the end of `QYUVOpenGLWidget::initializeGL()`:

```cpp
m_textureInited = false;
if (m_frameSize.isValid()) m_needUpdate = true;
```

**Why this works:**

- `m_textureInited = false` prevents `updateTextures()` from uploading to invalid texture IDs during the window between context destruction and the first new `paintGL()`
- `m_needUpdate = true` causes `paintGL()` to call `initTextures()`, creating fresh texture objects in the new context
- Normal frame flow (`updateRender()` → `updateTextures()` → `update()` → `paintGL()`) then resumes correctly

**Frame flow after fix:**

```
setParent(nullptr) called on VideoForm
    → QYUVOpenGLWidget's GL context destroyed (m_textureInited still true briefly)

reinitVideoWidget() → hide() + show() → schedules repaint (show event)
    → show event fires → initializeGL() runs before first paintGL()
    → shaders and VBO recreated
    → m_textureInited = false  ← NEW
    → m_needUpdate = true      ← NEW (if frame size valid)
    → paintGL() runs (same pass as initializeGL)
    → m_needUpdate = true → deInitTextures() + initTextures() → empty textures created
    → m_textureInited = true, m_needUpdate = false

Frames continue arriving → updateTextures()
    → m_textureInited = true → uploads texture data → update()
    → paintGL() → binds valid textures → video renders correctly
```

Note: any `updateTextures()` calls that arrive *before* `initializeGL()` runs (due to queued frame signals from the decoder thread) have `m_textureInited = true` (stale), so they call into the dead context and fail silently. This is the accepted race window documented in Trade-offs.

## Files Changed

| File | Change |
|------|--------|
| `QtScrcpy/render/qyuvopenglwidget.cpp` | 2 lines added to `initializeGL()` |
| `QtScrcpy/test/tst_qyuvopenglwidget.cpp` | New: TDD tests (written first) |
| `QtScrcpy/test/CMakeLists.txt` | New: test build config |

No changes to `devicetile.cpp` or `videoform.cpp` — the fix is self-contained in the GL widget.

## TDD Test Plan

Tests are written **before** the fix. Each test must fail before the fix and pass after.

A `TestableQYUVOpenGLWidget` subclass (or friend class) exposes `m_textureInited`, `m_needUpdate`, and `m_frameSize` for inspection.

All tests require a `QApplication` and a valid GL surface. Use `QOpenGLWidget` + `QOffscreenSurface` pattern for headless CI.

### Test 1 — `initializeGL` resets `m_textureInited`

**Given:** `m_textureInited = true`, `m_needUpdate = false`  
**When:** `initializeGL()` is called  
**Then:** `m_textureInited == false`

### Test 2 — `initializeGL` sets `m_needUpdate` when frame size is valid

**Given:** `m_frameSize = QSize(1920, 1080)`, `m_needUpdate = false`  
**When:** `initializeGL()` is called  
**Then:** `m_needUpdate == true`

### Test 3 — `initializeGL` does not set `m_needUpdate` when frame size is invalid

**Given:** `m_frameSize = QSize()` (default invalid), `m_needUpdate = false`  
**When:** `initializeGL()` is called  
**Then:** `m_needUpdate == false`

### Test 4 — `updateTextures` is a no-op when `m_textureInited = false`

**Given:** `m_textureInited = false`  
**When:** `updateTextures()` is called with dummy data  
**Then:** Returns without crash; no `update()` triggered

## Trade-offs

**Accepted:** A small race window exists between GL context destruction and `initializeGL()` being called. During this window, incoming frames call `updateTextures()` with stale `m_textureInited = true`. These GL calls fail silently on a dead context — harmless in practice, but impure. The canonical fix (connecting to `QOpenGLContext::aboutToBeDestroyed`) closes this window but adds complexity. We accept the race for now; it can be addressed separately.

## Out of Scope

- Re-attach black screen (not reported; confirmed working)
- `aboutToBeDestroyed` signal cleanup (Approach B; separate story if needed)
- Any changes to `DeviceTile::detach()` or `VideoForm::reinitVideoWidget()`
