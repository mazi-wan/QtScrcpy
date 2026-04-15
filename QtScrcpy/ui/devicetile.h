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
    explicit DeviceTile(const QString &serial, const QString &displayName,
                        bool frameless, bool skin, bool showToolbar, QWidget *parent = nullptr);
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
