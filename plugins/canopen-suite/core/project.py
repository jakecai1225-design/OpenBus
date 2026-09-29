# -*- coding: utf-8 -*-
"""CANopen project layout — folder with canopen-project.json + main EDS.

Lightweight engineering container (AUTOSAR-style). One primary EDS per project.
"""

from __future__ import annotations

import json
import os
import time
import uuid
from dataclasses import dataclass, field
from typing import Optional

MANIFEST_NAME = "canopen-project.json"


@dataclass
class ProjectManifest:
    """In-memory canopen-project.json."""

    id: str = ""
    name: str = "Untitled"
    node_id: int = 1
    eds: str = "device.eds"  # primary relative to root
    eds_files: list = field(default_factory=list)  # extra EDS in project
    dcf: str = ""
    notes: str = ""
    created: float = 0.0
    updated: float = 0.0
    root: str = field(default="", repr=False)

    def __post_init__(self):
        if not self.id:
            self.id = uuid.uuid4().hex[:12]
        now = time.time()
        if not self.created:
            self.created = now
        if not self.updated:
            self.updated = now

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "name": self.name,
            "node_id": int(self.node_id),
            "eds": (self.eds or "device.eds").replace("\\", "/"),
            "eds_files": [
                str(x).replace("\\", "/") for x in (self.eds_files or []) if x
            ],
            "dcf": (self.dcf or "").replace("\\", "/"),
            "notes": self.notes or "",
            "created": float(self.created),
            "updated": float(self.updated),
        }

    @staticmethod
    def from_dict(d: dict, root: str = "") -> "ProjectManifest":
        files = d.get("eds_files") or []
        if not isinstance(files, list):
            files = []
        primary = str(d.get("eds") or "device.eds")
        # Ensure primary is listed
        norm = [str(x).replace("\\", "/") for x in files if x]
        if primary and primary not in norm:
            norm.insert(0, primary)
        return ProjectManifest(
            id=str(d.get("id") or ""),
            name=str(d.get("name") or "Untitled"),
            node_id=max(1, min(127, int(d.get("node_id") or 1))),
            eds=primary,
            eds_files=norm,
            dcf=str(d.get("dcf") or ""),
            notes=str(d.get("notes") or ""),
            created=float(d.get("created") or 0),
            updated=float(d.get("updated") or 0),
            root=root or "",
        )

    def abs_eds(self) -> str:
        return _abs(self.root, self.eds)

    def abs_dcf(self) -> str:
        return _abs(self.root, self.dcf) if self.dcf else ""


def _abs(root: str, rel: str) -> str:
    rel = (rel or "").replace("\\", "/")
    if not rel:
        return ""
    if os.path.isabs(rel):
        return os.path.normpath(rel)
    if not root:
        return os.path.normpath(rel.replace("/", os.sep))
    return os.path.normpath(os.path.join(root, rel.replace("/", os.sep)))


def project_json_path(root: str) -> str:
    return os.path.join(root, MANIFEST_NAME)


def is_project_dir(path: str) -> bool:
    return bool(path) and os.path.isfile(project_json_path(path))


def load_manifest(root: str) -> ProjectManifest:
    path = project_json_path(root)
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError("invalid project manifest")
    return ProjectManifest.from_dict(data, root=root)


def save_manifest(manifest: ProjectManifest) -> None:
    if not manifest.root:
        raise ValueError("manifest.root required")
    manifest.updated = time.time()
    path = project_json_path(manifest.root)
    os.makedirs(manifest.root, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(manifest.to_dict(), f, indent=2)
        f.write("\n")


def create_project(
        root: str, *, name: str = "Untitled", node_id: int = 1,
        eds_name: str = "device.eds") -> ProjectManifest:
    """Create folder + manifest. Does not write EDS bytes."""
    os.makedirs(root, exist_ok=True)
    m = ProjectManifest(
        name=name or os.path.basename(root) or "Untitled",
        node_id=node_id,
        eds=eds_name.replace("\\", "/"),
        root=root,
    )
    save_manifest(m)
    return m


def resolve_eds_path(manifest: ProjectManifest) -> Optional[str]:
    p = manifest.abs_eds()
    return p if p and os.path.isfile(p) else None


def list_project_eds(root: str, manifest: Optional[ProjectManifest] = None) -> list:
    """Return absolute EDS/DCF paths for a project (primary first, then folder).

    Combines manifest ``eds`` / ``eds_files`` with a shallow ``*.eds`` / ``*.dcf``
    scan of the project root (not recursive).
    """
    if not root or not os.path.isdir(root):
        return []
    ordered: list[str] = []
    seen: set[str] = set()

    def _add(path: str):
        if not path:
            return
        ap = os.path.normpath(path)
        key = ap.lower()
        if key in seen:
            return
        if not os.path.isfile(ap):
            return
        seen.add(key)
        ordered.append(ap)

    m = manifest
    if m is None and is_project_dir(root):
        try:
            m = load_manifest(root)
        except (OSError, ValueError, TypeError):
            m = None
    if m is not None:
        _add(m.abs_eds())
        for rel in m.eds_files or ():
            _add(_abs(root, str(rel)))
    try:
        for name in sorted(os.listdir(root)):
            low = name.lower()
            if low.endswith(".eds") or low.endswith(".dcf"):
                _add(os.path.join(root, name))
    except OSError:
        pass
    return ordered


def set_primary_eds(manifest: ProjectManifest, abs_path: str) -> None:
    """Mark ``abs_path`` as primary and keep it in ``eds_files``."""
    if not manifest.root or not abs_path:
        return
    try:
        rel = os.path.relpath(abs_path, manifest.root).replace("\\", "/")
    except ValueError:
        rel = os.path.basename(abs_path)
    manifest.eds = rel
    files = [
        str(x).replace("\\", "/") for x in (manifest.eds_files or []) if x]
    if rel not in files:
        files.insert(0, rel)
    manifest.eds_files = files
