# -*- coding: utf-8 -*-
"""ASC/CSV log readers and writers (offline; host canfileio is C++)."""

from __future__ import annotations

import csv
import re
import time

# Vector-ish ASC: 0.123456 1  123 Rx   d 8 01 02 ...
_ASC_RE = re.compile(
    r"^\s*([\d.]+)\s+(\d+)\s+([0-9A-Fa-fxX]+)\s+(Rx|Tx)\s+d\s+(\d+)\s*(.*)$",
    re.IGNORECASE,
)


def read_asc(path):
    frames = []
    warnings = 0
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                m = _ASC_RE.match(line)
                if m:
                    ts = float(m.group(1))
                    ch = int(m.group(2))
                    id_s = m.group(3).rstrip("xX")
                    cid = int(id_s, 16)
                    direction = m.group(4).upper()
                    dlc = int(m.group(5))
                    data_hex = m.group(6).strip()
                    data = b""
                    if data_hex:
                        try:
                            data = bytes.fromhex(
                                data_hex.replace(" ", "")[: dlc * 2])
                        except ValueError:
                            warnings += 1
                            continue
                    frames.append((ts, ch, cid, direction, dlc, data))
                elif line.strip() and not line.lower().startswith(
                        ("date", "base", "no", "//", ";")):
                    warnings += 1
    except OSError as e:
        return None, str(e)
    return frames, warnings


def read_csv_log(path):
    frames = []
    try:
        with open(path, "r", encoding="utf-8-sig", errors="replace",
                  newline="") as f:
            reader = csv.reader(f)
            next(reader, None)
            for row in reader:
                if len(row) < 4:
                    continue
                try:
                    ts = float(row[0])
                    cid = int(row[1], 0)
                    direction = row[2] if row[2] in ("Rx", "Tx") else "Rx"
                    data_hex = row[3].replace(" ", "")
                    data = bytes.fromhex(data_hex) if data_hex else b""
                    frames.append((ts, 1, cid, direction, len(data), data))
                except (ValueError, IndexError):
                    continue
    except OSError as e:
        return None, str(e)
    return frames, 0


def write_asc(path, frames):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("date %s\n" % time.strftime("%a %b %d %H:%M:%S %Y"))
        f.write("base hex  timestamps absolute\n")
        f.write("no internal events logged\n")
        for ts, ch, cid, direction, dlc, data in frames:
            hexs = " ".join("%02X" % b for b in data)
            f.write("%.6f %d  %X %s d %d %s\n"
                    % (ts, ch, cid, direction, dlc, hexs))


def write_csv_log(path, frames):
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(["timestamp", "id", "dir", "data"])
        for ts, ch, cid, direction, dlc, data in frames:
            w.writerow(["%.6f" % ts, "0x%X" % cid, direction, data.hex()])


def load_any(path):
    lower = path.lower()
    if lower.endswith(".csv"):
        frames, err = read_csv_log(path)
        if frames is not None:
            return frames, err
        return read_asc(path)
    frames, err = read_asc(path)
    if frames is not None and frames:
        return frames, err
    if frames is not None and not frames and lower.endswith((".log", ".txt")):
        return read_csv_log(path)
    if frames is None:
        return read_csv_log(path)
    return frames, err
