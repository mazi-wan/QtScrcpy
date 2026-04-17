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
    bool isShaderLinked() const { return m_shaderProgram.isLinked(); }

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
    void initializeGL_resetsTextureInited();
    void initializeGL_setsNeedUpdateWhenFrameSizeValid();
    void initializeGL_doesNotSetNeedUpdateWhenFrameSizeInvalid();
    void updateTextures_isNoOpWhenNotInited();
    // New test — catches the shader link failure on repeated initializeGL() calls
    void initializeGL_shaderIsLinkedAfterReinit();
};

void TstQYUVOpenGLWidget::initializeGL_resetsTextureInited()
{
    TestableQYUVOpenGLWidget w;
    w.resize(320, 240);
    w.show();
    QApplication::processEvents();

    w.setTextureInited(true);
    w.setNeedUpdate(false);
    w.callInitializeGL();

    QCOMPARE(w.isTextureInited(), false);
}

void TstQYUVOpenGLWidget::initializeGL_setsNeedUpdateWhenFrameSizeValid()
{
    TestableQYUVOpenGLWidget w;
    w.resize(320, 240);
    w.show();
    QApplication::processEvents();

    w.setTestFrameSize(QSize(1920, 1080));
    w.setNeedUpdate(false);
    w.callInitializeGL();

    QCOMPARE(w.isNeedUpdate(), true);
}

void TstQYUVOpenGLWidget::initializeGL_doesNotSetNeedUpdateWhenFrameSizeInvalid()
{
    TestableQYUVOpenGLWidget w;
    w.resize(320, 240);
    w.show();
    QApplication::processEvents();

    w.setTestFrameSize(QSize());
    w.setNeedUpdate(false);
    w.callInitializeGL();

    QCOMPARE(w.isNeedUpdate(), false);
}

void TstQYUVOpenGLWidget::updateTextures_isNoOpWhenNotInited()
{
    TestableQYUVOpenGLWidget w;
    w.resize(320, 240);
    w.show();
    QApplication::processEvents();

    w.setTextureInited(false);

    quint8 dummyY[4] = { 128, 128, 128, 128 };
    quint8 dummyU[1] = { 128 };
    quint8 dummyV[1] = { 128 };
    w.updateTextures(dummyY, dummyU, dummyV, 2, 1, 1);

    QCOMPARE(w.isTextureInited(), false);
}

void TstQYUVOpenGLWidget::initializeGL_shaderIsLinkedAfterReinit()
{
    TestableQYUVOpenGLWidget w;
    w.resize(320, 240);
    w.show();
    QApplication::processEvents(); // first initializeGL() + paintGL()

    // Simulate context recreation after setParent() — Qt calls initializeGL() again
    w.callInitializeGL();

    // Shader program must be linked — an unlinked program produces a permanent black screen
    QVERIFY(w.isShaderLinked());
}

QTEST_MAIN(TstQYUVOpenGLWidget)
#include "tst_qyuvopenglwidget.moc"
