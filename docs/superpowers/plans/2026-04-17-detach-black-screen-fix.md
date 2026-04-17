# Detach Black Screen Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the black screen that occurs when a device tile is popped out (detached) by resetting stale GL texture state in `QYUVOpenGLWidget::initializeGL()`.

**Architecture:** Two lines added to `initializeGL()` reset `m_textureInited` and `m_needUpdate` so that after GL context recreation (triggered by `setParent()`), `paintGL()` creates fresh textures instead of trying to bind stale invalid IDs. Tests are written first (TDD) using a `TestableQYUVOpenGLWidget` subclass that exposes protected state.

**Tech Stack:** Qt 5/6, Qt Test, QOpenGLWidget, CMake 3.19+

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `QtScrcpy/render/qyuvopenglwidget.h` | Modify | Move 3 state fields from `private` to `protected` so the test subclass can read/write them |
| `QtScrcpy/render/qyuvopenglwidget.cpp` | Modify | Add 2 lines to `initializeGL()` — the fix |
| `QtScrcpy/CMakeLists.txt` | Modify | Add `enable_testing()` + `add_subdirectory(test)` |
| `QtScrcpy/test/CMakeLists.txt` | Create | Build config for the test executable |
| `QtScrcpy/test/tst_qyuvopenglwidget.cpp` | Create | Four Qt Test cases |

---

## Task 1: Expose QYUVOpenGLWidget state for testing

**Files:**
- Modify: `QtScrcpy/render/qyuvopenglwidget.h`

The three state fields that control texture lifecycle (`m_frameSize`, `m_needUpdate`, `m_textureInited`) are `private`. The test subclass (Task 3) needs to read and write them directly. Move them to `protected` — this is a pure accessibility change with no behavior impact.

- [ ] **Step 1: Move state fields to protected**

In `QtScrcpy/render/qyuvopenglwidget.h`, replace the private block containing the state fields:

```cpp
// BEFORE (lines 35-39 in the file):
private:
    // 视频帧尺寸
    QSize m_frameSize = { -1, -1 };
    bool m_needUpdate = false;
    bool m_textureInited = false;
```

```cpp
// AFTER:
protected:
    QSize m_frameSize = { -1, -1 };
    bool m_needUpdate = false;
    bool m_textureInited = false;
```

The `private:` block that remains below contains `m_vbo`, `m_shaderProgram`, and `m_texture[3]` — leave those untouched.

Full updated `qyuvopenglwidget.h` for reference:

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

private:
    void initShader();
    void initTextures();
    void deInitTextures();
    void updateTexture(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride);

    QOpenGLBuffer m_vbo;
    QOpenGLShaderProgram m_shaderProgram;
    GLuint m_texture[3] = { 0 };
};

#endif // QYUVOPENGLWIDGET_H
```

- [ ] **Step 2: Build to confirm no regressions**

```bash
cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
cmake -DCMAKE_PREFIX_PATH="$qt_cmake_path" -DCMAKE_BUILD_TYPE=Debug -S . -B build
cmake --build build --config Debug -j8
```

Expected: build succeeds, zero errors. (No behavior changed — accessibility only.)

- [ ] **Step 3: Commit**

```bash
git add QtScrcpy/render/qyuvopenglwidget.h
git commit -m "refactor: make QYUVOpenGLWidget state fields protected for testability"
```

---

## Task 2: Wire up test build infrastructure

**Files:**
- Create: `QtScrcpy/test/CMakeLists.txt`
- Modify: `QtScrcpy/CMakeLists.txt`

- [ ] **Step 1: Create `QtScrcpy/test/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.19 FATAL_ERROR)

set(TEST_NAME tst_qyuvopenglwidget)

find_package(Qt${QT_DESIRED_VERSION} REQUIRED COMPONENTS Test Widgets OpenGL)
if(QT_DESIRED_VERSION MATCHES 6)
    find_package(Qt${QT_DESIRED_VERSION} REQUIRED COMPONENTS OpenGLWidgets)
endif()

add_executable(${TEST_NAME}
    tst_qyuvopenglwidget.cpp
    ../render/qyuvopenglwidget.h
    ../render/qyuvopenglwidget.cpp
)

target_include_directories(${TEST_NAME} PRIVATE
    ../render
)

target_link_libraries(${TEST_NAME} PRIVATE
    Qt${QT_DESIRED_VERSION}::Test
    Qt${QT_DESIRED_VERSION}::Widgets
    Qt${QT_DESIRED_VERSION}::OpenGL
)

if(QT_DESIRED_VERSION MATCHES 6)
    target_link_libraries(${TEST_NAME} PRIVATE Qt${QT_DESIRED_VERSION}::OpenGLWidgets)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(${TEST_NAME} PRIVATE xcb Threads::Threads)
endif()

add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
set_tests_properties(${TEST_NAME} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

- [ ] **Step 2: Add test subdirectory to `QtScrcpy/CMakeLists.txt`**

At the very end of `QtScrcpy/CMakeLists.txt` (after the existing `add_subdirectory(QtScrcpyCore)` line), add:

```cmake
enable_testing()
add_subdirectory(test)
```

- [ ] **Step 3: Verify CMake configuration succeeds**

```bash
cmake -DCMAKE_PREFIX_PATH="$qt_cmake_path" -DCMAKE_BUILD_TYPE=Debug -S . -B build
```

Expected output includes:
```
-- Configuring done
-- Build files have been written to: .../build
```

The source file `tst_qyuvopenglwidget.cpp` doesn't exist yet, so don't run `cmake --build` yet — that comes in Task 3 after the test source is written.

- [ ] **Step 4: Commit**

```bash
git add QtScrcpy/test/CMakeLists.txt QtScrcpy/CMakeLists.txt
git commit -m "build: add Qt Test infrastructure for QYUVOpenGLWidget"
```

---

## Task 3: Write the failing tests

**Files:**
- Create: `QtScrcpy/test/tst_qyuvopenglwidget.cpp`

These tests are written against the **current (unfixed)** code. They must all FAIL before the fix (Task 5) and all PASS after.

- [ ] **Step 1: Create `QtScrcpy/test/tst_qyuvopenglwidget.cpp`**

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
```

- [ ] **Step 2: Build the test**

```bash
cmake -DCMAKE_PREFIX_PATH="$qt_cmake_path" -DCMAKE_BUILD_TYPE=Debug -S . -B build
cmake --build build --target tst_qyuvopenglwidget --config Debug -j8
```

Expected: compiles and links successfully.

- [ ] **Step 3: Run tests — verify they FAIL (red)**

```bash
QT_QPA_PLATFORM=offscreen ./build/QtScrcpy/test/tst_qyuvopenglwidget -v2
```

Expected output (before fix):
```
FAIL!  : TstQYUVOpenGLWidget::initializeGL_resetsTextureInited() Compared values are not the same
   Actual   (w.isTextureInited()): true
   Expected (false)              : false
FAIL!  : TstQYUVOpenGLWidget::initializeGL_setsNeedUpdateWhenFrameSizeValid() Compared values are not the same
   Actual   (w.isNeedUpdate()): false
   Expected (true)            : true
PASS   : TstQYUVOpenGLWidget::initializeGL_doesNotSetNeedUpdateWhenFrameSizeInvalid()
PASS   : TstQYUVOpenGLWidget::updateTextures_isNoOpWhenNotInited()
```

Tests 1 and 2 fail (the fix is not implemented yet). Tests 3 and 4 may already pass since they test existing behavior.

- [ ] **Step 4: Commit**

```bash
git add QtScrcpy/test/tst_qyuvopenglwidget.cpp
git commit -m "test: add failing tests for QYUVOpenGLWidget GL context recreation"
```

---

## Task 4: Implement the fix

**Files:**
- Modify: `QtScrcpy/render/qyuvopenglwidget.cpp`

The fix is two lines at the end of `initializeGL()`. They reset the texture state so that `paintGL()` (which runs immediately after `initializeGL()` on the new context) creates fresh texture objects instead of binding stale invalid IDs.

- [ ] **Step 1: Add two lines to `initializeGL()`**

In `QtScrcpy/render/qyuvopenglwidget.cpp`, find `initializeGL()` (starts at line 142):

```cpp
// BEFORE:
void QYUVOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);

    // 顶点缓冲对象初始化
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(coordinate, sizeof(coordinate));
    initShader();
    // 设置背景清理色为黑色
    glClearColor(0.0, 0.0, 0.0, 0.0);
    // 清理颜色背景
    glClear(GL_COLOR_BUFFER_BIT);
}
```

```cpp
// AFTER:
void QYUVOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);

    // 顶点缓冲对象初始化
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

- [ ] **Step 2: Build**

```bash
cmake --build build --target tst_qyuvopenglwidget --config Debug -j8
```

Expected: builds successfully.

- [ ] **Step 3: Run tests — verify they all PASS (green)**

```bash
QT_QPA_PLATFORM=offscreen ./build/QtScrcpy/test/tst_qyuvopenglwidget -v2
```

Expected output:
```
PASS   : TstQYUVOpenGLWidget::initializeGL_resetsTextureInited()
PASS   : TstQYUVOpenGLWidget::initializeGL_setsNeedUpdateWhenFrameSizeValid()
PASS   : TstQYUVOpenGLWidget::initializeGL_doesNotSetNeedUpdateWhenFrameSizeInvalid()
PASS   : TstQYUVOpenGLWidget::updateTextures_isNoOpWhenNotInited()
Totals: 4 passed, 0 failed, 0 skipped
```

- [ ] **Step 4: Also build and run the full app to confirm no regressions**

```bash
cmake --build build --config Debug -j8
pkill -f QtScrcpy || true
./output/x64/Debug/QtScrcpy &
```

Manually test: connect a device, verify video streams normally (no regression).

- [ ] **Step 5: Commit the fix**

```bash
git add QtScrcpy/render/qyuvopenglwidget.cpp
git commit -m "fix: reset texture state in initializeGL() to prevent black screen on detach"
```

---

## Done

At this point:
- All 4 tests pass
- The detach (pop-out) no longer produces a black screen
- No changes to `devicetile.cpp` or `videoform.cpp` — fix is self-contained in the GL widget
