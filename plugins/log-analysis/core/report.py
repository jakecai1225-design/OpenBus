# -*- coding: utf-8 -*-
"""Summaries for an offline CAN frame corpus."""

from __future__ import annotations


def summarize(frames) -> dict:
    if not frames:
        return {
            "count": 0,
            "ids": 0,
            "t0": 0.0,
            "t1": 0.0,
            "duration": 0.0,
            "fps": 0.0,
            "top": [],
        }
    ordered = sorted(frames, key=lambda fr: fr[0])
    counts = {}
    for fr in ordered:
        counts[fr[2]] = counts.get(fr[2], 0) + 1
    t0 = ordered[0][0]
    t1 = ordered[-1][0]
    duration = max(0.0, t1 - t0)
    fps = (len(ordered) / duration) if duration > 0 else float(len(ordered))
    top = sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:16]
    return {
        "count": len(ordered),
        "ids": len(counts),
        "t0": t0,
        "t1": t1,
        "duration": duration,
        "fps": fps,
        "top": top,
    }


def report_text(frames, names=None) -> str:
    info = summarize(frames)
    lines = ["Log Analysis report", ""]
    if names:
        lines.append("Files: %s" % ", ".join(names))
    lines.append("Frames: %d" % info["count"])
    lines.append("Unique IDs: %d" % info["ids"])
    if info["count"]:
        lines.append("Span: %.6f s .. %.6f s (%.3f s)" % (
            info["t0"], info["t1"], info["duration"]))
        lines.append("Average rate: %.1f frame/s" % info["fps"])
        lines.append("")
        lines.append("Top IDs")
        for cid, n in info["top"]:
            share = 100.0 * n / info["count"] if info["count"] else 0.0
            lines.append("  0x%X  %d  (%.1f%%)" % (cid, n, share))
    else:
        lines.append("No frames loaded. Use Open to load ASC or CSV.")
    return "\n".join(lines) + "\n"


def rate_bins(frames, bins: int = 24) -> list[int]:
    if not frames or bins < 1:
        return []
    t0 = min(fr[0] for fr in frames)
    t1 = max(fr[0] for fr in frames)
    span = t1 - t0
    counts = [0] * bins
    if span <= 0:
        counts[0] = len(frames)
        return counts
    for fr in frames:
        idx = int((fr[0] - t0) / span * bins)
        if idx >= bins:
            idx = bins - 1
        if idx < 0:
            idx = 0
        counts[idx] += 1
    return counts
