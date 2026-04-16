# Select All/None Toggle + Panel Button Position Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge the "All"/"None" device selection buttons into one smart toggle, and move the panel slide toggle button to follow the right edge of the panel.

**Architecture:** Task 1 touches `dialog.ui`, `dialog.h`, and `dialog.cpp` to collapse two buttons into one with dynamic label logic. Task 2 is a pure `dialog.cpp` change — reparenting `m_toggleBtn` from `this` to `m_panelContainer` so it moves with the panel automatically.

**Tech Stack:** Qt 5/6, C++11, QListWidget, QPushButton

---

## File Map

| File | Task | Change |
|------|------|--------|
| `QtScrcpy/ui/dialog.ui` | 1 | Remove `deselectAllDevicesBtn` widget |
| `QtScrcpy/ui/dialog.h` | 1 | Remove `on_deselectAllDevicesBtn_clicked`; add `updateToggleAllBtn` |
| `QtScrcpy/ui/dialog.cpp` | 1 | Rewrite toggle slot, remove deselect slot, add helper, add call sites |
| `QtScrcpy/ui/dialog.cpp` | 2 | Reparent `m_toggleBtn` to panel container; fix `resizeEvent` |

---

## Task 1: Select All/None single toggle button

**Files:**
- Modify: `QtScrcpy/ui/dialog.ui:156-164`
- Modify: `QtScrcpy/ui/dialog.h:88-89`
- Modify: `QtScrcpy/ui/dialog.cpp:925-937`, `961-974`, `141-142`, `194-196`

- [ ] **Step 1: Remove `deselectAllDevicesBtn` from `dialog.ui`**

  In `QtScrcpy/ui/dialog.ui`, find the `deviceCheckBtnsLayout` block (around lines 150–170). It currently contains:

  ```xml
  <item>
   <widget class="QPushButton" name="selectAllDevicesBtn">
    <property name="text"><string>All</string></property>
   </widget>
  </item>
  <item>
   <widget class="QPushButton" name="deselectAllDevicesBtn">
    <property name="text"><string>None</string></property>
   </widget>
  </item>
  ```

  Remove the entire `deselectAllDevicesBtn` `<item>` block so only `selectAllDevicesBtn` remains:

  ```xml
  <item>
   <widget class="QPushButton" name="selectAllDevicesBtn">
    <property name="text"><string>All</string></property>
   </widget>
  </item>
  ```

- [ ] **Step 2: Update `dialog.h`**

  In `QtScrcpy/ui/dialog.h`:

  Remove line:
  ```cpp
  void on_deselectAllDevicesBtn_clicked();
  ```

  Add to the `private:` section (after the other helper declarations, e.g. after `connectSerial`):
  ```cpp
  void updateToggleAllBtn();
  ```

- [ ] **Step 3: Replace `on_selectAllDevicesBtn_clicked` in `dialog.cpp`**

  Find `on_selectAllDevicesBtn_clicked` (line ~925). Replace the entire function:

  ```cpp
  void Dialog::on_selectAllDevicesBtn_clicked()
  {
      bool allChecked = true;
      for (int i = 0; i < ui->connectedPhoneList->count(); ++i) {
          if (ui->connectedPhoneList->item(i)->checkState() != Qt::Checked) {
              allChecked = false;
              break;
          }
      }
      Qt::CheckState newState = allChecked ? Qt::Unchecked : Qt::Checked;
      for (int i = 0; i < ui->connectedPhoneList->count(); ++i) {
          ui->connectedPhoneList->item(i)->setCheckState(newState);
      }
  }
  ```

- [ ] **Step 4: Remove `on_deselectAllDevicesBtn_clicked` from `dialog.cpp`**

  Delete the entire function (lines ~932–937):

  ```cpp
  void Dialog::on_deselectAllDevicesBtn_clicked()
  {
      for (int i = 0; i < ui->connectedPhoneList->count(); ++i) {
          ui->connectedPhoneList->item(i)->setCheckState(Qt::Unchecked);
      }
  }
  ```

- [ ] **Step 5: Add `updateToggleAllBtn` to `dialog.cpp`**

  Add the new helper immediately after `on_selectAllDevicesBtn_clicked`:

  ```cpp
  void Dialog::updateToggleAllBtn()
  {
      int total = ui->connectedPhoneList->count();
      if (total == 0) {
          ui->selectAllDevicesBtn->setText(tr("All"));
          return;
      }
      bool allChecked = true;
      for (int i = 0; i < total; ++i) {
          if (ui->connectedPhoneList->item(i)->checkState() != Qt::Checked) {
              allChecked = false;
              break;
          }
      }
      ui->selectAllDevicesBtn->setText(allChecked ? tr("None") : tr("All"));
  }
  ```

- [ ] **Step 6: Call `updateToggleAllBtn()` in `onDeviceItemChanged`**

  `onDeviceItemChanged` (line ~961) currently ends with:

  ```cpp
      Config::getInstance().setCheckedDevices(checked);
      Q_UNUSED(item)
  }
  ```

  Add the call before `Q_UNUSED`:

  ```cpp
      Config::getInstance().setCheckedDevices(checked);
      updateToggleAllBtn();
      Q_UNUSED(item)
  }
  ```

- [ ] **Step 7: Call `updateToggleAllBtn()` after each list rebuild**

  There are two places in the ADB result handler where the device list is rebuilt. Both end with a loop of `applyCheckStateToItem` calls followed by a `m_deviceUpdateInProgress = false;` line.

  **First rebuild path** (full `-l` update, around line 142). After the loop:

  ```cpp
                  for (const auto &sortedDevice : sortedDevices) {
                      ui->serialBox->addItem(sortedDevice.second);
                      auto *item = new QListWidgetItem(sortedDevice.first);
                      ui->connectedPhoneList->addItem(item);
                      applyCheckStateToItem(item, sortedDevice.second);
                  }

                  // Trigger async fetch for each device to get detailed properties
                  for (const auto &device : devices) {
                      m_adb.fetchDevicePropertiesAsync(device.serial);
                  }

                  // Reset progress flag after full device update
                  m_deviceUpdateInProgress = false;
  ```

  Add `updateToggleAllBtn();` after the `applyCheckStateToItem` loop and before the async fetch loop:

  ```cpp
                  for (const auto &sortedDevice : sortedDevices) {
                      ui->serialBox->addItem(sortedDevice.second);
                      auto *item = new QListWidgetItem(sortedDevice.first);
                      ui->connectedPhoneList->addItem(item);
                      applyCheckStateToItem(item, sortedDevice.second);
                  }
                  updateToggleAllBtn();

                  // Trigger async fetch for each device to get detailed properties
                  for (const auto &device : devices) {
                      m_adb.fetchDevicePropertiesAsync(device.serial);
                  }

                  // Reset progress flag after full device update
                  m_deviceUpdateInProgress = false;
  ```

  **Second rebuild path** (lightweight update, around line 195). After the loop:

  ```cpp
                  for (const auto &sortedDevice : sortedDevices) {
                      ui->serialBox->addItem(sortedDevice.second);
                      auto *item = new QListWidgetItem(sortedDevice.first);
                      ui->connectedPhoneList->addItem(item);
                      applyCheckStateToItem(item, sortedDevice.second);
                  }

                  // Reset progress flag after lightweight device update
                  m_deviceUpdateInProgress = false;
  ```

  Add `updateToggleAllBtn();` immediately after the loop:

  ```cpp
                  for (const auto &sortedDevice : sortedDevices) {
                      ui->serialBox->addItem(sortedDevice.second);
                      auto *item = new QListWidgetItem(sortedDevice.first);
                      ui->connectedPhoneList->addItem(item);
                      applyCheckStateToItem(item, sortedDevice.second);
                  }
                  updateToggleAllBtn();

                  // Reset progress flag after lightweight device update
                  m_deviceUpdateInProgress = false;
  ```

- [ ] **Step 8: Build**

  ```bash
  cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
  ./ci/linux/build_for_linux.sh "Debug" 2>&1 | tail -20
  ```

  Expected: build succeeds, no errors.

- [ ] **Step 9: Manual verify**

  ```bash
  pkill -f QtScrcpy || true
  ./output/x64/Debug/QtScrcpy
  ```

  - Open the left panel. The device list should show only one button ("All" or "None") where two used to be.
  - With no devices checked, button shows "All". Click → all devices checked. Button changes to "None".
  - Click "None" → all unchecked. Button changes back to "All".
  - Check some but not all → button shows "All".

- [ ] **Step 10: Commit**

  ```bash
  git add QtScrcpy/ui/dialog.ui QtScrcpy/ui/dialog.h QtScrcpy/ui/dialog.cpp
  git commit -m "feat: merge Select All/Deselect All into single All/None toggle button"
  ```

---

## Task 2: Panel toggle button follows right edge of panel

**Files:**
- Modify: `QtScrcpy/ui/dialog.cpp:326-335`, `569-581`

- [ ] **Step 1: Reparent `m_toggleBtn` to `m_panelContainer` in the constructor**

  In `QtScrcpy/ui/dialog.cpp`, find the toggle button construction block (around lines 326–335):

  ```cpp
  // Fixed toggle button at the left edge of the dialog
  m_toggleBtn = new QPushButton("▶", this);
  m_toggleBtn->setFixedSize(18, 30);
  m_toggleBtn->setToolTip(tr("Toggle panel"));
  m_toggleBtn->setStyleSheet(
      "QPushButton { border: none; background: palette(mid); font-size: 10px; }"
      "QPushButton:hover { background: palette(midlight); }");
  m_toggleBtn->move(0, height() / 2 - 15);
  m_toggleBtn->raise();
  m_toggleBtn->show();
  ```

  Replace with (parent becomes `m_panelContainer`, x becomes `panelW`):

  ```cpp
  // Toggle button sits at the right edge of the panel container — moves with it
  m_toggleBtn = new QPushButton("▶", m_panelContainer);
  m_toggleBtn->setFixedSize(18, 30);
  m_toggleBtn->setToolTip(tr("Toggle panel"));
  m_toggleBtn->setStyleSheet(
      "QPushButton { border: none; background: palette(mid); font-size: 10px; }"
      "QPushButton:hover { background: palette(midlight); }");
  m_toggleBtn->move(panelW, height() / 2 - 15);
  m_toggleBtn->raise();
  m_toggleBtn->show();
  ```

- [ ] **Step 2: Fix `resizeEvent` to use container width instead of 0**

  In `QtScrcpy/ui/dialog.cpp`, find `resizeEvent` (around lines 569–581):

  ```cpp
  if (m_toggleBtn) {
      m_toggleBtn->move(0, event->size().height() / 2 - 15);
  }
  ```

  Replace with:

  ```cpp
  if (m_toggleBtn) {
      m_toggleBtn->move(m_panelContainer->width(), event->size().height() / 2 - 15);
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

  - With panel closed: the "▶" toggle tab should appear at the left edge of the dialog (x=0) — same as before.
  - Click the button to open the panel: it should slide to the right edge of the panel as the panel opens.
  - Click again to close: button should slide back to the left edge.
  - Resize the dialog window: button should stay vertically centred on the panel's right edge.

- [ ] **Step 5: Commit**

  ```bash
  git add QtScrcpy/ui/dialog.cpp
  git commit -m "feat: move panel toggle button to right edge of panel so it slides with panel"
  ```
