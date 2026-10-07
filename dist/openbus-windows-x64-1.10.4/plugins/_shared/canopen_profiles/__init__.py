# -*- coding: utf-8 -*-
"""Shared CiA profile / pack catalog for EDS Studio and CANopen Suite.

Benchmarks (Industry Top2): Vector CANeds (profile insert / CODB-style
library) and emotas DeviceExplorer (DCF ParameterValue + object browser).

Objects are practical stubs of mandatory and commonly used indexes —
enough to scaffold a device EDS, not a licensed full CiA-CODB dump.
"""

from __future__ import annotations

from ._catalog import (  # noqa: F401
    COMM_TEMPLATES,
    PROFILE_CATALOG,
    catalog_by_category,
    catalog_entry,
    detect_device_profile,
    missing_entries,
    objects_for,
    present_keys,
    search_profile,
    validate_profile_coverage,
)
from .templates import (  # noqa: F401
    STARTER_TEMPLATES,
    assemble_document,
    build_template,
    merge_entries,
)

__all__ = [
    "COMM_TEMPLATES",
    "PROFILE_CATALOG",
    "STARTER_TEMPLATES",
    "assemble_document",
    "build_template",
    "catalog_by_category",
    "catalog_entry",
    "detect_device_profile",
    "merge_entries",
    "missing_entries",
    "objects_for",
    "present_keys",
    "search_profile",
    "validate_profile_coverage",
]
