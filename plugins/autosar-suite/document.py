# -*- coding: utf-8 -*-
"""Shared in-memory ARXML document + optional project for AUTOSAR Studio."""

from __future__ import annotations

import copy
import json
import os
from typing import Callable, Dict, List, Optional, Tuple

from _shared import arxml_bsw, arxml_project, arxmlparse

_MAX_HISTORY = 40


class ArxmlDocument:
    def __init__(self):
        self.path: str = ""
        self.model: arxmlparse.ArxmlModel = arxmlparse.empty_model()
        self.ecuc: arxmlparse.EcucModel = arxmlparse.empty_ecuc()
        self.swc: arxmlparse.SwcModel = arxmlparse.empty_swc()
        # Full BSW catalog: module name -> ECUC model (one ARXML each)
        self.bsw: Dict[str, arxmlparse.EcucModel] = {}
        self.active_bsw: str = "Com"
        self.manifest: Optional[arxml_project.ProjectManifest] = None
        self.editor_mode: str = "com"  # com | ecuc
        self.dirty: bool = False
        self.compare_model: Optional[arxmlparse.ArxmlModel] = None
        self.compare_path: str = ""
        self._listeners: List[Callable[[], None]] = []
        self._baseline: arxmlparse.ArxmlModel = copy.deepcopy(self.model)
        self._baseline_dirty: bool = False
        self._undo: List[Tuple[arxmlparse.ArxmlModel, bool]] = []
        self._redo: List[Tuple[arxmlparse.ArxmlModel, bool]] = []
        self._bsw_undo: List[Tuple[dict, str, bool]] = []
        self._bsw_redo: List[Tuple[dict, str, bool]] = []
        self._suspend_hist: bool = False
        # Finding acknowledgements: set of "rule|location|module"
        self.finding_acks: set = set()
        self.autosar_release: str = "R22-11"

    @property
    def ipdus(self):
        return self.model.ipdus

    @property
    def project_root(self) -> str:
        return self.manifest.root if self.manifest else ""

    def has_project(self) -> bool:
        return bool(self.manifest and self.manifest.root)

    def on_changed(self, fn: Callable[[], None]) -> None:
        self._listeners.append(fn)

    def _notify(self) -> None:
        for fn in list(self._listeners):
            try:
                fn()
            except Exception:
                import traceback
                traceback.print_exc()

    def _clear_history(self) -> None:
        self._undo.clear()
        self._redo.clear()
        self._bsw_undo.clear()
        self._bsw_redo.clear()
        self._baseline = copy.deepcopy(self.model)
        self._baseline_dirty = self.dirty

    def _push_undo(self, snap: arxmlparse.ArxmlModel, dirty: bool) -> None:
        self._undo.append((snap, dirty))
        if len(self._undo) > _MAX_HISTORY:
            self._undo.pop(0)

    def can_undo(self) -> bool:
        return bool(self._undo) or bool(self._bsw_undo)

    def can_redo(self) -> bool:
        return bool(self._redo) or bool(self._bsw_redo)

    def undo(self) -> bool:
        if self._bsw_undo and (
                not self._undo or len(self._bsw_undo) >= len(self._undo)):
            return self._undo_bsw()
        if not self._undo:
            return self._undo_bsw() if self._bsw_undo else False
        self._redo.append((copy.deepcopy(self.model), self.dirty))
        snap, dirty = self._undo.pop()
        self._suspend_hist = True
        self.model = snap
        self.dirty = dirty
        self._baseline = copy.deepcopy(self.model)
        self._baseline_dirty = self.dirty
        self._notify()
        self._suspend_hist = False
        return True

    def _undo_bsw(self) -> bool:
        if not self._bsw_undo:
            return False
        self._bsw_redo.append(
            (copy.deepcopy(self.bsw), self.active_bsw, self.dirty))
        snap, active, dirty = self._bsw_undo.pop()
        self._suspend_hist = True
        self.bsw = snap
        self.active_bsw = active
        self.dirty = dirty
        self._notify()
        self._suspend_hist = False
        return True

    def redo(self) -> bool:
        if self._bsw_redo and (
                not self._redo or len(self._bsw_redo) >= len(self._redo)):
            return self._redo_bsw()
        if not self._redo:
            return self._redo_bsw() if self._bsw_redo else False
        self._push_undo(copy.deepcopy(self.model), self.dirty)
        snap, dirty = self._redo.pop()
        self._suspend_hist = True
        self.model = snap
        self.dirty = dirty
        self._baseline = copy.deepcopy(self.model)
        self._baseline_dirty = self.dirty
        self._notify()
        self._suspend_hist = False
        return True

    def _redo_bsw(self) -> bool:
        if not self._bsw_redo:
            return False
        self._bsw_undo.append(
            (copy.deepcopy(self.bsw), self.active_bsw, self.dirty))
        if len(self._bsw_undo) > _MAX_HISTORY:
            self._bsw_undo.pop(0)
        snap, active, dirty = self._bsw_redo.pop()
        self._suspend_hist = True
        self.bsw = snap
        self.active_bsw = active
        self.dirty = dirty
        self._notify()
        self._suspend_hist = False
        return True

    def mark_dirty(self, dirty: bool = True) -> None:
        if dirty and not self._suspend_hist:
            self._push_undo(
                copy.deepcopy(self._baseline), self._baseline_dirty)
            self._redo.clear()
        self.dirty = dirty
        self._baseline = copy.deepcopy(self.model)
        self._baseline_dirty = self.dirty
        self._notify()

    def set_model(
            self, model: arxmlparse.ArxmlModel, path: str = "",
            dirty: bool = False) -> None:
        self.model = model
        self.path = path or getattr(model, "path", "") or ""
        if self.path:
            self.model.path = self.path
        self.dirty = dirty
        self._clear_history()
        self._notify()

    def set_ecuc(
            self, ecuc: arxmlparse.EcucModel, dirty: bool = True) -> None:
        self.ecuc = ecuc
        if dirty:
            self.dirty = True
            if self.manifest:
                self.manifest.derived_dirty = False
        self._notify()

    def set_editor_mode(self, mode: str) -> None:
        self.editor_mode = "ecuc" if mode == "ecuc" else "com"
        self._notify()

    def new(self) -> None:
        self.manifest = None
        self.ecuc = arxmlparse.empty_ecuc()
        self.swc = arxmlparse.empty_swc()
        self.bsw = {}
        self.active_bsw = "Com"
        self.editor_mode = "com"
        self.set_model(arxmlparse.empty_model(), path="", dirty=False)

    def load(self, path: str) -> arxmlparse.ArxmlModel:
        model = arxmlparse.parse_arxml_model(path)
        self.manifest = None
        self.ecuc = arxmlparse.empty_ecuc()
        self.swc = arxmlparse.empty_swc()
        self.bsw = {}
        self.active_bsw = "Com"
        self.editor_mode = "com"
        self.set_model(model, path=path, dirty=False)
        return model

    def save(self, path: Optional[str] = None) -> str:
        out = path or self.path
        if not out:
            raise ValueError("No path for save")
        text = arxmlparse.serialize_model(self.model)
        with open(out, "w", encoding="utf-8") as f:
            f.write(text)
        self.path = out
        self.model.path = out
        self.dirty = False
        self._baseline = copy.deepcopy(self.model)
        self._baseline_dirty = False
        self._notify()
        return out

    def apply_model(self, model: arxmlparse.ArxmlModel) -> None:
        if not self._suspend_hist:
            self._push_undo(
                copy.deepcopy(self._baseline), self._baseline_dirty)
            self._redo.clear()
        self.model = model
        if self.path:
            self.model.path = self.path
        self.dirty = True
        if self.manifest:
            self.manifest.derived_dirty = True
        self._baseline = copy.deepcopy(self.model)
        self._baseline_dirty = True
        self._notify()

    def apply_ecuc(self, ecuc: arxmlparse.EcucModel) -> None:
        self.ecuc = ecuc
        self.dirty = True
        self._notify()

    def clone_model(self) -> arxmlparse.ArxmlModel:
        return copy.deepcopy(self.model)

    def clone_ecuc(self) -> arxmlparse.EcucModel:
        return copy.deepcopy(self.ecuc)

    def set_active_bsw(self, name: str) -> None:
        if not name:
            return
        if name == self.active_bsw and name in self.bsw:
            return
        self.active_bsw = name
        if name not in self.bsw:
            ecu = self.manifest.ecu_name if self.manifest else "Ecu"
            self.bsw[name] = arxml_bsw.stub_module(name, ecu_name=ecu)
            self.dirty = True
        self._notify()

    def apply_bsw_module(self, name: str, model: arxmlparse.EcucModel) -> None:
        if not self._suspend_hist:
            self._bsw_undo.append(
                (copy.deepcopy(self.bsw), self.active_bsw, self.dirty))
            if len(self._bsw_undo) > _MAX_HISTORY:
                self._bsw_undo.pop(0)
            self._bsw_redo.clear()
        self.bsw[name] = model
        self.dirty = True
        self._notify()

    def finding_ack_key(self, f: dict) -> str:
        return "%s|%s|%s" % (
            f.get("rule") or "",
            f.get("location") or f.get("pdu") or "",
            f.get("module") or "",
        )

    def ack_finding(self, key: str) -> None:
        if key:
            self.finding_acks.add(key)
            if self.manifest is not None:
                self.manifest.finding_acks = sorted(self.finding_acks)

    def unack_finding(self, key: str) -> None:
        self.finding_acks.discard(key)
        if self.manifest is not None:
            self.manifest.finding_acks = sorted(self.finding_acks)

    def init_all_bsw(self) -> int:
        ecu = self.manifest.ecu_name if self.manifest else "Ecu"
        self.bsw = arxml_bsw.stub_all_modules(ecu_name=ecu)
        self.bsw = arxml_bsw.sync_com_into_bsw(
            self.bsw, self.model, ecu_name=ecu)
        # Keep aggregate ecuc as Com+CanIf+PduR+CanNm for Editor ECUC mode
        derived = arxmlparse.derive_ecuc_from_com(self.model, ecu_name=ecu)
        self.ecuc = derived
        if self.manifest:
            for name in self.bsw:
                self.manifest.ensure_module_entry(name)
        self.dirty = True
        self._notify()
        return len(self.bsw)

    def sync_bsw_from_com(self) -> None:
        ecu = self.manifest.ecu_name if self.manifest else "Ecu"
        if not self.bsw:
            self.bsw = arxml_bsw.stub_all_modules(ecu_name=ecu)
        self.bsw = arxml_bsw.sync_com_into_bsw(
            self.bsw, self.model, ecu_name=ecu)
        self.ecuc = arxmlparse.derive_ecuc_from_com(self.model, ecu_name=ecu)
        self.dirty = True
        if self.manifest:
            self.manifest.derived_dirty = False
        self._notify()

    def import_dbc(self, path: str) -> int:
        """Import DBC → COM model, sync BSW, optionally copy into input/."""
        from _shared import arxml_dbc
        model = arxml_dbc.import_dbc_to_model(path)
        self.apply_model(model)
        self.sync_bsw_from_com()
        if self.has_project():
            import shutil
            dest = os.path.join(self.manifest.root, "input", "network.dbc")
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            try:
                shutil.copy2(path, dest)
            except OSError:
                pass
            if self.manifest:
                self.manifest.roles["dbc"] = "input/network.dbc"
        return len(model.ipdus)

    def enable_bsw(self, name: str) -> bool:
        """Ensure module exists (stub if missing). Returns True if newly added."""
        if name in self.bsw:
            return False
        return self.toggle_bsw_module(name)

    def toggle_bsw_module(self, name: str) -> bool:
        """Enable (stub) or disable (remove) a BSW module. Returns enabled."""
        if name in self.bsw:
            del self.bsw[name]
            if self.manifest and name in self.manifest.modules:
                del self.manifest.modules[name]
            self.dirty = True
            self._notify()
            return False
        ecu = self.manifest.ecu_name if self.manifest else "Ecu"
        self.bsw[name] = arxml_bsw.stub_module(name, ecu_name=ecu)
        if self.manifest:
            self.manifest.ensure_module_entry(name)
        self.active_bsw = name
        self.dirty = True
        self._notify()
        return True

    def validate_all(self) -> list:
        findings = arxmlparse.validate_project(
            self.model, self.ecuc, self._role_flags())
        findings = [f for f in findings if f.get("rule") != "ok"]
        findings.extend([
            f for f in arxmlparse.validate_swc(self.model, self.swc)
            if f.get("rule") != "ok"])
        findings.extend([
            f for f in arxml_bsw.validate_bsw_set(self.bsw, self.model)
            if f.get("rule") != "ok"])
        if self.manifest:
            for name, rel in (self.manifest.modules or {}).items():
                abs_p = self.manifest.abs_module(name)
                if abs_p and not os.path.isfile(abs_p) and name in self.bsw:
                    findings.append({
                        "level": "warn", "severity": "warning",
                        "rule": "bsw_file_missing",
                        "message": "Enabled module %s has no file on disk" % name,
                        "fix": "Project → Write Intermediates",
                        "artifact": "bsw", "pdu": "", "signal": "",
                        "location": name,
                    })
            # Release gate: project autosarRelease vs schema packs
            release = (
                self.autosar_release
                or getattr(self.manifest, "autosar_release", "")
                or "R22-11")
            pack_rel = "R22-11"
            try:
                from _shared import arxml_ecuc_schema
                sch = arxml_ecuc_schema.load_schema("Com")
                pack_rel = (sch or {}).get("autosarRelease") or pack_rel
            except Exception:
                pass
            if release and pack_rel and release != pack_rel:
                findings.append({
                    "level": "warn", "severity": "warning",
                    "rule": "release_mismatch",
                    "message": (
                        "Project autosarRelease %s does not match schema "
                        "packs %s" % (release, pack_rel)),
                    "fix": "Set project autosarRelease to %s" % pack_rel,
                    "artifact": "project", "pdu": "", "signal": "",
                    "location": "project", "module": "",
                })
        for f in findings:
            key = self.finding_ack_key(f)
            f["ack_key"] = key
            f["acked"] = key in self.finding_acks
        if not findings:
            findings.append({
                "level": "info", "severity": "info", "rule": "ok",
                "message": "No issues", "fix": "", "artifact": "project",
                "pdu": "", "signal": "", "location": "project",
                "acked": False, "ack_key": "",
            })
        return findings

    def wizard_init_comm_stack(self) -> list:
        """Enable Com/CanIf/PduR/Can/CanSM/ComM and sync from COM."""
        log = []
        for name in ("Com", "CanIf", "PduR", "Can", "CanSM", "ComM", "EcuC"):
            if self.enable_bsw(name):
                log.append("enabled %s" % name)
            else:
                log.append("already %s" % name)
        self.sync_bsw_from_com()
        log.append("synced from COM")
        return log

    def wizard_add_diag_path(self) -> list:
        """Enable Dcm/Dem/CanTp for a basic diagnostic path."""
        log = []
        for name in ("Dcm", "Dem", "CanTp", "PduR"):
            if self.enable_bsw(name):
                log.append("enabled %s" % name)
        return log

    def wizard_os_tasks_lite(self) -> list:
        """Seed one OsTask per COM I-PDU runnable placeholder (lite)."""
        log = []
        if "Os" not in self.bsw:
            self.enable_bsw("Os")
            log.append("enabled Os")
        model = copy.deepcopy(self.bsw.get("Os") or arxml_bsw.stub_module("Os"))
        if not model.modules:
            log.append("Os empty")
            return log
        mod = model.modules[0]
        existing = {c.name for c in mod.containers}
        for pdu in self.model.ipdus[:16]:
            tname = "OsTask_%s" % pdu.name.replace(" ", "_")[:40]
            if tname in existing:
                continue
            from _shared import arxmlparse as _ap
            cont = _ap.EcucContainer(
                name=tname, definition="OsTask",
                params=[
                    _ap.EcucParam("OsTaskPriority", "10", kind="numerical"),
                    _ap.EcucParam(
                        "OsTaskSchedule", "FULL", kind="enumeration"),
                    _ap.EcucParam(
                        "OsTaskActivation", "1", kind="numerical"),
                ],
            )
            mod.containers.append(cont)
            existing.add(tname)
            log.append("added %s" % tname)
        self.apply_bsw_module("Os", model)
        return log

    def diff_bsw_modules(self, peer_bsw: dict, names: list | None = None) -> list:
        """Value-level BSW module diff vs peer dict of EcucModel."""
        rows = []
        names = names or sorted(set(self.bsw) | set(peer_bsw or {}))
        for name in names:
            a = self.bsw.get(name)
            b = (peer_bsw or {}).get(name)
            if a is None and b is None:
                continue
            if a is None:
                rows.append({
                    "kind": "removed", "module": name,
                    "container": "", "param": "", "detail": "only in B"})
                continue
            if b is None:
                rows.append({
                    "kind": "added", "module": name,
                    "container": "", "param": "", "detail": "only in A"})
                continue

            def flat(model):
                out = {}
                for m in model.modules:
                    for c in m.containers:
                        def walk(cont, prefix):
                            key = "%s/%s" % (prefix, cont.name) if prefix else cont.name
                            for p in cont.params:
                                out["%s.%s" % (key, p.name)] = str(p.value or "")
                            for ch in cont.children:
                                walk(ch, key)
                        walk(c, "")
                return out

            fa, fb = flat(a), flat(b)
            for k in sorted(set(fa) | set(fb)):
                va, vb = fa.get(k), fb.get(k)
                if va is None:
                    rows.append({
                        "kind": "removed", "module": name,
                        "container": k.rsplit(".", 1)[0],
                        "param": k.rsplit(".", 1)[-1],
                        "detail": "B=%s" % vb})
                elif vb is None:
                    rows.append({
                        "kind": "added", "module": name,
                        "container": k.rsplit(".", 1)[0],
                        "param": k.rsplit(".", 1)[-1],
                        "detail": "A=%s" % va})
                elif va != vb:
                    rows.append({
                        "kind": "changed", "module": name,
                        "container": k.rsplit(".", 1)[0],
                        "param": k.rsplit(".", 1)[-1],
                        "detail": "A=%s → B=%s" % (va, vb)})
        return rows

    def handoff_html_report(self) -> str:
        """HTML configuration + Open-in-DaVinci checklist."""
        import html as _html
        findings = [f for f in self.validate_all() if f.get("rule") != "ok"]
        n_err = sum(1 for f in findings if f.get("level") == "error"
                    or f.get("severity") == "error")
        n_warn = sum(1 for f in findings
                     if f.get("level") in ("warn", "warning")
                     or f.get("severity") in ("warn", "warning"))
        n_ack = sum(1 for f in findings if f.get("acked"))
        mods = sorted(self.bsw.keys())
        pdu_rows = []
        for p in self.model.ipdus:
            pdu_rows.append(
                "<tr><td>%s</td><td>0x%X</td><td>%d</td><td>%d</td></tr>" % (
                    _html.escape(p.name), p.can_id, p.dlc, len(p.signals)))
        checklist = [
            "Write Intermediates (work/com.arxml + work/bsw/*.arxml)",
            "Confirm Validate shows 0 errors (acks optional for warnings)",
            "Open DaVinci Configurator Classic → Import ARXML / project",
            "Map ECUC modules to MICROSAR / vendor GenData definitions",
            "Generate BSW/RTE in DaVinci (Studio never generates C)",
        ]
        cl_html = "".join("<li>%s</li>" % _html.escape(x) for x in checklist)
        return (
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<title>AUTOSAR Studio handoff</title>"
            "<style>body{font-family:Segoe UI,sans-serif;margin:24px;max-width:960px;}"
            "table{border-collapse:collapse;width:100%%;margin:12px 0;}"
            "th,td{border:1px solid #ddd;padding:6px;font-size:13px;}"
            "th{background:#f5f5f5;text-align:left;}"
            ".ok{color:#2E7D32}.warn{color:#EF6C00}.err{color:#C62828}</style>"
            "</head><body>"
            "<h1>AUTOSAR Studio → DaVinci handoff</h1>"
            "<p>Release <b>%s</b> · %d PDUs · %d BSW modules · "
            "<span class='%s'>%d errors</span> · "
            "<span class='warn'>%d warnings</span> (%d acked)</p>"
            "<h2>Open in DaVinci checklist</h2><ol>%s</ol>"
            "<h2>I-PDUs</h2><table><thead><tr>"
            "<th>Name</th><th>CAN ID</th><th>DLC</th><th>Signals</th>"
            "</tr></thead><tbody>%s</tbody></table>"
            "<h2>BSW modules</h2><p>%s</p>"
            "<p><em>Configuration only — no Com.c / Rte.c / Os.c codegen.</em></p>"
            "</body></html>"
        ) % (
            _html.escape(self.autosar_release or "R22-11"),
            len(self.model.ipdus), len(mods),
            "err" if n_err else "ok", n_err, n_warn, n_ack,
            cl_html, "\n".join(pdu_rows) or "<tr><td colspan=4>(none)</td></tr>",
            _html.escape(", ".join(mods) or "(none)"),
        )

    def derive_ecuc(self) -> arxmlparse.EcucModel:
        ecu = self.manifest.ecu_name if self.manifest else "Ecu"
        self.ecuc = arxmlparse.derive_ecuc_from_com(self.model, ecu_name=ecu)
        self.sync_bsw_from_com()
        self.dirty = True
        if self.manifest:
            self.manifest.derived_dirty = False
        self._notify()
        return self.ecuc

    def derive_swc(self, swc_name: str = "AppSwc") -> arxmlparse.SwcModel:
        self.swc = arxmlparse.derive_swc_from_com(self.model, swc_name=swc_name)
        self.dirty = True
        self._notify()
        return self.swc

    def apply_swc(self, swc: arxmlparse.SwcModel) -> None:
        self.swc = swc
        self.dirty = True
        self._notify()

    def apply_fix_pack(self, recipes: list) -> list:
        ecu = self.manifest.ecu_name if self.manifest else "Ecu"
        model, ecuc, log = arxmlparse.apply_fix_recipes(
            self.model, self.ecuc, recipes, ecu_name=ecu)
        self.apply_model(model)
        self.set_ecuc(ecuc, dirty=True)
        self.sync_bsw_from_com()
        return log

    def new_project(
            self, root: str, name: str = "UntitledEcu",
            ecu_name: str = "Ecu",
            seed: Optional[arxmlparse.ArxmlModel] = None) -> None:
        m = arxml_project.create_project(root, name=name, ecu_name=ecu_name)
        self.manifest = m
        self.editor_mode = "com"
        if seed is not None:
            self.model = seed
        else:
            self.model = arxmlparse.empty_model()
        self.model.package = name
        self.ecuc = arxmlparse.derive_ecuc_from_com(
            self.model, ecu_name=ecu_name)
        self.bsw = arxml_bsw.stub_all_modules(ecu_name=ecu_name)
        self.bsw = arxml_bsw.sync_com_into_bsw(
            self.bsw, self.model, ecu_name=ecu_name)
        self.active_bsw = "Com"
        self.swc = arxmlparse.derive_swc_from_com(
            self.model, swc_name="%sApp" % ecu_name)
        self.write_intermediates()
        com_path = m.abs_role("com")
        self.path = com_path
        self.model.path = com_path
        self.dirty = False
        self._clear_history()
        self._notify()

    def open_project(self, root: str) -> None:
        m = arxml_project.load_manifest(root)
        self.manifest = m
        self.autosar_release = getattr(m, "autosar_release", None) or "R22-11"
        self.finding_acks = set(getattr(m, "finding_acks", None) or [])
        self._open_diag = ""
        com_path = m.abs_role("com")
        if com_path and os.path.isfile(com_path):
            self.model = arxmlparse.parse_arxml_model(com_path)
            self.path = com_path
        else:
            self.model = arxmlparse.empty_model()
            self.path = com_path or ""
        ecuc_path = m.abs_role("ecuc")
        if ecuc_path and os.path.isfile(ecuc_path):
            try:
                self.ecuc = arxmlparse.parse_ecuc_lite(ecuc_path)
            except Exception as exc:
                self.ecuc = arxmlparse.empty_ecuc()
                # Surfaced via validate / OUTPUT — do not fail open silently.
                self._open_diag = "ECUC parse failed: %s" % exc
        else:
            self.ecuc = arxmlparse.empty_ecuc()
        # Load per-module BSW ARXMLs
        self.bsw = {}
        ecu = m.ecu_name
        open_errs = []
        for name in arxml_bsw.module_names():
            m.ensure_module_entry(name)
            path = m.abs_module(name)
            if path and os.path.isfile(path):
                try:
                    self.bsw[name] = arxmlparse.parse_ecuc_lite(path)
                except Exception as exc:
                    self.bsw[name] = arxml_bsw.stub_module(name, ecu_name=ecu)
                    open_errs.append("%s: %s" % (name, exc))
            else:
                self.bsw[name] = arxml_bsw.stub_module(name, ecu_name=ecu)
        if open_errs:
            self._open_diag = (
                (getattr(self, "_open_diag", "") or "")
                + (" | " if getattr(self, "_open_diag", "") else "")
                + "BSW parse issues: " + "; ".join(open_errs[:5]))
        self.bsw = arxml_bsw.sync_com_into_bsw(
            self.bsw, self.model, ecu_name=ecu)
        self.active_bsw = "Com"
        swc_path = os.path.join(m.root, "work", "swc_lite.arxml")
        if os.path.isfile(swc_path):
            try:
                self.swc = arxmlparse.parse_swc_lite(swc_path)
            except Exception as exc:
                self.swc = arxmlparse.empty_swc()
                self._open_diag = (
                    (getattr(self, "_open_diag", "") or "")
                    + " | SWC parse failed: %s" % exc)
        else:
            self.swc = arxmlparse.empty_swc()
        self.editor_mode = "com"
        self.dirty = False
        self._clear_history()
        self._notify()

    def save_project(self) -> str:
        if not self.manifest or not self.manifest.root:
            raise ValueError("No project open")
        self.manifest.autosar_release = self.autosar_release or "R22-11"
        self.manifest.finding_acks = sorted(self.finding_acks)
        self.write_intermediates()
        arxml_project.save_manifest(self.manifest)
        self.dirty = False
        self._baseline = copy.deepcopy(self.model)
        self._baseline_dirty = False
        self._notify()
        return self.manifest.root

    def write_intermediates(self) -> dict:
        """Write work/com.arxml + work/ecuc_com.arxml (+ optional out files)."""
        if not self.manifest or not self.manifest.root:
            raise ValueError("No project open")
        arxml_project.ensure_layout(self.manifest.root)
        com_path = self.manifest.abs_role("com")
        ecuc_path = self.manifest.abs_role("ecuc")
        os.makedirs(os.path.dirname(com_path), exist_ok=True)
        os.makedirs(os.path.dirname(ecuc_path), exist_ok=True)
        with open(com_path, "w", encoding="utf-8") as f:
            f.write(arxmlparse.serialize_model(self.model))
        if not self.ecuc.modules:
            self.ecuc = arxmlparse.derive_ecuc_from_com(
                self.model, ecu_name=self.manifest.ecu_name)
        with open(ecuc_path, "w", encoding="utf-8") as f:
            f.write(arxmlparse.serialize_ecuc_lite(self.ecuc))
        # Per-module BSW ARXML under work/bsw/
        if not self.bsw:
            self.bsw = arxml_bsw.stub_all_modules(
                ecu_name=self.manifest.ecu_name)
            self.bsw = arxml_bsw.sync_com_into_bsw(
                self.bsw, self.model, ecu_name=self.manifest.ecu_name)
        written_mods = {}
        for name, model in self.bsw.items():
            self.manifest.ensure_module_entry(name)
            mp = self.manifest.abs_module(name)
            os.makedirs(os.path.dirname(mp), exist_ok=True)
            with open(mp, "w", encoding="utf-8") as f:
                f.write(arxmlparse.serialize_ecuc_lite(model))
            written_mods[name] = mp
        swc_path = os.path.join(self.manifest.root, "work", "swc_lite.arxml")
        if self.swc.components:
            with open(swc_path, "w", encoding="utf-8") as f:
                f.write(arxmlparse.serialize_swc_lite(self.swc))
        self.path = com_path
        self.model.path = com_path
        self.ecuc.path = ecuc_path
        self.manifest.derived_dirty = False
        arxml_project.save_manifest(self.manifest)
        return {
            "com": com_path, "ecuc": ecuc_path, "swc": swc_path,
            "bsw": written_mods,
        }

    def write_out_reports(self, findings: Optional[list] = None) -> dict:
        if not self.manifest or not self.manifest.root:
            raise ValueError("No project open")
        out_dir = os.path.join(self.manifest.root, "out")
        os.makedirs(out_dir, exist_ok=True)
        html_path = os.path.join(out_dir, "report.html")
        sarif_path = os.path.join(out_dir, "findings.sarif")
        rows = []
        for pdu in self.model.ipdus:
            for sig in pdu.signals:
                rows.append(
                    "<tr><td>%s</td><td>0x%X</td><td>%s</td><td>%d</td>"
                    "<td>%d</td></tr>" % (
                        pdu.name, pdu.can_id, sig.name,
                        sig.start_bit, sig.length))
        html = (
            "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<title>%s</title></head><body>"
            "<h1>%s — AUTOSAR Studio</h1>"
            "<p>Configuration intermediates only — no stack codegen.</p>"
            "<table border='1' cellpadding='4'><tr>"
            "<th>PDU</th><th>CAN</th><th>Signal</th><th>Start</th>"
            "<th>Len</th></tr>%s</table></body></html>"
        ) % (self.manifest.name, self.manifest.name, "\n".join(rows))
        with open(html_path, "w", encoding="utf-8") as f:
            f.write(html)
        if findings is None:
            findings = self.validate_all()
        with open(sarif_path, "w", encoding="utf-8") as f:
            json.dump(arxmlparse.findings_to_sarif(findings), f, indent=2)
        return {"html": html_path, "sarif": sarif_path}

    def _role_flags(self) -> dict:
        if not self.manifest:
            return {}
        flags = {}
        for key in arxml_project.ROLE_KEYS:
            p = self.manifest.abs_role(key)
            flags[key] = p
            flags["%s_exists" % key] = bool(p) and os.path.isfile(p)
        return flags

    def handoff_com_path(self) -> str:
        return arxml_project.resolve_com_path(self.manifest, self.path)

    def display_name(self) -> str:
        if self.has_project():
            return self.manifest.name
        if self.path:
            return os.path.basename(self.path)
        return "Untitled.arxml"

    def strip_label(self) -> str:
        if self.has_project():
            mode = self.editor_mode.upper()
            name = "%s [%s] %s" % (
                self.manifest.name, mode, self.manifest.root)
            return "%s%s" % (name, " *" if self.dirty else "")
        name = self.path or "(unsaved)"
        return "%s%s" % (name, " *" if self.dirty else "")

    def summary(self) -> dict:
        return {
            "pdu_count": len(self.model.ipdus),
            "signal_count": sum(len(p.signals) for p in self.model.ipdus),
            "package": self.model.package,
            "project": self.manifest.name if self.manifest else "",
            "ecuc_modules": len(self.ecuc.modules),
            "bsw_modules": len(self.bsw),
            "swc_ports": sum(len(c.ports) for c in self.swc.components),
            "editor_mode": self.editor_mode,
            "active_bsw": self.active_bsw,
        }
