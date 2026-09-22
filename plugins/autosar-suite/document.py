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
        self._suspend_hist: bool = False

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
                pass

    def _clear_history(self) -> None:
        self._undo.clear()
        self._redo.clear()
        self._baseline = copy.deepcopy(self.model)
        self._baseline_dirty = self.dirty

    def _push_undo(self, snap: arxmlparse.ArxmlModel, dirty: bool) -> None:
        self._undo.append((snap, dirty))
        if len(self._undo) > _MAX_HISTORY:
            self._undo.pop(0)

    def can_undo(self) -> bool:
        return bool(self._undo)

    def can_redo(self) -> bool:
        return bool(self._redo)

    def undo(self) -> bool:
        if not self._undo:
            return False
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

    def redo(self) -> bool:
        if not self._redo:
            return False
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
        if name:
            self.active_bsw = name
            if name not in self.bsw:
                ecu = self.manifest.ecu_name if self.manifest else "Ecu"
                self.bsw[name] = arxml_bsw.stub_module(name, ecu_name=ecu)
            self._notify()

    def apply_bsw_module(self, name: str, model: arxmlparse.EcucModel) -> None:
        self.bsw[name] = model
        self.dirty = True
        self._notify()

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
        if not findings:
            findings.append({
                "level": "info", "severity": "info", "rule": "ok",
                "message": "No issues", "fix": "", "artifact": "project",
                "pdu": "", "signal": "", "location": "project",
            })
        return findings

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
            except Exception:
                self.ecuc = arxmlparse.empty_ecuc()
        else:
            self.ecuc = arxmlparse.empty_ecuc()
        # Load per-module BSW ARXMLs
        self.bsw = {}
        ecu = m.ecu_name
        for name in arxml_bsw.module_names():
            m.ensure_module_entry(name)
            path = m.abs_module(name)
            if path and os.path.isfile(path):
                try:
                    self.bsw[name] = arxmlparse.parse_ecuc_lite(path)
                except Exception:
                    self.bsw[name] = arxml_bsw.stub_module(name, ecu_name=ecu)
            else:
                self.bsw[name] = arxml_bsw.stub_module(name, ecu_name=ecu)
        self.bsw = arxml_bsw.sync_com_into_bsw(
            self.bsw, self.model, ecu_name=ecu)
        self.active_bsw = "Com"
        swc_path = os.path.join(m.root, "work", "swc_lite.arxml")
        if os.path.isfile(swc_path):
            try:
                self.swc = arxmlparse.parse_swc_lite(swc_path)
            except Exception:
                self.swc = arxmlparse.empty_swc()
        else:
            self.swc = arxmlparse.empty_swc()
        self.editor_mode = "com"
        self.dirty = False
        self._clear_history()
        self._notify()

    def save_project(self) -> str:
        if not self.manifest or not self.manifest.root:
            raise ValueError("No project open")
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
