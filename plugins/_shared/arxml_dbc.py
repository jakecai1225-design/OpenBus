# -*- coding: utf-8 -*-
"""arxml_dbc — Import DBC into ARXML COM model (no codegen)."""

from __future__ import annotations

from _shared import arxmlparse, dbcparse


def import_dbc_to_model(path: str) -> arxmlparse.ArxmlModel:
    """Parse a DBC file into an ArxmlModel (I-PDUs + signals)."""
    db = dbcparse.parse_file(path)
    model = arxmlparse.ArxmlModel(package="FromDbc", path="")
    ipdus = []
    msgs = getattr(db, "messages", None) or {}
    if isinstance(msgs, dict):
        msg_list = list(msgs.values())
    else:
        msg_list = list(msgs)
    for msg in msg_list:
        sigs = []
        for s in msg.signals or []:
            endian = (
                arxmlparse.ENDIAN_INTEL if getattr(s, "little_endian", True)
                else arxmlparse.ENDIAN_MOTOROLA)
            sigs.append(arxmlparse.Signal(
                name=str(s.name or "Signal"),
                start_bit=int(s.start_bit or 0),
                length=max(1, int(s.bit_length or 1)),
                endian=endian,
                factor=float(s.factor if s.factor is not None else 1.0),
                offset=float(s.offset or 0.0),
                unit=str(s.unit or ""),
            ))
        can_id = int(msg.can_id or 0) & 0x1FFFFFFF
        ipdus.append(arxmlparse.Ipdu(
            name=str(msg.name or "Msg"),
            can_id=can_id,
            dlc=max(0, int(msg.dlc if msg.dlc is not None else 8)),
            signals=sigs,
        ))
    model.ipdus = ipdus
    model.notes = "Imported from DBC: %s" % path
    return model
