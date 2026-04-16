#include "devicetile.h"
#include "videoform.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

DeviceTile::DeviceTile(const QString &serial, const QString &displayName, bool frameless, bool skin, bool showToolbar, QWidget *parent)
    : QWidget(parent)
    , m_serial(serial)
{
    // Header bar
    auto *headerWidget = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(4);

    m_titleLabel = new QLabel(displayName.isEmpty() ? serial : displayName, headerWidget);
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
    m_videoForm->setFocusPolicy(Qt::StrongFocus);
    setFocusProxy(m_videoForm);
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

DeviceTile::~DeviceTile()
{
    if (m_detached && m_videoForm) {
        m_videoForm->removeEventFilter(this);
    }
}

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
    m_videoForm->reinitVideoWidget();
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
    m_videoForm->setFocusPolicy(Qt::StrongFocus);
    m_videoForm->show();
    m_videoForm->reinitVideoWidget();
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
