# -*- coding: utf-8 -*-
"""CAN / CAN FD log format catalog and lightweight file probes."""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from typing import Optional

# Host CanFileIO engine formats (sin.files.convert).
ENGINE_FORMATS = ("blf", "asc", "csv", "pcap", "trc")

OPEN_FILTER = (
    "CAN logs (*.blf *.asc *.csv *.pcap *.pcapng *.trc);;"
    "Vector BLF (*.blf);;"
    "Vector ASC (*.asc);;"
    "CSV (*.csv);;"
    "PCAP (*.pcap *.pcapng);;"
    "TRC (*.trc);;"
    "All files (*)"
)

SAVE_FILTERS = {
    "blf": "Vector BLF (*.blf)",
    "asc": "Vector ASC (*.asc)",
    "csv": "CSV log (*.csv)",
    "pcap": "PCAP (*.pcap)",
    "trc": "TRC (*.trc)",
}


@dataclass(frozen=True)
class FormatInfo:
    key: str
    label: str
    extensions: tuple[str, ...]
    engine: bool
    can_fd: str  # "full" | "partial" | "planned"
    description: str
    typical_tools: str


FORMAT_CATALOG: tuple[FormatInfo, ...] = (
    FormatInfo(
        "blf", "BLF", (".blf",), True, "full",
        "Vector Binary Logging Format — compact, industry default for "
        "CANoe / CANalyzer / OpenBus record & playback.",
        "Vector CANoe, CANalyzer, OpenBus",
    ),
    FormatInfo(
        "asc", "ASC", (".asc",), True, "full",
        "Vector ASCII Logging Format — human-readable, Git-friendly, "
        "supports classic CAN and CAN FD markers.",
        "Vector tools, OpenBus, many converters",
    ),
    FormatInfo(
        "csv", "CSV", (".csv",), True, "partial",
        "Tabular export (timestamp, channel, ID, DLC, data). Ideal for "
        "spreadsheets and scripting; CAN FD length preserved when present.",
        "Excel, Python, OpenBus",
    ),
    FormatInfo(
        "pcap", "PCAP / PCAPNG", (".pcap", ".pcapng"), True, "full",
        "libpcap network capture with SocketCAN / CAN_FD link types — "
        "opens directly in Wireshark.",
        "Wireshark, Linux SocketCAN, OpenBus",
    ),
    FormatInfo(
        "trc", "TRC", (".trc",), True, "full",
        "PEAK / Vector-style text trace used by PCAN-View and related tools.",
        "PEAK PCAN-View, OpenBus",
    ),
    FormatInfo(
        "mf4", "MF4 / MDF4", (".mf4", ".mdf"), False, "planned",
        "ASAM MDF 4.x measurement containers used by OEM calibration stacks "
        "(INCA, CANape). Planned via asammdf bridge.",
        "ETAS INCA, Vector CANape, asammdf",
    ),
    FormatInfo(
        "log", "Generic LOG / TXT", (".log", ".txt"), False, "planned",
        "Vendor-specific ASCII dumps. Often re-exported to ASC/CSV first.",
        "OEM benches, custom scripts",
    ),
)


def by_key(key: str) -> Optional[FormatInfo]:
    k = (key or "").lower().strip()
    if k == "pcapng":
        k = "pcap"
    for info in FORMAT_CATALOG:
        if info.key == k:
            return info
    return None


def detect_format(path: str) -> Optional[str]:
    """Return engine format key from path suffix (or None)."""
    if not path:
        return None
    ext = os.path.splitext(path)[1].lower()
    if ext == ".pcapng":
        return "pcap"
    for info in FORMAT_CATALOG:
        if info.engine and ext in info.extensions:
            return info.key
    return None


def suggest_target(source: str, fmt: str) -> str:
    """Same directory, basename with new extension."""
    fmt = (fmt or "asc").lower()
    if fmt == "pcapng":
        fmt = "pcap"
    base, _ = os.path.splitext(source)
    ext = ".pcap" if fmt == "pcap" else (".%s" % fmt)
    return base + ext


def engine_label(fmt: str) -> str:
    info = by_key(fmt)
    return info.label if info else (fmt or "?").upper()


def probe_file(path: str) -> dict:
    """Lightweight local probe (no full frame decode)."""
    result = {
        "path": path,
        "exists": False,
        "size": 0,
        "format": None,
        "format_label": "",
        "engine": False,
        "can_fd_hint": "",
        "notes": [],
        "preview": "",
    }
    if not path or not os.path.isfile(path):
        result["notes"].append("File not found")
        return result

    result["exists"] = True
    result["size"] = os.path.getsize(path)
    fmt = detect_format(path)
    result["format"] = fmt
    info = by_key(fmt) if fmt else None
    if info:
        result["format_label"] = info.label
        result["engine"] = info.engine
        result["can_fd_hint"] = info.can_fd
    else:
        result["notes"].append("Unknown extension — pick a target format manually")

    # Magic / header sniff
    try:
        with open(path, "rb") as f:
            head = f.read(512)
    except OSError as e:
        result["notes"].append("Read error: %s" % e)
        return result

    if head.startswith(b"LOGG"):  # Vector BLF signature
        result["format"] = result["format"] or "blf"
        result["format_label"] = "BLF"
        result["engine"] = True
        result["notes"].append("BLF magic LOGG detected")
    elif head.startswith(b"\xd4\xc3\xb2\xa1") or head.startswith(b"\xa1\xb2\xc3\xd4"):
        result["format"] = result["format"] or "pcap"
        result["format_label"] = "PCAP"
        result["engine"] = True
        result["notes"].append("Classic PCAP magic detected")
    elif head.startswith(b"\x0a\x0d\x0d\x0a"):
        result["format"] = result["format"] or "pcap"
        result["format_label"] = "PCAPNG"
        result["engine"] = True
        result["notes"].append("PCAPNG block magic detected")

    # ASC / text heuristics
    try:
        text = head.decode("utf-8", errors="replace")
    except Exception:
        text = ""
    if text and (fmt in (None, "asc", "csv", "trc", "log") or not fmt):
        lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
        preview_lines = lines[:8]
        result["preview"] = "\n".join(preview_lines)
        joined = "\n".join(lines[:40]).lower()
        if "canfd" in joined or " can fd" in joined or "fd " in joined:
            result["can_fd_hint"] = "full"
            result["notes"].append("CAN FD markers found in header / early lines")
        if "date" in joined or "base hex" in joined or "timestamps" in joined:
            if not result["format"]:
                result["format"] = "asc"
                result["format_label"] = "ASC"
                result["engine"] = True
            result["notes"].append("Vector ASC-style header keywords")
        # ID hex pattern count
        id_hits = len(re.findall(r"\b[0-9a-fA-F]{3,8}\b", text))
        if id_hits >= 4 and result["format"] in (None, "asc", "csv", "trc"):
            result["notes"].append("Hex ID-like tokens present (%d in head)" % id_hits)

    if result["size"] == 0:
        result["notes"].append("Empty file")
    elif result["size"] > 512 * 1024 * 1024:
        result["notes"].append("Large file (>512 MB) — conversion may take a while")

    return result


def human_size(n: int) -> str:
    if n < 1024:
        return "%d B" % n
    if n < 1024 * 1024:
        return "%.1f KB" % (n / 1024.0)
    if n < 1024 * 1024 * 1024:
        return "%.1f MB" % (n / (1024.0 * 1024.0))
    return "%.2f GB" % (n / (1024.0 * 1024.0 * 1024.0))
