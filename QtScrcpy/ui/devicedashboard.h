#pragma once

#include <QHash>
#include <QPointer>
#include <QStringList>
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

    // Returns the optimal max_size (longer tile dimension in px) for the next device to connect.
    // Pass this to DeviceParams.maxSize so the server streams at the tile's display resolution.
    quint16 optimalMaxSize() const;

public slots:
    void onDeviceConnected(bool success, const QString &serial,
                           const QString &deviceName, const QSize &size);
    void onDeviceDisconnected(const QString &serial);

private:
    void addTile(const QString &serial, const QString &deviceName, const QSize &size);
    void removeTile(const QString &serial);
    void relayoutGrid();

    QHash<QString, QPointer<DeviceTile>> m_tiles;
    QStringList m_insertionOrder;
    QGridLayout *m_gridLayout = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_gridWidget = nullptr;
};
