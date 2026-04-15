#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include <QThread>
#include <QProcess>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QAtomicInt>

class QAudioSink;
class QAudioOutput;
class QIODevice;
class AudioOutput : public QObject
{
    Q_OBJECT
public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput();

    bool start(const QString& serial, int port);
    void stop();
    void installonly(const QString& serial, int port);

private slots:
    void onSndcpyFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onSndcpyTimeout();
    void onAudioDataReady(const QByteArray &data);

private:
    bool runSndcpyProcess(const QString& serial, int port, bool wait = true);
    void startAudioOutput();
    void stopAudioOutput();
    void startRecvData(int port);
    void stopRecvData();

signals:
    void connectTo(int port);
    void audioDataReady(const QByteArray &data);

private:
    QPointer<QIODevice> m_outputDevice;
    QThread m_workerThread;
    QProcess m_sndcpy;
    QTimer m_sndcpyTimeoutTimer;
    QVector<char> m_buffer;
    bool m_running = false;
    bool m_waitingForSndcpy = false;
    int m_pendingPort = 0;
    QAtomicInt m_stopping;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QAudioOutput* m_audioOutput = nullptr;
#else
    QAudioSink *m_audioSink = nullptr;
#endif
};

#endif // AUDIOOUTPUT_H
