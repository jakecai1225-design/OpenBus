# R1 Act

## Keep

Editor tab residency as shipped in R1 Do.

## Hotfix (same requirement)

Leaf click wiped the tab strip: `highlight_activity` → `suite_chrome.set_editor_title` because DBC lacked `_on_workbench_page` and activity keys were not in `tab_pages`.

Fix:
- DBC: add `_on_workbench_page`
- `suite_chrome`: skip title overwrite when `_chrome_tabs` is set; add edit/analyze/integrate/deliver to `tab_pages`

## Round 2 candidates (optional)

- On activity click with existing tabs: prefer activating an open tab that belongs to that activity
- Dirty-dot / document name on Messages tab title

## Close R1

Requirement met for sidebar → main-window tabs after hotfix.
