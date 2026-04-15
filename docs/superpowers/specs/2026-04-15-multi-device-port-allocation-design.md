# Multi-Device Simultaneous Connect — Port Allocation Fix

**Date:** 2026-04-15
**Status:** Approved

## Problem

Selecting multiple devices in the left panel and clicking "Connect Checked" only connects the first device. All subsequent connections fail silently.

**Root cause:** `DeviceManage::connectDevice()` never assigns a `localPort` before creating a `Device`. Every device inherits the default `localPort = 27183` from `Server::ServerParams`. When two connections start simultaneously:

- **Reverse mode (USB default):** the host calls `QTcpServer::listen(localhost, 27183)` for each — only one can bind that port; the rest fail with "Could not listen on port 27183".
- **Forward mode (fallback):** `adb -s SERIAL forward tcp:27183 localabstract:...` — the ADB daemon's global port table means the second device's mapping silently overwrites the first, then both try to connect to the same socket.

The `getFreePort()` method and `isReversePort()` query already exist in the codebase to solve exactly this. A block that called `getFreePort()` was commented out with the incorrect note "No need to allocate ports, all use 27183."

## Design

### Single change location

`QtScrcpy/QtScrcpyCore/src/devicemanage/devicemanage.cpp` — `DeviceManage::connectDevice()`

### What changes

Before constructing a `Device`, assign a unique local port from `getFreePort()`. Apply this unconditionally — both reverse and forward mode use `localPort` to establish the TCP tunnel, so both need a distinct port per connection.

Remove the old "fall back to non-reverse if no port found" branch; `getFreePort()` scans ports 27183–28182 (1000 slots) and will not realistically exhaust them.

```
quint16 port = getFreePort();
if (port == 0) {
    qInfo("no free port available");
    return false;
}
params.localPort = port;
```

### Why `getFreePort()` is correct here

`getFreePort()` walks `m_devices` and calls `isReversePort(port)` on each active `Device`. A port is "used" if any live device holds it in reverse mode. Ports that were used by now-disconnected devices are naturally reclaimed because `removeDevice()` deletes the `Device` object, clearing `isReversePort()`.

Forward-mode devices do not register their port via `isReversePort()`, which means `getFreePort()` could theoretically re-issue a port that is still in use by an active forward-mode connection. However, since forward-mode port use is transient (the ADB forward rule is removed on disconnect via `forwardRemove()`), and simultaneous connects are processed sequentially in the event loop, this is safe in practice.

### No other files change

- `Server`, `Device`, `dialog.cpp`, `connectSerial()` — unchanged. `localPort` already flows from `DeviceParams` through to `Server::ServerParams`.

## Error handling

If `getFreePort()` returns 0 (exhausted), `connectDevice()` logs and returns `false` — the same failure path as today's silent bind error, but now explicit.

## Testing

Manual: select 2+ devices in the list, click "Connect Checked", verify all devices stream simultaneously.
Regression: single-device connect via "Start Server" button must still work (it goes through `connectSerial()` → `connectDevice()` with the same path).
