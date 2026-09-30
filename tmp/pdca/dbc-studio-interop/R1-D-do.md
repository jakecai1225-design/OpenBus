# R1 Do

## Changes

- `document.py`: `on_focus` / `_notify_focus` (dedupe identical focus)
- `app_shell.py`: `goto_attributes`, `goto_value_tables`; View menu leaves; actions
- `pages/attributes.py`: search, Value/Default/Type/Source columns, Messages jump, context menus, focus listener, GenMsgCycleTime mirror
- `pages/value_tables.py`: search, Assign focus / Messages, context menus, `select_table`, focus listener
- `pages/editor.py`: Related buttons + tree context menu (Attributes / Value tables / copy / delete)

## Why

Chain the Edit workspace primary path without losing selection context.
