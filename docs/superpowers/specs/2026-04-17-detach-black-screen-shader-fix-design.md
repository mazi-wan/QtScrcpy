# Design: Fix Shader Re-initialization on GL Context Recreation (Detach Black Screen — Real Fix)

**Date:** 2026-04-17  
**Branch:** local/dev  
**Status:** Approved

## Background

A previous fix (`5817cce`) addressed `m_textureInited` and `m_needUpdate` not being reset in `initializeGL()`. That fix was correct but incomplete — it fixed the texture state, but not the shader program state. The detach still produces a permanent black screen.

## Root Cause

When `VideoForm::setParent(nullptr)` is called during detach, Qt destroys and recreates the `QYUVOpenGLWidget`'s GL context. On context recreation, Qt calls `initializeGL()` again. This triggers `initShader()`, which has two bugs:

### Bug 1 — Duplicate shaders crash the link step

`initShader()` calls `m_shaderProgram.addShaderFromSourceCode()` without first clearing the program. On the second call, the program already holds shaders from the first call. Adding new shaders produces duplicates:

```
QOpenGLShader::link: error: function '...' is multiply defined
QOpenGLShaderProgram::attributeLocation(vertexIn): shader program is not linked
```

With an unlinked program, `paintGL()` calls `m_shaderProgram.bind()` on a broken program → draws nothing → **permanent black screen**.

### Bug 2 — Static fragShader string mutated on every call

```cpp
static QString s_fragShader = R"(...)";

void QYUVOpenGLWidget::initShader()
{
    if (QCoreApplication::testAttribute(Qt::AA_UseOpenGLES)) {
        s_fragShader.prepend("...");  // ← mutates global static every call
    }
    ...
}
```

On the second `initShader()` call on OpenGL ES, the precision qualifiers are prepended again — corrupting the shader source string permanently.

### Bug 3 — VBO handle invalid after context recreation

`m_vbo.create()` is a no-op if the VBO is already "created" (i.e., has a non-zero handle from the old, destroyed context). `m_vbo.bind()` then tries to bind a handle that belongs to the destroyed context — undefined behavior, typically a silent no-op.

## Evidence

The test suite from the previous fix already showed these warnings (incorrectly dismissed as "environmental noise"):

```
QWARN: QOpenGLShader::link: error: function '...' is multiply defined
QWARN: QOpenGLShaderProgram::attributeLocation(vertexIn): shader program is not linked
```

These are not Mesa artifacts — they are the real link failure that causes the black screen.

## Fix (Approach A)

### Fix 1: `initShader()` — clear program before re-adding shaders

```cpp
void QYUVOpenGLWidget::initShader()
{
    m_shaderProgram.removeAllShaders();  // ← prevents duplicate shaders on re-init

    QString fragShader = s_fragShader;   // ← local copy, never mutates the static
    if (QCoreApplication::testAttribute(Qt::AA_UseOpenGLES)) {
        fragShader.prepend("precision mediump int;\nprecision mediump float;\n");
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

### Fix 2: `initializeGL()` — destroy VBO before recreating

```cpp
void QYUVOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);

    if (m_vbo.isCreated()) {
        m_vbo.destroy();              // ← release stale handle before new context
    }
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(coordinate, sizeof(coordinate));
    initShader();
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    // Reset texture state (from previous fix — still needed)
    m_textureInited = false;
    if (m_frameSize.isValid()) {
        m_needUpdate = true;
    }
}
```

## New Test

Add one test that exposes the real bug — the shader not being linked after a second `initializeGL()`. This is the test that should have been written in the previous cycle.

**In `qyuvopenglwidget.h`:** move `m_shaderProgram` from `private` to `protected` (same pattern as the previous protected-fields change).

**In `TestableQYUVOpenGLWidget`:**
```cpp
bool isShaderLinked() const { return m_shaderProgram.isLinked(); }
```

**Test:**
```cpp
void initializeGL_shaderIsLinkedAfterReinit()
{
    TestableQYUVOpenGLWidget w;
    w.resize(320, 240);
    w.show();
    QApplication::processEvents();  // first initializeGL()

    // Simulate context recreation (setParent() scenario)
    w.callInitializeGL();           // second initializeGL()

    // Shader must be linked — unlinked program produces black screen
    QVERIFY(w.isShaderLinked());
}
```

**This test fails before the fix and passes after.**

## Files Changed

| File | Change |
|------|--------|
| `QtScrcpy/render/qyuvopenglwidget.h` | Move `m_shaderProgram` from `private` to `protected` |
| `QtScrcpy/render/qyuvopenglwidget.cpp` | `initShader()`: add `removeAllShaders()`, use local fragShader; `initializeGL()`: add VBO destroy |
| `QtScrcpy/test/tst_qyuvopenglwidget.cpp` | Add `initializeGL_shaderIsLinkedAfterReinit()` test |

## Complete Frame Flow After Fix

```
setParent(nullptr) called on VideoForm
    → QYUVOpenGLWidget's GL context destroyed

reinitVideoWidget() → hide() + show() → schedules repaint
    → initializeGL() fires on new context
    → m_vbo.destroy() + m_vbo.create() → fresh VBO handle
    → initShader():
        removeAllShaders() → clean program
        addShaderFromSourceCode() × 2 → fresh shaders
        link() → SUCCESS (no duplicates)
    → m_textureInited = false, m_needUpdate = true

paintGL() fires:
    → m_shaderProgram.bind() — LINKED, works correctly
    → m_needUpdate = true → initTextures() → empty textures created
    → m_textureInited = true

Frames arrive → updateTextures() → uploads data → update() → paintGL() → video renders
```

## Out of Scope

- The `aboutToBeDestroyed` race window (accepted trade-off from previous spec)
- Any changes to `devicetile.cpp` or `videoform.cpp`
