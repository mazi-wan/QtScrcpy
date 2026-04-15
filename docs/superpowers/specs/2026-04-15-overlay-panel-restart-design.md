# Design: Overlay Left Pane + Restart All Streams

**Date:** 2026-04-15
**Branch:** local/dev

---

## Overview

Two features:
1. **Overlay left pane** — the control panel slides over the dashboard instead of sitting beside it, toggled by a fixed button at the left edge
2. **Restart All Streams** — a button that disconnects all devices and immediately reconnects them with recalculated settings (primarily `max_size` based on current tile layout)

---

## 1. Overlay Left Pane

### Layout change

`leftWidget` is removed from `horizontalLayout_11` and re-parented directly as a **child of Dialog** with absolute manual positioning. The dashboard (`DeviceDashboard`) fills the full Dialog width as the only item in the horizontal layout. `leftWidget` floats on top at z-order above the dashboard.

```
Dialog (full width)
├── DeviceDashboard  ← fills entire width, sits behind the panel
├── leftWidget       ← absolutely positioned overlay, z-order above dashboard
└── toggleBtn        ← small fixed button at left edge, always visible
```

### Toggle button

The existing `◀/▶` toggle button is repositioned to a **fixed absolute position** at the left edge of the Dialog:
- Position: `x=0, y=Dialog.height()/2 - 15`
- Size: `18×30` px
- Always visible regardless of panel open/closed state
- Text: `◀` when panel is open, `▶` when panel is closed

### Slide animation

`QPropertyAnimation` on `leftWidget->pos()`:
- **Open:** `x = -leftWidget->width()` → `x = 0`, duration 200ms, `QEasingCurve::OutCubic`
- **Close:** `x = 0` → `x = -leftWidget->width()`, duration 200ms, `QEasingCurve::InCubic`

Initial state: panel closed (off-screen to the left). Opens only when toggle button is clicked.

### Resize handling

`Dialog::resizeEvent` must:
1. Resize `leftWidget` height to match `event->size().height()`
2. Reposition toggle button to `x=0, y=newHeight/2 - 15`
3. If panel is open: keep `leftWidget` at `x=0` (no adjustment needed for x)
4. If panel is closed: keep `leftWidget` at `x=-leftWidget->width()`

### Files changed

| File | Change |
|---|---|
| `QtScrcpy/ui/dialog.cpp` | Remove `leftWidget` from layout, re-parent as overlay, create `QPropertyAnimation`, handle `resizeEvent` |
| `QtScrcpy/ui/dialog.h` | Add `QPointer<QPropertyAnimation> m_panelAnim`, `bool m_panelOpen = false` |

---

## 2. Restart All Streams

### Stored params

`Dialog` maintains:
```cpp
QMap<QString, qsc::DeviceParams> m_connectedParams;  // serial → original params
quint16 m_userMaxSize = 0;  // the raw value from maxSizeBox at connect time (before tile-cap)
```

- `m_connectedParams` is populated in `onDeviceConnected` (store) and cleaned in `onDeviceDisconnected` (remove)
- The stored `DeviceParams.maxSize` holds the **original user-configured value** (before tile-size capping), so restart can re-apply `min(originalUserMaxSize, newTileSize)` correctly

### Button

A `QPushButton` labeled **"Restart All"** added to `dialog.ui` in the left pane, near the existing "Stop All" button.

### Restart sequence

```
slot on_restartAllBtn_clicked():
1. snapshot = copy of m_connectedParams (avoids modification during iteration)
2. IDeviceManage::disconnectAllDevice()
3. for each (serial, params) in snapshot:
     a. updatedParams = params
     b. autoSize = m_dashboard->optimalMaxSize()
     c. if autoSize > 0:
            updatedParams.maxSize = (params.maxSize == 0)
                                    ? autoSize
                                    : qMin(params.maxSize, autoSize)
     d. IDeviceManage::connectDevice(updatedParams)
```

Steps 2 and 3 are called in immediate sequence — no async waiting. The device manager handles connection lifecycle internally.

### What recalculates on restart

| Field | Behavior |
|---|---|
| `maxSize` | Recalculated: `min(originalUserSetting, currentTileSize)` |
| `bitRate` | Carried over unchanged |
| `serial` | Carried over unchanged |
| All other params | Carried over unchanged |

### Files changed

| File | Change |
|---|---|
| `QtScrcpy/ui/dialog.ui` | Add `restartAllBtn` QPushButton near `stopAllServerBtn` |
| `QtScrcpy/ui/dialog.h` | Add `m_connectedParams` map, `on_restartAllBtn_clicked` slot |
| `QtScrcpy/ui/dialog.cpp` | Populate/clear map in connect/disconnect slots, implement restart logic |

---

## Out of Scope

- Per-device restart (only all-at-once)
- Animating individual tiles during restart
- Saving panel open/closed state across app restarts
