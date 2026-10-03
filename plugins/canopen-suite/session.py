# -*- coding: utf-8 -*-
"""SharedSession — node, EDS OD, SDO client, NMT helpers for CANopen Suite."""

from __future__ import annotations

import copy
import os
from typing import Callable, List, Optional

import sin
from PyQt6.QtCore import QObject, QTimer

from core.eds_parse import (
    EdsDocument,
    OdEntry,
    export_eds_text,
    parse_eds_file_document,
)
from core.network_health import NetworkHealth
from core.sdo_client import SdoClient


NMT_START = 0x01
NMT_STOP = 0x02
NMT_PREOP = 0x80
NMT_RESET_NODE = 0x81
NMT_RESET_COMM = 0x82

NMT_CMD_NAMES = {
    NMT_START: "Start",
    NMT_STOP: "Stop",
    NMT_PREOP: "Enter Pre-op",
    NMT_RESET_NODE: "Reset Node",
    NMT_RESET_COMM: "Reset Communication",
}


class SharedSession(QObject):
    """Suite-wide CANopen state: selected node, EDS path/entries, SDO client."""

    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.node_id = 1
        self.eds_path = ""
        self.od_entries: List[OdEntry] = []
        self.draft_entries: List[OdEntry] = []  # EDS editor working copy
        self.eds_file_info: dict = {}
        self.eds_device_info: dict = {}
        self.eds_device_commissioning: dict = {}
        self.eds_other_meta: dict = {}
        self.live_values: dict = {}  # (index, subindex) -> "0x.." display
        self.debug_mismatch_count = 0
        self.bitrate_hint = "(bus bitrate from host)"
        self.eds_dirty = False
        self._clean_fingerprint = ""
        # Engineering project (folder + canopen-project.json); optional.
        self.project_root = ""
        self.project_name = ""
        # Cross-page OD selection carry (Interop Unity).
        self.focus_index = 0
        self.focus_subindex = 0
        self._validated_ok = False  # set True after a clean Validate run
        # After Apply → OD: 0=Trace, 1=NMT Start, 2=Codegen (Context Next).
        self._post_od_stage = 0

        self._node_listeners: list[Callable[[], None]] = []
        self._od_listeners: list[Callable[[], None]] = []
        self._project_listeners: list[Callable[[], None]] = []
        self._focus_listeners: list[Callable[[], None]] = []
        self._log_fn: Optional[Callable] = None
        self._frame_listeners: list[Callable] = []
        self._health_listeners: list[Callable[[], None]] = []

        def _send(can_id, data):
            sin.frames.send(can_id, data)

        self.sdo = SdoClient(_send, parent=self, timeout_ms=800)
        self.sdo.set_node(self.node_id)
        self.sdo.on_log = self._sdo_log
        self.health = NetworkHealth(hb_timeout_s=2.0, emcy_limit=64)
        self._health_timer = QTimer(self)
        self._health_timer.setInterval(500)
        self._health_timer.timeout.connect(self._poll_health)
        self._health_timer.start()

    def set_log_fn(self, fn: Callable) -> None:
        self._log_fn = fn

    def log(self, direction, can_id, pdu, note, color=None):
        if self._log_fn:
            self._log_fn(direction, can_id, pdu, note, color)

    def _sdo_log(self, direction, can_id, pdu, note):
        self.log(direction, can_id, pdu, note)

    def on_node_changed(self, cb: Callable[[], None]) -> None:
        self._node_listeners.append(cb)

    def on_od_changed(self, cb: Callable[[], None]) -> None:
        self._od_listeners.append(cb)

    def on_project_changed(self, cb: Callable[[], None]) -> None:
        self._project_listeners.append(cb)

    def on_focus_changed(self, cb: Callable[[], None]) -> None:
        self._focus_listeners.append(cb)

    def on_bus_frame(self, cb: Callable) -> None:
        """Extra listeners for Monitor / Network (after SDO handling)."""
        self._frame_listeners.append(cb)

    def on_health_changed(self, cb: Callable[[], None]) -> None:
        self._health_listeners.append(cb)

    def _notify_health(self) -> None:
        for cb in list(self._health_listeners):
            try:
                cb()
            except Exception:
                pass

    def _poll_health(self) -> None:
        newly = self.health.poll_timeouts()
        for nid in newly:
            self.log(
                "ERR", "-", b"",
                "Heartbeat timeout node %d (>%.1fs)" % (
                    nid, self.health.hb_timeout_s))
        if newly:
            self._notify_health()

    def health_summary(self) -> str:
        """One-line status for Live chrome (selected node + EMCY count)."""
        nh = self.health.nodes.get(self.node_id)
        emcy_n = len(self.health.emcy)
        if nh is None:
            base = "Node %d · no HB yet" % self.node_id
        else:
            age = nh.age_s()
            age_s = ("%.1fs" % age) if age < 1e8 else "—"
            flag = " LOST" if nh.missed else ""
            base = "Node %d · %s · HB %s%s" % (
                self.node_id, nh.nmt_label, age_s, flag)
        if emcy_n:
            base += " · EMCY %d" % emcy_n
        return base

    def _notify_node(self) -> None:
        for cb in list(self._node_listeners):
            try:
                cb()
            except Exception:
                pass

    def _notify_od(self) -> None:
        for cb in list(self._od_listeners):
            try:
                cb()
            except Exception:
                pass

    def _notify_project(self) -> None:
        for cb in list(self._project_listeners):
            try:
                cb()
            except Exception:
                pass

    def _notify_focus(self) -> None:
        for cb in list(self._focus_listeners):
            try:
                cb()
            except Exception:
                pass

    def set_focus(self, index: int, subindex: int = 0) -> None:
        """Remember OD selection for cross-page carry (Dictionary / Live / PDO)."""
        idx = max(0, int(index) & 0xFFFF)
        sub = max(0, min(255, int(subindex)))
        if self.focus_index == idx and self.focus_subindex == sub:
            return
        self.focus_index = idx
        self.focus_subindex = sub
        self._notify_focus()

    def clear_focus(self) -> None:
        self.focus_index = 0
        self.focus_subindex = 0
        self._notify_focus()

    def mark_validated(self, ok: bool) -> None:
        self._validated_ok = bool(ok)

    def advance_next_hint(self) -> None:
        """Advance post-Apply Context Next stage (Trace → NMT → Codegen)."""
        if self.od_entries:
            self._post_od_stage = min(2, int(self._post_od_stage or 0) + 1)

    def reset_post_od_hint(self) -> None:
        self._post_od_stage = 0

    def _has_pdo_maps(self) -> bool:
        for e in self.draft_entries or ():
            if 0x1600 <= e.index <= 0x17FF or 0x1A00 <= e.index <= 0x1BFF:
                if e.subindex >= 1:
                    raw = (e.parameter_value or e.default_value or "").strip()
                    if raw and raw not in ("0", "0x0", "0x00", "0x00000000"):
                        try:
                            if int(str(raw).replace("0x", ""), 16) != 0:
                                return True
                        except ValueError:
                            return True
        return False

    def next_hint(self) -> tuple:
        """Return ``(label, action_name, kwargs)`` for the Context Next button.

        Deterministic golden-path heuristic (Interop Unity IU-1).
        """
        draft = self.draft_entries or []
        if not draft and not self.eds_path:
            return ("New EDS…", "eds.new", {})
        self.refresh_dirty()
        if self.eds_dirty or (draft and not self.eds_path):
            return ("Save", "eds.save", {})
        if draft and not self._has_pdo_maps():
            return ("Map PDOs", "view.pdo_map", {})
        if draft and not self._validated_ok:
            return ("Validate", "view.check", {})
        if draft and not self.od_entries:
            return ("Apply → Live OD", "eds.apply_od", {})
        if self.od_entries:
            # Do not stall on Scan — Scan stays in Live sidebar.
            stage = int(self._post_od_stage or 0)
            if stage <= 0:
                return ("Open Trace", "view.trace", {})
            if stage == 1:
                return ("NMT Start", "network.nmt", {"cmd": NMT_START})
            return ("Codegen", "eds.codegen", {})
        return ("Open Dictionary", "view.eds", {})

    def has_project(self) -> bool:
        return bool(self.project_root)

    def set_project(self, root: str, name: str = "") -> None:
        self.project_root = root or ""
        self.project_name = name or (
            os.path.basename(root) if root else "")
        self._notify_project()

    def clear_project(self) -> None:
        self.project_root = ""
        self.project_name = ""
        self._notify_project()

    def set_node_id(self, node_id: int) -> None:
        self.node_id = max(1, min(127, int(node_id)))
        self.sdo.set_node(self.node_id)
        self._notify_node()
        self.log("RX", "-", b"", "Node-ID set to %d" % self.node_id)

    def _fingerprint(self) -> str:
        """Cheap dirty check: entry count + key fields + meta sizes."""
        parts = [
            str(len(self.draft_entries)),
            str(len(self.eds_file_info)),
            str(len(self.eds_device_info)),
            str(len(self.eds_device_commissioning)),
        ]
        for e in self.draft_entries[:64]:
            parts.append(
                "%04X:%02X:%s:%s"
                % (e.index, e.subindex, e.name or "", e.default_value or ""))
        if len(self.draft_entries) > 64:
            parts.append("…%d" % len(self.draft_entries))
        return "|".join(parts)

    def _mark_clean(self) -> None:
        self._clean_fingerprint = self._fingerprint()
        self.eds_dirty = False

    def mark_dirty(self) -> None:
        self.eds_dirty = self._fingerprint() != self._clean_fingerprint

    def refresh_dirty(self) -> bool:
        self.eds_dirty = self._fingerprint() != self._clean_fingerprint
        return self.eds_dirty

    def new_from_document(self, doc: EdsDocument, *, note: str = "") -> None:
        """Replace draft with *doc*; clear path (unsaved untitled)."""
        self.eds_path = ""
        self.od_entries = []
        self.reset_post_od_hint()
        self.draft_entries = copy.deepcopy(doc.entries)
        self.eds_file_info = dict(doc.file_info)
        self.eds_device_info = dict(doc.device_info)
        self.eds_device_commissioning = dict(doc.device_commissioning)
        self.eds_other_meta = {
            k: dict(v) for k, v in (doc.other_meta or {}).items()}
        self.live_values = {}
        self.debug_mismatch_count = 0
        self._mark_clean()
        # New document is clean relative to itself; first edit dirties.
        self._notify_od()
        msg = note or "New EDS (%d objects)" % len(self.draft_entries)
        self.log("RX", "-", b"", msg)

    def new_empty(self) -> None:
        from _shared.canopen_profiles import assemble_document
        doc = assemble_document(
            base="301", packs=("Identity", "SDO server", "Heartbeat producer"),
            product="Untitled", description="Empty CiA 301 shell")
        self.new_from_document(doc, note="New empty EDS")

    def new_from_template(self, template_id: str) -> None:
        from _shared.canopen_profiles import build_template
        doc = build_template(template_id)
        self.new_from_document(
            doc, note="New from starter '%s' (%d objects)"
            % (template_id, len(doc.entries)))

    def new_from_profile(
            self, *, base: str = "301", device: str | None = None,
            packs: list | tuple = (), product: str = "New Device") -> None:
        from _shared.canopen_profiles import assemble_document
        doc = assemble_document(
            base=base, device=device, packs=packs, product=product)
        self.new_from_document(
            doc, note="New from profile CiA %s%s (%d objects)"
            % (base, ("+%s" % device) if device else "", len(doc.entries)))

    def load_eds(self, path: str) -> bool:
        try:
            doc = parse_eds_file_document(path)
        except OSError as e:
            self.log("ERR", "-", b"", "EDS open failed: %s" % e)
            return False
        self.eds_path = path
        self.od_entries = doc.entries
        self.draft_entries = copy.deepcopy(doc.entries)
        self.eds_file_info = dict(doc.file_info)
        self.eds_device_info = dict(doc.device_info)
        self.eds_device_commissioning = dict(doc.device_commissioning)
        self.eds_other_meta = {k: dict(v) for k, v in doc.other_meta.items()}
        self.live_values = {}
        self.debug_mismatch_count = 0
        # Prefer commissioned NodeID when present
        nid = (doc.device_commissioning.get("NodeID")
               or doc.device_commissioning.get("NodeId") or "")
        if nid:
            try:
                self.set_node_id(int(str(nid), 0))
            except ValueError:
                pass
        self._notify_od()
        self._mark_clean()
        self.log(
            "RX", "-", b"",
            "Loaded EDS: %s (%d objects)" % (path, len(doc.entries)))
        return True

    def clear_eds(self) -> None:
        self.eds_path = ""
        self.od_entries = []
        self.draft_entries = []
        self.eds_file_info = {}
        self.eds_device_info = {}
        self.eds_device_commissioning = {}
        self.eds_other_meta = {}
        self.live_values = {}
        self.debug_mismatch_count = 0
        self._mark_clean()
        self._notify_od()
        self.log("RX", "-", b"", "EDS cleared")

    def to_document(self, *, use_draft: bool = True) -> EdsDocument:
        """Build EdsDocument from session (for validate / codegen)."""
        doc = EdsDocument()
        doc.entries = list(
            self.draft_entries if use_draft else self.od_entries)
        doc.file_info = dict(self.eds_file_info)
        doc.device_info = dict(self.eds_device_info)
        doc.device_commissioning = dict(self.eds_device_commissioning)
        doc.other_meta = {
            k: dict(v) for k, v in (self.eds_other_meta or {}).items()}
        doc.path = self.eds_path or ""
        if self.eds_path.lower().endswith(".dcf"):
            doc.is_dcf = True
        return doc

    def save_eds(self, path: str | None = None) -> bool:
        """Serialize draft (+ meta / commissioning) to path."""
        out = path or self.eds_path
        if not out:
            return False
        text = export_eds_text(
            self.draft_entries,
            file_name=os.path.basename(out),
            file_info=self.eds_file_info,
            device_info=self.eds_device_info,
            other_meta=self.eds_other_meta,
            device_commissioning=self.eds_device_commissioning or None,
            as_dcf=out.lower().endswith(".dcf"),
        )
        try:
            with open(out, "w", encoding="utf-8") as f:
                f.write(text)
        except OSError as e:
            self.log("ERR", "-", b"", "EDS save failed: %s" % e)
            return False
        self.eds_path = out
        self._mark_clean()
        self.log("RX", "-", b"", "Saved EDS %s" % out)
        return True

    def set_live_value(self, index: int, subindex: int, display: str,
                       *, notify: bool = True) -> None:
        self.live_values[(index, subindex)] = display
        self._recompute_mismatches()
        if notify:
            self._notify_od()

    def _recompute_mismatches(self) -> None:
        n = 0
        for e in (self.draft_entries or self.od_entries):
            eds_val = (e.effective_value() or "").strip().lower()
            live = (self.live_values.get((e.index, e.subindex)) or "").strip().lower()
            if eds_val and live and eds_val != live:
                # normalize 0x forms
                try:
                    if int(eds_val, 0) == int(live, 0):
                        continue
                except ValueError:
                    pass
                n += 1
        self.debug_mismatch_count = n

    def set_draft_from_library(
            self, entries: List[OdEntry], merge: bool = True,
            overwrite: bool = True) -> dict:
        """Insert library stubs into EDS editor draft.

        Returns counts: added / updated / skipped.
        """
        stats = {"added": 0, "updated": 0, "skipped": 0}
        if not merge:
            self.draft_entries = copy.deepcopy(entries)
            stats["added"] = len(entries)
        else:
            by_key = {(e.index, e.subindex): e for e in self.draft_entries}
            for e in entries:
                key = (e.index, e.subindex)
                if key in by_key:
                    if overwrite:
                        by_key[key] = copy.deepcopy(e)
                        stats["updated"] += 1
                    else:
                        stats["skipped"] += 1
                else:
                    by_key[key] = copy.deepcopy(e)
                    stats["added"] += 1
            self.draft_entries = sorted(
                by_key.values(), key=lambda x: (x.index, x.subindex))
        self.refresh_dirty()
        self._notify_od()
        return stats

    def sync_od_from_draft(self) -> None:
        self.od_entries = copy.deepcopy(self.draft_entries)
        self.reset_post_od_hint()
        self._notify_od()

    def send_nmt(self, command: int, node_id: Optional[int] = None) -> None:
        nid = self.node_id if node_id is None else int(node_id)
        # 0 = all nodes
        nid = max(0, min(127, nid))
        pdu = bytes([command & 0xFF, nid & 0xFF])
        sin.frames.send(0x000, pdu)
        name = NMT_CMD_NAMES.get(command, "0x%02X" % command)
        self.log("TX", 0x000, pdu, "NMT %s → node %d" % (name, nid))

    def send_raw(self, can_id: int, data: bytes, note: str = "") -> None:
        """Transmit an arbitrary CANopen frame (interactive Trace generator)."""
        cid = int(can_id) & 0x7FF
        pdu = bytes(data or b"")[:8]
        sin.frames.send(cid, pdu)
        self.log("TX", cid, pdu, note or ("TX 0x%03X" % cid))

    def send_sync(self, counter: Optional[int] = None) -> None:
        pdu = bytes([int(counter) & 0xFF]) if counter is not None else b""
        self.send_raw(0x080, pdu, "SYNC" + (
            " counter=%d" % counter if counter is not None else ""))

    def on_frame(self, frame) -> None:
        data = bytes(frame.data) if frame.data else b""
        if data:
            self.sdo.on_frame(frame.id, data)
            note = self.health.on_frame(frame.id, data)
            if note:
                if note.startswith("EMCY"):
                    self.log("ERR", frame.id, data, note)
                self._notify_health()
        for cb in list(self._frame_listeners):
            try:
                cb(frame)
            except Exception:
                pass

    def sdo_upload(self, index: int, subindex: int = 0, on_done=None) -> bool:
        return self.sdo.upload(index, subindex, on_done=on_done)

    def sdo_download(
        self, index: int, subindex: int, value: int, size: int = 4, on_done=None
    ) -> bool:
        return self.sdo.download(index, subindex, value, size, on_done=on_done)

    def sdo_download_bytes(
        self, index: int, subindex: int, payload: bytes, on_done=None
    ) -> bool:
        return self.sdo.download_bytes(
            index, subindex, payload, on_done=on_done)

    def shutdown(self) -> None:
        self._health_timer.stop()
        self.sdo.cancel()
