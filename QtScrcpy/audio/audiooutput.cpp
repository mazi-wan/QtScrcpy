#include <QAudioOutput>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTime>

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#endif

#include "audiooutput.h"

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
{
    m_running = false;
    m_stopping.storeRelaxed(0);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    m_audioOutput = nullptr;
#else
    m_audioSink = nullptr;
#endif
    connect(&m_sndcpy, &QProcess::readyReadStandardOutput, this, [this]() {
        qInfo() << QString("AudioOutput::") << QString(m_sndcpy.readAllStandardOutput());
    });
    connect(&m_sndcpy, &QProcess::readyReadStandardError, this, [this]() {
        qInfo() << QString("AudioOutput::") << QString(m_sndcpy.readAllStandardError());
    });
    connect(&m_sndcpy, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AudioOutput::onSndcpyFinished);
    connect(&m_sndcpyTimeoutTimer, &QTimer::timeout, this, &AudioOutput::onSndcpyTimeout);
    m_sndcpyTimeoutTimer.setSingleShot(true);

    // Connect signal to slot with QueuedConnection for thread-safe audio data handling
    connect(this, &AudioOutput::audioDataReady, this, &AudioOutput::onAudioDataReady, Qt::QueuedConnection);
}

AudioOutput::~AudioOutput()
{
    if (QProcess::NotRunning != m_sndcpy.state()) {
        m_sndcpy.kill();
    }
    stop();
}

bool AudioOutput::start(const QString& serial, int port)
{
    if (m_running) {
        stop();
    }

    QElapsedTimer timeConsumeCount;
    timeConsumeCount.start();
    bool ret = runSndcpyProcess(serial, port);
    qInfo() << "AudioOutput::run sndcpy cost:" << timeConsumeCount.elapsed() << "milliseconds";
    if (!ret) {
        return ret;
    }

    // If we're waiting for sndcpy, audio will start in onSndcpyFinished
    // Otherwise start immediately
    if (!m_waitingForSndcpy) {
        startAudioOutput();
        startRecvData(port);
        m_running = true;
    }

    return true;
}

void AudioOutput::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;

    stopRecvData();
    stopAudioOutput();
}

void AudioOutput::installonly(const QString &serial, int port)
{
    runSndcpyProcess(serial, port, false);
}

bool AudioOutput::runSndcpyProcess(const QString &serial, int port, bool wait)
{
    if (QProcess::NotRunning != m_sndcpy.state()) {
        m_sndcpy.kill();
    }

#ifdef Q_OS_WIN32
    QStringList params{serial, QString::number(port)};
    m_sndcpy.start("sndcpy.bat", params);
#else
    QStringList params{"sndcpy.sh", serial, QString::number(port)};
    m_sndcpy.setWorkingDirectory(QCoreApplication::applicationDirPath());
    m_sndcpy.start("bash", params);
#endif

    if (!wait) {
        return true;
    }

    if (!m_sndcpy.waitForStarted()) {
        qWarning() << "AudioOutput::start sndcpy process failed";
        return false;
    }

    // Instead of blocking with waitForFinished(), start async timeout
    m_waitingForSndcpy = true;
    m_pendingPort = port;
    m_sndcpyTimeoutTimer.start(10000); // 10 second timeout
    return true;
}

void AudioOutput::startAudioOutput()
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    if (m_audioOutput) {
        return;
    }

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);
    QAudioDeviceInfo info(QAudioDeviceInfo::defaultOutputDevice());

    if (!info.isFormatSupported(format)) {
        qWarning() << "AudioOutput::audio format not supported, cannot play audio.";
        return;
    }

    m_audioOutput = new QAudioOutput(format, this);
    connect(m_audioOutput, &QAudioOutput::stateChanged, this, [](QAudio::State state) {
        qInfo() << "AudioOutput::audio state changed:" << state;
    });
    m_audioOutput->setBufferSize(48000*2*15/1000 * 20);
    m_outputDevice = m_audioOutput->start();
#else
    if (m_audioSink) {
        return;
    }

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    if (!defaultDevice.isFormatSupported(format)) {
        qWarning() << "AudioOutput::audio format not supported, cannot play audio.";
        return;
    }
    m_audioSink = new QAudioSink(defaultDevice, format, this);
    m_outputDevice = m_audioSink->start();
    if (!m_outputDevice) {
        qWarning() << "AudioOutput::audio output device not available, cannot play audio.";
        delete m_audioSink;
        m_audioSink = nullptr;
        return;
    }
#endif
}

void AudioOutput::stopAudioOutput()
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    if (m_audioOutput) {
        m_audioOutput->stop();
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }
#else
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }
#endif
    m_outputDevice = nullptr;
}

void AudioOutput::startRecvData(int port)
{
    if (m_workerThread.isRunning()) {
        stopRecvData();
    }

    m_stopping.storeRelaxed(0);

    auto audioSocket = new QTcpSocket();
    audioSocket->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, audioSocket, &QObject::deleteLater);

    connect(this, &AudioOutput::connectTo, audioSocket, [audioSocket](int port) {
        audioSocket->connectToHost(QHostAddress::LocalHost, port);
        if (!audioSocket->waitForConnected(500)) {
            qWarning("AudioOutput::audio socket connect failed");
            return;
        }
        qInfo("AudioOutput::audio socket connect success");
    });
    connect(audioSocket, &QIODevice::readyRead, audioSocket, [this, audioSocket]() {
        if (m_stopping.loadRelaxed()) {
            return;
        }

        qint64 recv = audioSocket->bytesAvailable();
        //qDebug() << "AudioOutput::recv data:" << recv;

        // Read data and emit signal for thread-safe handling
        QByteArray data = audioSocket->read(recv);
        emit audioDataReady(data);
    }, Qt::DirectConnection);
    connect(audioSocket, &QTcpSocket::stateChanged, audioSocket, [](QAbstractSocket::SocketState state) {
        qInfo() << "AudioOutput::audio socket state changed:" << state;

    });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(audioSocket, &QTcpSocket::errorOccurred, audioSocket, [](QAbstractSocket::SocketError error) {
        qInfo() << "AudioOutput::audio socket error occurred:" << error;
    });
#else
    connect(audioSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), audioSocket, [](QAbstractSocket::SocketError error) {
        qInfo() << "AudioOutput::audio socket error occurred:" << error;
    });
#endif

    m_workerThread.start();
    emit connectTo(port);
}

void AudioOutput::stopRecvData()
{
    if (!m_workerThread.isRunning()) {
        return;
    }

    m_stopping.storeRelaxed(1);  // Prevent new data processing

    m_workerThread.quit();
    if (!m_workerThread.wait(3000)) {
        qWarning("Audio worker thread timeout, terminating");
        m_workerThread.terminate();
        m_workerThread.wait(1000);
    }
}

void AudioOutput::onSndcpyFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_waitingForSndcpy) {
        return;
    }

    m_sndcpyTimeoutTimer.stop();
    m_waitingForSndcpy = false;

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        qWarning() << "AudioOutput::sndcpy process failed with exit code" << exitCode;
        return;
    }

    // sndcpy completed successfully, start audio
    startAudioOutput();
    startRecvData(m_pendingPort);
    m_running = true;
}

void AudioOutput::onSndcpyTimeout()
{
    if (!m_waitingForSndcpy) {
        return;
    }

    m_waitingForSndcpy = false;
    qWarning() << "AudioOutput::sndcpy process timeout after 10 seconds";
    if (QProcess::NotRunning != m_sndcpy.state()) {
        m_sndcpy.kill();
    }
}

void AudioOutput::onAudioDataReady(const QByteArray &data)
{
    if (m_outputDevice && !data.isEmpty()) {
        m_outputDevice->write(data);
    }
}
