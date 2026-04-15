# Multi-Mirror, High Resolution, Low Latency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a tiled multi-device dashboard with pop-out support, free-form resolution input up to native, and an opt-in low-latency mode that skips stale packets in the demuxer.

**Architecture:** `DeviceDashboard` (new) manages a tiled grid of `DeviceTile` widgets (new), each embedding a `VideoForm`. Dialog loses its VideoForm creation logic; the dashboard takes over. `DeviceParams` gains a `lowLatency` flag that makes `Demuxer` skip ahead when socket data is already waiting. Resolution switches from a fixed index to a plain integer in config and UI.

**Tech Stack:** C++11, Qt 5.12+, FFmpeg (libavcodec/libavformat), QSettings for config persistence.

---

## File Map

| Action | File | What changes |
|---|---|---|
| Modify | `QtScrcpy/QtScrcpyCore/include/QtScrcpyCoreDef.h` | Add `lowLatency = false` to `DeviceParams` |
| Modify | `QtScrcpy/util/config.h` | Replace `maxSizeIndex` → `maxSize`; add `lowLatency` to `UserBootConfig` |
| Modify | `QtScrcpy/util/config.cpp` | Update save/load with `maxSize`/`lowLatency`; add migration from old index |
| Modify | `QtScrcpy/QtScrcpyCore/src/device/demuxer/demuxer.h` | Add `setLowLatency(bool)` + `m_lowLatency` |
| Modify | `QtScrcpy/QtScrcpyCore/src/device/demuxer/demuxer.cpp` | Skip stale non-config packets when `m_lowLatency=true` in `processFrame()` |
| Modify | `QtScrcpy/QtScrcpyCore/src/device/device.cpp` | Pass `lowLatency` to `m_stream`; enforce `renderExpiredFrames=false` in low-latency mode |
| Modify | `QtScrcpy/ui/dialog.ui` | Add `lowLatencyCheck` QCheckBox to options grid |
| Modify | `QtScrcpy/ui/dialog.cpp` / `.h` | Max-size editable combo; `lowLatency` in config read/write; move VideoForm creation to dashboard |
| Create | `QtScrcpy/ui/devicetile.h` / `.cpp` | Single tile: embedded VideoForm, header bar, pop-out/disconnect buttons |
| Create | `QtScrcpy/ui/devicedashboard.h` / `.cpp` | Tiled grid, connects to IDeviceManage signals, manages tiles |
| Modify | `QtScrcpy/CMakeLists.txt` | Register new `devicetile` and `devicedashboard` source files |

---

## Task 1: Add `lowLatency` to DeviceParams

**Files:**
- Modify: `QtScrcpy/QtScrcpyCore/include/QtScrcpyCoreDef.h`

- [ ] **Step 1: Open and read the file (required before editing)**

```bash
# File location: QtScrcpy/QtScrcpyCore/include/QtScrcpyCoreDef.h
# Confirm the last field before the closing brace
```

- [ ] **Step 2: Add `lowLatency` field to `DeviceParams`**

In `QtScrcpyCoreDef.h`, after the line `bool renderExpiredFrames = false;`, add:

```cpp
    bool lowLatency = false;         // 低延迟模式：跳过已有更新数据时的旧帧
```

- [ ] **Step 3: Build to verify no compile errors**

```bash
cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
cmake --build build --config Release -j4 2>&1 | tail -20
```

Expected: build succeeds (no errors referencing `DeviceParams`).

- [ ] **Step 4: Commit**

```bash
git add QtScrcpy/QtScrcpyCore/include/QtScrcpyCoreDef.h
git commit -m "feat: add lowLatency field to DeviceParams"
```

---

## Task 2: Update UserBootConfig — maxSize + lowLatency

**Files:**
- Modify: `QtScrcpy/util/config.h`
- Modify: `QtScrcpy/util/config.cpp`

- [ ] **Step 1: Replace `maxSizeIndex` with `maxSize` in `UserBootConfig` (config.h)**

In `QtScrcpy/util/config.h`, replace:

```cpp
    int maxSizeIndex = 0;
```

with:

```cpp
    quint16 maxSize = 0;             // 0 = native resolution
    bool lowLatency = false;
```

- [ ] **Step 2: Add new config key constants in config.cpp**

In `QtScrcpy/util/config.cpp`, after the existing `#define COMMON_MAX_SIZE_INDEX_KEY` block, add:

```cpp
#define COMMON_MAX_SIZE_KEY         "MaxSize"
#define COMMON_MAX_SIZE_DEF         0

#define COMMON_LOW_LATENCY_KEY      "LowLatency"
#define COMMON_LOW_LATENCY_DEF      false
```

- [ ] **Step 3: Update `setUserBootConfig` in config.cpp**

Replace the line:
```cpp
    m_userData->setValue(COMMON_MAX_SIZE_INDEX_KEY, config.maxSizeIndex);
```
with:
```cpp
    m_userData->setValue(COMMON_MAX_SIZE_KEY, config.maxSize);
    m_userData->setValue(COMMON_LOW_LATENCY_KEY, config.lowLatency);
    m_userData->remove(COMMON_MAX_SIZE_INDEX_KEY);  // remove legacy key
```

- [ ] **Step 4: Update `getUserBootConfig` in config.cpp — add migration**

Replace the line:
```cpp
    config.maxSizeIndex = m_userData->value(COMMON_MAX_SIZE_INDEX_KEY, COMMON_MAX_SIZE_INDEX_DEF).toInt();
```
with:
```cpp
    // Migration: convert old maxSizeIndex to maxSize
    if (m_userData->contains(COMMON_MAX_SIZE_INDEX_KEY)) {
        static const quint16 indexToSize[] = {640, 720, 1080, 1280, 1920, 0};
        int idx = m_userData->value(COMMON_MAX_SIZE_INDEX_KEY, 2).toInt();
        if (idx < 0 || idx > 5) idx = 5;
        config.maxSize = indexToSize[idx];
        m_userData->remove(COMMON_MAX_SIZE_INDEX_KEY);
        m_userData->setValue(COMMON_MAX_SIZE_KEY, config.maxSize);
    } else {
        config.maxSize = static_cast<quint16>(m_userData->value(COMMON_MAX_SIZE_KEY, COMMON_MAX_SIZE_DEF).toUInt());
    }
    config.lowLatency = m_userData->value(COMMON_LOW_LATENCY_KEY, COMMON_LOW_LATENCY_DEF).toBool();
```

- [ ] **Step 5: Build to verify**

```bash
cmake --build build --config Release -j4 2>&1 | tail -20
```

Expected: no errors. Any reference to `maxSizeIndex` elsewhere will be caught here.

- [ ] **Step 6: Commit**

```bash
git add QtScrcpy/util/config.h QtScrcpy/util/config.cpp
git commit -m "feat: replace maxSizeIndex with maxSize int, add lowLatency to UserBootConfig"
```

---

## Task 3: Demuxer — low-latency stale-packet skip

**Files:**
- Modify: `QtScrcpy/QtScrcpyCore/src/device/demuxer/demuxer.h`
- Modify: `QtScrcpy/QtScrcpyCore/src/device/demuxer/demuxer.cpp`

- [ ] **Step 1: Add `setLowLatency` + `m_lowLatency` to demuxer.h**

In `QtScrcpy/QtScrcpyCore/src/device/demuxer/demuxer.h`, add the public method after `setFrameSize`:

```cpp
    void setLowLatency(bool lowLatency);
```

Add the private member after `m_stopRequested`:

```cpp
    bool m_lowLatency = false;
```

- [ ] **Step 2: Implement `setLowLatency` in demuxer.cpp**

At the bottom of `QtScrcpy/QtScrcpyCore/src/device/demuxer/demuxer.cpp`, add:

```cpp
void Demuxer::setLowLatency(bool lowLatency)
{
    m_lowLatency = lowLatency;
}
```

- [ ] **Step 3: Add stale-packet skip logic in `processFrame`**

In `demuxer.cpp`, replace the `processFrame` function:

```cpp
bool Demuxer::processFrame(AVPacket *packet)
{
    packet->dts = packet->pts;

    if (m_lowLatency && m_videoSocket) {
        // If more data is already available, drop this frame — it's stale.
        // We skip only data packets (not config/SPS packets) to avoid
        // corrupting the codec state.
        qint64 available = m_videoSocket->bytesAvailable();
        if (available >= HEADER_SIZE) {
            qDebug("[Demuxer] low-latency: dropping stale frame, %lld bytes already waiting", (long long)available);
            return true;  // drop: caller unrefs the packet
        }
    }

    emit getFrame(packet);
    return true;
}
```

- [ ] **Step 4: Build to verify**

```bash
cmake --build build --config Release -j4 2>&1 | tail -20
```

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add QtScrcpy/QtScrcpyCore/src/device/demuxer/demuxer.h \
        QtScrcpy/QtScrcpyCore/src/device/demuxer/demuxer.cpp
git commit -m "feat: add low-latency stale-packet skip to Demuxer"
```

---

## Task 4: Wire `lowLatency` through Device

**Files:**
- Modify: `QtScrcpy/QtScrcpyCore/src/device/device.cpp`

- [ ] **Step 1: Pass `lowLatency` to the Demuxer in `Device::Device()`**

In `QtScrcpy/QtScrcpyCore/src/device/device.cpp`, after the line:

```cpp
    m_stream = new Demuxer(this);
```

add:

```cpp
    m_stream->setLowLatency(params.lowLatency);
```

- [ ] **Step 2: Enforce `renderExpiredFrames=false` in low-latency mode**

`renderExpiredFrames` is set on the VideoBuffer inside Decoder. Currently `setRenderExpiredFrames` is never called, leaving it at the default `false`. In low-latency mode we must ensure it stays `false` even if the config value is `true`. Add after `m_decoder = new Decoder(...)`:

```cpp
    if (params.lowLatency) {
        // Force frame-drop mode: never wait for the renderer to consume a frame.
        // This overrides the RenderExpiredFrames config setting.
        params.renderExpiredFrames = false;
    }
```

Note: this must appear **before** `params.renderExpiredFrames` is used. Currently `renderExpiredFrames` is stored in `m_params` but `VideoBuffer::setRenderExpiredFrames` is never actually called in `device.cpp` — so add the explicit call to make the config take effect. After creating the decoder, add:

```cpp
    if (m_decoder) {
        m_decoder->setRenderExpiredFrames(params.renderExpiredFrames);
    }
```

Then add `setRenderExpiredFrames` to `Decoder`'s public API in `decoder.h`:

```cpp
    void setRenderExpiredFrames(bool value);
```

And implement in `decoder.cpp`:

```cpp
void Decoder::setRenderExpiredFrames(bool value)
{
    if (m_vb) {
        m_vb->setRenderExpiredFrames(value);
    }
}
```

- [ ] **Step 3: Build to verify**

```bash
cmake --build build --config Release -j4 2>&1 | tail -20
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add QtScrcpy/QtScrcpyCore/src/device/device.cpp \
        QtScrcpy/QtScrcpyCore/src/device/decoder/decoder.h \
        QtScrcpy/QtScrcpyCore/src/device/decoder/decoder.cpp
git commit -m "feat: wire lowLatency flag through Device to Demuxer and VideoBuffer"
```

---

## Task 5: Dialog UI — editable resolution + low-latency checkbox

**Files:**
- Modify: `QtScrcpy/ui/dialog.ui`
- Modify: `QtScrcpy/ui/dialog.cpp`

- [ ] **Step 1: Add `lowLatencyCheck` to dialog.ui**

In `QtScrcpy/ui/dialog.ui`, find the block at `row="2" column="0"` which contains `showToolbar`. After that item's closing `</item>` tag, add:

```xml
            <item row="2" column="1">
             <widget class="QCheckBox" name="lowLatencyCheck">
              <property name="text">
               <string>Low latency</string>
              </property>
             </widget>
            </item>
```

- [ ] **Step 2: Make `maxSizeBox` editable with updated presets in `initUI()` (dialog.cpp)**

Replace the existing `maxSizeBox` item block in `initUI()`:

```cpp
    ui->maxSizeBox->addItem("640");
    ui->maxSizeBox->addItem("720");
    ui->maxSizeBox->addItem("1080");
    ui->maxSizeBox->addItem("1280");
    ui->maxSizeBox->addItem("1920");
    ui->maxSizeBox->addItem(tr("original"));
```

with:

```cpp
    ui->maxSizeBox->setEditable(true);
    ui->maxSizeBox->addItem("640");
    ui->maxSizeBox->addItem("720");
    ui->maxSizeBox->addItem("1080");
    ui->maxSizeBox->addItem("1440");
    ui->maxSizeBox->addItem("1920");
    ui->maxSizeBox->addItem("2560");
    ui->maxSizeBox->addItem(tr("Native"));
    ui->maxSizeBox->setValidator(new QIntValidator(1, 9999, this));
    // "Native" bypasses the validator; handle it explicitly in updateBootConfig
```

- [ ] **Step 3: Update `updateBootConfig(toView=true)` to load `maxSize` and `lowLatency`**

Replace:

```cpp
        ui->maxSizeBox->setCurrentIndex(config.maxSizeIndex);
```

with:

```cpp
        if (config.maxSize == 0) {
            ui->maxSizeBox->setCurrentText(tr("Native"));
        } else {
            ui->maxSizeBox->setCurrentText(QString::number(config.maxSize));
        }
        ui->lowLatencyCheck->setChecked(config.lowLatency);
```

- [ ] **Step 4: Update `updateBootConfig(toView=false)` to save `maxSize` and `lowLatency`**

Replace:

```cpp
        config.maxSizeIndex = ui->maxSizeBox->currentIndex();
```

with:

```cpp
        QString sizeText = ui->maxSizeBox->currentText().trimmed();
        config.maxSize = (sizeText == tr("Native") || sizeText == tr("original")) ? 0
                         : static_cast<quint16>(sizeText.toUShort());
        config.lowLatency = ui->lowLatencyCheck->isChecked();
```

- [ ] **Step 5: Update `on_startServerBtn_clicked` to use `maxSize` and `lowLatency`**

The existing line:

```cpp
    quint16 videoSize = ui->maxSizeBox->currentText().trimmed().toUShort();
```

already returns `0` for "Native" (since `"Native".toUShort() == 0`). Keep this line.

After `params.renderExpiredFrames = Config::getInstance().getRenderExpiredFrames();`, add:

```cpp
    params.lowLatency = ui->lowLatencyCheck->isChecked();
```

- [ ] **Step 6: Build to verify**

```bash
cmake --build build --config Release -j4 2>&1 | tail -20
```

Expected: build succeeds. Any remaining references to `maxSizeIndex` in dialog.cpp will be flagged.

- [ ] **Step 7: Manual test — resolution UI**

Run the application. In the config panel:
- `maxSizeBox` should now be editable (can type arbitrary values)
- "Native" option should appear in the dropdown
- Type `2400` — it should be accepted
- Type `abc` — the validator should reject it (revert)
- Select "Native" — `max_size=0` is sent to server

- [ ] **Step 8: Manual test — low-latency checkbox**

Enable "Low latency", connect a device. Verify in logs that the lowLatency flag reaches `Demuxer::setLowLatency(true)` (add a `qDebug` temporarily if needed).

- [ ] **Step 9: Commit**

```bash
git add QtScrcpy/ui/dialog.ui QtScrcpy/ui/dialog.cpp QtScrcpy/ui/dialog.h
git commit -m "feat: editable resolution combo and low-latency checkbox in dialog UI"
```

---

## Task 6: Create DeviceTile widget

**Files:**
- Create: `QtScrcpy/ui/devicetile.h`
- Create: `QtScrcpy/ui/devicetile.cpp`

- [ ] **Step 1: Create `devicetile.h`**

Create `QtScrcpy/ui/devicetile.h`:

```cpp
#pragma once

#include <QPointer>
#include <QWidget>

class VideoForm;
class QLabel;
class QPushButton;
class QVBoxLayout;

class DeviceTile : public QWidget
{
    Q_OBJECT
public:
    explicit DeviceTile(const QString &serial, bool frameless, bool skin, bool showToolbar, QWidget *parent = nullptr);
    ~DeviceTile();

    const QString &serial() const;
    VideoForm *videoForm() const;

    // Detach VideoForm to a standalone window. Tile shows placeholder.
    void detach();
    // Re-embed VideoForm back into the tile (called when pop-out window is closed).
    void attach();
    bool isDetached() const;

    void updateShowSize(const QSize &size);

signals:
    void popOutRequested(const QString &serial);
    void disconnectRequested(const QString &serial);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onPopOut();
    void onDisconnect();

private:
    QString m_serial;
    QPointer<VideoForm> m_videoForm;
    QPointer<QLabel> m_placeholder;
    QPointer<QLabel> m_titleLabel;
    QPointer<QPushButton> m_popOutBtn;
    QPointer<QPushButton> m_disconnectBtn;
    QVBoxLayout *m_videoLayout = nullptr;
    bool m_detached = false;
};
```

- [ ] **Step 2: Create `devicetile.cpp`**

Create `QtScrcpy/ui/devicetile.cpp`:

```cpp
#include "devicetile.h"
#include "videoform.h"
#include "config.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

DeviceTile::DeviceTile(const QString &serial, bool frameless, bool skin, bool showToolbar, QWidget *parent)
    : QWidget(parent)
    , m_serial(serial)
{
    // Header bar
    auto *headerWidget = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(4);

    m_titleLabel = new QLabel(serial, headerWidget);
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_popOutBtn = new QPushButton("↗", headerWidget);
    m_popOutBtn->setFixedSize(22, 22);
    m_popOutBtn->setToolTip(tr("Pop out"));

    m_disconnectBtn = new QPushButton("×", headerWidget);
    m_disconnectBtn->setFixedSize(22, 22);
    m_disconnectBtn->setToolTip(tr("Disconnect"));

    headerLayout->addWidget(m_titleLabel, 1);
    headerLayout->addWidget(m_popOutBtn);
    headerLayout->addWidget(m_disconnectBtn);

    // Video area
    auto *videoAreaWidget = new QWidget(this);
    m_videoLayout = new QVBoxLayout(videoAreaWidget);
    m_videoLayout->setContentsMargins(0, 0, 0, 0);

    m_videoForm = new VideoForm(frameless, skin, showToolbar, videoAreaWidget);
    m_videoForm->setSerial(serial);
    m_videoLayout->addWidget(m_videoForm);

    m_placeholder = new QLabel(tr("Detached"), videoAreaWidget);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->hide();
    m_videoLayout->addWidget(m_placeholder);

    // Tile outer layout
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(2, 2, 2, 2);
    outerLayout->setSpacing(0);
    outerLayout->addWidget(headerWidget);
    outerLayout->addWidget(videoAreaWidget, 1);

    setMinimumSize(240, 135 + 28);  // 16:9 + header

    connect(m_popOutBtn, &QPushButton::clicked, this, &DeviceTile::onPopOut);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &DeviceTile::onDisconnect);
}

DeviceTile::~DeviceTile() {}

const QString &DeviceTile::serial() const
{
    return m_serial;
}

VideoForm *DeviceTile::videoForm() const
{
    return m_videoForm;
}

void DeviceTile::detach()
{
    if (m_detached || !m_videoForm) {
        return;
    }
    m_detached = true;
    // Prevent VideoForm from being destroyed when the pop-out window closes.
    m_videoForm->setAttribute(Qt::WA_DeleteOnClose, false);
    m_videoForm->installEventFilter(this);
    m_videoForm->setParent(nullptr);   // become top-level window
    m_videoForm->show();
    m_placeholder->show();
    m_popOutBtn->setEnabled(false);
}

void DeviceTile::attach()
{
    if (!m_detached || !m_videoForm) {
        return;
    }
    m_detached = false;
    m_placeholder->hide();
    m_popOutBtn->setEnabled(true);
    m_videoForm->removeEventFilter(this);
    m_videoForm->setParent(m_videoLayout->parentWidget());
    m_videoLayout->insertWidget(0, m_videoForm);
    m_videoForm->show();
}

bool DeviceTile::isDetached() const
{
    return m_detached;
}

bool DeviceTile::eventFilter(QObject *obj, QEvent *event)
{
    // Intercept close on the detached VideoForm window — re-embed instead of closing.
    if (obj == m_videoForm && event->type() == QEvent::Close) {
        QTimer::singleShot(0, this, &DeviceTile::attach);
        return true;  // consume the close event
    }
    return QWidget::eventFilter(obj, event);
}

void DeviceTile::updateShowSize(const QSize &size)
{
    if (m_videoForm) {
        m_videoForm->updateShowSize(size);
    }
}

void DeviceTile::onPopOut()
{
    emit popOutRequested(m_serial);
}

void DeviceTile::onDisconnect()
{
    emit disconnectRequested(m_serial);
}
```

- [ ] **Step 3: Build to verify**

```bash
cmake --build build --config Release -j4 2>&1 | grep -E "error:|warning:" | head -30
```

DeviceTile won't be linked yet (not in CMakeLists). This build step is to catch syntax errors via the IDE's compile database — skip if you haven't added it to CMakeLists yet (do that in Task 8).

- [ ] **Step 4: Commit**

```bash
git add QtScrcpy/ui/devicetile.h QtScrcpy/ui/devicetile.cpp
git commit -m "feat: add DeviceTile widget for embedded VideoForm with pop-out support"
```

---

## Task 7: Create DeviceDashboard widget

**Files:**
- Create: `QtScrcpy/ui/devicedashboard.h`
- Create: `QtScrcpy/ui/devicedashboard.cpp`

- [ ] **Step 1: Create `devicedashboard.h`**

Create `QtScrcpy/ui/devicedashboard.h`:

```cpp
#pragma once

#include <QHash>
#include <QPointer>
#include <QWidget>

class DeviceTile;
class QGridLayout;
class QScrollArea;

class DeviceDashboard : public QWidget
{
    Q_OBJECT
public:
    explicit DeviceDashboard(QWidget *parent = nullptr);
    ~DeviceDashboard();

public slots:
    void onDeviceConnected(bool success, const QString &serial,
                           const QString &deviceName, const QSize &size);
    void onDeviceDisconnected(const QString &serial);

private:
    void addTile(const QString &serial, const QSize &size);
    void removeTile(const QString &serial);
    void relayoutGrid();

    QHash<QString, QPointer<DeviceTile>> m_tiles;
    QGridLayout *m_gridLayout = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_gridWidget = nullptr;
};
```

- [ ] **Step 2: Create `devicedashboard.cpp`**

Create `QtScrcpy/ui/devicedashboard.cpp`:

```cpp
#include "devicedashboard.h"
#include "devicetile.h"
#include "config.h"

#include <QDebug>
#include <QGridLayout>
#include <QScrollArea>
#include <QVBoxLayout>

#include "../QtScrcpyCore/include/QtScrcpyCore.h"

DeviceDashboard::DeviceDashboard(QWidget *parent)
    : QWidget(parent)
{
    m_gridWidget = new QWidget;
    m_gridLayout = new QGridLayout(m_gridWidget);
    m_gridLayout->setSpacing(4);
    m_gridLayout->setContentsMargins(4, 4, 4, 4);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidget(m_gridWidget);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(m_scrollArea);
}

DeviceDashboard::~DeviceDashboard() {}

void DeviceDashboard::onDeviceConnected(bool success, const QString &serial,
                                         const QString &deviceName, const QSize &size)
{
    Q_UNUSED(deviceName)
    if (!success) {
        return;
    }
    addTile(serial, size);
}

void DeviceDashboard::onDeviceDisconnected(const QString &serial)
{
    removeTile(serial);
}

void DeviceDashboard::addTile(const QString &serial, const QSize &size)
{
    if (m_tiles.contains(serial)) {
        return;
    }

    bool frameless = false;   // read from config if needed
    bool skin = Config::getInstance().getSkin();
    bool showToolbar = true;  // set from config or dialog setting

    auto *tile = new DeviceTile(serial, frameless, skin, showToolbar, m_gridWidget);
    tile->updateShowSize(size);

    // Register the tile's VideoForm as a device observer
    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    if (device && tile->videoForm()) {
        device->setUserData(static_cast<void *>(tile));
        device->registerDeviceObserver(tile->videoForm());
    }

    m_tiles.insert(serial, tile);
    relayoutGrid();

    connect(tile, &DeviceTile::disconnectRequested, this, [](const QString &s) {
        qsc::IDeviceManage::getInstance().disconnectDevice(s);
    });
    connect(tile, &DeviceTile::popOutRequested, this, [this](const QString &s) {
        if (auto t = m_tiles.value(s)) {
            t->detach();
        }
    });
}

void DeviceDashboard::removeTile(const QString &serial)
{
    auto it = m_tiles.find(serial);
    if (it == m_tiles.end()) {
        return;
    }

    auto *tile = it.value();
    if (tile) {
        auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
        if (device && tile->videoForm()) {
            device->deRegisterDeviceObserver(tile->videoForm());
        }
        m_tiles.erase(it);
        relayoutGrid();
        tile->deleteLater();
    } else {
        m_tiles.erase(it);
    }
}

void DeviceDashboard::relayoutGrid()
{
    // Remove all items from grid (widgets stay alive, just removed from layout)
    QList<DeviceTile *> activeTiles;
    for (auto t : m_tiles) {
        if (t) {
            activeTiles.append(t);
            m_gridLayout->removeWidget(t);
        }
    }

    int cols = (activeTiles.size() <= 1) ? 1
             : (activeTiles.size() <= 4) ? 2
             : 3;

    for (int i = 0; i < activeTiles.size(); ++i) {
        m_gridLayout->addWidget(activeTiles[i], i / cols, i % cols);
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add QtScrcpy/ui/devicedashboard.h QtScrcpy/ui/devicedashboard.cpp
git commit -m "feat: add DeviceDashboard tiled grid widget"
```

---

## Task 8: Register new files in CMakeLists and integrate into Dialog

**Files:**
- Modify: `QtScrcpy/CMakeLists.txt`
- Modify: `QtScrcpy/ui/dialog.h`
- Modify: `QtScrcpy/ui/dialog.cpp`

- [ ] **Step 1: Add devicetile and devicedashboard to CMakeLists.txt**

In `QtScrcpy/CMakeLists.txt`, find the source file list where `ui/videoform.cpp` and `ui/dialog.cpp` are listed. Add:

```cmake
    ui/devicetile.cpp
    ui/devicedashboard.cpp
```

Also add their headers to the header list if one exists:

```cmake
    ui/devicetile.h
    ui/devicedashboard.h
```

- [ ] **Step 2: Add `DeviceDashboard` member to `dialog.h`**

In `QtScrcpy/ui/dialog.h`, add the include and forward declaration near the top:

```cpp
class DeviceDashboard;
```

Add the member to the private section of `Dialog`:

```cpp
    QPointer<DeviceDashboard> m_dashboard;
```

- [ ] **Step 3: Add `#include "devicedashboard.h"` to dialog.cpp**

At the top of `QtScrcpy/ui/dialog.cpp`, add:

```cpp
#include "devicedashboard.h"
```

- [ ] **Step 4: Create and connect DeviceDashboard in Dialog constructor**

In `Dialog::Dialog()`, after `initUI();`, add:

```cpp
    // Create the device dashboard and add it to the main horizontal layout
    m_dashboard = new DeviceDashboard(this);
    // The main layout is horizontalLayout_11; append dashboard after leftWidget
    auto *mainLayout = qobject_cast<QHBoxLayout *>(layout());
    if (mainLayout) {
        mainLayout->addWidget(m_dashboard, 1);  // stretch factor 1
    }

    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceConnected,
            m_dashboard, &DeviceDashboard::onDeviceConnected);
    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceDisconnected,
            m_dashboard, &DeviceDashboard::onDeviceDisconnected);
```

- [ ] **Step 5: Remove VideoForm creation from Dialog::onDeviceConnected**

In `dialog.cpp`, the `onDeviceConnected` slot currently creates a `VideoForm`. Since the dashboard now handles this, replace the entire body of `Dialog::onDeviceConnected` with just the parts that don't involve VideoForm:

```cpp
void Dialog::onDeviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size)
{
    Q_UNUSED(deviceName)
    Q_UNUSED(size)
    if (!success) {
        outLog(tr("Failed to connect device: %1").arg(serial));
        return;
    }
    outLog(tr("Device connected: %1").arg(serial));
}
```

- [ ] **Step 6: Remove VideoForm deletion from Dialog::onDeviceDisconnected**

Replace the existing `Dialog::onDeviceDisconnected` body with:

```cpp
void Dialog::onDeviceDisconnected(QString serial)
{
    GroupController::instance().removeDevice(serial);
    outLog(tr("Device disconnected: %1").arg(serial));
}
```

(The tile cleanup is now handled by `DeviceDashboard::onDeviceDisconnected`.)

- [ ] **Step 7: Build to verify**

```bash
cmake --build build --config Release -j4 2>&1 | tail -30
```

Expected: build succeeds. Fix any missing includes or type mismatches before proceeding.

- [ ] **Step 8: Commit**

```bash
git add QtScrcpy/CMakeLists.txt \
        QtScrcpy/ui/dialog.h \
        QtScrcpy/ui/dialog.cpp
git commit -m "feat: integrate DeviceDashboard into Dialog, move VideoForm management to dashboard"
```

---

## Task 9: End-to-end manual test

No code changes in this task. Validate all three features work correctly.

- [ ] **Step 1: Build release binary**

```bash
./ci/linux/build_for_linux.sh "Release"
```

Expected: build succeeds, binary at `./output/x64/Release/`.

- [ ] **Step 2: Test — single device**

Connect one Android device via USB.
1. Click "Start" — a single `DeviceTile` should appear in the dashboard filling the panel.
2. The video stream should play inside the tile.
3. Resolution: set to "Native" → server logs should show `max_size=0`.
4. Resolution: type `1440` → server should use 1440.
5. Resolution: type `abc` → rejected by validator, reverts to previous value.

- [ ] **Step 3: Test — multiple devices**

Connect a second device.
1. Dashboard auto-reflows to a 2×2 grid.
2. Both streams play simultaneously.
3. Click ↗ on one tile — the VideoForm detaches as a standalone window; tile shows "Detached" placeholder.
4. Close the standalone window — it re-embeds in the tile.
5. Click × on a tile — device disconnects, tile is removed, grid reflows.

- [ ] **Step 4: Test — low latency**

1. Enable "Low latency" checkbox.
2. Connect a device.
3. Run `adb logcat -s QtScrcpy` and look for `low-latency: dropping stale frame` log lines — these appear only when the pipeline falls behind (may not appear under ideal network conditions, which is correct).
4. Disable "Low latency" — no drop logs should appear.

- [ ] **Step 5: Test — config persistence**

1. Set resolution to `1440`, enable "Low latency".
2. Close and reopen the app.
3. Resolution should show `1440`, "Low latency" should be checked.
4. On a fresh install (no config file), default resolution should be "Native" (`maxSize=0`).

- [ ] **Step 6: Commit final state**

```bash
git add -A
git status  # confirm only expected files are staged
git commit -m "test: verify multi-mirror, high-res, and low-latency features manually"
```
