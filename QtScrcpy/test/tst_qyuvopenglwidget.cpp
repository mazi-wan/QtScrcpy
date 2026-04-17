#include <QApplication>
#include <QTest>

#include "qyuvopenglwidget.h"

// Subclass that exposes protected state and lets us call initializeGL() directly.
class TestableQYUVOpenGLWidget : public QYUVOpenGLWidget
{
public:
    explicit TestableQYUVOpenGLWidget(QWidget *parent = nullptr) : QYUVOpenGLWidget(parent) {}

    bool isTextureInited() const { return m_textureInited; }
    bool isNeedUpdate() const { return m_needUpdate; }

    void setTextureInited(bool v) { m_textureInited = v; }
    void setNeedUpdate(bool v) { m_needUpdate = v; }
    void setTestFrameSize(const QSize &s) { m_frameSize = s; }

    // Simulate Qt calling initializeGL() on a fresh context (as happens after setParent()).
    void callInitializeGL()
    {
        makeCurrent();
        QYUVOpenGLWidget::initializeGL();
        doneCurrent();
    }
};

class TstQYUVOpenGLWidget : public QObject
{
    Q_OBJECT

private slots:
    // Test 1: initializeGL() must reset m_textureInited to false.
    // Without the fix, initializeGL() leaves m_textureInited at whatever stale value it had.
    void initializeGL_resetsTextureInited();

    // Test 2: initializeGL() must set m_needUpdate = true when a frame size is already known.
    // Without the fix, initializeGL() does not set m_needUpdate.
    void initializeGL_setsNeedUpdateWhenFrameSizeValid();

    // Test 3: initializeGL() must NOT set m_needUpdate when no frame size is known yet.
    // Without a frame size, there is nothing to re-initialize.
    void initializeGL_doesNotSetNeedUpdateWhenFrameSizeInvalid();

    // Test 4: updateTextures() must be a no-op (no crash, no GL calls) when m_textureInited = false.
    // This guards the race window between context destruction and the first new paintGL().
    void updateTextures_isNoOpWhenNotInited();
};

void TstQYUVOpenGLWidget::initializeGL_resetsTextureInited()
{
    TestableQYUVOpenGLWidget w;
    w.resize(320, 240);
    w.show();
    QApplication::processEvents(); // triggers Qt-managed initializeGL() + paintGL()

    // Simulate the stale state left after GL context destruction (setParent() scenario)
    w.setTextureInited(true);
    w.setNeedUpdate(false);

    // Simulate context recreation: Qt calls initializeGL() again on the fresh context
    w.callInitializeGL();

    // After initializeGL(), m_textureInited must be false so paintGL() recreates textures
    QCOMPARE(w.isTextureInited(), false);
}

void TstQYUVOpenGLWidget::initializeGL_setsNeedUpdateWhenFrameSizeValid()
{
    TestableQYUVOpenGLWidget w;
    w.resize(320, 240);
    w.show();
    QApplication::processEvents();

    w.setTestFrameSize(QSize(1920, 1080)); // valid frame size — device was streaming
    w.setNeedUpdate(false);

    w.callInitializeGL();

    // paintGL() must see m_needUpdate = true to call initTextures() in the new context
    QCOMPARE(w.isNeedUpdate(), true);
}

void TstQYUVOpenGLWidget::initializeGL_doesNotSetNeedUpdateWhenFrameSizeInvalid()
{
    TestableQYUVOpenGLWidget w;
    w.resize(320, 240);
    w.show();
    QApplication::processEvents();

    w.setTestFrameSize(QSize()); // QSize() is (-1, -1) — invalid, no frame seen yet
    w.setNeedUpdate(false);

    w.callInitializeGL();

    // No frame size known yet — setFrameSize() will set m_needUpdate when first frame arrives
    QCOMPARE(w.isNeedUpdate(), false);
}

void TstQYUVOpenGLWidget::updateTextures_isNoOpWhenNotInited()
{
    TestableQYUVOpenGLWidget w;
    w.resize(320, 240);
    w.show();
    QApplication::processEvents();

    w.setTextureInited(false);

    // updateTextures() guards on m_textureInited — must return early, no crash
    quint8 dummyY[4] = { 128, 128, 128, 128 };
    quint8 dummyU[1] = { 128 };
    quint8 dummyV[1] = { 128 };
    w.updateTextures(dummyY, dummyU, dummyV, 2, 1, 1); // must not crash

    // State must be unchanged
    QCOMPARE(w.isTextureInited(), false);
}

QTEST_MAIN(TstQYUVOpenGLWidget)
#include "tst_qyuvopenglwidget.moc"
