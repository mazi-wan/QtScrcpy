# Overlay Left Pane + Restart All Streams Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the left control panel slide over the dashboard as an overlay (toggled by a fixed button), and add a "Restart All" button that reconnects all devices with recalculated max_size.

**Architecture:** `leftWidget` is removed from the HBoxLayout and re-parented as a direct Dialog child positioned absolutely. `QPropertyAnimation` on `pos` handles the slide. `Dialog` stores a `QMap<serial, DeviceParams>` snapshot at connect time; restart disconnects all then reconnects each with updated max_size.

**Tech Stack:** Qt 5.12+, QPropertyAnimation, QWidget absolute positioning.

---

## File Map

| Action | File | What changes |
|---|---|---|
| Modify | `QtScrcpy/ui/dialog.h` | Add `m_toggleBtn`, `m_panelAnim`, `m_panelOpen`, `m_connectedParams`, `resizeEvent` override, `on_restartAllBtn_clicked` slot |
| Modify | `QtScrcpy/ui/dialog.cpp` | Overlay panel setup, animation toggle, resizeEvent, params storage, restart logic |
| Modify | `QtScrcpy/ui/dialog.ui` | Add `restartAllBtn` QPushButton next to `stopAllServerBtn` |

---

## Task 1: Overlay panel with slide animation

**Files:**
- Modify: `QtScrcpy/ui/dialog.h`
- Modify: `QtScrcpy/ui/dialog.cpp`

- [ ] **Step 1: Add members to dialog.h**

In `QtScrcpy/ui/dialog.h`, add includes near the top:
```cpp
#include <QPropertyAnimation>
```

In the private section, add after `m_dashboard`:
```cpp
    QPointer<QPushButton> m_toggleBtn;
    QPointer<QPropertyAnimation> m_panelAnim;
    bool m_panelOpen = false;
```

Add `resizeEvent` override in the `protected` section (after `closeEvent`):
```cpp
    void resizeEvent(QResizeEvent *event) override;
```

- [ ] **Step 2: Replace the current toggle button + layout setup in dialog.cpp**

Read `QtScrcpy/ui/dialog.cpp`. Find the block that currently creates the dashboard and the toggle button (around line 241–264). Replace the entire block with:

```cpp
    // Create dashboard — fills the full layout width now
    m_dashboard = new DeviceDashboard(this);
    auto *mainLayout = qobject_cast<QHBoxLayout *>(layout());
    if (mainLayout) {
        // Remove leftWidget from the layout so dashboard takes full width
        mainLayout->removeWidget(ui->leftWidget);
        mainLayout->addWidget(m_dashboard, 1);
    }

    // Re-parent leftWidget as a direct Dialog child (absolute overlay)
    ui->leftWidget->setParent(this);
    int panelW = ui->leftWidget->sizeHint().width();
    if (panelW <= 0) panelW = 320;
    ui->leftWidget->resize(panelW, height());
    ui->leftWidget->move(-panelW, 0);   // start off-screen left (closed)
    ui->leftWidget->show();
    ui->leftWidget->raise();            // keep above dashboard

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

    // Slide animation on leftWidget's pos property
    m_panelAnim = new QPropertyAnimation(ui->leftWidget, "pos", this);
    m_panelAnim->setDuration(200);

    connect(m_toggleBtn, &QPushButton::clicked, this, [this]() {
        m_panelAnim->stop();
        int panelWidth = ui->leftWidget->width();
        if (!m_panelOpen) {
            // Open: slide in from left
            m_panelAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_panelAnim->setStartValue(QPoint(-panelWidth, 0));
            m_panelAnim->setEndValue(QPoint(0, 0));
            m_panelOpen = true;
            m_toggleBtn->setText("◀");
        } else {
            // Close: slide out to left
            m_panelAnim->setEasingCurve(QEasingCurve::InCubic);
            m_panelAnim->setStartValue(QPoint(0, 0));
            m_panelAnim->setEndValue(QPoint(-panelWidth, 0));
            m_panelOpen = false;
            m_toggleBtn->setText("▶");
        }
        m_panelAnim->start();
    });
```

- [ ] **Step 3: Add resizeEvent implementation in dialog.cpp**

After `Dialog::closeEvent`, add:

```cpp
void Dialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Keep panel height in sync with dialog height
    ui->leftWidget->resize(ui->leftWidget->width(), event->size().height());
    // Keep panel at correct horizontal position (open=0, closed=off-screen)
    if (!m_panelOpen) {
        ui->leftWidget->move(-ui->leftWidget->width(), 0);
    }
    // Reposition the toggle button at the left edge, vertically centered
    if (m_toggleBtn) {
        m_toggleBtn->move(0, event->size().height() / 2 - 15);
    }
}
```

- [ ] **Step 4: Add missing includes to dialog.cpp**

At the top of `dialog.cpp`, add after existing includes:
```cpp
#include <QPropertyAnimation>
#include <QEasingCurve>
```

- [ ] **Step 5: Build to verify**

```bash
cmake --build /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy/build --config Release -j8 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 6: Manual test**

Run the app. Verify:
- Dashboard fills the full window width on startup
- Toggle button (▶) is visible at the left edge, vertically centered
- Clicking ▶ slides the panel in smoothly from the left; button changes to ◀
- Clicking ◀ slides the panel back out; button changes to ▶
- Resizing the window keeps the panel height correct and toggle button centered

- [ ] **Step 7: Commit**

```bash
git add QtScrcpy/ui/dialog.h QtScrcpy/ui/dialog.cpp
git commit -m "feat: overlay left panel with slide animation — dashboard now fills full width"
```

---

## Task 2: Restart All Streams button

**Files:**
- Modify: `QtScrcpy/ui/dialog.ui`
- Modify: `QtScrcpy/ui/dialog.h`
- Modify: `QtScrcpy/ui/dialog.cpp`

- [ ] **Step 1: Add `restartAllBtn` to dialog.ui**

In `QtScrcpy/ui/dialog.ui`, find the `stopAllServerBtn` item (around line 910). After its closing `</item>` tag, add:

```xml
            <item>
             <widget class="QPushButton" name="restartAllBtn">
              <property name="sizePolicy">
               <sizepolicy hsizetype="Preferred" vsizetype="Preferred">
                <horstretch>0</horstretch>
                <verstretch>0</verstretch>
               </sizepolicy>
              </property>
              <property name="text">
               <string>restart all</string>
              </property>
             </widget>
            </item>
```

- [ ] **Step 2: Add members and slot to dialog.h**

In `dialog.h`, add include at top:
```cpp
#include <QMap>
```

In the private slots section, add after `on_stopAllServerBtn_clicked`:
```cpp
    void on_restartAllBtn_clicked();
```

In the private members section, add after `m_connectionIsWifi`:
```cpp
    QMap<QString, qsc::DeviceParams> m_connectedParams;  // serial → params used at connect time
```

- [ ] **Step 3: Store params snapshot in `on_startServerBtn_clicked` in dialog.cpp**

`IDevice` doesn't expose its params publicly, so we capture them at connection time in `on_startServerBtn_clicked`, which is where all params are assembled. The snapshot stores the **raw user-configured maxSize** (before tile-cap) so restart can recalculate correctly.

In `Dialog::on_startServerBtn_clicked`, find the line `qsc::IDeviceManage::getInstance().connectDevice(params)`. Immediately after it, add:

```cpp
    // Snapshot params for restart; store raw user maxSize (before tile-cap) so
    // restart can apply min(userSetting, newTileSize) correctly.
    qsc::DeviceParams snapshot = params;
    snapshot.maxSize = ui->maxSizeBox->currentText().trimmed().toUShort();
    m_connectedParams[params.serial] = snapshot;
```

- [ ] **Step 4: Remove params on disconnect in dialog.cpp**

In `Dialog::onDeviceDisconnected`, add after `GroupController::instance().removeDevice(serial)`:

```cpp
    m_connectedParams.remove(serial);
```

- [ ] **Step 5: Implement `on_restartAllBtn_clicked` in dialog.cpp**

Add the slot implementation:

```cpp
void Dialog::on_restartAllBtn_clicked()
{
    if (m_connectedParams.isEmpty()) {
        return;
    }

    // Snapshot params before disconnecting (map will be cleared by disconnect signals)
    QList<qsc::DeviceParams> snapshot = m_connectedParams.values();

    qsc::IDeviceManage::getInstance().disconnectAllDevice();

    // Reconnect each device with recalculated max_size
    for (qsc::DeviceParams params : snapshot) {
        quint16 userMaxSize = params.maxSize;  // stored as original user value

        if (m_dashboard) {
            quint16 autoSize = m_dashboard->optimalMaxSize();
            if (autoSize > 0) {
                params.maxSize = (userMaxSize == 0) ? autoSize : qMin(userMaxSize, autoSize);
            } else {
                params.maxSize = userMaxSize;
            }
        }

        qsc::IDeviceManage::getInstance().connectDevice(params);
    }
}
```

- [ ] **Step 6: Build to verify**

```bash
cmake --build /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy/build --config Release -j8 2>&1 | tail -10
```

Expected: build succeeds with no errors about `restartAllBtn` or `m_connectedParams`.

- [ ] **Step 7: Manual test**

Run the app. Connect a device. Verify:
- "restart all" button is visible in the left panel (open the panel first with the toggle)
- Click "restart all" → device disconnects and reconnects, tile reappears
- Connect 2 devices, click "restart all" → both reconnect
- After restart, `max_size` in logs reflects the tile size at time of restart (not original)

- [ ] **Step 8: Commit**

```bash
git add QtScrcpy/ui/dialog.ui QtScrcpy/ui/dialog.h QtScrcpy/ui/dialog.cpp
git commit -m "feat: add Restart All button — reconnects all devices with recalculated max_size"
```
