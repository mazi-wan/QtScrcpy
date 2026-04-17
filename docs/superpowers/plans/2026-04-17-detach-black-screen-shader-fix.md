# Detach Black Screen — Shader Re-init Fix

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the permanent black screen on detach by making `initializeGL()` correctly reset the shader program and VBO when the GL context is recreated after reparenting.

**Architecture:** Three bugs in `QYUVOpenGLWidget` caused the black screen: (1) `initShader()` accumulates duplicate shaders on repeated calls → link fails → unlinked shader program → no rendering; (2) the `static QString s_fragShader` is mutated on every `initShader()` call on OpenGL ES, corrupting the source; (3) the VBO handle is stale after context destruction. Fixes are `removeAllShaders()` at the start of `initShader()`, switching to a local copy of fragShader, and `m_vbo.destroy()` before `m_vbo.create()` in `initializeGL()`. A new TDD test verifies the shader is linked after a second `initializeGL()` call — this is the test that would have caught the real bug.

**Tech Stack:** Qt 5/6, Qt Test, QOpenGLWidget, CMake 3.19+

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `QtScrcpy/render/qyuvopenglwidget.h` | Modify | Move `m_shaderProgram` from `private` to `protected` so the test subclass can call `isLinked()` |
| `QtScrcpy/render/qyuvopenglwidget.cpp` | Modify | Fix `initShader()` (removeAllShaders + local fragShader) and `initializeGL()` (VBO destroy) |
| `QtScrcpy/test/tst_qyuvopenglwidget.cpp` | Modify | Add `isShaderLinked()` getter to `TestableQYUVOpenGLWidget` and new test `initializeGL_shaderIsLinkedAfterReinit()` |

---

## Task 1: Expose `m_shaderProgram` for testing

**Files:**
- Modify: `QtScrcpy/render/qyuvopenglwidget.h`

Move `m_shaderProgram` from `private` to `protected` so `TestableQYUVOpenGLWidget` can call `m_shaderProgram.isLinked()`. Pure accessibility change — no behavior impact.

- [ ] **Step 1: Update `QtScrcpy/render/qyuvopenglwidget.h`**

Replace the entire file with:

```cpp
#ifndef QYUVOPENGLWIDGET_H
#define QYUVOPENGLWIDGET_H
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

class QYUVOpenGLWidget
    : public QOpenGLWidget
    , protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit QYUVOpenGLWidget(QWidget *parent = nullptr);
    virtual ~QYUVOpenGLWidget() override;

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

    void setFrameSize(const QSize &frameSize);
    const QSize &frameSize();
    void updateTextures(quint8 *dataY, quint8 *dataU, quint8 *dataV, quint32 linesizeY, quint32 linesizeU, quint32 linesizeV);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;

    QSize m_frameSize = { -1, -1 };
    bool m_needUpdate = false;
    bool m_textureInited = false;
    QOpenGLShaderProgram m_shaderProgram;

private:
    void initShader();
    void initTextures();
    void deInitTextures();
    void updateTexture(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride);

    QOpenGLBuffer m_vbo;
    GLuint m_texture[3] = { 0 };
};

#endif // QYUVOPENGLWIDGET_H
```

- [ ] **Step 2: Build to verify no regressions**

```bash
cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
cmake --build build --config Debug -j8 2>&1 | tail -5
```

Expected: zero errors. (The `m_shaderProgram` comment `// 着色器程序：编译链接着色器` is removed — that's intentional, it was in the private section and moving the field is the change.)

- [ ] **Step 3: Commit**

```bash
git add QtScrcpy/render/qyuvopenglwidget.h
git commit -m "refactor: expose m_shaderProgram as protected for shader link testability

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 2: Write the failing test

**Files:**
- Modify: `QtScrcpy/test/tst_qyuvopenglwidget.cpp`

Add `isShaderLinked()` to `TestableQYUVOpenGLWidget` and add `initializeGL_shaderIsLinkedAfterReinit()` to `TstQYUVOpenGLWidget`. This test must **FAIL** before the fix and **PASS** after.

- [ ] **Step 1: Update `QtScrcpy/test/tst_qyuvopenglwidget.cpp`**

Replace the entire file with:

```cpp
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
```

- [ ] **Step 2: Build**

```bash
cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
cmake --build build --target tst_qyuvopenglwidget --config Debug -j8 2>&1 | tail -10
```

Expected: compiles and links successfully.

- [ ] **Step 3: Run tests — verify the new test FAILS (red)**

```bash
LIBGL_ALWAYS_SOFTWARE=1 QT_QPA_PLATFORM=offscreen ./build/QtScrcpy/test/tst_qyuvopenglwidget -v2
```

Expected: `initializeGL_shaderIsLinkedAfterReinit` FAILS. The existing 4 tests still pass.

```
PASS   : TstQYUVOpenGLWidget::initializeGL_resetsTextureInited()
PASS   : TstQYUVOpenGLWidget::initializeGL_setsNeedUpdateWhenFrameSizeValid()
PASS   : TstQYUVOpenGLWidget::initializeGL_doesNotSetNeedUpdateWhenFrameSizeInvalid()
PASS   : TstQYUVOpenGLWidget::updateTextures_isNoOpWhenNotInited()
FAIL!  : TstQYUVOpenGLWidget::initializeGL_shaderIsLinkedAfterReinit() 'w.isShaderLinked()' returned FALSE
Totals: 4 passed, 1 failed
```

- [ ] **Step 4: Commit**

```bash
git add QtScrcpy/test/tst_qyuvopenglwidget.cpp
git commit -m "test: add failing test for shader link after initializeGL() re-invocation

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 3: Implement the fix

**Files:**
- Modify: `QtScrcpy/render/qyuvopenglwidget.cpp`

Apply three fixes: `removeAllShaders()` in `initShader()`, local fragShader copy, and VBO destroy in `initializeGL()`.

- [ ] **Step 1: Replace `initShader()` in `QtScrcpy/render/qyuvopenglwidget.cpp`**

Find `initShader()` starting at line 197. Replace the entire function:

```cpp
// BEFORE (lines 197-226):
void QYUVOpenGLWidget::initShader()
{
    // opengles的float、int等要手动指定精度
    if (QCoreApplication::testAttribute(Qt::AA_UseOpenGLES)) {
        s_fragShader.prepend(R"(
                             precision mediump int;
                             precision mediump float;
                             )");
    }
    m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader);
    m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShader);
    m_shaderProgram.link();
    m_shaderProgram.bind();

    m_shaderProgram.setAttributeBuffer("vertexIn", GL_FLOAT, 0, 3, 3 * sizeof(float));
    m_shaderProgram.enableAttributeArray("vertexIn");
    m_shaderProgram.setAttributeBuffer("textureIn", GL_FLOAT, 12 * sizeof(float), 2, 2 * sizeof(float));
    m_shaderProgram.enableAttributeArray("textureIn");
    m_shaderProgram.setUniformValue("textureY", 0);
    m_shaderProgram.setUniformValue("textureU", 1);
    m_shaderProgram.setUniformValue("textureV", 2);
}
```

```cpp
// AFTER:
void QYUVOpenGLWidget::initShader()
{
    // Clear any shaders from a previous context — prevents duplicate-shader link failure on reinit.
    m_shaderProgram.removeAllShaders();

    // Use a local copy so the static source string is never mutated between calls.
    QString fragShader = s_fragShader;
    if (QCoreApplication::testAttribute(Qt::AA_UseOpenGLES)) {
        fragShader.prepend(R"(
                             precision mediump int;
                             precision mediump float;
                             )");
    }
    m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader);
    m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, fragShader);
    m_shaderProgram.link();
    m_shaderProgram.bind();

    m_shaderProgram.setAttributeBuffer("vertexIn", GL_FLOAT, 0, 3, 3 * sizeof(float));
    m_shaderProgram.enableAttributeArray("vertexIn");
    m_shaderProgram.setAttributeBuffer("textureIn", GL_FLOAT, 12 * sizeof(float), 2, 2 * sizeof(float));
    m_shaderProgram.enableAttributeArray("textureIn");
    m_shaderProgram.setUniformValue("textureY", 0);
    m_shaderProgram.setUniformValue("textureU", 1);
    m_shaderProgram.setUniformValue("textureV", 2);
}
```

- [ ] **Step 2: Update `initializeGL()` to destroy the VBO before recreating it**

Find `initializeGL()` starting at line 142. Replace the VBO setup block:

```cpp
// BEFORE (lines 147-150):
    // 顶点缓冲对象初始化
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(coordinate, sizeof(coordinate));
```

```cpp
// AFTER:
    // Destroy stale VBO handle before recreating — the old handle is invalid after context recreation.
    if (m_vbo.isCreated()) {
        m_vbo.destroy();
    }
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(coordinate, sizeof(coordinate));
```

The full `initializeGL()` after both edits:

```cpp
void QYUVOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);

    // Destroy stale VBO handle before recreating — the old handle is invalid after context recreation.
    if (m_vbo.isCreated()) {
        m_vbo.destroy();
    }
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(coordinate, sizeof(coordinate));
    initShader();
    // 设置背景清理色为黑色
    glClearColor(0.0, 0.0, 0.0, 0.0);
    // 清理颜色背景
    glClear(GL_COLOR_BUFFER_BIT);

    // Reset texture state — context may have been recreated after reparenting (detach/attach).
    // paintGL() will call initTextures() on the next pass via m_needUpdate.
    m_textureInited = false;
    if (m_frameSize.isValid()) {
        m_needUpdate = true;
    }
}
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target tst_qyuvopenglwidget --config Debug -j8 2>&1 | tail -5
```

Expected: compiles successfully.

- [ ] **Step 4: Run tests — verify ALL 5 PASS (green)**

```bash
LIBGL_ALWAYS_SOFTWARE=1 QT_QPA_PLATFORM=offscreen ./build/QtScrcpy/test/tst_qyuvopenglwidget -v2
```

Expected:
```
PASS   : TstQYUVOpenGLWidget::initializeGL_resetsTextureInited()
PASS   : TstQYUVOpenGLWidget::initializeGL_setsNeedUpdateWhenFrameSizeValid()
PASS   : TstQYUVOpenGLWidget::initializeGL_doesNotSetNeedUpdateWhenFrameSizeInvalid()
PASS   : TstQYUVOpenGLWidget::updateTextures_isNoOpWhenNotInited()
PASS   : TstQYUVOpenGLWidget::initializeGL_shaderIsLinkedAfterReinit()
Totals: 5 passed, 0 failed, 0 skipped
```

No shader "multiply defined" warnings should appear for `initializeGL_shaderIsLinkedAfterReinit`.

- [ ] **Step 5: Build the full app**

```bash
cmake --build build --config Debug -j8 2>&1 | tail -5
```

Expected: zero errors.

- [ ] **Step 6: Commit**

```bash
git add QtScrcpy/render/qyuvopenglwidget.cpp
git commit -m "fix: clear shader program and VBO before reinit to fix detach black screen

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Done

At this point:
- 5 tests pass, 0 failed
- No "multiply defined" shader warnings on context recreation
- Detach pop-out renders video correctly (no black screen)
- No changes to `devicetile.cpp` or `videoform.cpp`
