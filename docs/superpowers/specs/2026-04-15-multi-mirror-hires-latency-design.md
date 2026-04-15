# QtScrcpy Feature Design: Multi-Mirror, High Resolution, Low Latency

**Date:** 2026-04-15  
**Branch:** local/dev  
**Author:** rock-tran

---

## Overview

Three features to implement in QtScrcpy:

1. **Multiple screen mirroring** — Hybrid tiled dashboard with pop-out support
2. **High resolution** — Free-form resolution input supporting native Android resolution
3. **Low latency pipeline** — Frame pipeline tuning for minimum latency

---

## 1. Architecture Overview

### New Components

| Component | Location | Responsibility |
|---|---|---|
| `DeviceDashboard` | `QtScrcpy/ui/devicedashboard.{h,cpp}` | Tiled grid panel, owns thumbnail cells, handles pop-out |
| `DeviceTile` | `QtScrcpy/ui/devicetile.{h,cpp}` | Single cell — embedded mini VideoForm + label + pop-out/disconnect buttons |
| `LatencyMode` | `QtScrcpyCore/src/device/` (enum in device config) | Configures buffer depth and frame-drop policy per device |

### Component Relationships

```
Dialog (main window)
├── DeviceDashboard              ← NEW: central panel
│   ├── DeviceTile [device A]   ← NEW: wraps VideoForm
│   ├── DeviceTile [device B]
│   └── ...
└── [existing toolbar / config panel — unchanged]

DeviceManage (unchanged)
  └── IDevice per serial        ← DeviceDashboard observes existing signals

VideoForm (unchanged)
  └── reused as pop-out window when a tile is detached
```

`DeviceDashboard` connects to `DeviceManage`'s existing `deviceConnected` / `deviceDisconnected` signals. No changes to `DeviceManage` itself.

---

## 2. Feature: Multiple Screen Mirroring

### DeviceDashboard — Tiled Grid

- `QWidget` with a `QGridLayout` that auto-reflows based on device count
- Tiles resize proportionally on window resize; minimum tile size: 240×135px (16:9)
- Replaces the current empty center area of the main `Dialog`

**Grid layout rules:**
- 1 device → single tile fills the panel
- 2–4 devices → 2×2 grid
- 5+ devices → wrapping flow with scroll

### DeviceTile — Single Cell

Each tile contains:
- An embedded `VideoForm` scaled to fit the tile
- A thin header bar: device serial/name label
- **Pop-out button** (↗): detaches the `VideoForm` to a standalone window
- **Disconnect button** (×): disconnects the device

### Pop-out Behavior

1. User clicks ↗ on a tile
2. `VideoForm` is removed from the tile layout and re-parented as a top-level window (same as current single-device behavior)
3. The tile slot remains in the grid showing a "Detached" placeholder label
4. Closing the pop-out window re-embeds the `VideoForm` back into the tile

`VideoForm` is reused entirely unchanged — only its Qt parent changes.

---

## 3. Feature: High Resolution

### Current State

`dialog.cpp` uses a fixed `QComboBox` with entries `640, 720, 1080, 1280, 1920, original`. The selected index maps to a `max_size` value sent to the scrcpy server. `original` already sends `max_size=0`.

### Changes

**UI (`dialog.cpp` — config panel):**
- Replace the fixed `QComboBox` with an editable combo (`setEditable(true)`)
- Preset options: `640, 720, 1080, 1440, 1920, 2560, Native`
- `Native` sends `max_size=0` (no scaling)
- Free-text input accepts any integer in range `[0, 9999]`; invalid input reverts to last valid value

**Config (`config.h` / `UserBootConfig`):**
- Replace `maxSizeIndex` (int index into old list) with `maxSize` (int, where `0` = native)
- Migration: on load, if `maxSizeIndex` key is present, convert using the map `{0→640, 1→720, 2→1080, 3→1280, 4→1920, 5→0}` and write `maxSize`; remove the old key. If neither key exists, default to `0` (native).

**Server parameters (`server.cpp`):**
- `max_size` is already passed as a string — use the new `maxSize` int directly
- No other changes to the server launch path

---

## 4. Feature: Low Latency Pipeline

### Pipeline Stages

```
Android → [network] → VideoSocket → Demuxer → VideoBuffer → Decoder → render
```

### Root Causes of Latency

1. `VideoBuffer` depth > 1 — old frames queue up while newer frames arrive
2. No stale-frame dropping — the demuxer and decoder process every frame in order

### LatencyMode Flag

New enum added to the device startup config:

```cpp
enum LatencyMode { LatencyBalanced, LatencyLow };
```

- `LatencyLow` is opt-in via a **"Low latency mode" checkbox** in the UI config panel
- Default is `LatencyBalanced` to preserve existing behavior

### Pipeline Changes

**VideoBuffer (`videobuffer.cpp`):**
- In `LatencyLow`: cap decode queue to **1 frame**
- If a new frame arrives while the queue is full, drop the oldest and enqueue the new one
- The decoder always operates on the most recent frame

**Demuxer (`demuxer.cpp`):**
- In `LatencyLow`: after parsing a packet, check if newer data is already readable on the socket
- If so, skip the current packet (log as dropped) to prevent the demuxer from falling behind during bursts

**Decoder (`decoder.cpp`):**
- No structural changes — naturally processes fresh frames with a depth-1 buffer

**Render (`qyuvopenglwidget.cpp`):**
- Confirm `newFrame` signal uses `Qt::DirectConnection` to avoid cross-thread queuing delay
- Call `update()` immediately on frame arrival (verify existing behavior is correct)

### Trade-off

`LatencyLow` trades smoothness for responsiveness. Frame rate may appear less steady under poor network conditions. This is the intended trade-off for interactive use (gaming, remote control).

---

## 5. Files Changed Summary

| File | Change |
|---|---|
| `QtScrcpy/ui/dialog.cpp/.h` | Add `DeviceDashboard`, resolution free-form input, latency checkbox |
| `QtScrcpy/ui/devicedashboard.cpp/.h` | **NEW** — tiled grid panel |
| `QtScrcpy/ui/devicetile.cpp/.h` | **NEW** — single device tile |
| `QtScrcpy/util/config.h` | `maxSizeIndex` → `maxSize`, add `LatencyMode` to `UserBootConfig` |
| `QtScrcpyCore/src/device/decoder/videobuffer.cpp/.h` | LatencyLow depth-1 frame cap + drop logic |
| `QtScrcpyCore/src/device/demuxer/demuxer.cpp/.h` | LatencyLow stale-packet skip logic |
| `QtScrcpyCore/src/device/server/server.cpp` | Use `maxSize` int directly for `max_size` param |
| `QtScrcpyCore/src/device/deviceconfig.h` | **NEW** — `LatencyMode` enum and per-device config struct |

---

## 6. Out of Scope

- Hardware video decoding (VAAPI / MediaCodec)
- Codec upgrades (H.265, AV1)
- Full UI overhaul
- Audio changes
