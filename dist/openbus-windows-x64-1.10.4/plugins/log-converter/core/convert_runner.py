# -*- coding: utf-8 -*-
"""Host-backed convert runner (sin.files.convert)."""

from __future__ import annotations

import os
import time
from typing import Callable, Optional

import sin

from formats import detect_format
from session import JobRecord, SharedSession


ProgressFn = Callable[[int], None]
DoneFn = Callable[[JobRecord], None]


def start_convert(
        session: SharedSession,
        source: str,
        target: str,
        fmt: str,
        *,
        batch: bool = False,
        on_progress: Optional[ProgressFn] = None,
        on_done: Optional[DoneFn] = None,
) -> Optional[int]:
    """Kick off async host convert. Returns jobId or None."""
    fmt = (fmt or "").lower().strip()
    if fmt == "pcapng":
        fmt = "pcap"
    if not source or not os.path.isfile(source):
        rec = JobRecord(
            source=source or "", target=target or "", fmt=fmt,
            ok=False, error="Source file missing",
            started=time.time(), finished=time.time(), batch=batch)
        session.add_job(rec)
        if on_done:
            on_done(rec)
        return None
    if not target:
        rec = JobRecord(
            source=source, target="", fmt=fmt, ok=False,
            error="Target path empty",
            started=time.time(), finished=time.time(), batch=batch)
        session.add_job(rec)
        if on_done:
            on_done(rec)
        return None

    src_fmt = detect_format(source)
    if src_fmt and src_fmt == fmt and os.path.normcase(
            os.path.abspath(source)) == os.path.normcase(
            os.path.abspath(target)):
        rec = JobRecord(
            source=source, target=target, fmt=fmt, ok=False,
            error="Source and target are the same path/format",
            started=time.time(), finished=time.time(), batch=batch)
        session.add_job(rec)
        if on_done:
            on_done(rec)
        return None

    out_dir = os.path.dirname(target)
    if out_dir and not os.path.isdir(out_dir):
        try:
            os.makedirs(out_dir, exist_ok=True)
        except OSError as e:
            rec = JobRecord(
                source=source, target=target, fmt=fmt, ok=False,
                error="Cannot create output dir: %s" % e,
                started=time.time(), finished=time.time(), batch=batch)
            session.add_job(rec)
            if on_done:
                on_done(rec)
            return None

    started = time.time()
    session.log(
        "Convert start %s → %s (%s)" % (
            os.path.basename(source), os.path.basename(target), fmt.upper()))

    def _progress(pct: int):
        if on_progress:
            try:
                on_progress(int(pct or 0))
            except Exception:
                pass

    def _finished(ok: bool, frame_count: int, error: str):
        rec = JobRecord(
            source=source, target=target, fmt=fmt,
            ok=bool(ok), frames=int(frame_count or 0),
            error=str(error or ""),
            started=started, finished=time.time(), batch=batch)
        session.set_active_job(None)
        session.add_job(rec)
        if ok:
            session.log(
                "Convert OK %s · %d frames · %.2fs" % (
                    os.path.basename(target), rec.frames, rec.duration_s),
                level="RX")
        else:
            session.log(
                "Convert FAIL %s · %s" % (
                    os.path.basename(source), rec.error or "unknown"),
                level="ERR")
        if on_done:
            try:
                on_done(rec)
            except Exception:
                pass

    job_id = sin.files.convert(
        source, target, fmt,
        on_progress=_progress, on_finished=_finished)
    session.set_active_job(job_id)
    return job_id


def cancel_active(session: SharedSession) -> bool:
    job_id = session.active_job()
    if job_id is None:
        return False
    try:
        sin.files.cancel(job_id)
        session.log("Convert cancel requested (job %s)" % job_id)
        return True
    except Exception as e:
        session.log("Cancel failed: %s" % e, level="ERR")
        return False
