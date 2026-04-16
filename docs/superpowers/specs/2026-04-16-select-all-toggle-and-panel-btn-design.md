# Design: Select All/None Toggle + Panel Button Position

Date: 2026-04-16

## Overview

Two independent UI improvements:

1. **Merge "All" and "None" buttons into one toggle button** — label and action depend on current check state.
2. **Move the panel toggle button to the right edge of the panel** — it slides with the panel instead of being pinned to the dialog's left edge.

---

## Change 1: Select All/None toggle button

### Behaviour

- If **all** items in `connectedPhoneList` are checked → button text is `"None"` → clicking unchecks all.
- If **any** item is unchecked (including zero items checked) → button text is `"All"` → clicking checks all.
- Button text updates live whenever the check state of any item changes.
- When the list is empty, the button shows `"All"` (harmless click, nothing to check).

### `.ui` changes (`QtScrcpy/ui/dialog.ui`)

Remove the `deselectAllDevicesBtn` widget from `deviceCheckBtnsLayout`. Keep `selectAllDevicesBtn` — its static text property is irrelevant (set dynamically at runtime).

Before:
```xml
<item>
 <widget class="QPushButton" name="selectAllDevicesBtn">
  <property name="text"><string>All</string></property>
 </widget>
</item>
<item>
 <widget class="QPushButton" name="deselectAllDevicesBtn">
  <property name="text"><string>None</string></property>
 </widget>
</item>
```

After:
```xml
<item>
 <widget class="QPushButton" name="selectAllDevicesBtn">
  <property name="text"><string>All</string></property>
 </widget>
</item>
```

### `dialog.h` changes

- Remove declaration: `void on_deselectAllDevicesBtn_clicked();`
- Add private helper declaration: `void updateToggleAllBtn();`

### `dialog.cpp` changes

**Replace `on_selectAllDevicesBtn_clicked`:**
```cpp
void Dialog::on_selectAllDevicesBtn_clicked()
{
    bool allChecked = true;
    for (int i = 0; i < ui->connectedPhoneList->count(); ++i) {
        if (ui->connectedPhoneList->item(i)->checkState() != Qt::Checked) {
            allChecked = false;
            break;
        }
    }
    Qt::CheckState newState = allChecked ? Qt::Unchecked : Qt::Checked;
    for (int i = 0; i < ui->connectedPhoneList->count(); ++i) {
        ui->connectedPhoneList->item(i)->setCheckState(newState);
    }
}
```

**Remove `on_deselectAllDevicesBtn_clicked` entirely.**

**Add `updateToggleAllBtn`:**
```cpp
void Dialog::updateToggleAllBtn()
{
    int total = ui->connectedPhoneList->count();
    if (total == 0) {
        ui->selectAllDevicesBtn->setText(tr("All"));
        return;
    }
    bool allChecked = true;
    for (int i = 0; i < total; ++i) {
        if (ui->connectedPhoneList->item(i)->checkState() != Qt::Checked) {
            allChecked = false;
            break;
        }
    }
    ui->selectAllDevicesBtn->setText(allChecked ? tr("None") : tr("All"));
}
```

**Call sites for `updateToggleAllBtn()`:**
1. End of `onDeviceItemChanged` — after saving checked devices to config.
2. End of each list rebuild path in the ADB result handler (both the `-l` path and the basic `devices` path), after the last `applyCheckStateToItem` call.

---

## Change 2: Panel toggle button follows right edge of panel

### Current behaviour

`m_toggleBtn` is a child of the dialog (`this`), pinned at `(0, height/2 − 15)`. It does not move when the panel slides.

### New behaviour

`m_toggleBtn` becomes a child of `m_panelContainer`, positioned at `x = panelWidth` (just outside the container's right edge). Qt does not clip children to parent bounds by default, so the button appears as a tab that slides with the panel.

- Panel **closed** (container at `x = −panelWidth`): button appears at dialog `x = 0` — same visible position as today.
- Panel **open** (container at `x = 0`): button appears at dialog `x = panelWidth` — on the right edge of the panel.

### Constructor changes (`dialog.cpp`)

Change parent from `this` to `m_panelContainer` and set initial x to `panelW`:

```cpp
// Before:
m_toggleBtn = new QPushButton("▶", this);
// ...
m_toggleBtn->move(0, height() / 2 - 15);
m_toggleBtn->raise();
m_toggleBtn->show();

// After:
m_toggleBtn = new QPushButton("▶", m_panelContainer);
// ...
m_toggleBtn->move(panelW, height() / 2 - 15);
m_toggleBtn->raise();
m_toggleBtn->show();
```

Remove the `m_toggleBtn->raise()` call on the dialog level is not needed — the button is raised within its parent container instead.

### `resizeEvent` changes (`dialog.cpp`)

Update x from `0` to `m_panelContainer->width()`:

```cpp
// Before:
if (m_toggleBtn) {
    m_toggleBtn->move(0, event->size().height() / 2 - 15);
}

// After:
if (m_toggleBtn) {
    m_toggleBtn->move(m_panelContainer->width(), event->size().height() / 2 - 15);
}
```

No other changes needed — the button moves with the container automatically.

---

## Files changed

| File | Change |
|------|--------|
| `QtScrcpy/ui/dialog.ui` | Remove `deselectAllDevicesBtn` widget |
| `QtScrcpy/ui/dialog.h` | Remove `on_deselectAllDevicesBtn_clicked`; add `updateToggleAllBtn` |
| `QtScrcpy/ui/dialog.cpp` | Rewrite `on_selectAllDevicesBtn_clicked`; remove `on_deselectAllDevicesBtn_clicked`; add `updateToggleAllBtn`; add call sites; reparent `m_toggleBtn`; fix `resizeEvent` |

## Non-goals

- No changes to `connectCheckedBtn` or connect logic.
- No visual styling changes to `selectAllDevicesBtn`.
- No changes to the panel animation duration or easing.
