# -*- coding: utf-8 -*-
"""Legacy Profiles page — redirects to shared Library."""

from __future__ import annotations

from pages.library import build as build_library


def build(parent, session, log_fn):
    return build_library(parent, session, log_fn)
