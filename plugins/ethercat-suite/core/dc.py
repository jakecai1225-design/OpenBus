# -*- coding: utf-8 -*-
"""Distributed clocks helpers: cable delay and frame shift on a 100 Mbit link."""

from __future__ import annotations


NS_PER_M = 5.0  # ~5 ns/m copper, one-way rule of thumb
NS_PER_BIT_100M = 10.0


def cable_delay_ns(length_m: float, ns_per_m: float = NS_PER_M) -> float:
    return max(0.0, float(length_m)) * float(ns_per_m)


def frame_time_ns(frame_bytes: int, line_mbps: float = 100.0) -> float:
    if line_mbps <= 0:
        return 0.0
    ns_per_bit = 1000.0 / float(line_mbps)
    return max(0, int(frame_bytes)) * 8 * ns_per_bit


def system_delay_ns(slave_lengths_m: list, ns_per_m: float = NS_PER_M) -> list:
    """One-way delay to each slave, summing cable segments in order."""
    acc = 0.0
    out = []
    for length in slave_lengths_m:
        acc += cable_delay_ns(length, ns_per_m)
        out.append(acc)
    return out


def shift_ns(cycle_ns: float, frame_bytes: int, delay_ns: float) -> float:
    """SYNC0 shift suggestion: frame time + propagation, wrapped into the cycle."""
    shift = frame_time_ns(frame_bytes) + float(delay_ns)
    cycle = float(cycle_ns)
    if cycle > 0:
        shift = shift % cycle
    return shift
