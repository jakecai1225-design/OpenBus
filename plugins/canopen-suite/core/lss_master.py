# -*- coding: utf-8 -*-
"""CiA 305 LSS master helpers (lite) — encode / decode only."""

from __future__ import annotations

from typing import Optional, Tuple

# LSS COB-IDs
LSS_MASTER_ID = 0x7E5
LSS_SLAVE_ID = 0x7E4

# CS codes (subset)
CS_SWITCH_GLOBAL = 0x04
CS_SWITCH_SEL_VENDOR = 0x40
CS_SWITCH_SEL_PRODUCT = 0x41
CS_SWITCH_SEL_REV = 0x42
CS_SWITCH_SEL_SERIAL = 0x43
CS_CONFIGURE_NODE_ID = 0x11
CS_CONFIGURE_BIT_TIMING = 0x13
CS_ACTIVATE_BIT_TIMING = 0x15
CS_STORE_CONFIGURATION = 0x17
CS_INQUIRE_NODE_ID = 0x5E
CS_IDENTIFY_REMOTE_VENDOR = 0x46
CS_IDENTIFY_NON_CONFIG_REMOTE = 0x4C


def encode_switch_global(mode: int) -> bytes:
    """mode: 0 = waiting, 1 = configuration."""
    return bytes([CS_SWITCH_GLOBAL, mode & 0xFF, 0, 0, 0, 0, 0, 0])


def encode_configure_node_id(node_id: int) -> bytes:
    nid = max(1, min(127, int(node_id)))
    return bytes([CS_CONFIGURE_NODE_ID, nid & 0xFF, 0, 0, 0, 0, 0, 0])


def encode_configure_bit_timing(table_index: int, bitrate_index: int) -> bytes:
    """CiA 305 bit timing table — caller supplies indices."""
    return bytes([
        CS_CONFIGURE_BIT_TIMING,
        table_index & 0xFF, bitrate_index & 0xFF,
        0, 0, 0, 0, 0,
    ])


def encode_inquire_node_id() -> bytes:
    return bytes([CS_INQUIRE_NODE_ID, 0, 0, 0, 0, 0, 0, 0])


def encode_identify_non_configured() -> bytes:
    return bytes([CS_IDENTIFY_NON_CONFIG_REMOTE, 0, 0, 0, 0, 0, 0, 0])


def decode_lss_slave(data: bytes) -> Tuple[str, dict]:
    if not data:
        return ("other", {})
    cs = data[0]
    if cs == 0x5E and len(data) >= 2:
        return ("inquire_node_id", {"node_id": data[1]})
    if cs == 0x11 and len(data) >= 2:
        return ("configure_node_id_ack", {"error": data[1]})
    if cs == 0x4C:
        return ("identify_slave", {})
    return ("other", {"cs": cs})
