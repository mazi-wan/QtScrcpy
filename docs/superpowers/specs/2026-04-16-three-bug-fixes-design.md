# Design: Three Bug Fixes

Date: 2026-04-16

## Overview

Three independent bugs affecting persistence and rendering:

1. **Check state not persisting** — selected devices in the left pane are lost on restart.
2. **Detach/attach black screen** — popping out a device window renders nothing; re-attaching is also black.
3. **Left pane state not persisted** — the panel is always closed on startup regardless of last state.

---

## Fix 1: Device check state persistence

### Root cause

`applyCheckStateToItem` (`dialog.cpp:907`) calls `item->setCheckState()` before `item->setData(Qt::UserRole, serial)`. The moment `setCheckState` is called, Qt emits `itemChanged`, which triggers `onDeviceItemChanged`. At that point the item's `Qt::UserRole` is still empty, so `onDeviceItemChanged` cannot save the serial. This overwrites the config with an incomplete list, corrupting check state for every subsequent item in the same rebuild.

### Change

In `applyCheckStateToItem`, swap the two lines so `setData` is called before `setCheckState`:

```cpp
// fixed order:
item->setData(Qt::UserRole, serial);   // serial is ready first
item->setCheckState(checked.contains(serial) ? Qt::Checked : Qt::Unchecked);
```

**Files changed:** `QtScrcpy/ui/dialog.cpp`

---

## Fix 2: Detach/attach black screen

### Root cause

`DeviceTile::detach()` calls `m_videoForm->setParent(nullptr)`, which destroys the native window handle. The `QYUVOpenGLWidget` inside `VideoForm` has its OpenGL context tied to that handle. After reparenting, the context is invalid until `initializeGL()` is called on first paint. However, frames keep arriving and `updateTextures()` calls `makeCurrent()` before `initializeGL()` has run for the new context — textures are uploaded into a dead context, so nothing renders. The same problem recurs on `attach()` when VideoForm is reparented back into the tile.

### Change

Add `VideoForm::reinitVideoWidget()` — a one-liner that hides then re-shows the `QYUVOpenGLWidget`. This forces Qt to call `initializeGL()` immediately, establishing a valid context before the next texture upload.

```cpp
// videoform.h — new public method:
void reinitVideoWidget();

// videoform.cpp:
void VideoForm::reinitVideoWidget()
{
    if (m_videoWidget && !m_videoWidget->isHidden()) {
        m_videoWidget->hide();
        m_videoWidget->show();
    }
}
```

Call it in `DeviceTile` after each `show()`:

```cpp
// detach():
m_videoForm->setParent(nullptr);
m_videoForm->show();
m_videoForm->reinitVideoWidget();

// attach():
m_videoForm->setParent(m_videoLayout->parentWidget());
m_videoLayout->insertWidget(0, m_videoForm);
m_videoForm->setFocusPolicy(Qt::StrongFocus);
m_videoForm->show();
m_videoForm->reinitVideoWidget();
```

**Files changed:** `QtScrcpy/ui/videoform.h`, `QtScrcpy/ui/videoform.cpp`, `QtScrcpy/ui/devicetile.cpp`

---

## Fix 3: Left panel open state persistence

### Root cause

`m_panelOpen` is hardcoded to `false` and the panel container always starts at `x = -panelWidth`. There is no save or restore of this state.

### Change

**Config layer** — add two methods to `Config`, stored in the `[common]` group under key `"leftPanelOpen"`, default `false`. Pattern is identical to `getCheckedDevices` / `setCheckedDevices`.

```cpp
// config.h:
bool getPanelOpen();
void setPanelOpen(bool open);
```

**Dialog — save on toggle:** In the `m_toggleBtn` clicked lambda, call `Config::getInstance().setPanelOpen(m_panelOpen)` after `m_panelOpen` is updated (before starting the animation).

**Dialog — restore on startup:** After the panel container is built and positioned (after `m_panelContainer->move(-panelW, 0)`), read the saved state and apply it without animation:

```cpp
if (Config::getInstance().getPanelOpen()) {
    m_panelOpen = true;
    m_panelContainer->move(0, 0);
    m_toggleBtn->setText("◀");
}
```

**Files changed:** `QtScrcpy/util/config.h`, `QtScrcpy/util/config.cpp`, `QtScrcpy/ui/dialog.cpp`

---

## Non-goals

- No changes to auto-update frequency or the ADB rebuild logic.
- No changes to how `UserBootConfig` is structured.
- No changes to `DeviceDashboard` or tile grid layout.
