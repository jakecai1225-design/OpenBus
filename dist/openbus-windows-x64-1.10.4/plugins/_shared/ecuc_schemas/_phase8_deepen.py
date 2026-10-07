# -*- coding: utf-8 -*-
"""Phase 8 — deepen EcucDefs JSON packs toward R22-11 public coverage.

Run: python plugins/_shared/ecuc_schemas/_phase8_deepen.py
Merges additional AUTOSAR-named parameters into existing schema JSON.
English only. Idempotent by shortName.
"""

from __future__ import annotations

import json
import os
from typing import Any, Dict, List

HERE = os.path.dirname(os.path.abspath(__file__))


def P(short, kind="string", default="", summary="", detail="",
      range_s="", literals=None, min_v=None, max_v=None,
      destination="", post_build=None, condition=""):
    d: Dict[str, Any] = {
        "shortName": short,
        "kind": kind,
        "default": default if default != "" else (
            "false" if kind == "boolean" else (
                "0" if kind == "numerical" else (
                    (literals or [""])[0] if kind == "enumeration" else ""))),
        "summary": summary or short,
        "detail": detail or summary or short,
        "range": range_s,
        "definition": "",
    }
    if literals:
        d["literals"] = literals
    if min_v is not None:
        d["min"] = min_v
    if max_v is not None:
        d["max"] = max_v
    if destination:
        d["destination"] = destination
    if post_build is not None:
        d["postBuild"] = bool(post_build)
    if condition:
        d["condition"] = condition
    return d


def merge_params(existing: List[dict], extra: List[dict]) -> List[dict]:
    by = {p.get("shortName"): dict(p) for p in existing}
    for p in extra:
        name = p.get("shortName")
        if not name:
            continue
        if name in by:
            cur = by[name]
            for k, v in p.items():
                if k == "shortName":
                    continue
                if k in ("summary", "detail") and v and (
                        not cur.get(k) or cur.get(k) == name):
                    cur[k] = v
                elif k not in cur or cur[k] in ("", None, name):
                    cur[k] = v
            by[name] = cur
        else:
            by[name] = dict(p)
    return list(by.values())


def ensure_container(schema: dict, short: str, parent: str = "",
                     lower: int = 0, upper="*", seed: bool = False,
                     params: List[dict] | None = None,
                     sub: List[str] | None = None) -> dict:
    for c in schema.get("containers") or []:
        if c.get("shortName") == short:
            if params:
                c["params"] = merge_params(c.get("params") or [], params)
            if sub:
                existing = list(c.get("subContainers") or [])
                for s in sub:
                    if s not in existing:
                        existing.append(s)
                c["subContainers"] = existing
            if parent and not c.get("parent"):
                c["parent"] = parent
            return c
    c = {
        "shortName": short,
        "definition": "/AUTOSAR/EcucDefs/%s/%s" % (
            schema.get("module"), short),
        "lowerMultiplicity": lower,
        "upperMultiplicity": upper,
        "parent": parent,
        "seed": seed,
        "params": list(params or []),
        "subContainers": list(sub or []),
    }
    schema.setdefault("containers", []).append(c)
    return c


def fill_defs(schema: dict):
    mod = schema.get("module") or "Module"
    schema["autosarRelease"] = "R22-11"
    schema["definition"] = schema.get("definition") or (
        "/AUTOSAR/EcucDefs/%s" % mod)
    for c in schema.get("containers") or []:
        cname = c.get("shortName") or "C"
        parent = c.get("parent") or ""
        if parent:
            c["definition"] = "/AUTOSAR/EcucDefs/%s/%s/%s" % (
                mod, parent, cname)
        else:
            c["definition"] = "/AUTOSAR/EcucDefs/%s/%s" % (mod, cname)
        for p in c.get("params") or []:
            pname = p.get("shortName") or "P"
            p["definition"] = p.get("definition") or (
                "%s/%s" % (c["definition"], pname))
            if not p.get("summary") or p.get("summary") == pname:
                p["summary"] = pname
            if not p.get("detail") or p.get("detail") == pname:
                p["detail"] = "AUTOSAR EcucDefs %s parameter." % pname


# ---- Priority packs (R22-11-oriented names) ----

COM_EXTRA = {
    "ComGeneral": [
        P("ComCancellationSupport", "boolean", "false",
          "Cancellation support", "Enable I-PDU cancellation API."),
        P("ComDevErrorDetect", "boolean", "false",
          "DET", "Development error detection."),
        P("ComEnableSignalGroupArrayApi", "boolean", "false",
          "Signal group array API"),
        P("ComSupportedIPduGroups", "numerical", "8",
          "Max I-PDU groups", min_v=1, max_v=256),
        P("ComUserCbkHeaderFile", "string", "",
          "User callback header"),
        P("ComRetryFailedTransmitRequests", "boolean", "false",
          "Retry failed TX"),
        P("ComEnableMDTForCyclicTransmission", "boolean", "false",
          "MDT for cyclic TX"),
        P("ComMetaDataSupport", "boolean", "false",
          "Meta data support"),
        P("ComVersionInfoApi", "boolean", "false",
          "Version info API"),
        P("ComMultipartitionSupport", "boolean", "false",
          "Multi-partition"),
        P("ComGwInvalidateTx", "boolean", "false",
          "GW invalidate on TX"),
        P("ComPublicCddHeaderFile", "string", "",
          "Public CDD header"),
    ],
    "ComConfig": [
        P("ComDataMemSize", "numerical", "4096",
          "COM data memory size (bytes)", min_v=0, max_v=1048576),
        P("ComMaxIPduCnt", "numerical", "64",
          "Max I-PDU count", min_v=1, max_v=65535),
        P("ComTimeBase", "numerical", "10",
          "COM time base (ms)", min_v=1, max_v=1000),
    ],
    "ComIPdu": [
        P("ComIPduHandleId", "numerical", "0",
          "I-PDU handle id", min_v=0, max_v=65535, post_build=True),
        P("ComIPduType", "enumeration", "NORMAL",
          "I-PDU type", literals=["NORMAL", "TP"]),
        P("ComIPduCallout", "string", "",
          "I-PDU callout function"),
        P("ComIPduTriggerTransmitCallout", "string", "",
          "TriggerTransmit callout"),
        P("ComIPduCancellationSupport", "boolean", "false",
          "Cancellation support", condition="ComIPduType==TP"),
        P("ComIPduGroupRef", "reference", "",
          "I-PDU group ref", destination="ComIPduGroup"),
        P("ComIPduSignalProcessing", "enumeration", "IMMEDIATE",
          "Signal processing", literals=["IMMEDIATE", "DEFERRED"]),
        P("ComIPduDirection", "enumeration", "SEND",
          "Direction", literals=["SEND", "RECEIVE"]),
        P("ComIPduSize", "numerical", "8",
          "Length bytes", min_v=0, max_v=64),
        P("ComPduIdRef", "reference", "",
          "Underlying PDU ref", destination="pdu"),
        P("ComIPduUnusedAreasDefault", "numerical", "0",
          "Unused areas default", min_v=0, max_v=255),
        P("ComIPduReplicationEnabled", "boolean", "false",
          "Replication enabled"),
        P("ComMetaDataDefault", "string", "",
          "Default meta data"),
        P("ComTxIPduMinimumDelayFactor", "numerical", "0",
          "Min delay factor", min_v=0, max_v=65535,
          condition="ComIPduDirection==SEND"),
        P("ComTxIPduClearUpdateBit", "enumeration", "Transmit",
          "Clear update bit",
          literals=["Transmit", "TriggerTransmit", "Confirmation"],
          condition="ComIPduDirection==SEND"),
        P("ComTxIPduUnusedAreasDefault", "numerical", "0",
          "TX unused default", min_v=0, max_v=255,
          condition="ComIPduDirection==SEND"),
    ],
    "ComSignal": [
        P("ComBitPosition", "numerical", "0",
          "Bit position in I-PDU", min_v=0, max_v=512),
        P("ComBitSize", "numerical", "8",
          "Bit size", min_v=0, max_v=64),
        P("ComSignalType", "enumeration", "UINT8",
          "Signal type",
          literals=["BOOLEAN", "FLOAT32", "FLOAT64", "SINT8", "SINT16",
                    "SINT32", "SINT64", "UINT8", "UINT16", "UINT32",
                    "UINT64", "UINT8_N", "UINT8_DYN"]),
        P("ComSignalEndianness", "enumeration", "LITTLE_ENDIAN",
          "Endianness",
          literals=["BIG_ENDIAN", "LITTLE_ENDIAN", "OPAQUE"]),
        P("ComSignalInitValue", "string", "0",
          "Init value"),
        P("ComSignalLength", "numerical", "1",
          "Length for UINT8_N/DYN", min_v=0, max_v=64),
        P("ComHandleId", "numerical", "0",
          "Signal handle", min_v=0, max_v=65535, post_build=True),
        P("ComTransferProperty", "enumeration", "PENDING",
          "Transfer property",
          literals=["PENDING", "TRIGGERED", "TRIGGERED_WITHOUT_REPETITION",
                    "TRIGGERED_ON_CHANGE",
                    "TRIGGERED_ON_CHANGE_WITHOUT_REPETITION"]),
        P("ComUpdateBitPosition", "numerical", "0",
          "Update bit position", min_v=0, max_v=512),
        P("ComTimeout", "numerical", "0",
          "Timeout (s)", min_v=0, max_v=65535),
        P("ComFirstTimeout", "numerical", "0",
          "First timeout (s)", min_v=0, max_v=65535),
        P("ComDataInvalidAction", "enumeration", "NOTIFY",
          "Invalid action", literals=["NOTIFY", "REPLACE"]),
        P("ComTimeoutNotification", "string", "",
          "Timeout notification"),
        P("ComNotification", "string", "",
          "RX/TX notification"),
        P("ComErrorNotification", "string", "",
          "Error notification"),
        P("ComInvalidNotification", "string", "",
          "Invalid notification"),
        P("ComSignalDataInvalidValue", "string", "",
          "Invalid value"),
        P("ComTimeoutSubstitutionValue", "string", "",
          "Timeout substitution"),
        P("ComFilterAlgorithm", "enumeration", "ALWAYS",
          "Filter algorithm",
          literals=["ALWAYS", "NEVER", "MASKED_NEW_EQUALS_X",
                    "MASKED_NEW_DIFFERS_X", "NEW_IS_WITHIN",
                    "NEW_IS_OUTSIDE", "MASKED_NEW_DIFFERS_MASKED_OLD",
                    "ONE_EVERY_N"]),
        P("ComFilterMask", "numerical", "0", "Filter mask", min_v=0),
        P("ComFilterX", "numerical", "0", "Filter X", min_v=0),
        P("ComFilterMin", "numerical", "0", "Filter min"),
        P("ComFilterMax", "numerical", "0", "Filter max"),
        P("ComFilterPeriod", "numerical", "1", "Filter period", min_v=1),
        P("ComFilterOffset", "numerical", "0", "Filter offset", min_v=0),
        P("ComSystemTemplateSystemSignalRef", "reference", "",
          "System signal ref", destination="signal"),
    ],
    "ComIPduGroup": [
        P("ComIPduGroupHandleId", "numerical", "0",
          "Group handle", min_v=0, max_v=65535, post_build=True),
        P("ComIPduGroupGroupRef", "reference", "",
          "Parent group", destination="ComIPduGroup"),
        P("ComStartIPduGroup", "boolean", "true",
          "Start at init"),
    ],
    "ComTxMode": [
        P("ComTxModeMode", "enumeration", "PERIODIC",
          "TX mode",
          literals=["DIRECT", "MIXED", "NONE", "PERIODIC"]),
        P("ComTxModeNumberOfRepetitions", "numerical", "0",
          "Repetitions", min_v=0, max_v=255),
        P("ComTxModeRepetitionPeriod", "numerical", "0",
          "Repetition period", min_v=0),
        P("ComTxModeTimeOffset", "numerical", "0",
          "Time offset", min_v=0),
        P("ComTxModeTimePeriod", "numerical", "10",
          "Period", min_v=0),
    ],
    "ComSignalGroup": [
        P("ComHandleId", "numerical", "0", "Handle", min_v=0, max_v=65535),
        P("ComTransferProperty", "enumeration", "PENDING",
          "Transfer property",
          literals=["PENDING", "TRIGGERED", "TRIGGERED_WITHOUT_REPETITION",
                    "TRIGGERED_ON_CHANGE",
                    "TRIGGERED_ON_CHANGE_WITHOUT_REPETITION"]),
        P("ComUpdateBitPosition", "numerical", "0",
          "Update bit", min_v=0, max_v=512),
        P("ComTimeout", "numerical", "0", "Timeout", min_v=0),
        P("ComNotification", "string", "", "Notification"),
        P("ComSignalGroupArrayAccess", "boolean", "false",
          "Array access"),
    ],
    "ComGroupSignal": [
        P("ComBitPosition", "numerical", "0", "Bit position", min_v=0),
        P("ComBitSize", "numerical", "8", "Bit size", min_v=0, max_v=64),
        P("ComSignalType", "enumeration", "UINT8", "Type",
          literals=["BOOLEAN", "FLOAT32", "SINT16", "SINT32", "SINT8",
                    "UINT16", "UINT32", "UINT8", "UINT8_N"]),
        P("ComSignalEndianness", "enumeration", "LITTLE_ENDIAN",
          "Endianness",
          literals=["BIG_ENDIAN", "LITTLE_ENDIAN", "OPAQUE"]),
        P("ComSignalInitValue", "string", "0", "Init value"),
        P("ComHandleId", "numerical", "0", "Handle", min_v=0),
    ],
    "ComMainFunctionTx": [
        P("ComMainFunctionPeriod", "numerical", "10",
          "Main function period (ms)", min_v=1),
    ],
    "ComMainFunctionRx": [
        P("ComMainFunctionPeriod", "numerical", "10",
          "Main function period (ms)", min_v=1),
    ],
    "ComGwMapping": [
        P("ComGwSource", "reference", "", "GW source", destination="ComSignal"),
        P("ComGwDestination", "reference", "",
          "GW destination", destination="ComSignal"),
        P("ComGwDestinationDescription", "string", "", "Description"),
    ],
}

CANIF_EXTRA = {
    "CanIfInitCfg": [
        P("CanIfMaxTrcvBaseAddr", "numerical", "0", "Max trcv base"),
        P("CanIfMaxBufferSize", "numerical", "8", "Max buffer size", min_v=1),
        P("CanIfMaxRxPduCfg", "numerical", "64", "Max RX PDUs", min_v=1),
        P("CanIfMaxTxPduCfg", "numerical", "64", "Max TX PDUs", min_v=1),
    ],
    "CanIfCtrlCfg": [
        P("CanIfCtrlId", "numerical", "0", "Controller id", min_v=0),
        P("CanIfCtrlCanCtrlRef", "reference", "",
          "CAN driver ctrl ref", destination="CanController"),
        P("CanIfCtrlWakeupSupport", "boolean", "false", "Wakeup support"),
        P("CanIfCtrlWakeupSourceRef", "reference", "", "Wakeup source"),
    ],
    "CanIfTxPduCfg": [
        P("CanIfTxPduId", "numerical", "0", "TX PDU id", min_v=0, post_build=True),
        P("CanIfTxPduRef", "reference", "", "PDU ref", destination="pdu"),
        P("CanIfTxPduCanId", "numerical", "0", "CAN id", min_v=0, max_v=0x1FFFFFFF),
        P("CanIfTxPduCanIdType", "enumeration", "STANDARD_CAN",
          "CAN id type",
          literals=["STANDARD_CAN", "STANDARD_FD_CAN", "EXTENDED_CAN",
                    "EXTENDED_FD_CAN"]),
        P("CanIfTxPduDlc", "numerical", "8", "DLC", min_v=0, max_v=64),
        P("CanIfTxPduType", "enumeration", "STATIC", "Type",
          literals=["STATIC", "DYNAMIC"]),
        P("CanIfTxPduUserTxConfirmationUL", "enumeration", "PDUR",
          "UL confirmation",
          literals=["CAN_TP", "CAN_NM", "PDUR", "XCP", "CDD", "J1939TP"]),
        P("CanIfTxPduUserTxConfirmationName", "string", "",
          "Confirmation name"),
        P("CanIfTxPduBufferRef", "reference", "", "Buffer ref"),
        P("CanIfTxPduTruncation", "boolean", "false", "Truncation"),
        P("CanIfTxPduPnFilterPdu", "boolean", "false", "PN filter PDU"),
    ],
    "CanIfRxPduCfg": [
        P("CanIfRxPduId", "numerical", "0", "RX PDU id", min_v=0, post_build=True),
        P("CanIfRxPduRef", "reference", "", "PDU ref", destination="pdu"),
        P("CanIfRxPduCanId", "numerical", "0", "CAN id", min_v=0),
        P("CanIfRxPduCanIdMask", "numerical", "0x7FF", "Id mask"),
        P("CanIfRxPduCanIdType", "enumeration", "STANDARD_CAN",
          "CAN id type",
          literals=["STANDARD_CAN", "STANDARD_FD_CAN", "EXTENDED_CAN",
                    "EXTENDED_FD_CAN"]),
        P("CanIfRxPduDlc", "numerical", "8", "DLC", min_v=0, max_v=64),
        P("CanIfRxPduUserRxIndicationUL", "enumeration", "PDUR",
          "UL indication",
          literals=["CAN_TP", "CAN_NM", "PDUR", "XCP", "CDD", "J1939TP"]),
        P("CanIfRxPduUserRxIndicationName", "string", "", "Indication name"),
        P("CanIfRxPduReadData", "boolean", "false", "Read data API"),
        P("CanIfRxPduReadNotifyStatus", "boolean", "false", "Notify status"),
    ],
    "CanIfPublicCfg": [
        P("CanIfPublicDevErrorDetect", "boolean", "false", "DET"),
        P("CanIfPublicVersionInfoApi", "boolean", "false", "Version info"),
        P("CanIfPublicReadRxPduDataApi", "boolean", "false", "Read RX data"),
        P("CanIfPublicReadTxNotifStatusApi", "boolean", "false",
          "Read TX notif"),
        P("CanIfPublicReadRxNotifStatusApi", "boolean", "false",
          "Read RX notif"),
        P("CanIfPublicSetDynamicTxIdApi", "boolean", "false", "Dynamic TX id"),
        P("CanIfPublicTxBuffering", "boolean", "false", "TX buffering"),
        P("CanIfPublicTxConfirmPollingSupport", "boolean", "false",
          "TX confirm polling"),
        P("CanIfPublicWakeupCheckValidSupport", "boolean", "false",
          "Wakeup check"),
        P("CanIfPublicPnSupport", "boolean", "false", "Partial networking"),
        P("CanIfPublicMultipleDrvSupport", "boolean", "false",
          "Multiple drivers"),
    ],
    "CanIfInitHohCfg": [
        P("CanIfHohType", "enumeration", "RECEIVE", "HOH type",
          literals=["RECEIVE", "TRANSMIT"]),
        P("CanIfCanControllerIdRef", "reference", "", "Controller ref"),
        P("CanIfHthIdSymRef", "reference", "", "HTH ref"),
        P("CanIfHrhIdSymRef", "reference", "", "HRH ref"),
        P("CanIfHrhSoftwareFilter", "boolean", "false", "SW filter"),
    ],
}

PDUR_EXTRA = {
    "PduRGeneral": [
        P("PduRDevErrorDetect", "boolean", "false", "DET"),
        P("PduRVersionInfoApi", "boolean", "false", "Version info"),
        P("PduRZeroCostOperation", "boolean", "false", "Zero-cost"),
        P("PduRMulticastSupport", "boolean", "false", "Multicast"),
        P("PduRRoutingPathGroupSupport", "boolean", "false",
          "Routing path groups"),
    ],
    "PduRRoutingPath": [
        P("PduRSrcPduRef", "reference", "", "Source PDU", destination="pdu"),
        P("PduRDestPduRef", "reference", "", "Dest PDU", destination="pdu"),
        P("PduRDestPduDataProvision", "enumeration", "DIRECT",
          "Data provision", literals=["DIRECT", "TRIGGERTRANSMIT"]),
        P("PduRTransmissionConfirmation", "boolean", "false",
          "TX confirmation"),
        P("PduRQueueLength", "numerical", "0", "Queue length", min_v=0),
    ],
    "PduRBswModules": [
        P("PduRBswModuleRef", "string", "Com", "BSW module"),
        P("PduRCommunicationInterface", "boolean", "true", "IF module"),
        P("PduRTransportProtocol", "boolean", "false", "TP module"),
        P("PduRLowerModule", "boolean", "false", "Lower module"),
        P("PduRUpperModule", "boolean", "true", "Upper module"),
    ],
}

CAN_EXTRA = {
    "CanGeneral": [
        P("CanDevErrorDetect", "boolean", "false", "DET"),
        P("CanVersionInfoApi", "boolean", "false", "Version info"),
        P("CanMultiplexedTransmission", "boolean", "false", "Mux TX"),
        P("CanCancelTransmitApi", "boolean", "false", "Cancel TX"),
        P("CanHardwareTimeout", "numerical", "10", "HW timeout", min_v=0),
        P("CanIndex", "numerical", "0", "Driver index", min_v=0),
        P("CanMainFunctionBusPeriod", "numerical", "10",
          "Bus period (ms)", min_v=1),
        P("CanMainFunctionModePeriod", "numerical", "10",
          "Mode period (ms)", min_v=1),
        P("CanMainFunctionReadPeriod", "numerical", "10",
          "Read period (ms)", min_v=1),
        P("CanMainFunctionWritePeriod", "numerical", "10",
          "Write period (ms)", min_v=1),
        P("CanMainFunctionWakeupPeriod", "numerical", "10",
          "Wakeup period (ms)", min_v=1),
    ],
    "CanController": [
        P("CanControllerId", "numerical", "0", "Controller id", min_v=0),
        P("CanControllerBaseAddress", "numerical", "0", "Base address"),
        P("CanControllerBaudRate", "numerical", "500",
          "Baud (kbps)", min_v=1),
        P("CanControllerActivation", "boolean", "true", "Activation"),
        P("CanTxProcessing", "enumeration", "POLLING", "TX processing",
          literals=["INTERRUPT", "POLLING", "MIXED"]),
        P("CanRxProcessing", "enumeration", "POLLING", "RX processing",
          literals=["INTERRUPT", "POLLING", "MIXED"]),
        P("CanBusoffProcessing", "enumeration", "POLLING", "BusOff",
          literals=["INTERRUPT", "POLLING"]),
        P("CanWakeupProcessing", "enumeration", "POLLING", "Wakeup",
          literals=["INTERRUPT", "POLLING"]),
        P("CanControllerDefaultBaudrate", "reference", "",
          "Default baudrate ref"),
        P("CanWakeupSupport", "boolean", "false", "Wakeup support"),
    ],
    "CanHardwareObject": [
        P("CanObjectId", "numerical", "0", "Object id", min_v=0),
        P("CanObjectType", "enumeration", "RECEIVE", "Object type",
          literals=["RECEIVE", "TRANSMIT"]),
        P("CanIdType", "enumeration", "STANDARD", "Id type",
          literals=["STANDARD", "EXTENDED", "MIXED"]),
        P("CanHandleType", "enumeration", "BASIC", "Handle type",
          literals=["BASIC", "FULL"]),
        P("CanHwObjectCount", "numerical", "1", "HW object count", min_v=1),
        P("CanFdPaddingValue", "numerical", "0", "FD padding", min_v=0, max_v=255),
        P("CanControllerRef", "reference", "",
          "Controller ref", destination="CanController"),
    ],
    "CanControllerBaudrateConfig": [
        P("CanControllerBaudRateConfigID", "numerical", "0", "Baud cfg id"),
        P("CanControllerBaudRate", "numerical", "500", "Baud rate"),
        P("CanControllerPropSeg", "numerical", "2", "Prop seg"),
        P("CanControllerSeg1", "numerical", "3", "Seg1"),
        P("CanControllerSeg2", "numerical", "2", "Seg2"),
        P("CanControllerSyncJumpWidth", "numerical", "1", "SJW"),
    ],
}

CANTP_EXTRA = {
    "CanTpGeneral": [
        P("CanTpDevErrorDetect", "boolean", "false", "DET"),
        P("CanTpVersionInfoApi", "boolean", "false", "Version info"),
        P("CanTpChangeParameterApi", "boolean", "false", "Change parameter"),
        P("CanTpReadParameterApi", "boolean", "false", "Read parameter"),
        P("CanTpFlexibleDataRateSupport", "boolean", "false", "FD support"),
        P("CanTpMainFunctionPeriod", "numerical", "5",
          "Main period (ms)", min_v=1),
    ],
    "CanTpChannel": [
        P("CanTpChannelId", "numerical", "0", "Channel id", min_v=0),
        P("CanTpChannelMode", "enumeration", "FULL_DUPLEX_MODE",
          "Channel mode",
          literals=["HALF_DUPLEX_MODE", "FULL_DUPLEX_MODE"]),
    ],
    "CanTpRxNSdu": [
        P("CanTpRxNSduId", "numerical", "0", "RX N-SDU id", min_v=0),
        P("CanTpRxNSduRef", "reference", "", "N-SDU ref", destination="pdu"),
        P("CanTpBs", "numerical", "8", "Block size", min_v=0),
        P("CanTpNar", "numerical", "100", "N_Ar (ms)", min_v=0),
        P("CanTpNbr", "numerical", "100", "N_Br (ms)", min_v=0),
        P("CanTpNcr", "numerical", "100", "N_Cr (ms)", min_v=0),
        P("CanTpRxWftMax", "numerical", "0", "WFT max", min_v=0),
        P("CanTpSTmin", "numerical", "0", "STmin", min_v=0),
        P("CanTpRxPaddingActivation", "enumeration", "OFF",
          "Padding", literals=["ON", "OFF"]),
        P("CanTpRxTaType", "enumeration", "PHYSICAL",
          "TA type", literals=["PHYSICAL", "FUNCTIONAL"]),
        P("CanTpRxAddressingFormat", "enumeration", "STANDARD",
          "Addressing",
          literals=["STANDARD", "EXTENDED", "MIXED", "NORMALFIXED"]),
        P("CanTpRxNSduMainFunctionPeriod", "numerical", "5",
          "RX main period", min_v=1),
    ],
    "CanTpTxNSdu": [
        P("CanTpTxNSduId", "numerical", "0", "TX N-SDU id", min_v=0),
        P("CanTpTxNSduRef", "reference", "", "N-SDU ref", destination="pdu"),
        P("CanTpNas", "numerical", "100", "N_As (ms)", min_v=0),
        P("CanTpNbs", "numerical", "100", "N_Bs (ms)", min_v=0),
        P("CanTpNcs", "numerical", "100", "N_Cs (ms)", min_v=0),
        P("CanTpTc", "boolean", "true", "Transmit cancellation"),
        P("CanTpTxPaddingActivation", "enumeration", "OFF",
          "Padding", literals=["ON", "OFF"]),
        P("CanTpTxTaType", "enumeration", "PHYSICAL",
          "TA type", literals=["PHYSICAL", "FUNCTIONAL"]),
        P("CanTpTxAddressingFormat", "enumeration", "STANDARD",
          "Addressing",
          literals=["STANDARD", "EXTENDED", "MIXED", "NORMALFIXED"]),
    ],
}

OS_EXTRA = {
    "OsOS": [
        P("OsStatus", "enumeration", "EXTENDED", "Status",
          literals=["STANDARD", "EXTENDED"]),
        P("OsScalabilityClass", "enumeration", "SC1", "SC",
          literals=["SC1", "SC2", "SC3", "SC4"]),
        P("OsStackMonitoring", "boolean", "false", "Stack monitoring"),
        P("OsUseGetServiceId", "boolean", "false", "GetServiceId"),
        P("OsUseParameterAccess", "boolean", "false", "Parameter access"),
        P("OsUseResScheduler", "boolean", "false", "RES_SCHEDULER"),
    ],
    "OsTask": [
        P("OsTaskPriority", "numerical", "1", "Priority", min_v=0, max_v=255),
        P("OsTaskActivation", "numerical", "1", "Max activations", min_v=1),
        P("OsTaskSchedule", "enumeration", "FULL", "Schedule",
          literals=["FULL", "NON"]),
        P("OsTaskType", "enumeration", "BASIC", "Type",
          literals=["BASIC", "EXTENDED"]),
        P("OsTaskStackSize", "numerical", "1024", "Stack size", min_v=64),
        P("OsTaskEventRef", "reference", "", "Event ref"),
        P("OsTaskResourceRef", "reference", "", "Resource ref"),
        P("OsTaskAccessingApplication", "string", "", "Accessing app"),
    ],
    "OsAlarm": [
        P("OsAlarmCounterRef", "reference", "", "Counter ref"),
        P("OsAlarmAction", "enumeration", "ACTIVATETASK", "Action",
          literals=["ACTIVATETASK", "SETEVENT", "INCREMENTCOUNTER",
                    "CALLBACK"]),
        P("OsAlarmActivateTaskRef", "reference", "", "Task ref"),
        P("OsAlarmSetEventRef", "reference", "", "Event ref"),
        P("OsAlarmCallbackName", "string", "", "Callback"),
        P("OsAlarmAutostart", "boolean", "false", "Autostart"),
        P("OsAlarmAlarmTime", "numerical", "0", "Alarm time", min_v=0),
        P("OsAlarmCycleTime", "numerical", "0", "Cycle time", min_v=0),
    ],
    "OsCounter": [
        P("OsCounterMaxAllowedValue", "numerical", "65535", "Max value"),
        P("OsCounterMinCycle", "numerical", "1", "Min cycle"),
        P("OsCounterTicksPerBase", "numerical", "1", "Ticks per base"),
        P("OsCounterType", "enumeration", "HARDWARE", "Type",
          literals=["HARDWARE", "SOFTWARE"]),
        P("OsSecondsPerTick", "string", "0.001", "Seconds per tick"),
    ],
    "OsResource": [
        P("OsResourceProperty", "enumeration", "STANDARD", "Property",
          literals=["STANDARD", "LINKED", "INTERNAL"]),
        P("OsResourceLinkedResourceRef", "reference", "", "Linked resource"),
    ],
    "OsIsr": [
        P("OsIsrCategory", "enumeration", "CATEGORY_2", "Category",
          literals=["CATEGORY_1", "CATEGORY_2"]),
        P("OsIsrPriority", "numerical", "1", "Priority", min_v=0),
        P("OsIsrStackSize", "numerical", "512", "Stack size", min_v=64),
    ],
    "OsApplication": [
        P("OsTrusted", "boolean", "false", "Trusted"),
        P("OsAppScheduleTableRef", "reference", "", "Schedule table"),
        P("OsAppTaskRef", "reference", "", "Task ref"),
        P("OsAppIsrRef", "reference", "", "ISR ref"),
        P("OsAppAlarmRef", "reference", "", "Alarm ref"),
        P("OsAppCounterRef", "reference", "", "Counter ref"),
    ],
}

DCM_EXTRA = {
    "DcmGeneral": [
        P("DcmDevErrorDetect", "boolean", "false", "DET"),
        P("DcmVersionInfoApi", "boolean", "false", "Version info"),
        P("DcmRespondAllRequest", "boolean", "true", "Respond all"),
        P("DcmTaskTime", "numerical", "10", "Task time (ms)", min_v=1),
        P("DcmRequestManufacturerNotification", "boolean", "false",
          "Manufacturer notification"),
        P("DcmRequestSupplierNotification", "boolean", "false",
          "Supplier notification"),
    ],
    "DcmDslProtocolRow": [
        P("DcmDslProtocolID", "enumeration", "UDS_ON_CAN", "Protocol",
          literals=["UDS_ON_CAN", "UDS_ON_FLEXRAY", "UDS_ON_IP",
                    "OBD_ON_CAN", "OBD_ON_FLEXRAY", "OBD_ON_IP"]),
        P("DcmDslProtocolPriority", "numerical", "0", "Priority", min_v=0),
        P("DcmDslProtocolTransType", "enumeration", "TYPE1",
          "Trans type", literals=["TYPE1", "TYPE2"]),
        P("DcmDslProtocolPreemptTimeout", "numerical", "0",
          "Preempt timeout"),
        P("DcmTimStrP2ServerAdjust", "numerical", "0", "P2 adjust"),
        P("DcmTimStrP2StarServerAdjust", "numerical", "0", "P2* adjust"),
    ],
    "DcmDslConnection": [
        P("DcmDslProtocolRxPduRef", "reference", "",
          "RX PDU", destination="pdu"),
        P("DcmDslProtocolTxPduRef", "reference", "",
          "TX PDU", destination="pdu"),
        P("DcmDslProtocolRxTesterSourceAddr", "numerical", "0",
          "Tester source addr"),
        P("DcmDslProtocolComMChannelRef", "reference", "", "ComM channel"),
    ],
    "DcmDspSessionRow": [
        P("DcmDspSessionLevel", "numerical", "1", "Session level", min_v=1),
        P("DcmDspSessionP2ServerMax", "numerical", "50", "P2 server max"),
        P("DcmDspSessionP2StarServerMax", "numerical", "5000", "P2* max"),
        P("DcmDspSessionForBoot", "enumeration", "OTA", "For boot",
          literals=["OTA", "SYS_BOOT", "SYS_BOOT_RESPAPP"]),
    ],
    "DcmDspSecurityRow": [
        P("DcmDspSecurityLevel", "numerical", "1", "Security level", min_v=1),
        P("DcmDspSecuritySeedSize", "numerical", "4", "Seed size", min_v=1),
        P("DcmDspSecurityKeySize", "numerical", "4", "Key size", min_v=1),
        P("DcmDspSecurityADRSize", "numerical", "0", "ADR size"),
        P("DcmDspSecurityDelayTime", "numerical", "0", "Delay time"),
        P("DcmDspSecurityNumAttDelay", "numerical", "0", "Attempts delay"),
        P("DcmDspSecurityDelayTimeOnBoot", "numerical", "0",
          "Delay on boot"),
    ],
    "DcmDspDid": [
        P("DcmDspDidIdentifier", "numerical", "0xF190", "DID", min_v=0, max_v=0xFFFF),
        P("DcmDspDidSize", "numerical", "1", "Size", min_v=1),
        P("DcmDspDidUsed", "boolean", "true", "Used"),
        P("DcmDspDidInfoRef", "reference", "", "DID info ref"),
        P("DcmDspDidAccess", "enumeration", "RO", "Access",
          literals=["RO", "WO", "RW"]),
    ],
    "DcmDspRoutine": [
        P("DcmDspRoutineIdentifier", "numerical", "0xFF00", "RID",
          min_v=0, max_v=0xFFFF),
        P("DcmDspRoutineUsed", "boolean", "true", "Used"),
        P("DcmDspRoutineInfoRef", "reference", "", "Routine info"),
    ],
}

DEM_EXTRA = {
    "DemGeneral": [
        P("DemDevErrorDetect", "boolean", "false", "DET"),
        P("DemVersionInfoApi", "boolean", "false", "Version info"),
        P("DemOBDSupport", "enumeration", "DEM_OBD_NO_OBD_SUPPORT",
          "OBD support",
          literals=["DEM_OBD_NO_OBD_SUPPORT", "DEM_OBD_PRIMARY_ECU",
                    "DEM_OBD_DEP_SEC_ECU", "DEM_OBD_MASTER_ECU"]),
        P("DemEventMemoryEntryCount", "numerical", "10",
          "Event memory entries", min_v=1),
        P("DemMaxNumberEventEntryPrimary", "numerical", "10",
          "Primary entries", min_v=1),
        P("DemClearDTCBehavior", "enumeration", "DEM_CLRRESP_VOLATILE",
          "Clear DTC behavior",
          literals=["DEM_CLRRESP_VOLATILE", "DEM_CLRRESP_NONVOLATILE_TRIGGER",
                    "DEM_CLRRESP_NONVOLATILE_FINISH"]),
        P("DemTaskTime", "numerical", "10", "Task time (ms)", min_v=1),
    ],
    "DemEventParameter": [
        P("DemEventId", "numerical", "1", "Event id", min_v=1),
        P("DemEventKind", "enumeration", "DEM_EVENT_KIND_BSW",
          "Kind",
          literals=["DEM_EVENT_KIND_BSW", "DEM_EVENT_KIND_SWC"]),
        P("DemDTCRef", "reference", "", "DTC ref"),
        P("DemDebounceAlgorithmClass", "enumeration", "DEM_DEBOUNCE_COUNTER",
          "Debounce",
          literals=["DEM_DEBOUNCE_COUNTER", "DEM_DEBOUNCE_TIME",
                    "DEM_DEBOUNCE_MONITOR_INTERNAL"]),
        P("DemEventAvailable", "boolean", "true", "Available"),
        P("DemFFPrestorageSupported", "boolean", "false", "FF prestorage"),
        P("DemReportBehavior", "enumeration", "REPORT_BEFORE_INIT",
          "Report behavior",
          literals=["REPORT_BEFORE_INIT", "REPORT_AFTER_INIT"]),
    ],
    "DemDTCClass": [
        P("DemDtcValue", "numerical", "0x100001", "DTC value"),
        P("DemDTCSeverity", "enumeration", "DEM_SEVERITY_NO_SEVERITY",
          "Severity",
          literals=["DEM_SEVERITY_NO_SEVERITY", "DEM_SEVERITY_MAINTENANCE_ONLY",
                    "DEM_SEVERITY_CHECK_AT_NEXT_HALT",
                    "DEM_SEVERITY_CHECK_IMMEDIATELY"]),
        P("DemDTCFunctionalUnit", "numerical", "0", "Functional unit"),
        P("DemImmediateNvStorage", "boolean", "false", "Immediate NV"),
    ],
}

NVM_EXTRA = {
    "NvMCommon": [
        P("NvMDevErrorDetect", "boolean", "false", "DET"),
        P("NvMVersionInfoApi", "boolean", "false", "Version info"),
        P("NvMApiConfigClass", "enumeration", "NVM_API_CONFIG_CLASS_3",
          "API class",
          literals=["NVM_API_CONFIG_CLASS_1", "NVM_API_CONFIG_CLASS_2",
                    "NVM_API_CONFIG_CLASS_3"]),
        P("NvMBswMMultiBlockJobStatusInformation", "boolean", "false",
          "BswM multi-block status"),
        P("NvMCompressedEncryption", "boolean", "false", "Compressed"),
        P("NvMDynamicConfiguration", "boolean", "false", "Dynamic config"),
        P("NvMJobPrioritization", "boolean", "false", "Job prioritization"),
        P("NvMPollingMode", "boolean", "false", "Polling mode"),
        P("NvMRepeatMirrorOperations", "numerical", "3",
          "Repeat mirror ops", min_v=0),
        P("NvMSizeImmediateJobQueue", "numerical", "1",
          "Immediate queue", min_v=1),
        P("NvMSizeStandardJobQueue", "numerical", "1",
          "Standard queue", min_v=1),
        P("NvMMainFunctionPeriod", "numerical", "10",
          "Main period (ms)", min_v=1),
    ],
    "NvMBlockDescriptor": [
        P("NvMNvramBlockIdentifier", "numerical", "1", "Block id", min_v=1),
        P("NvMNvBlockLength", "numerical", "4", "Block length", min_v=1),
        P("NvMBlockManagementType", "enumeration", "NVM_BLOCK_NATIVE",
          "Management",
          literals=["NVM_BLOCK_NATIVE", "NVM_BLOCK_REDUNDANT",
                    "NVM_BLOCK_DATASET"]),
        P("NvMBlockJobPriority", "numerical", "0", "Job priority", min_v=0),
        P("NvMBlockUseCrc", "boolean", "false", "Use CRC"),
        P("NvMBlockCrcType", "enumeration", "NVM_CRC16", "CRC type",
          literals=["NVM_CRC8", "NVM_CRC16", "NVM_CRC32"]),
        P("NvMBlockUseSyncMechanism", "boolean", "false", "Sync mechanism"),
        P("NvMResistantToChangedSw", "boolean", "false", "Resistant to SW"),
        P("NvMSelectBlockForReadAll", "boolean", "true", "ReadAll"),
        P("NvMSelectBlockForWriteAll", "boolean", "true", "WriteAll"),
        P("NvMWriteBlockOnce", "boolean", "false", "Write once"),
        P("NvMMaxNumOfReadRetries", "numerical", "3", "Read retries"),
        P("NvMMaxNumOfWriteRetries", "numerical", "3", "Write retries"),
        P("NvMRamBlockDataAddress", "string", "", "RAM address"),
        P("NvMRomBlockDataAddress", "string", "", "ROM address"),
        P("NvMNvBlockBaseNumber", "numerical", "0", "NV base number"),
        P("NvMNvramDeviceId", "numerical", "0", "Device id"),
    ],
}

PACKS = {
    "Com": COM_EXTRA,
    "CanIf": CANIF_EXTRA,
    "PduR": PDUR_EXTRA,
    "Can": CAN_EXTRA,
    "CanTp": CANTP_EXTRA,
    "Os": OS_EXTRA,
    "Dcm": DCM_EXTRA,
    "Dem": DEM_EXTRA,
    "NvM": NVM_EXTRA,
}


def deepen_module(module: str, extras: dict) -> int:
    path = os.path.join(HERE, "%s.json" % module)
    if not os.path.isfile(path):
        print("skip missing", module)
        return 0
    with open(path, "r", encoding="utf-8") as f:
        schema = json.load(f)
    before = sum(len(c.get("params") or []) for c in schema.get("containers") or [])
    for cname, params in extras.items():
        ensure_container(schema, cname, params=params)
    # Link parents for known Com nesting
    if module == "Com":
        ensure_container(
            schema, "ComConfig", parent="", lower=1, upper=1,
            sub=["ComIPduGroup", "ComIPdu", "ComMainFunctionTx",
                 "ComMainFunctionRx", "ComGwMapping"])
        ensure_container(schema, "ComIPdu", parent="ComConfig", seed=True,
                         sub=["ComSignal", "ComTxMode", "ComSignalGroup"])
        ensure_container(schema, "ComSignalGroup", parent="ComIPdu",
                         sub=["ComGroupSignal"])
    fill_defs(schema)
    # Enrich any remaining empty prose
    for c in schema.get("containers") or []:
        for p in c.get("params") or []:
            sn = p.get("shortName") or ""
            if not p.get("detail") or p.get("detail") == sn:
                p["detail"] = (
                    "AUTOSAR R22-11 EcucDefs parameter %s "
                    "(configure → validate → ARXML handoff; no codegen)."
                    % sn)
            if not p.get("summary") or p.get("summary") == sn:
                p["summary"] = sn
    after = sum(len(c.get("params") or []) for c in schema.get("containers") or [])
    with open(path, "w", encoding="utf-8") as f:
        json.dump(schema, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("%s: %d → %d params (+%d)" % (module, before, after, after - before))
    return after - before


def deepen_all_prose():
    """Ensure every schema module has non-trivial detail strings."""
    for name in os.listdir(HERE):
        if not name.endswith(".json") or name.startswith("_"):
            continue
        path = os.path.join(HERE, name)
        with open(path, "r", encoding="utf-8") as f:
            schema = json.load(f)
        fill_defs(schema)
        for c in schema.get("containers") or []:
            for p in c.get("params") or []:
                sn = p.get("shortName") or ""
                if not p.get("detail") or p.get("detail") == sn:
                    p["detail"] = (
                        "AUTOSAR EcucDefs %s — see Spec / BSWMD tips."
                        % sn)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(schema, f, indent=2, ensure_ascii=False)
            f.write("\n")
        print("prose", name)


def main():
    total = 0
    for mod, extras in PACKS.items():
        total += deepen_module(mod, extras)
    deepen_all_prose()
    # Recount priority
    for mod in PACKS:
        path = os.path.join(HERE, "%s.json" % mod)
        j = json.load(open(path, encoding="utf-8"))
        pc = sum(len(c.get("params") or []) for c in j.get("containers") or [])
        print("FINAL", mod, pc)
    print("done, added ~", total)


if __name__ == "__main__":
    main()
