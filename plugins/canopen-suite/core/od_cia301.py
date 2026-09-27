# -*- coding: utf-8 -*-
"""CiA 301 — thin wrapper over shared canopen_profiles catalog."""

from __future__ import annotations

from typing import List

from _shared.canopen_profiles import objects_for, search_profile
from .eds_parse import OdEntry

CIA301_OBJECTS: List[OdEntry] = objects_for("301")


def search_cia301(query: str = "") -> List[OdEntry]:
    return search_profile(query, "301")
