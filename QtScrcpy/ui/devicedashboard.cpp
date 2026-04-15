#include "devicedashboard.h"
#include "devicetile.h"
#include "videoform.h"
#include "config.h"

#include "../groupcontroller/groupcontroller.h"

#include <QDebug>
#include <QFrame>
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

quint16 DeviceDashboard::optimalMaxSize() const
{
    // Calculate expected tile size for the NEXT device (current count + 1)
    int nextCount = m_tiles.size() + 1;
    int cols = (nextCount <= 1) ? 1 : (nextCount <= 4) ? 2 : 3;
    int rows = (nextCount + cols - 1) / cols;

    QSize avail = m_scrollArea->viewport()->size();
    if (!avail.isValid() || avail.isEmpty()) {
        return 0;  // fallback: let caller use native or configured value
    }

    int tileW = avail.width() / cols;
    int tileH = avail.height() / rows;
    // Use the longer dimension so portrait and landscape devices are handled correctly
    return static_cast<quint16>(qMax(tileW, tileH));
}

void DeviceDashboard::onDeviceConnected(bool success, const QString &serial,
                                         const QString &deviceName, const QSize &size)
{
    if (!success) {
        return;
    }
    addTile(serial, deviceName, size);
}

void DeviceDashboard::onDeviceDisconnected(const QString &serial)
{
    removeTile(serial);
}

void DeviceDashboard::addTile(const QString &serial, const QString &deviceName, const QSize &size)
{
    if (m_tiles.contains(serial)) {
        return;
    }

    // Verify device still exists (it could have disconnected between signal and slot)
    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    if (!device) {
        return;
    }

    UserBootConfig bootConfig = Config::getInstance().getUserBootConfig();
    bool frameless = false;  // frameless/skin cause WA_TranslucentBackground on child widgets,
    bool skin = false;       // which breaks OpenGL rendering when embedded — always off for tiles
    bool showToolbar = bootConfig.showToolbar;

    auto *tile = new DeviceTile(serial, deviceName, frameless, skin, showToolbar, m_gridWidget);
    tile->updateShowSize(size);

    if (tile->videoForm()) {
        device->setUserData(static_cast<void *>(tile));
        device->registerDeviceObserver(tile->videoForm());

        tile->videoForm()->showFPS(bootConfig.showFPS);
        if (bootConfig.windowOnTop) {
            tile->videoForm()->staysOnTop(true);
        }
    }

    m_tiles.insert(serial, tile);
    m_insertionOrder.append(serial);
    relayoutGrid();
    GroupController::instance().addDevice(serial);

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

    DeviceTile *tile = it.value();
    if (tile) {
        auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
        if (device && tile->videoForm()) {
            device->deRegisterDeviceObserver(tile->videoForm());
        }
        m_tiles.erase(it);
        m_insertionOrder.removeAll(serial);
        relayoutGrid();
        tile->deleteLater();
    } else {
        m_tiles.erase(it);
        m_insertionOrder.removeAll(serial);
    }
}

void DeviceDashboard::relayoutGrid()
{
    // Remove all items from grid (widgets stay alive, just removed from layout)
    QList<DeviceTile *> activeTiles;
    for (const QString &s : m_insertionOrder) {
        auto t = m_tiles.value(s);
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
