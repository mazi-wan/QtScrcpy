# Multi-Device Port Allocation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix simultaneous multi-device connect so all checked devices stream, not just the first.

**Architecture:** Restore the commented-out `getFreePort()` call in `DeviceManage::connectDevice()`, applying it unconditionally for both reverse and forward modes. Every connection gets a unique local port (27183, 27184, …) so TCP listeners and ADB forward rules don't collide.

**Tech Stack:** Qt 5/6, C++11, ADB

---

### Task 1: Restore per-device port allocation in `DeviceManage::connectDevice()`

**Files:**
- Modify: `QtScrcpy/QtScrcpyCore/src/devicemanage/devicemanage.cpp:47-60`

- [ ] **Step 1: Replace the commented-out block**

Open `QtScrcpy/QtScrcpyCore/src/devicemanage/devicemanage.cpp`.

Replace the dead commented block (lines 47–60) with the active port allocation:

```cpp
bool DeviceManage::connectDevice(qsc::DeviceParams params)
{
    if (params.serial.trimmed().isEmpty()) {
        return false;
    }
    if (m_devices.contains(params.serial)) {
        return false;
    }
    if (DM_MAX_DEVICES_NUM < m_devices.size()) {
        qInfo("over the maximum number of connections");
        return false;
    }
    quint16 port = getFreePort();
    if (port == 0) {
        qInfo("no free port available");
        return false;
    }
    params.localPort = port;
    IDevice *device = new Device(params);
    connect(device, &Device::deviceConnected, this, &DeviceManage::onDeviceConnected);
    connect(device, &Device::deviceDisconnected, this, &DeviceManage::onDeviceDisconnected);
    if (!device->connectDevice()) {
        delete device;
        return false;
    }
    m_devices[params.serial] = device;
    return true;
}
```

- [ ] **Step 2: Build**

```bash
./ci/linux/build_for_linux.sh "Debug" 2>&1 | tail -10
```

Expected: `CMake Build Succeeded`

- [ ] **Step 3: Commit**

```bash
git add QtScrcpy/QtScrcpyCore/src/devicemanage/devicemanage.cpp
git commit -m "fix: assign unique local port per device — enables simultaneous multi-device connect"
```

---

### Task 2: Manual verification

- [ ] **Step 1: Connect 2+ physical Android devices via USB**

Run:
```bash
adb devices
```
Confirm at least 2 devices are listed.

- [ ] **Step 2: Launch the app**

```bash
./output/x64/Debug/QtScrcpy
```

- [ ] **Step 3: Verify multi-connect**

1. Open the left panel.
2. Check the boxes for 2+ devices in the list.
3. Click **Connect Checked**.
4. Confirm all selected devices open a stream window simultaneously.

- [ ] **Step 4: Verify single-device connect still works**

1. Disconnect all streams.
2. Use the **Start Server** button (single-device path via `serialBox`).
3. Confirm it connects normally.
