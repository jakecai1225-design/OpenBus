# -*- coding: utf-8 -*-
"""Re-export shared Activity Snapshot for the AI Agent package."""

from _shared.activity_snapshot import (  # noqa: F401
    as_json,
    format_for_prompt,
    get,
    touch_from_env,
    update,
)
