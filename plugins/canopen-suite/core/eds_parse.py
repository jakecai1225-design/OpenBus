# -*- coding: utf-8 -*-
"""Compatibility shim — implementation lives in plugins/_shared/edsparse.py."""

from __future__ import annotations

import os
import sys

# Tests and host may only put this suite on sys.path; ensure plugins/ is visible.
_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)

from _shared.edsparse import (  # noqa: E402,F401
    ACCESS_TYPES,
    DATA_TYPES,
    META_SECTIONS,
    MANDATORY_INDEXES,
    OBJECT_TYPES,
    EdsDocument,
    OdEntry,
    data_type_label,
    diff_documents,
    empty_document,
    entries_to_dict,
    export_document,
    export_eds_text,
    find_entry,
    index_group,
    object_type_label,
    parse_eds,
    parse_eds_document,
    parse_eds_file,
    parse_eds_file_document,
    validate_document,
    validate_eds,
)

__all__ = [
    "ACCESS_TYPES",
    "DATA_TYPES",
    "META_SECTIONS",
    "MANDATORY_INDEXES",
    "OBJECT_TYPES",
    "EdsDocument",
    "OdEntry",
    "data_type_label",
    "diff_documents",
    "empty_document",
    "entries_to_dict",
    "export_document",
    "export_eds_text",
    "find_entry",
    "index_group",
    "object_type_label",
    "parse_eds",
    "parse_eds_document",
    "parse_eds_file",
    "parse_eds_file_document",
    "validate_document",
    "validate_eds",
]
