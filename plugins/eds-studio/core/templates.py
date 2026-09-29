# -*- coding: utf-8 -*-
"""Beginner device templates — re-export shared catalog (EDS Studio)."""

from __future__ import annotations

from _shared.canopen_profiles.templates import (  # noqa: F401
    STARTER_TEMPLATES,
    assemble_document,
    build_template,
    merge_entries,
    tpl_analog_io,
    tpl_battery,
    tpl_digital_io,
    tpl_encoder,
    tpl_gateway_stub,
    tpl_generic_node,
    tpl_measuring,
    tpl_servo_drive,
)

__all__ = [
    "STARTER_TEMPLATES",
    "assemble_document",
    "build_template",
    "merge_entries",
    "tpl_analog_io",
    "tpl_battery",
    "tpl_digital_io",
    "tpl_encoder",
    "tpl_gateway_stub",
    "tpl_generic_node",
    "tpl_measuring",
    "tpl_servo_drive",
]
