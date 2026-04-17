# Panel Default Open & Per-Tile Hotkey Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the left panel always open on app startup, and fix keyboard shortcuts (e.g. Ctrl+P) so they fire for the focused tile when multiple device tiles are open in the same window.

**Architecture:** Two independent, surgical edits. Fix 1 removes the persisted-state restore block in `Dialog`'s constructor and replaces it with an unconditional open-state init. Fix 2 changes every `QShortcut` in `VideoForm::installShortcut()` from the default `Qt::WindowShortcut` context (activates for any widget in the window — causing ambiguity with multiple tiles) to `Qt::WidgetWithChildrenShortcut` (activates only when that specific `VideoForm` or its children holds keyboard focus).

**Tech Stack:** Qt 5.12+ / Qt 6, C++11, QShortcut, QPropertyAnimation, CMake

---

## File Map

| File | Change |
|------|--------|
| `QtScrcpy/ui/dialog.cpp` | Remove conditional panel restore; unconditionally initialize panel as open |
| `QtScrcpy/ui/videoform.cpp` | Add `Qt::WidgetWithChildrenShortcut` context to all 17 `QShortcut` constructors in `installShortcut()` |

---

## Task 1: Panel always open on startup

**Files:**
- Modify: `QtScrcpy/ui/dialog.cpp:348-354`

### Background

Lines 330–354 of `dialog.cpp` build the panel and toggle button, then conditionally restore the last-saved open state from `Config::getPanelOpen()`. The stored default is `false`, so the panel starts closed. We remove the conditional and unconditionally initialize the panel as open.

The toggle button and animation wiring that follows (lines 356–388) is untouched.

- [ ] **Step 1: Replace the conditional restore block with unconditional open init**

In `QtScrcpy/ui/dialog.cpp`, find this block (lines 348–354):

```cpp
    // Restore panel open state from last session (no animation on startup)
    if (Config::getInstance().getPanelOpen()) {
        m_panelOpen = true;
        m_panelContainer->move(0, 0);
        m_toggleBtn->setText("◀");
        m_toggleBtn->move(panelW, height() / 2 - 15);
    }
```

Replace it with:

```cpp
    // Panel always starts open
    m_panelOpen = true;
    m_panelContainer->move(0, 0);
    m_toggleBtn->setText("◀");
    m_toggleBtn->move(panelW, height() / 2 - 15);
```

- [ ] **Step 2: Build and verify**

```bash
cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
./ci/linux/build_for_linux.sh "Debug"
```

Expected: build succeeds with no errors or warnings related to the changed lines.

- [ ] **Step 3: Manual test — fresh start**

```bash
pkill -f QtScrcpy || true
./output/x64/Debug/QtScrcpy
```

Expected: panel is open (visible on the left) immediately on launch, with the `◀` toggle button visible at the panel's right edge.

- [ ] **Step 4: Manual test — close panel, restart**

1. Click the `◀` button to close the panel.
2. Quit the app.
3. Relaunch: `pkill -f QtScrcpy || true && ./output/x64/Debug/QtScrcpy`

Expected: panel is open again — the previously closed state is NOT restored.

- [ ] **Step 5: Commit**

```bash
git add QtScrcpy/ui/dialog.cpp
git commit -m "feat: panel always starts open on app launch"
```

---

## Task 2: Per-tile hotkeys with multiple tiles

**Files:**
- Modify: `QtScrcpy/ui/videoform.cpp:190-365` (`installShortcut()`)

### Background

`VideoForm::installShortcut()` registers 17 `QShortcut` objects. The default `Qt::WindowShortcut` context means each shortcut activates whenever the parent's top-level window is active. With two or more `VideoForm` tiles embedded in the same `Dialog` window, Qt sees duplicate key sequences in the same window context, detects ambiguity, and fires neither handler. Changing to `Qt::WidgetWithChildrenShortcut` scopes each shortcut to its own `VideoForm` — it only fires when that widget or any of its children (the OpenGL video renderer) has keyboard focus.

The `QShortcut` constructor signature is:
```cpp
QShortcut(const QKeySequence &key, QWidget *parent,
          const char *member = nullptr,
          const char *ambiguousMember = nullptr,
          Qt::ShortcutContext context = Qt::WindowShortcut)
```

We pass `nullptr, nullptr, Qt::WidgetWithChildrenShortcut` as the last three arguments.

- [ ] **Step 1: Update all 17 QShortcut constructors in `installShortcut()`**

In `QtScrcpy/ui/videoform.cpp`, replace every `QShortcut` constructor call inside `installShortcut()`. The full updated function body (lines 191–365) is:

```cpp
void VideoForm::installShortcut()
{
    QShortcut *shortcut = nullptr;

    // switchFullScreen
    shortcut = new QShortcut(QKeySequence("Ctrl+f"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        switchFullScreen();
    });

    // resizeSquare
    shortcut = new QShortcut(QKeySequence("Ctrl+g"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() { resizeSquare(); });

    // removeBlackRect
    shortcut = new QShortcut(QKeySequence("Ctrl+w"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() { removeBlackRect(); });

    // postGoHome
    shortcut = new QShortcut(QKeySequence("Ctrl+h"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        device->postGoHome();
    });

    // postGoBack
    shortcut = new QShortcut(QKeySequence("Ctrl+b"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        device->postGoBack();
    });

    // postAppSwitch
    shortcut = new QShortcut(QKeySequence("Ctrl+s"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postAppSwitch();
    });

    // postGoMenu
    shortcut = new QShortcut(QKeySequence("Ctrl+m"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        device->postGoMenu();
    });

    // postVolumeUp
    shortcut = new QShortcut(QKeySequence("Ctrl+up"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postVolumeUp();
    });

    // postVolumeDown
    shortcut = new QShortcut(QKeySequence("Ctrl+down"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postVolumeDown();
    });

    // postPower
    shortcut = new QShortcut(QKeySequence("Ctrl+p"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postPower();
    });

    shortcut = new QShortcut(QKeySequence("Ctrl+o"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->setDisplayPower(false);
    });

    // expandNotificationPanel
    shortcut = new QShortcut(QKeySequence("Ctrl+n"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->expandNotificationPanel();
    });

    // collapsePanel
    shortcut = new QShortcut(QKeySequence("Ctrl+Shift+n"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->collapsePanel();
    });

    // copy
    shortcut = new QShortcut(QKeySequence("Ctrl+c"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postCopy();
    });

    // cut
    shortcut = new QShortcut(QKeySequence("Ctrl+x"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postCut();
    });

    // clipboardPaste
    shortcut = new QShortcut(QKeySequence("Ctrl+v"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->setDeviceClipboard();
    });

    // setDeviceClipboard
    shortcut = new QShortcut(QKeySequence("Ctrl+Shift+v"), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->clipboardPaste();
    });
}
```

- [ ] **Step 2: Build and verify**

```bash
cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
./ci/linux/build_for_linux.sh "Debug"
```

Expected: build succeeds with no errors or warnings.

- [ ] **Step 3: Manual test — single device regression**

```bash
pkill -f QtScrcpy || true
./output/x64/Debug/QtScrcpy
```

1. Connect one device and start mirroring.
2. Click the video tile to focus it.
3. Press `Ctrl+P` — device screen should toggle power.
4. Press `Ctrl+H` — device should go to home screen.

Expected: all shortcuts work as before.

- [ ] **Step 4: Manual test — two devices, per-tile isolation**

1. Connect two devices and start mirroring both.
2. Click on Device A's tile to focus it.
3. Press `Ctrl+P` — only Device A's screen should toggle power.
4. Click on Device B's tile.
5. Press `Ctrl+P` — only Device B's screen should toggle power.

Expected: each `Ctrl+P` press only affects the currently focused tile. No silent failures.

- [ ] **Step 5: Commit**

```bash
git add QtScrcpy/ui/videoform.cpp
git commit -m "fix: use WidgetWithChildrenShortcut to prevent ambiguity with multiple device tiles"
```
