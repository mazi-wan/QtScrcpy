#include "devicedashboard.h"
#include "devicetile.h"
#include "config.h"

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

    bool frameless = false;
    bool skin = static_cast<bool>(Config::getInstance().getSkin());
    bool showToolbar = true;

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
