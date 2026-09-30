# -*- coding: utf-8 -*-
"""SharedSession next_hint / set_focus (no QApplication / bus)."""

from __future__ import annotations

import os
import sys
from unittest.mock import MagicMock

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

# Avoid importing sin / Qt stack: load session module with stubs.
sys.modules.setdefault("sin", MagicMock())


def test_next_hint_stages():
    from PyQt6.QtCore import QObject
    # SharedSession needs QObject parent path — import after sin mock
    import session as sess_mod

    class Fake(sess_mod.SharedSession):
        def __init__(self):
            QObject.__init__(self)
            self.tx_id = 0x7E0
            self.rx_id = 0x7E8
            self.func_id = 0x7DF
            self.functional = False
            self.session_name = "unknown"
            self.tester_present = False
            self.flashing = False
            self.show_isotp_frames = False
            self._id_listeners = []
            self._session_listeners = []
            self._log_fn = None
            self.focus_leaf = "services"
            self.focus_service = 0
            self.focus_did = 0
            self.focus_dtc = ""
            self._focus_listeners = []
            self._ids_touched = False
            self._did_request = False
            self._scan_hits = 0
            self._post_hint_stage = 0
            self._profile_path = ""

    s = Fake()
    lab, act, kw = s.next_hint()
    assert act == "uds.goto" and kw.get("page") == "session"

    s._ids_touched = True
    lab, act, kw = s.next_hint()
    assert act == "uds.extended"

    s.session_name = "extendedDiagnosticSession"
    lab, act, kw = s.next_hint()
    assert act == "uds.goto" and kw.get("page") == "services"

    s._did_request = True
    lab, act, kw = s.next_hint()
    assert "batch" in (kw or {}).values() or act == "uds.goto"

    seen = []
    s.on_focus(lambda leaf, svc, did, dtc: seen.append((leaf, svc, did, dtc)))
    s.set_focus("did", did=0xF190)
    assert s.focus_leaf == "did"
    assert s.focus_did == 0xF190
    assert seen and seen[-1][0] == "did" and seen[-1][2] == 0xF190
    print("PASS next_hint stages")


if __name__ == "__main__":
    test_next_hint_stages()
    print("PASS session hint")
