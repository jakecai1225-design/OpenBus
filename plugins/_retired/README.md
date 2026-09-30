# Retired mixed plugins

These trees are **archived** and are **not** discovered by PluginManager
(directories under `plugins/` that start with `_` are skipped; the five ids
below are also denylisted so stale `build/bin/plugins/` copies stay hidden).

| ID | Former name |
|----|-------------|
| `tx-lab` | TX Lab |
| `bus-security` | Bus Security |
| `protocol-hub` | Protocol Hub |
| `log-analysis` | Log Analysis |
| `bus-utilities` | Bus Utilities |

They do not match the domain-suite product surface (`*-suite` / `*-studio` +
AI Agent). Do **not** pack them with `plugin_tool.py pack-suites` or list them
in `make_market.py` / `market.json`.

Future work may fold useful pieces into a real domain suite; until then keep
this folder as source archive only.
