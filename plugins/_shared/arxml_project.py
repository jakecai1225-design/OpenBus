# -*- coding: utf-8 -*-
"""arxml_project — ARXML Studio project layout (DaVinci-style workspace).

Folder with project.json + role-tagged ARXML under input/ and work/.
BSW modules live in work/bsw/<Module>.arxml (one file per module).
Does NOT generate BSW/RTE source — intermediates are ARXML only.
"""

from __future__ import annotations

import json
import os
import uuid
from dataclasses import dataclass, field
from typing import Dict, List, Optional

ROLE_KEYS = ("system", "extract", "com", "ecuc")

DEFAULT_REL = {
    "system": "input/system.arxml",
    "extract": "input/extract.arxml",
    "com": "work/com.arxml",
    "ecuc": "work/ecuc_com.arxml",
}


@dataclass
class ProjectManifest:
    """In-memory project.json."""
    id: str = ""
    name: str = "UntitledEcu"
    ecu_name: str = "Ecu"
    schema_hint: str = "AUTOSAR_4-3-0"
    roles: Dict[str, str] = field(default_factory=dict)
    # module short-name -> relative path under project root
    modules: Dict[str, str] = field(default_factory=dict)
    derived_dirty: bool = False
    lint_before_save: bool = True
    autosar_release: str = "R22-11"
    finding_acks: List[str] = field(default_factory=list)
    root: str = ""

    def __post_init__(self):
        if not self.id:
            self.id = uuid.uuid4().hex[:12]
        if not self.roles:
            self.roles = dict(DEFAULT_REL)

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "ecu_name": self.ecu_name,
            "schema_hint": self.schema_hint,
            "roles": dict(self.roles),
            "modules": dict(self.modules),
            "derived_dirty": bool(self.derived_dirty),
            "lint_before_save": bool(self.lint_before_save),
            "autosarRelease": self.autosar_release or "R22-11",
            "findingAcks": list(self.finding_acks or []),
        }

    @staticmethod
    def from_dict(d: dict, root: str = "") -> "ProjectManifest":
        roles = dict(DEFAULT_REL)
        roles.update(d.get("roles") or {})
        acks = d.get("findingAcks") or d.get("finding_acks") or []
        return ProjectManifest(
            id=str(d.get("id") or ""),
            name=str(d.get("name") or "UntitledEcu"),
            ecu_name=str(d.get("ecu_name") or "Ecu"),
            schema_hint=str(d.get("schema_hint") or "AUTOSAR_4-3-0"),
            roles=roles,
            modules=dict(d.get("modules") or {}),
            derived_dirty=bool(d.get("derived_dirty")),
            lint_before_save=bool(
                d["lint_before_save"] if "lint_before_save" in d else True),
            autosar_release=str(
                d.get("autosarRelease") or d.get("autosar_release")
                or "R22-11"),
            finding_acks=[str(x) for x in acks if x],
            root=root or "",
        )

    def abs_role(self, role: str) -> str:
        rel = (self.roles.get(role) or DEFAULT_REL.get(role) or "").replace(
            "/", os.sep)
        if not rel:
            return ""
        if os.path.isabs(rel):
            return rel
        return os.path.normpath(os.path.join(self.root, rel)) if self.root else rel

    def abs_module(self, name: str) -> str:
        rel = (self.modules.get(name) or ("work/bsw/%s.arxml" % name)).replace(
            "/", os.sep)
        if os.path.isabs(rel):
            return rel
        return os.path.normpath(os.path.join(self.root, rel)) if self.root else rel

    def set_role_rel(self, role: str, rel_path: str) -> None:
        self.roles[role] = rel_path.replace("\\", "/")

    def ensure_module_entry(self, name: str) -> str:
        rel = "work/bsw/%s.arxml" % name
        self.modules[name] = rel
        return rel


def project_json_path(root: str) -> str:
    return os.path.join(root, "project.json")


def is_project_dir(path: str) -> bool:
    return bool(path) and os.path.isfile(project_json_path(path))


def ensure_layout(root: str) -> None:
    for sub in ("input", "work", "work/bsw", "out", "docs"):
        os.makedirs(os.path.join(root, sub.replace("/", os.sep)), exist_ok=True)


def create_project(
        root: str, name: str = "UntitledEcu",
        ecu_name: str = "Ecu") -> ProjectManifest:
    """Create folder layout + project.json. Does not write ARXML bodies."""
    root = os.path.abspath(root)
    ensure_layout(root)
    m = ProjectManifest(name=name, ecu_name=ecu_name, root=root)
    try:
        from _shared import arxml_bsw
        for mod in arxml_bsw.module_names():
            m.ensure_module_entry(mod)
    except ImportError:
        pass
    save_manifest(m)
    return m


def load_manifest(root: str) -> ProjectManifest:
    root = os.path.abspath(root)
    path = project_json_path(root)
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    m = ProjectManifest.from_dict(data, root=root)
    # Backfill module paths for older projects
    try:
        from _shared import arxml_bsw
        for mod in arxml_bsw.module_names():
            if mod not in m.modules:
                m.ensure_module_entry(mod)
    except ImportError:
        pass
    return m


def save_manifest(manifest: ProjectManifest) -> str:
    if not manifest.root:
        raise ValueError("Project root not set")
    ensure_layout(manifest.root)
    path = project_json_path(manifest.root)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(manifest.to_dict(), f, indent=2)
        f.write("\n")
    return path


def resolve_com_path(manifest: Optional[ProjectManifest], fallback: str = "") -> str:
    """Prefer project work/com.arxml for autosar-suite handoff."""
    if manifest and manifest.root:
        p = manifest.abs_role("com")
        if p and os.path.isfile(p):
            return p
        if p:
            return p
    return fallback or ""


def list_role_status(manifest: ProjectManifest) -> List[dict]:
    rows = []
    for key in ROLE_KEYS:
        abs_path = manifest.abs_role(key)
        rows.append({
            "role": key,
            "rel": manifest.roles.get(key, DEFAULT_REL.get(key, "")),
            "path": abs_path,
            "exists": bool(abs_path) and os.path.isfile(abs_path),
        })
    return rows


def list_module_status(manifest: ProjectManifest) -> List[dict]:
    rows = []
    names = list(manifest.modules.keys())
    if not names:
        try:
            from _shared import arxml_bsw
            names = arxml_bsw.module_names()
        except ImportError:
            names = []
    for name in names:
        abs_path = manifest.abs_module(name)
        rows.append({
            "module": name,
            "rel": manifest.modules.get(name, "work/bsw/%s.arxml" % name),
            "path": abs_path,
            "exists": bool(abs_path) and os.path.isfile(abs_path),
        })
    return rows
