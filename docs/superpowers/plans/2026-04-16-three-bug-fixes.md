# Three Bug Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix three independent bugs: device check state not persisting across restarts, detach/attach causing black screen on the OpenGL video widget, and the left panel always starting closed.

**Architecture:** Each fix is self-contained. Fix 1 is a two-line swap in `dialog.cpp`. Fix 2 adds one method to `VideoForm` and two call sites in `DeviceTile`. Fix 3 adds two config methods and two call sites in `Dialog`.

**Tech Stack:** Qt 5/6, C++11, QSettings (userData), QOpenGLWidget

---

## File Map

| File | Change |
|------|--------|
| `QtScrcpy/ui/dialog.cpp` | Fix 1: swap setData/setCheckState order; Fix 3: save on toggle, restore on startup |
| `QtScrcpy/ui/videoform.h` | Fix 2: declare `reinitVideoWidget()` |
| `QtScrcpy/ui/videoform.cpp` | Fix 2: implement `reinitVideoWidget()` |
| `QtScrcpy/ui/devicetile.cpp` | Fix 2: call `reinitVideoWidget()` after detach and attach |
| `QtScrcpy/util/config.h` | Fix 3: declare `getPanelOpen()` / `setPanelOpen()` |
| `QtScrcpy/util/config.cpp` | Fix 3: implement `getPanelOpen()` / `setPanelOpen()` |

---

## Task 1: Fix device check state ordering

**Root cause recap:** `applyCheckStateToItem` calls `setCheckState` before `setData(Qt::UserRole, serial)`. The `itemChanged` signal fires during `setCheckState` when the item's serial is still empty — `onDeviceItemChanged` can't save it, so the config is overwritten with an incomplete list. Every subsequent item in the same rebuild reads the now-corrupt config.

**Files:**
- Modify: `QtScrcpy/ui/dialog.cpp:907-913`

- [ ] **Step 1: Locate and read `applyCheckStateToItem`**

  Open `QtScrcpy/ui/dialog.cpp` lines 907–913. It currently reads:

  ```cpp
  void Dialog::applyCheckStateToItem(QListWidgetItem *item, const QString &serial)
  {
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      QStringList checked = Config::getInstance().getCheckedDevices();
      item->setCheckState(checked.contains(serial) ? Qt::Checked : Qt::Unchecked);
      item->setData(Qt::UserRole, serial);  // store serial for later lookup
  }
  ```

- [ ] **Step 2: Swap the two lines — `setData` must come before `setCheckState`**

  Replace the function body so it reads:

  ```cpp
  void Dialog::applyCheckStateToItem(QListWidgetItem *item, const QString &serial)
  {
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setData(Qt::UserRole, serial);  // set serial BEFORE setCheckState fires itemChanged
      QStringList checked = Config::getInstance().getCheckedDevices();
      item->setCheckState(checked.contains(serial) ? Qt::Checked : Qt::Unchecked);
  }
  ```

- [ ] **Step 3: Build**

  ```bash
  cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
  ./ci/linux/build_for_linux.sh "Debug" 2>&1 | tail -20
  ```

  Expected: build succeeds, no errors.

- [ ] **Step 4: Manual verify**

  ```bash
  pkill -f QtScrcpy || true
  ./output/x64/Debug/QtScrcpy
  ```

  - Open the left pane, check two or more devices.
  - Close the app.
  - Reopen: the same devices should be checked.

- [ ] **Step 5: Commit**

  ```bash
  git add QtScrcpy/ui/dialog.cpp
  git commit -m "fix: set item serial before setCheckState to prevent config corruption on rebuild"
  ```

---

## Task 2: Fix detach/attach black screen

**Root cause recap:** `VideoForm::setParent()` destroys the native window — invalidating the `QYUVOpenGLWidget`'s OpenGL context. Qt only calls `initializeGL()` again on first paint. Frames arrive and `makeCurrent()` is called in `updateTextures()` before that happens, uploading into a dead context. Hiding then re-showing the `QYUVOpenGLWidget` forces an immediate `initializeGL()` cycle.

**Files:**
- Modify: `QtScrcpy/ui/videoform.h` — add declaration
- Modify: `QtScrcpy/ui/videoform.cpp` — add implementation
- Modify: `QtScrcpy/ui/devicetile.cpp` — add two call sites

- [ ] **Step 1: Declare `reinitVideoWidget()` in `videoform.h`**

  In `QtScrcpy/ui/videoform.h`, inside the `public:` section (after `void showFPS(bool show);`), add:

  ```cpp
  void reinitVideoWidget();
  ```

  The public section (lines ~25–36) should look like:

  ```cpp
  public:
      explicit VideoForm(bool framelessWindow = false, bool skin = true, bool showToolBar = true, QWidget *parent = 0);
      ~VideoForm();

      void staysOnTop(bool top = true);
      void updateShowSize(const QSize &newSize);
      void updateRender(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV);
      void setSerial(const QString& serial);
      QRect getGrabCursorRect();
      const QSize &frameSize();
      void resizeSquare();
      void removeBlackRect();
      void showFPS(bool show);
      void reinitVideoWidget();   // ← add this line
      void switchFullScreen();
      bool isHost();
  ```

- [ ] **Step 2: Implement `reinitVideoWidget()` in `videoform.cpp`**

  Add the following function at the end of `QtScrcpy/ui/videoform.cpp`, after `VideoForm::onFrame`:

  ```cpp
  void VideoForm::reinitVideoWidget()
  {
      if (m_videoWidget && !m_videoWidget->isHidden()) {
          m_videoWidget->hide();
          m_videoWidget->show();
      }
  }
  ```

- [ ] **Step 3: Call `reinitVideoWidget()` in `DeviceTile::detach()`**

  In `QtScrcpy/ui/devicetile.cpp`, the `detach()` function (lines 82–95) currently ends with:

  ```cpp
      m_videoForm->setParent(nullptr);   // become top-level window
      m_videoForm->show();
      m_placeholder->show();
      m_popOutBtn->setEnabled(false);
  ```

  Change it to:

  ```cpp
      m_videoForm->setParent(nullptr);   // become top-level window
      m_videoForm->show();
      m_videoForm->reinitVideoWidget();
      m_placeholder->show();
      m_popOutBtn->setEnabled(false);
  ```

- [ ] **Step 4: Call `reinitVideoWidget()` in `DeviceTile::attach()`**

  In `QtScrcpy/ui/devicetile.cpp`, the `attach()` function (lines 97–110) currently ends with:

  ```cpp
      m_videoForm->setParent(m_videoLayout->parentWidget());
      m_videoLayout->insertWidget(0, m_videoForm);
      m_videoForm->setFocusPolicy(Qt::StrongFocus);
      m_videoForm->show();
  ```

  Change it to:

  ```cpp
      m_videoForm->setParent(m_videoLayout->parentWidget());
      m_videoLayout->insertWidget(0, m_videoForm);
      m_videoForm->setFocusPolicy(Qt::StrongFocus);
      m_videoForm->show();
      m_videoForm->reinitVideoWidget();
  ```

- [ ] **Step 5: Build**

  ```bash
  cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
  ./ci/linux/build_for_linux.sh "Debug" 2>&1 | tail -20
  ```

  Expected: build succeeds, no errors.

- [ ] **Step 6: Manual verify**

  ```bash
  pkill -f QtScrcpy || true
  ./output/x64/Debug/QtScrcpy
  ```

  - Connect a device and wait for video to appear in the dashboard tile.
  - Click the "↗" pop-out button: the video should appear in a standalone window (not black).
  - Close the standalone window: video should re-appear inside the tile (not black).

- [ ] **Step 7: Commit**

  ```bash
  git add QtScrcpy/ui/videoform.h QtScrcpy/ui/videoform.cpp QtScrcpy/ui/devicetile.cpp
  git commit -m "fix: reinit OpenGL widget after VideoForm reparent to restore rendering on detach/attach"
  ```

---

## Task 3: Persist left panel open/closed state

**Root cause recap:** `m_panelOpen = false` is hardcoded; the panel always starts hidden. No save or restore in `Config`.

**Files:**
- Modify: `QtScrcpy/util/config.h` — declare two methods
- Modify: `QtScrcpy/util/config.cpp` — implement two methods, add key constant
- Modify: `QtScrcpy/ui/dialog.cpp` — save on toggle, restore on startup

- [ ] **Step 1: Add key constant and declare methods in `config.h`**

  In `QtScrcpy/util/config.h`, add the two new method declarations after `setCheckedDevices` (line 57):

  ```cpp
  bool getPanelOpen();
  void setPanelOpen(bool open);
  ```

  The relevant block should look like:

  ```cpp
  QStringList getCheckedDevices();
  void setCheckedDevices(const QStringList &serials);
  bool getPanelOpen();
  void setPanelOpen(bool open);
  ```

- [ ] **Step 2: Add key constant in `config.cpp`**

  In `QtScrcpy/util/config.cpp`, near line 105 where `COMMON_CHECKED_DEVICES_KEY` is defined, add:

  ```cpp
  #define COMMON_LEFT_PANEL_OPEN_KEY  "LeftPanelOpen"
  ```

  The block should look like:

  ```cpp
  #define COMMON_CHECKED_DEVICES_KEY  "CheckedDevices"
  #define COMMON_LEFT_PANEL_OPEN_KEY  "LeftPanelOpen"
  ```

- [ ] **Step 3: Implement `getPanelOpen()` and `setPanelOpen()` in `config.cpp`**

  Add the two functions after `setCheckedDevices` (after line 256):

  ```cpp
  bool Config::getPanelOpen()
  {
      m_userData->beginGroup(GROUP_COMMON);
      bool open = m_userData->value(COMMON_LEFT_PANEL_OPEN_KEY, false).toBool();
      m_userData->endGroup();
      return open;
  }

  void Config::setPanelOpen(bool open)
  {
      m_userData->beginGroup(GROUP_COMMON);
      m_userData->setValue(COMMON_LEFT_PANEL_OPEN_KEY, open);
      m_userData->endGroup();
      m_userData->sync();
  }
  ```

- [ ] **Step 4: Save panel state on toggle in `dialog.cpp`**

  In `QtScrcpy/ui/dialog.cpp`, inside the `m_toggleBtn` clicked lambda (lines 340–358), add `Config::getInstance().setPanelOpen(m_panelOpen)` after each assignment to `m_panelOpen`:

  ```cpp
  connect(m_toggleBtn, &QPushButton::clicked, this, [this]() {
      m_panelAnim->stop();
      int panelWidth = m_panelContainer->width();
      QPoint currentPos = m_panelContainer->pos();
      if (!m_panelOpen) {
          m_panelAnim->setEasingCurve(QEasingCurve::OutCubic);
          m_panelAnim->setStartValue(currentPos);
          m_panelAnim->setEndValue(QPoint(0, 0));
          m_panelOpen = true;
          Config::getInstance().setPanelOpen(true);   // ← add
          m_toggleBtn->setText("◀");
      } else {
          m_panelAnim->setEasingCurve(QEasingCurve::InCubic);
          m_panelAnim->setStartValue(currentPos);
          m_panelAnim->setEndValue(QPoint(-panelWidth, 0));
          m_panelOpen = false;
          Config::getInstance().setPanelOpen(false);  // ← add
          m_toggleBtn->setText("▶");
      }
      m_panelAnim->start();
  });
  ```

- [ ] **Step 5: Restore panel state on startup in `dialog.cpp`**

  In `QtScrcpy/ui/dialog.cpp`, after the block that positions the panel container (around line 321–334, ending with `m_toggleBtn->show();`), add the restore block immediately after `m_toggleBtn->show()`:

  ```cpp
  m_toggleBtn->show();

  // Restore panel open state from last session (no animation on startup)
  if (Config::getInstance().getPanelOpen()) {
      m_panelOpen = true;
      m_panelContainer->move(0, 0);
      m_toggleBtn->setText("◀");
  }
  ```

- [ ] **Step 6: Build**

  ```bash
  cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
  ./ci/linux/build_for_linux.sh "Debug" 2>&1 | tail -20
  ```

  Expected: build succeeds, no errors.

- [ ] **Step 7: Manual verify**

  ```bash
  pkill -f QtScrcpy || true
  ./output/x64/Debug/QtScrcpy
  ```

  - Open the left panel with the toggle button.
  - Close the app.
  - Reopen: the left panel should be open immediately on startup, with the "◀" button visible.
  - Close the panel, close the app, reopen: panel should be closed on startup.

- [ ] **Step 8: Commit**

  ```bash
  git add QtScrcpy/util/config.h QtScrcpy/util/config.cpp QtScrcpy/ui/dialog.cpp
  git commit -m "fix: persist left panel open/closed state across app restarts"
  ```
