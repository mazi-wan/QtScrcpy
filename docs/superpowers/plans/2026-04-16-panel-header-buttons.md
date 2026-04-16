# Panel Header Button Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the "Restart All" button from the panel header and color the "Stop All" button orange.

**Architecture:** Both changes are confined to the panel header construction block in `Dialog::Dialog()` in `dialog.cpp` (lines 267–276). No new files, no new classes.

**Tech Stack:** Qt 5/6, C++11, QPushButton stylesheet

---

## File Map

| File | Change |
|------|--------|
| `QtScrcpy/ui/dialog.cpp` | Remove Restart All button; add orange stylesheet to Stop All button |

---

## Task 1: Remove Restart All and color Stop All

**Files:**
- Modify: `QtScrcpy/ui/dialog.cpp:267-276`

- [ ] **Step 1: Read the current panel header block**

  Open `QtScrcpy/ui/dialog.cpp` around lines 261–276. It currently reads:

  ```cpp
  // Fixed header with action buttons at top (always visible, not scrollable)
  auto *panelHeader = new QWidget(this);
  panelHeader->setObjectName("panelHeader");
  auto *headerLayout = new QHBoxLayout(panelHeader);
  headerLayout->setContentsMargins(4, 4, 4, 4);
  headerLayout->setSpacing(4);
  auto *stopAllTopBtn = new QPushButton(tr("Stop All"), panelHeader);
  auto *restartAllTopBtn = new QPushButton(tr("Restart All"), panelHeader);
  headerLayout->addWidget(stopAllTopBtn);
  headerLayout->addWidget(restartAllTopBtn);
  connect(stopAllTopBtn, &QPushButton::clicked, this, &Dialog::on_stopAllServerBtn_clicked);
  connect(restartAllTopBtn, &QPushButton::clicked, this, &Dialog::on_restartAllBtn_clicked);

  // Hide the duplicate buttons inside the scrollable leftWidget
  ui->stopAllServerBtn->hide();
  ui->restartAllBtn->hide();
  ```

- [ ] **Step 2: Apply both changes**

  Replace that block with:

  ```cpp
  // Fixed header with action buttons at top (always visible, not scrollable)
  auto *panelHeader = new QWidget(this);
  panelHeader->setObjectName("panelHeader");
  auto *headerLayout = new QHBoxLayout(panelHeader);
  headerLayout->setContentsMargins(4, 4, 4, 4);
  headerLayout->setSpacing(4);
  auto *stopAllTopBtn = new QPushButton(tr("Stop All"), panelHeader);
  stopAllTopBtn->setStyleSheet(
      "QPushButton { background-color: #F97316; color: #FFFFFF; border-radius: 3px; padding: 2px 6px; }"
      "QPushButton:hover { background-color: #EA6C0A; }"
      "QPushButton:pressed { background-color: #C2560A; }");
  headerLayout->addWidget(stopAllTopBtn);
  connect(stopAllTopBtn, &QPushButton::clicked, this, &Dialog::on_stopAllServerBtn_clicked);

  // Hide the duplicate buttons inside the scrollable leftWidget
  ui->stopAllServerBtn->hide();
  ui->restartAllBtn->hide();
  ```

  What changed:
  - Removed the three `restartAllTopBtn` lines (create, addWidget, connect).
  - Added `setStyleSheet(...)` on `stopAllTopBtn` after creation.
  - Kept `ui->restartAllBtn->hide()` — the `.ui` widget still exists and must stay hidden.

- [ ] **Step 3: Build**

  ```bash
  cd /mnt/Data/Workspace/1.CloneAndRun/QtScrcpy
  ./ci/linux/build_for_linux.sh "Debug" 2>&1 | tail -20
  ```

  Expected: build succeeds, no errors.

- [ ] **Step 4: Manual verify**

  ```bash
  pkill -f QtScrcpy || true
  ./output/x64/Debug/QtScrcpy
  ```

  - Open the left panel with the toggle button.
  - Confirm only one button ("Stop All") is visible in the header — no "Restart All".
  - Confirm the "Stop All" button is orange with white text.
  - Hover over it — it should darken slightly.

- [ ] **Step 5: Commit**

  ```bash
  git add QtScrcpy/ui/dialog.cpp
  git commit -m "feat: remove Restart All button and color Stop All orange in panel header"
  ```
