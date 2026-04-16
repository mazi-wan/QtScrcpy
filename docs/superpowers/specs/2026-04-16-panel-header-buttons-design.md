# Design: Panel Header Button Cleanup

Date: 2026-04-16

## Overview

Two small UI changes to the left panel header in `dialog.cpp`:

1. **Remove the "Restart All" button** from the panel header.
2. **Color the "Stop All" button orange** (`#F97316`).

---

## Change 1: Remove Restart All button

### Location

`QtScrcpy/ui/dialog.cpp` — the panel header construction block (lines 267–276).

### Change

Remove the three lines that create, lay out, and connect `restartAllTopBtn`:

```cpp
// DELETE these three lines:
auto *restartAllTopBtn = new QPushButton(tr("Restart All"), panelHeader);
headerLayout->addWidget(restartAllTopBtn);
connect(restartAllTopBtn, &QPushButton::clicked, this, &Dialog::on_restartAllBtn_clicked);
```

Also remove `ui->restartAllBtn->hide();` — it exists only to suppress the `.ui` file duplicate of this button. With the header button gone, hiding the `.ui` button is still correct (it remains hidden), but the explicit `hide()` call is dead code.

The slot `on_restartAllBtn_clicked` and its declaration in `dialog.h` are **not** removed — that is a larger refactor outside this scope.

### Non-goals

- Do not remove `on_restartAllBtn_clicked` or its declaration.
- Do not touch `dialog.ui`.

---

## Change 2: Orange Stop All button

### Location

`QtScrcpy/ui/dialog.cpp` — immediately after `stopAllTopBtn` is created (line 267).

### Change

Set a stylesheet on `stopAllTopBtn`:

```cpp
stopAllTopBtn->setStyleSheet(
    "QPushButton { background-color: #F97316; color: #FFFFFF; border-radius: 3px; padding: 2px 6px; }"
    "QPushButton:hover { background-color: #EA6C0A; }"
    "QPushButton:pressed { background-color: #C2560A; }");
```

Colors:
- Normal: `#F97316` (orange)
- Hover: `#EA6C0A` (darker orange)
- Pressed: `#C2560A` (darkest orange)
- Text: `#FFFFFF` (white)

---

## Files changed

| File | Change |
|------|--------|
| `QtScrcpy/ui/dialog.cpp` | Remove Restart All button lines; add stylesheet to Stop All button |
