#ifndef DIALOG_H
#define DIALOG_H

#include <QWidget>
#include <QPointer>
#include <QMessageBox>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QListWidget>
#include <QTimer>
#include <QDateTime>
#include <QThread>
#include <QMutex>


#include "adbprocess.h"
#include "../QtScrcpyCore/include/QtScrcpyCore.h"
#include "audio/audiooutput.h"

struct CsvDeviceInfo {
    QString brand;
    QString device;
    QString manufacturer;
    QString modelName;
};

namespace Ui
{
    class Widget;
}

class QYUVOpenGLWidget;
class DeviceDashboard;
class Dialog : public QWidget
{
    Q_OBJECT

public:
    explicit Dialog(QWidget *parent = 0);
    ~Dialog();

    void outLog(const QString &log, bool newLine = true);
    bool filterLog(const QString &log);
    void getIPbyIp();

private slots:
    void onDeviceConnected(bool success, const QString& serial, const QString& deviceName, const QSize& size);
    void onDeviceDisconnected(QString serial);
    void onDeviceInfoUpdated(const qsc::DeviceInfo &info);

    void on_updateDevice_clicked();
    void on_startServerBtn_clicked();
    void on_stopServerBtn_clicked();
    void on_wirelessConnectBtn_clicked();
    void on_startAdbdBtn_clicked();
    void on_getIPBtn_clicked();
    void on_wirelessDisConnectBtn_clicked();
    void on_selectRecordPathBtn_clicked();
    void on_recordPathEdt_textChanged(const QString &arg1);
    void on_adbCommandBtn_clicked();
    void on_stopAdbBtn_clicked();
    void on_clearOut_clicked();
    void on_stopAllServerBtn_clicked();
    void on_refreshGameScriptBtn_clicked();
    void on_applyScriptBtn_clicked();
    void on_recordScreenCheck_clicked(bool checked);
    void on_usbConnectBtn_clicked();
    void on_wifiConnectBtn_clicked();
    void on_connectedPhoneList_itemDoubleClicked(QListWidgetItem *item);
    void on_updateNameBtn_clicked();
    void on_useSingleModeCheck_clicked();
    void on_serialBox_currentIndexChanged(const QString &arg1);

    void on_startAudioBtn_clicked();

    void on_stopAudioBtn_clicked();

    void on_installSndcpyBtn_clicked();

    void on_autoUpdatecheckBox_toggled(bool checked);

    void showIpEditMenu(const QPoint &pos);

private:
    bool checkAdbRun();
    void initUI();
    void updateBootConfig(bool toView = true);
    void execAdbCmd();
    QString getGameScript(const QString &fileName);
    void slotActivated(QSystemTrayIcon::ActivationReason reason);
    int findDeviceFromeSerialBox(bool wifi);
    quint32 getBitRate();
    const QString &getServerPath();
    void loadIpHistory();
    void saveIpHistory(const QString &ip);
    QString getDeviceDisplayName(const QString &serial);
    void loadDevicesCsv();
    QString getDeviceModelFromCsv(const QString &deviceId);
    void cacheDeviceInfo(const QString &serial, const QString &manufacturer, const QString &device);
    void loadPortHistory();
    void savePortHistory(const QString &port);

    void showPortEditMenu(const QPoint &pos);

protected:
    void closeEvent(QCloseEvent *event);

private:
    enum ConnectionState {
        CS_IDLE,
        CS_STOPPING_ALL,
        CS_UPDATING_DEVICES_INITIAL,
        CS_GETTING_IP,
        CS_STARTING_ADBD,
        CS_WIRELESS_CONNECT,
        CS_UPDATING_DEVICES_FINAL,
        CS_STARTING_SERVER
    };

    void advanceConnectionState();
    void startConnectionWorkflow(bool isWifi);

    Ui::Widget *ui;
    qsc::AdbProcess m_adb;
    QSystemTrayIcon *m_hideIcon;
    QMenu *m_menu;
    QAction *m_showWindow;
    QAction *m_quit;
    AudioOutput m_audioOutput;
    QTimer m_autoUpdatetimer;
    QTimer m_connectionTimer;
    QList<CsvDeviceInfo> m_devicesCsv;
    QHash<QString, QPair<QString, QString>> m_deviceInfoCache; // serial -> (manufacturer, device)
    QDateTime m_lastFullDeviceUpdate;
    bool m_deviceUpdateInProgress;
    ConnectionState m_connectionState = CS_IDLE;
    bool m_connectionIsWifi = false;
    QPointer<DeviceDashboard> m_dashboard;
};

#endif // DIALOG_H
