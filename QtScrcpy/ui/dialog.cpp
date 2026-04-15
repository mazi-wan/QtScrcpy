#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QProcess>
#include <QRandomGenerator>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <algorithm>

#include "../groupcontroller/groupcontroller.h"
#include "config.h"
#include "devicedashboard.h"
#include "dialog.h"
#include "ui_dialog.h"

#ifdef Q_OS_WIN32
#include "../util/winutils.h"
#endif

QString s_keyMapPath = "";

const QString &getKeyMapPath()
{
    if (s_keyMapPath.isEmpty()) {
        s_keyMapPath = QString::fromLocal8Bit(qgetenv("QTSCRCPY_KEYMAP_PATH"));
        QFileInfo fileInfo(s_keyMapPath);
        if (s_keyMapPath.isEmpty() || !fileInfo.isDir()) {
            s_keyMapPath = QCoreApplication::applicationDirPath() + "/keymap";
        }
    }
    return s_keyMapPath;
}

Dialog::Dialog(QWidget *parent) : QWidget(parent), ui(new Ui::Widget)
{
    ui->setupUi(this);
    initUI();

    // Load CSV device database
    loadDevicesCsv();
    
    // Initialize device update state
    m_deviceUpdateInProgress = false;

    updateBootConfig(true);

    on_useSingleModeCheck_clicked();
    on_updateDevice_clicked();

    connect(&m_autoUpdatetimer, &QTimer::timeout, this, &Dialog::on_updateDevice_clicked);
    if (ui->autoUpdatecheckBox->isChecked()) {
        m_autoUpdatetimer.start(5000);
    }

    m_connectionTimer.setSingleShot(true);
    connect(&m_connectionTimer, &QTimer::timeout, this, &Dialog::advanceConnectionState);

    // Connect to device info updated signal for async property fetching
    connect(&m_adb, &qsc::AdbProcess::deviceInfoUpdated, this, &Dialog::onDeviceInfoUpdated);

    connect(&m_adb, &qsc::AdbProcess::adbProcessResult, this, [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        QString log = "";
        bool newLine = true;
        QStringList args = m_adb.arguments();

        switch (processResult) {
        case qsc::AdbProcess::AER_ERROR_START:
            break;
        case qsc::AdbProcess::AER_SUCCESS_START:
            log = "adb run";
            newLine = false;
            break;
        case qsc::AdbProcess::AER_ERROR_EXEC:
            //log = m_adb.getErrorOut();
            if (args.contains("ifconfig") && args.contains("wlan0")) {
                getIPbyIp();
            }
            // Reset progress flag on error
            if (args.contains("devices")) {
                m_deviceUpdateInProgress = false;
            }
            break;
        case qsc::AdbProcess::AER_ERROR_MISSING_BINARY:
            log = "adb not found";
            // Reset progress flag on error
            if (args.contains("devices")) {
                m_deviceUpdateInProgress = false;
            }
            break;
        case qsc::AdbProcess::AER_SUCCESS_EXEC:
            //log = m_adb.getStdOut();
            if (args.contains("devices") && args.contains("-l")) {
                QList<qsc::DeviceInfo> devices = m_adb.getDevicesInfo();
                ui->serialBox->clear();
                ui->connectedPhoneList->clear();

                // Create a list of device display items for sorting
                QStringList deviceDisplayList;
                QStringList serialList;

                for (const auto &device : devices) {
                    // Cache device info for future auto-updates
                    cacheDeviceInfo(device.serial, device.manufacturer, device.device);

                    // Try to get model name from CSV first
                    QString displayName = getDeviceModelFromCsv(device.device);
                    if (displayName.isEmpty()) {
                        // Fallback to original method
                        displayName = device.manufacturer + " " + device.model;
                    }

                    QString fullDisplayName = displayName;
                    deviceDisplayList.append(fullDisplayName);
                    serialList.append(device.serial);
                }

                // Sort devices alphabetically by display name
                QList<QPair<QString, QString>> sortedDevices;
                for (int i = 0; i < deviceDisplayList.size(); ++i) {
                    sortedDevices.append(QPair<QString, QString>(deviceDisplayList[i], serialList[i]));
                }

                std::sort(sortedDevices.begin(), sortedDevices.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                    return a.first < b.first;
                });

                // Add sorted devices to UI
                for (const auto &sortedDevice : sortedDevices) {
                    ui->serialBox->addItem(sortedDevice.second);
                    ui->connectedPhoneList->addItem(sortedDevice.first);
                }

                // Trigger async fetch for each device to get detailed properties
                for (const auto &device : devices) {
                    m_adb.fetchDevicePropertiesAsync(device.serial);
                }

                // Reset progress flag after full device update
                m_deviceUpdateInProgress = false;
            } else if (args.contains("devices") && !args.contains("-l")) {
                // Handle basic device list (for auto-updates) - use cached info
                QStringList serials = m_adb.getDevicesSerialFromStdOut();
                ui->serialBox->clear();
                ui->connectedPhoneList->clear();

                // Create a list of device display items for sorting
                QStringList deviceDisplayList;
                QStringList serialList;

                for (const QString &serial : serials) {
                    QString displayName = "Unknown Device";

                    // Try to use cached info first
                    if (m_deviceInfoCache.contains(serial)) {
                        QPair<QString, QString> cached = m_deviceInfoCache[serial];
                        QString csvModelName = getDeviceModelFromCsv(cached.second);
                        if (!csvModelName.isEmpty()) {
                            displayName = csvModelName;
                        } else {
                            displayName = cached.first + " Device";
                        }
                    }

                    QString fullDisplayName = Config::getInstance().getNickName(serial) + "-" + serial + " (" + displayName + ")";
                    deviceDisplayList.append(fullDisplayName);
                    serialList.append(serial);
                }

                // Sort devices alphabetically by display name
                QList<QPair<QString, QString>> sortedDevices;
                for (int i = 0; i < deviceDisplayList.size(); ++i) {
                    sortedDevices.append(QPair<QString, QString>(deviceDisplayList[i], serialList[i]));
                }

                std::sort(sortedDevices.begin(), sortedDevices.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                    return a.first < b.first;
                });

                // Add sorted devices to UI
                for (const auto &sortedDevice : sortedDevices) {
                    ui->serialBox->addItem(sortedDevice.second);
                    ui->connectedPhoneList->addItem(sortedDevice.first);
                }
                
                // Reset progress flag after lightweight device update
                m_deviceUpdateInProgress = false;
            } else if (args.contains("show") && args.contains("wlan0")) {
                QString ip = m_adb.getDeviceIPFromStdOut();
                if (ip.isEmpty()) {
                    log = "ip not find, connect to wifi?";
                    break;
                }
                ui->deviceIpEdt->setEditText(ip);
            } else if (args.contains("ifconfig") && args.contains("wlan0")) {
                QString ip = m_adb.getDeviceIPFromStdOut();
                if (ip.isEmpty()) {
                    log = "ip not find, connect to wifi?";
                    break;
                }
                ui->deviceIpEdt->setEditText(ip);
            } else if (args.contains("ip -o a")) {
                QString ip = m_adb.getDeviceIPByIpFromStdOut();
                if (ip.isEmpty()) {
                    log = "ip not find, connect to wifi?";
                    break;
                }
                ui->deviceIpEdt->setEditText(ip);
            }
            break;
        }
        if (!log.isEmpty()) {
            outLog(log, newLine);
        }
    });

    m_hideIcon = new QSystemTrayIcon(this);
    m_hideIcon->setIcon(QIcon(":/image/tray/logo.png"));
    m_menu = new QMenu(this);
    m_quit = new QAction(this);
    m_showWindow = new QAction(this);
    m_showWindow->setText(tr("show"));
    m_quit->setText(tr("quit"));
    m_menu->addAction(m_showWindow);
    m_menu->addAction(m_quit);
    m_hideIcon->setContextMenu(m_menu);
    m_hideIcon->show();
    connect(m_showWindow, &QAction::triggered, this, &Dialog::show);
    connect(m_quit, &QAction::triggered, this, [this]() {
        m_hideIcon->hide();
        qApp->quit();
    });
    connect(m_hideIcon, &QSystemTrayIcon::activated, this, &Dialog::slotActivated);

    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceConnected, this, &Dialog::onDeviceConnected);
    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceDisconnected, this, &Dialog::onDeviceDisconnected);

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
}

Dialog::~Dialog()
{
    qDebug() << "~Dialog()";
    updateBootConfig(false);
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
    delete ui;
}

void Dialog::initUI()
{
    setAttribute(Qt::WA_DeleteOnClose);
    //setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint);

    setWindowTitle(Config::getInstance().getTitle());
#ifdef Q_OS_LINUX
    // Set window icon (inherits from application icon set in main.cpp)
    // If application icon was set, this will use it automatically
    if (!qApp->windowIcon().isNull()) {
        setWindowIcon(qApp->windowIcon());
    }
#endif

#ifdef Q_OS_WIN32
    WinUtils::setDarkBorderToWindow((HWND)this->winId(), true);
#endif

    ui->bitRateEdit->setValidator(new QIntValidator(1, 99999, this));

    ui->maxSizeBox->setEditable(true);
    ui->maxSizeBox->addItem("640");
    ui->maxSizeBox->addItem("720");
    ui->maxSizeBox->addItem("1080");
    ui->maxSizeBox->addItem("1440");
    ui->maxSizeBox->addItem("1920");
    ui->maxSizeBox->addItem("2560");
    ui->maxSizeBox->addItem(tr("Native"));
    ui->maxSizeBox->setValidator(new QIntValidator(1, 9999, this));

    ui->formatBox->addItem("mp4");
    ui->formatBox->addItem("mkv");

    ui->lockOrientationBox->addItem(tr("no lock"));
    ui->lockOrientationBox->addItem("0");
    ui->lockOrientationBox->addItem("90");
    ui->lockOrientationBox->addItem("180");
    ui->lockOrientationBox->addItem("270");
    ui->lockOrientationBox->setCurrentIndex(0);

    // 加载IP历史记录
    loadIpHistory();

    // 加载端口历史记录
    loadPortHistory();

    // 为deviceIpEdt添加右键菜单
    if (ui->deviceIpEdt->lineEdit()) {
        ui->deviceIpEdt->lineEdit()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ui->deviceIpEdt->lineEdit(), &QWidget::customContextMenuRequested, this, &Dialog::showIpEditMenu);
    }
    
    // 为devicePortEdt添加右键菜单
    if (ui->devicePortEdt->lineEdit()) {
        ui->devicePortEdt->lineEdit()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ui->devicePortEdt->lineEdit(), &QWidget::customContextMenuRequested,
                this, &Dialog::showPortEditMenu);
    }
}

void Dialog::updateBootConfig(bool toView)
{
    if (toView) {
        UserBootConfig config = Config::getInstance().getUserBootConfig();

        if (config.bitRate == 0) {
            ui->bitRateBox->setCurrentText("Mbps");
        } else if (config.bitRate % 1000000 == 0) {
            ui->bitRateEdit->setText(QString::number(config.bitRate / 1000000));
            ui->bitRateBox->setCurrentText("Mbps");
        } else {
            ui->bitRateEdit->setText(QString::number(config.bitRate / 1000));
            ui->bitRateBox->setCurrentText("Kbps");
        }

        if (config.maxSize == 0) {
            ui->maxSizeBox->setCurrentText(tr("Native"));
        } else {
            ui->maxSizeBox->setCurrentText(QString::number(config.maxSize));
        }
        ui->formatBox->setCurrentIndex(config.recordFormatIndex);
        ui->recordPathEdt->setText(config.recordPath);
        ui->lockOrientationBox->setCurrentIndex(config.lockOrientationIndex);
        ui->framelessCheck->setChecked(config.framelessWindow);
        ui->recordScreenCheck->setChecked(config.recordScreen);
        ui->notDisplayCheck->setChecked(config.recordBackground);
        ui->useReverseCheck->setChecked(config.reverseConnect);
        ui->fpsCheck->setChecked(config.showFPS);
        ui->alwaysTopCheck->setChecked(config.windowOnTop);
        ui->closeScreenCheck->setChecked(config.autoOffScreen);
        ui->stayAwakeCheck->setChecked(config.keepAlive);
        ui->useSingleModeCheck->setChecked(config.simpleMode);
        ui->autoUpdatecheckBox->setChecked(config.autoUpdateDevice);
        ui->showToolbar->setChecked(config.showToolbar);
    } else {
        UserBootConfig config;

        config.bitRate = getBitRate();
        QString sizeText = ui->maxSizeBox->currentText().trimmed();
        config.maxSize = (sizeText == tr("Native") || sizeText == tr("original")) ? 0
                         : static_cast<quint16>(sizeText.toUShort());
        config.recordFormatIndex = ui->formatBox->currentIndex();
        config.recordPath = ui->recordPathEdt->text();
        config.lockOrientationIndex = ui->lockOrientationBox->currentIndex();
        config.recordScreen = ui->recordScreenCheck->isChecked();
        config.recordBackground = ui->notDisplayCheck->isChecked();
        config.reverseConnect = ui->useReverseCheck->isChecked();
        config.showFPS = ui->fpsCheck->isChecked();
        config.windowOnTop = ui->alwaysTopCheck->isChecked();
        config.autoOffScreen = ui->closeScreenCheck->isChecked();
        config.framelessWindow = ui->framelessCheck->isChecked();
        config.keepAlive = ui->stayAwakeCheck->isChecked();
        config.simpleMode = ui->useSingleModeCheck->isChecked();
        config.autoUpdateDevice = ui->autoUpdatecheckBox->isChecked();
        config.showToolbar = ui->showToolbar->isChecked();

        // 保存当前IP到历史记录
        QString currentIp = ui->deviceIpEdt->currentText().trimmed();
        if (!currentIp.isEmpty()) {
            saveIpHistory(currentIp);
        }

        Config::getInstance().setUserBootConfig(config);
    }
}

void Dialog::execAdbCmd()
{
    if (checkAdbRun()) {
        return;
    }
    QString cmd = ui->adbCommandEdt->text().trimmed();
    outLog("adb " + cmd, false);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    m_adb.execute(ui->serialBox->currentText().trimmed(), cmd.split(" ", Qt::SkipEmptyParts));
#else
    m_adb.execute(ui->serialBox->currentText().trimmed(), cmd.split(" ", QString::SkipEmptyParts));
#endif
}

// Removed delayMs() - replaced with async connection state machine

QString Dialog::getGameScript(const QString &fileName)
{
    if (fileName.isEmpty()) {
        return "";
    }

    QFile loadFile(getKeyMapPath() + "/" + fileName);
    if (!loadFile.open(QIODevice::ReadOnly)) {
        outLog("open file failed:" + fileName, true);
        return "";
    }

    QString ret = loadFile.readAll();
    loadFile.close();
    return ret;
}

void Dialog::slotActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
    case QSystemTrayIcon::Trigger:
#ifdef Q_OS_WIN32
        this->show();
#endif
        break;
    default:
        break;
    }
}

void Dialog::closeEvent(QCloseEvent *event)
{
    this->hide();
    if (!Config::getInstance().getTrayMessageShown()) {
        Config::getInstance().setTrayMessageShown(true);
        m_hideIcon->showMessage(tr("Notice"), tr("Hidden here!"), QSystemTrayIcon::Information, 3000);
    }
    event->ignore();
}

void Dialog::on_updateDevice_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    
    // Prevent overlapping device updates to avoid blocking the main thread
    if (m_deviceUpdateInProgress) {
        qDebug() << "Device update already in progress, skipping...";
        return;
    }
    
    m_deviceUpdateInProgress = true;
    outLog("update devices...", false);

    // For auto-updates, use a lightweight check and cache if recent full update was done
    QDateTime now = QDateTime::currentDateTime();
    bool isAutoUpdate = sender() == &m_autoUpdatetimer;

    if (isAutoUpdate && m_lastFullDeviceUpdate.isValid() && m_lastFullDeviceUpdate.secsTo(now) < 30) {
        // For auto-updates within 30 seconds, use basic device list without expensive ADB calls
        m_adb.execute("", QStringList() << "devices");
    } else {
        // For manual updates or when cache is stale, do full update
        m_adb.execute("", QStringList() << "devices" << "-l");
        if (!isAutoUpdate) {
            m_lastFullDeviceUpdate = now;
        }
    }
}

void Dialog::on_startServerBtn_clicked()
{
    outLog("start server...", false);

    // "Native".toUShort() == 0, which correctly maps to native resolution (no scaling)
    quint16 videoSize = ui->maxSizeBox->currentText().trimmed().toUShort();
    qsc::DeviceParams params;
    params.serial = ui->serialBox->currentText().trimmed();
    params.maxSize = videoSize;
    params.bitRate = getBitRate();
    // on devices with Android >= 10, the capture frame rate can be limited
    params.maxFps = static_cast<quint32>(Config::getInstance().getMaxFps());
    params.closeScreen = ui->closeScreenCheck->isChecked();
    params.useReverse = ui->useReverseCheck->isChecked();
    params.display = !ui->notDisplayCheck->isChecked();
    params.renderExpiredFrames = Config::getInstance().getRenderExpiredFrames();
    params.lowLatency = true;
    if (ui->lockOrientationBox->currentIndex() > 0) {
        params.captureOrientationLock = 1;
        params.captureOrientation = (ui->lockOrientationBox->currentIndex() - 1) * 90;
    }
    params.stayAwake = ui->stayAwakeCheck->isChecked();
    params.recordFile = ui->recordScreenCheck->isChecked();
    params.recordPath = ui->recordPathEdt->text().trimmed();
    params.recordFileFormat = ui->formatBox->currentText().trimmed();
    params.serverLocalPath = getServerPath();
    params.serverRemotePath = Config::getInstance().getServerPath();
    params.pushFilePath = Config::getInstance().getPushFilePath();
    params.gameScript = getGameScript(ui->gameBox->currentText());
    params.logLevel = Config::getInstance().getLogLevel();
    params.codecOptions = Config::getInstance().getCodecOptions();
    params.codecName = Config::getInstance().getCodecName();
    params.scid = QRandomGenerator::global()->bounded(1, 10000) & 0x7FFFFFFF;

    qsc::IDeviceManage::getInstance().connectDevice(params);
}

void Dialog::on_stopServerBtn_clicked()
{
    if (qsc::IDeviceManage::getInstance().disconnectDevice(ui->serialBox->currentText().trimmed())) {
        outLog("stop server");
    }
}

void Dialog::on_wirelessConnectBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    QString addr = ui->deviceIpEdt->currentText().trimmed();
    if (addr.isEmpty()) {
        outLog("error: device ip is null", false);
        return;
    }

    if (!ui->devicePortEdt->currentText().isEmpty()) {
        addr += ":";
        addr += ui->devicePortEdt->currentText().trimmed();
    } else if (!ui->devicePortEdt->lineEdit()->placeholderText().isEmpty()) {
        addr += ":";
        addr += ui->devicePortEdt->lineEdit()->placeholderText().trimmed();
    } else {
        outLog("error: device port is null", false);
        return;
    }

    // 保存IP历史记录 - 只保存IP部分,不包含端口
    QString ip = addr.split(":").first();
    if (!ip.isEmpty()) {
        saveIpHistory(ip);
    }
    
    // 保存端口历史记录
    QString port = addr.split(":").last();
    if (!port.isEmpty() && port != ip) {
        savePortHistory(port);
    }

    outLog("wireless connect...", false);
    QStringList adbArgs;
    adbArgs << "connect";
    adbArgs << addr;
    m_adb.execute("", adbArgs);
}

void Dialog::on_startAdbdBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    outLog("start devices adbd...", false);
    // adb tcpip 5555
    QStringList adbArgs;
    adbArgs << "tcpip";
    adbArgs << "5555";
    m_adb.execute(ui->serialBox->currentText().trimmed(), adbArgs);
}

void Dialog::outLog(const QString &log, bool newLine)
{
    // avoid sub thread update ui
    QString backLog = log;
    QTimer::singleShot(0, this, [this, backLog, newLine]() {
        ui->outEdit->append(backLog);
        if (newLine) {
            ui->outEdit->append("<br/>");
        }
    });
}

bool Dialog::filterLog(const QString &log)
{
    if (log.contains("app_proces")) {
        return true;
    }
    if (log.contains("Unable to set geometry")) {
        return true;
    }
    return false;
}

bool Dialog::checkAdbRun()
{
    if (m_adb.isRuning()) {
        outLog("wait for the end of the current command to run");
    }
    return m_adb.isRuning();
}

void Dialog::on_getIPBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }

    outLog("get ip...", false);
    // adb -s P7C0218510000537 shell ifconfig wlan0
    // or
    // adb -s P7C0218510000537 shell ip -f inet addr show wlan0
    QStringList adbArgs;
#if 0
    adbArgs << "shell";
    adbArgs << "ip";
    adbArgs << "-f";
    adbArgs << "inet";
    adbArgs << "addr";
    adbArgs << "show";
    adbArgs << "wlan0";
#else
    adbArgs << "shell";
    adbArgs << "ifconfig";
    adbArgs << "wlan0";
#endif
    m_adb.execute(ui->serialBox->currentText().trimmed(), adbArgs);
}

void Dialog::getIPbyIp()
{
    if (checkAdbRun()) {
        return;
    }

    QStringList adbArgs;
    adbArgs << "shell";
    adbArgs << "ip -o a";

    m_adb.execute(ui->serialBox->currentText().trimmed(), adbArgs);
}

void Dialog::onDeviceConnected(bool success, const QString &serial, const QString &deviceName, const QSize &size)
{
    Q_UNUSED(deviceName)
    Q_UNUSED(size)
    if (!success) {
        outLog(tr("Failed to connect device: %1").arg(serial));
        return;
    }
    outLog(tr("Device connected: %1").arg(serial));
}

void Dialog::onDeviceDisconnected(QString serial)
{
    GroupController::instance().removeDevice(serial);
    outLog(tr("Device disconnected: %1").arg(serial));
}

void Dialog::onDeviceInfoUpdated(const qsc::DeviceInfo &info)
{
    // Update the cached device info
    cacheDeviceInfo(info.serial, info.manufacturer, info.device);

    // Find and update the device in connectedPhoneList
    for (int i = 0; i < ui->serialBox->count(); ++i) {
        if (ui->serialBox->itemText(i) == info.serial) {
            QString displayName = getDeviceModelFromCsv(info.device);
            if (displayName.isEmpty()) {
                // Fallback: use manufacturer + model if no CSV match
                // Since we don't have the model here, just use manufacturer
                if (!info.manufacturer.isEmpty()) {
                    displayName = info.manufacturer + " Device";
                } else {
                    displayName = "Unknown Device";
                }
            }
            ui->connectedPhoneList->item(i)->setText(displayName);
            break;
        }
    }
}

void Dialog::on_wirelessDisConnectBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    QString addr = ui->deviceIpEdt->currentText().trimmed();
    outLog("wireless disconnect...", false);
    QStringList adbArgs;
    adbArgs << "disconnect";
    adbArgs << addr;
    m_adb.execute("", adbArgs);
}

void Dialog::on_selectRecordPathBtn_clicked()
{
    QFileDialog::Options options = QFileDialog::DontResolveSymlinks | QFileDialog::ShowDirsOnly;
    QString directory = QFileDialog::getExistingDirectory(this, tr("select path"), "", options);
    ui->recordPathEdt->setText(directory);
}

void Dialog::on_recordPathEdt_textChanged(const QString &arg1)
{
    ui->recordPathEdt->setToolTip(arg1.trimmed());
    ui->notDisplayCheck->setCheckable(!arg1.trimmed().isEmpty());
}

void Dialog::on_adbCommandBtn_clicked()
{
    execAdbCmd();
}

void Dialog::on_stopAdbBtn_clicked()
{
    m_adb.kill();
}

void Dialog::on_clearOut_clicked()
{
    ui->outEdit->clear();
}

void Dialog::on_stopAllServerBtn_clicked()
{
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
}

void Dialog::on_refreshGameScriptBtn_clicked()
{
    ui->gameBox->clear();
    QDir dir(getKeyMapPath());
    if (!dir.exists()) {
        outLog("keymap directory not find", true);
        return;
    }
    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    QFileInfoList list = dir.entryInfoList();
    QFileInfo fileInfo;
    int size = list.size();
    for (int i = 0; i < size; ++i) {
        fileInfo = list.at(i);
        ui->gameBox->addItem(fileInfo.fileName());
    }
}

void Dialog::on_applyScriptBtn_clicked()
{
    auto curSerial = ui->serialBox->currentText().trimmed();
    auto device = qsc::IDeviceManage::getInstance().getDevice(curSerial);
    if (!device) {
        return;
    }

    device->updateScript(getGameScript(ui->gameBox->currentText()));
}

void Dialog::on_recordScreenCheck_clicked(bool checked)
{
    if (!checked) {
        return;
    }

    QString fileDir(ui->recordPathEdt->text().trimmed());
    if (fileDir.isEmpty()) {
        qWarning() << "please select record save path!!!";
        ui->recordScreenCheck->setChecked(false);
    }
}

void Dialog::on_usbConnectBtn_clicked()
{
    startConnectionWorkflow(false);
}

int Dialog::findDeviceFromeSerialBox(bool wifi)
{
    QString regStr = "\\b(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\:([0-9]|[1-9]\\d|[1-9]\\d{2}|[1-9]\\d{3}|["
                     "1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])\\b";
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QRegExp regIP(regStr);
#else
    QRegularExpression regIP(regStr);
#endif
    for (int i = 0; i < ui->serialBox->count(); ++i) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        bool isWifi = regIP.exactMatch(ui->serialBox->itemText(i));
#else
        bool isWifi = regIP.match(ui->serialBox->itemText(i)).hasMatch();
#endif
        bool found = wifi ? isWifi : !isWifi;
        if (found) {
            return i;
        }
    }

    return -1;
}

void Dialog::on_wifiConnectBtn_clicked()
{
    startConnectionWorkflow(true);
}

void Dialog::on_connectedPhoneList_itemDoubleClicked(QListWidgetItem *item)
{
    Q_UNUSED(item);
    ui->serialBox->setCurrentIndex(ui->connectedPhoneList->currentRow());
    on_startServerBtn_clicked();
}

void Dialog::on_updateNameBtn_clicked()
{
    if (ui->serialBox->count() != 0) {
        if (ui->userNameEdt->text().isEmpty()) {
            Config::getInstance().setNickName(ui->serialBox->currentText(), "Phone");
        } else {
            Config::getInstance().setNickName(ui->serialBox->currentText(), ui->userNameEdt->text());
        }

        on_updateDevice_clicked();

        qDebug() << "Update OK!";
    } else {
        qWarning() << "No device is connected!";
    }
}

void Dialog::on_useSingleModeCheck_clicked()
{
    if (ui->useSingleModeCheck->isChecked()) {
        ui->rightWidget->hide();
    } else {
        ui->rightWidget->show();
    }

    adjustSize();
}

void Dialog::on_serialBox_currentIndexChanged(const QString &arg1)
{
    ui->userNameEdt->setText(Config::getInstance().getNickName(arg1));
}

quint32 Dialog::getBitRate()
{
    return ui->bitRateEdit->text().trimmed().toUInt() * (ui->bitRateBox->currentText() == QString("Mbps") ? 1000000 : 1000);
}

const QString &Dialog::getServerPath()
{
    static QString serverPath;
    if (serverPath.isEmpty()) {
        serverPath = QString::fromLocal8Bit(qgetenv("QTSCRCPY_SERVER_PATH"));
        QFileInfo fileInfo(serverPath);
        if (serverPath.isEmpty() || !fileInfo.isFile()) {
            serverPath = QCoreApplication::applicationDirPath() + "/scrcpy-server";
        }
    }
    return serverPath;
}

void Dialog::on_startAudioBtn_clicked()
{
    if (ui->serialBox->count() == 0) {
        qWarning() << "No device is connected!";
        return;
    }

    m_audioOutput.start(ui->serialBox->currentText(), 28200);
}

void Dialog::on_stopAudioBtn_clicked()
{
    m_audioOutput.stop();
}

void Dialog::on_installSndcpyBtn_clicked()
{
    if (ui->serialBox->count() == 0) {
        qWarning() << "No device is connected!";
        return;
    }
    m_audioOutput.installonly(ui->serialBox->currentText(), 28200);
}

void Dialog::on_autoUpdatecheckBox_toggled(bool checked)
{
    if (checked) {
        m_autoUpdatetimer.start(5000);
    } else {
        m_autoUpdatetimer.stop();
    }
}

void Dialog::loadIpHistory()
{
    QStringList ipList = Config::getInstance().getIpHistory();
    ui->deviceIpEdt->clear();
    ui->deviceIpEdt->addItems(ipList);
    ui->deviceIpEdt->setContentsMargins(0, 0, 0, 0);

    if (ui->deviceIpEdt->lineEdit()) {
        ui->deviceIpEdt->lineEdit()->setMaxLength(128);
        ui->deviceIpEdt->lineEdit()->setPlaceholderText("192.168.0.1");
    }
}

void Dialog::saveIpHistory(const QString &ip)
{
    if (ip.isEmpty()) {
        return;
    }

    Config::getInstance().saveIpHistory(ip);

    // 更新ComboBox
    loadIpHistory();
    ui->deviceIpEdt->setCurrentText(ip);
}

void Dialog::showIpEditMenu(const QPoint &pos)
{
    QMenu *menu = ui->deviceIpEdt->lineEdit()->createStandardContextMenu();
    menu->addSeparator();

    QAction *clearHistoryAction = new QAction(tr("Clear History"), menu);
    connect(clearHistoryAction, &QAction::triggered, this, [this]() {
        Config::getInstance().clearIpHistory();
        loadIpHistory();
    });

    menu->addAction(clearHistoryAction);
    menu->exec(ui->deviceIpEdt->lineEdit()->mapToGlobal(pos));
    delete menu;
}

QString Dialog::getDeviceDisplayName(const QString &serial)
{
    // First, check if we have device info from adb devices -l (already available)
    QList<qsc::DeviceInfo> devices = m_adb.getDevicesInfo();
    for (const auto &device : devices) {
        if (device.serial == serial) {
            // Try to get model name from CSV first
            QString csvModelName = getDeviceModelFromCsv(device.device);
            if (!csvModelName.isEmpty()) {
                return csvModelName;
            }

            // If we have manufacturer and model, use them
            if (!device.manufacturer.isEmpty() && !device.model.isEmpty()) {
                return device.manufacturer + " " + device.model;
            }
        }
    }

    // Second, check cache for previously fetched detailed properties
    if (m_deviceInfoCache.contains(serial)) {
        const auto &cachedInfo = m_deviceInfoCache[serial];
        QString csvModelName = getDeviceModelFromCsv(cachedInfo.second); // device ID
        if (!csvModelName.isEmpty()) {
            return csvModelName;
        }
        if (!cachedInfo.first.isEmpty()) { // manufacturer
            return cachedInfo.first + " Device";
        }
    }

    // Third, trigger async fetch for detailed properties (non-blocking)
    // This will update the UI later via onDeviceInfoUpdated signal
    m_adb.fetchDevicePropertiesAsync(serial);

    // Return placeholder while fetching
    return "Loading...";
}

void Dialog::loadDevicesCsv()
{
    m_devicesCsv.clear();

    QString csvPath = QString::fromLocal8Bit(qgetenv("DEVICE_CSV_FILE"));
    if (csvPath.isEmpty()) {
        csvPath = QCoreApplication::applicationDirPath() + "/devices.csv";
    }
    QFile file(csvPath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open devices.csv:" << csvPath;
        return;
    }

    QTextStream in(&file);
    QString line = in.readLine(); // Skip header line

    while (!in.atEnd()) {
        line = in.readLine();
        if (line.trimmed().isEmpty())
            continue;

        QStringList parts = line.split(",");
        if (parts.count() >= 4) {
            CsvDeviceInfo info;
            info.brand = parts[0].trimmed();
            info.device = parts[1].trimmed();
            info.manufacturer = parts[2].trimmed();
            info.modelName = parts[3].trimmed();

            m_devicesCsv.append(info);
        }
    }

    file.close();
    qDebug() << "Loaded" << m_devicesCsv.size() << "devices from CSV";
}

QString Dialog::getDeviceModelFromCsv(const QString &deviceId)
{
    for (const auto &csvDevice : m_devicesCsv) {
        if (csvDevice.device == deviceId) {
            return csvDevice.manufacturer + " " + csvDevice.modelName;
        }
    }

    return QString(); // Not found
}

void Dialog::cacheDeviceInfo(const QString &serial, const QString &manufacturer, const QString &device)
{
    m_deviceInfoCache[serial] = QPair<QString, QString>(manufacturer, device);
}

void Dialog::loadPortHistory()
{
    QStringList portList = Config::getInstance().getPortHistory();
    ui->devicePortEdt->clear();
    ui->devicePortEdt->addItems(portList);
    ui->devicePortEdt->setContentsMargins(0, 0, 0, 0);

    if (ui->devicePortEdt->lineEdit()) {
        ui->devicePortEdt->lineEdit()->setMaxLength(6);
        ui->devicePortEdt->lineEdit()->setPlaceholderText("5555");
    }
}

void Dialog::savePortHistory(const QString &port)
{
    if (port.isEmpty()) {
        return;
    }
    
    Config::getInstance().savePortHistory(port);
    
    // 更新ComboBox
    loadPortHistory();
    ui->devicePortEdt->setCurrentText(port);
}

void Dialog::showPortEditMenu(const QPoint &pos)
{
    QMenu *menu = ui->devicePortEdt->lineEdit()->createStandardContextMenu();
    menu->addSeparator();

    QAction *clearHistoryAction = new QAction(tr("Clear History"), menu);
    connect(clearHistoryAction, &QAction::triggered, this, [this]() {
        Config::getInstance().clearPortHistory();
        loadPortHistory();
    });

    menu->addAction(clearHistoryAction);
    menu->exec(ui->devicePortEdt->lineEdit()->mapToGlobal(pos));
    delete menu;
}

void Dialog::startConnectionWorkflow(bool isWifi)
{
    if (m_connectionState != CS_IDLE) {
        qWarning("Connection workflow already in progress");
        return;
    }

    m_connectionIsWifi = isWifi;
    m_connectionState = CS_STOPPING_ALL;
    on_stopAllServerBtn_clicked();
    m_connectionTimer.start(200);
}

void Dialog::advanceConnectionState()
{
    m_connectionTimer.stop();

    switch (m_connectionState) {
    case CS_STOPPING_ALL:
        m_connectionState = CS_UPDATING_DEVICES_INITIAL;
        on_updateDevice_clicked();
        m_connectionTimer.start(200);
        break;

    case CS_UPDATING_DEVICES_INITIAL:
        {
            int firstUsbDevice = findDeviceFromeSerialBox(false);
            if (-1 == firstUsbDevice) {
                qWarning() << "No USB device found!";
                m_connectionState = CS_IDLE;
                return;
            }
            ui->serialBox->setCurrentIndex(firstUsbDevice);

            if (!m_connectionIsWifi) {
                // USB connection - go directly to start server
                m_connectionState = CS_IDLE;
                on_startServerBtn_clicked();
            } else {
                // WiFi connection - continue workflow
                m_connectionState = CS_GETTING_IP;
                on_getIPBtn_clicked();
                m_connectionTimer.start(200);
            }
        }
        break;

    case CS_GETTING_IP:
        m_connectionState = CS_STARTING_ADBD;
        on_startAdbdBtn_clicked();
        m_connectionTimer.start(1000);
        break;

    case CS_STARTING_ADBD:
        m_connectionState = CS_WIRELESS_CONNECT;
        on_wirelessConnectBtn_clicked();
        m_connectionTimer.start(2000);
        break;

    case CS_WIRELESS_CONNECT:
        m_connectionState = CS_UPDATING_DEVICES_FINAL;
        on_updateDevice_clicked();
        m_connectionTimer.start(200);
        break;

    case CS_UPDATING_DEVICES_FINAL:
        {
            int firstWifiDevice = findDeviceFromeSerialBox(true);
            if (-1 == firstWifiDevice) {
                qWarning() << "No WiFi device found!";
                m_connectionState = CS_IDLE;
                return;
            }
            ui->serialBox->setCurrentIndex(firstWifiDevice);

            m_connectionState = CS_IDLE;
            on_startServerBtn_clicked();
        }
        break;

    default:
        m_connectionState = CS_IDLE;
        break;
    }
}
