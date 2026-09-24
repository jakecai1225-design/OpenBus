# openbus UI translations

- Source language in C++/Python: **English** (`tr("...")` / `t("...")`).
- Non-English catalogs: `openbus_<locale>.ts` → build produces `.qm` beside the exe.
- Regenerate stubs / refresh catalogs: `python scripts/generate_i18n_catalogs.py`
- After adding new `tr()` strings, prefer `lupdate` (or extend the generator) then rebuild.
- Python plugin strings: `plugins/_shared/locales/<locale>.json`
