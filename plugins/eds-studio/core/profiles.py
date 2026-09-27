# -*- coding: utf-8 -*-
"""CiA profile stubs — re-export shared catalog (EDS Studio + CANopen Suite)."""

from __future__ import annotations

from typing import List, Optional, Tuple

from _shared.canopen_profiles import (  # noqa: F401
    COMM_TEMPLATES,
    PROFILE_CATALOG as _SHARED_CATALOG,
    catalog_by_category,
    catalog_entry as _shared_entry,
    missing_entries,
    objects_for,
    present_keys,
    search_profile,
)
from _shared.canopen_profiles._catalog import (  # noqa: F401
    CIA301_OBJECTS,
    CIA302_OBJECTS,
    CIA401_OBJECTS,
    CIA402_OBJECTS,
    CIA403_OBJECTS,
    CIA404_OBJECTS,
    CIA405_OBJECTS,
    CIA406_OBJECTS,
    CIA418_OBJECTS,
    CIA419_OBJECTS,
)
from _shared.edsparse import OdEntry

# Back-compat: 4-tuple (id, title, kind, blurb) for existing callers.
PROFILE_CATALOG: List[Tuple[str, str, str, str]] = [
    (pid, title, kind, blurb)
    for pid, title, kind, _cat, blurb in _SHARED_CATALOG
]


def catalog_entry(profile_id: str) -> Optional[Tuple[str, str, str, str]]:
    row = _shared_entry(profile_id)
    if not row:
        return None
    return (row[0], row[1], row[2], row[4])


# Re-export helpers used by Library UI
__all__ = [
    "COMM_TEMPLATES",
    "PROFILE_CATALOG",
    "OdEntry",
    "catalog_by_category",
    "catalog_entry",
    "missing_entries",
    "objects_for",
    "present_keys",
    "search_profile",
]
