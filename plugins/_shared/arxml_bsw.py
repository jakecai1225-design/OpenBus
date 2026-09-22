# -*- coding: utf-8 -*-
"""AUTOSAR CP BSW module catalog — ECUC-lite stubs (config only, no codegen).

Each module maps to work/bsw/<Module>.arxml inside an ARXML Studio project.
Container trees are starter schemas (tresos/DaVinci-style short names) so users
can expand parameters; full vendor BSWMD import remains optional.
"""

from __future__ import annotations

from typing import Dict, List, Tuple

from _shared import arxmlparse

# (module_short_name, group, summary)
BSW_CATALOG: List[Tuple[str, str, str]] = [
    ("Os", "System", "OS tasks, counters, alarms, schedule tables"),
    ("EcuC", "System", "ECU configuration / Pdu collection"),
    ("Rte", "System", "RTE generation config (handoff only)"),
    ("BswM", "System", "Mode arbitration and action lists"),
    ("EcuM", "System", "ECU state manager"),
    ("Com", "Communication", "COM I-PDU / signal layer"),
    ("ComM", "Communication", "Communication manager channels"),
    ("PduR", "Communication", "PDU router"),
    ("IpduM", "Communication", "I-PDU multiplexer"),
    ("LdCom", "Communication", "Large data COM"),
    ("Can", "CAN", "CAN driver controllers"),
    ("CanIf", "CAN", "CAN interface"),
    ("CanSM", "CAN", "CAN state manager"),
    ("CanNm", "CAN", "CAN network management"),
    ("CanTp", "CAN", "CAN transport protocol"),
    ("CanTrcv", "CAN", "CAN transceiver"),
    ("LinIf", "LIN", "LIN interface"),
    ("LinSM", "LIN", "LIN state manager"),
    ("LinTp", "LIN", "LIN transport"),
    ("FrIf", "FlexRay", "FlexRay interface"),
    ("FrSM", "FlexRay", "FlexRay state manager"),
    ("FrNm", "FlexRay", "FlexRay NM"),
    ("FrTp", "FlexRay", "FlexRay TP"),
    ("EthIf", "Ethernet", "Ethernet interface"),
    ("EthSM", "Ethernet", "Ethernet state manager"),
    ("Eth", "Ethernet", "Ethernet driver"),
    ("TcpIp", "Ethernet", "TCP/IP stack config"),
    ("SoAd", "Ethernet", "Socket adaptor"),
    ("DoIP", "Ethernet", "Diagnostics over IP"),
    ("Sd", "Ethernet", "Service discovery"),
    ("SomeIpXf", "Ethernet", "SOME/IP transformer"),
    ("Nm", "Network", "Generic NM coordinator"),
    ("UdpNm", "Network", "UDP NM"),
    ("Dcm", "Diagnostic", "Diagnostic communication manager"),
    ("Dem", "Diagnostic", "Diagnostic event manager"),
    ("Fim", "Diagnostic", "Function inhibition manager"),
    ("Xcp", "Diagnostic", "XCP on CAN/Ethernet"),
    ("NvM", "Memory", "NVRAM manager"),
    ("MemIf", "Memory", "Memory abstraction interface"),
    ("Fee", "Memory", "Flash EEPROM emulation"),
    ("Ea", "Memory", "EEPROM abstraction"),
    ("WdgM", "Services", "Watchdog manager"),
    ("WdgIf", "Services", "Watchdog interface"),
    ("Crc", "Services", "CRC library"),
    ("Crypto", "Security", "Crypto stack"),
    ("Csm", "Security", "Crypto service manager"),
    ("SecOC", "Security", "Secure onboard communication"),
    ("E2EXf", "Safety", "E2E transformer"),
    ("StbM", "Time", "Synchronized time-base manager"),
    ("Tm", "Time", "Time manager"),
    ("Mirror", "Debug", "Bus mirror"),
]


def catalog_by_group() -> Dict[str, List[Tuple[str, str]]]:
    groups: Dict[str, List[Tuple[str, str]]] = {}
    for name, group, summary in BSW_CATALOG:
        groups.setdefault(group, []).append((name, summary))
    return groups


def module_names() -> List[str]:
    return [n for n, _g, _s in BSW_CATALOG]


def module_rel_path(name: str) -> str:
    return "work/bsw/%s.arxml" % name


# Starter container short-names per module (expandable in UI)
_MODULE_CONTAINERS: Dict[str, List[Tuple[str, List[str]]]] = {
    # name -> [(container, [param names])]
    "Os": [
        ("OsAppMode", ["OsAppModeId", "OsAppModeAutostart"]),
        ("OsTask", [
            "OsTaskPriority", "OsTaskSchedule", "OsTaskActivation",
            "OsTaskStackSize", "OsTaskType"]),
        ("OsCounter", [
            "OsCounterMaxAllowedValue", "OsCounterTicksPerBase",
            "OsCounterMinCycle"]),
        ("OsAlarm", [
            "OsAlarmCounterRef", "OsAlarmAction", "OsAlarmAutostart"]),
        ("OsScheduleTable", [
            "OsScheduleTableDuration", "OsScheduleTableRepeating",
            "OsScheduleTableCounterRef"]),
        ("OsIsr", ["OsIsrCategory", "OsIsrResourceRef", "OsIsrStackSize"]),
        ("OsResource", ["OsResourceProperty", "OsResourceAccessingApplication"]),
        ("OsApplication", ["OsTrusted"]),
    ],
    "EcuC": [
        ("EcucConfigSet", ["EcucPduCollection", "EcucHardwareUnit"]),
        ("EcucPduCollection", ["EcucPduId", "EcucPduLength", "EcucPduType"]),
    ],
    "Rte": [
        ("RteGeneration", [
            "RteOptimizationMode", "RteDevErrorDetect", "RteVfbTraceEnabled"]),
        ("RteSwComponentInstance", ["RteSoftwareComponentInstanceRef"]),
    ],
    "BswM": [
        ("BswMGeneral", ["BswMDevErrorDetect", "BswMMainFunctionPeriod"]),
        ("BswMConfig", ["BswMModeArbitration", "BswMModeControl"]),
        ("BswMModeRequestPort", [
            "BswMModeRequestSource", "BswMRequestProcessing",
            "BswMModeRequestMaxQueueSize"]),
        ("BswMActionList", [
            "BswMAbortOnFail", "BswMExecution", "BswMActionListPriority"]),
        ("BswMRule", ["BswMNestedExecutionOnly", "BswMRuleExpression"]),
    ],
    "EcuM": [
        ("EcuMGeneral", ["EcuMDevErrorDetect", "EcuMMainFunctionPeriod"]),
        ("EcuMConfiguration", [
            "EcuMDefaultAppMode", "EcuMNormalMcuModeRef"]),
        ("EcuMFlexConfiguration", ["EcuMAlarmWakeupSource"]),
        ("EcuMWakeupSource", ["EcuMWakeupSourceId", "EcuMValidationTimeout"]),
    ],
    "Com": [
        ("ComGeneral", [
            "ComConfigurationUseDet", "ComRetryFailedTransmitRequests",
            "ComEnableMDTForCyclicTransmission"]),
        ("ComConfig", [
            "ComIPduGroup", "ComIPdu", "ComSignal", "ComSignalGroup"]),
        ("ComIPdu", [
            "ComIPduDirection", "ComIPduSignalProcessing", "ComIPduSize"]),
        ("ComSignal", [
            "ComBitPosition", "ComBitSize", "ComSignalEndianness",
            "ComSignalType", "ComTransferProperty"]),
    ],
    "ComM": [
        ("ComMConfigSet", ["ComMChannel", "ComMUser"]),
        ("ComMGeneral", ["ComMModeLimitationEnabled", "ComMPncSupport"]),
        ("ComMChannel", [
            "ComMChannelId", "ComMBusType", "ComMMainFunctionPeriod",
            "ComMNmVariant"]),
        ("ComMUser", ["ComMUserIdentifier", "ComMUserChannel"]),
    ],
    "PduR": [
        ("PduRRoutingTables", ["PduRRoutingTable", "PduRMaxRoutingTableEntries"]),
        ("PduRGeneral", ["PduRDevErrorDetect", "PduRVersionInfoApi"]),
        ("PduRRoutingTable", [
            "PduRSrcPduRef", "PduRDestPduRef", "PduRDestPduDataProvision"]),
    ],
    "IpduM": [
        ("IpduMConfig", ["IpduMTxPathway", "IpduMRxPathway"]),
    ],
    "LdCom": [
        ("LdComConfig", ["LdComIPdu"]),
    ],
    "Can": [
        ("CanConfigSet", ["CanController", "CanHardwareObject"]),
        ("CanGeneral", ["CanDevErrorDetect", "CanVersionInfoApi"]),
        ("CanController", [
            "CanControllerId", "CanControllerBaudRate", "CanControllerBaseAddress"]),
        ("CanHardwareObject", [
            "CanObjectId", "CanIdType", "CanObjectType"]),
    ],
    "CanIf": [
        ("CanIfInitCfg", ["CanIfTxPduCfg", "CanIfRxPduCfg", "CanIfInitHohCfg"]),
        ("CanIfPublicCfg", [
            "CanIfPublicDevErrorDetect", "CanIfPublicMultipleDrvSupport"]),
        ("CanIfTxPduCfg", [
            "CanIfTxPduCanId", "CanIfTxPduDlc", "CanIfTxPduRef", "CanIfTxPduType"]),
        ("CanIfRxPduCfg", [
            "CanIfRxPduCanId", "CanIfRxPduDlc", "CanIfRxPduRef"]),
    ],
    "CanSM": [
        ("CanSMConfiguration", ["CanSMNetwork", "CanSMManagerNetwork"]),
        ("CanSMGeneral", ["CanSMDevErrorDetect", "CanSMMainFunctionTimePeriod"]),
        ("CanSMManagerNetwork", [
            "CanSMComMNetworkHandleRef", "CanSMBorTimeL1", "CanSMBorTimeL2"]),
    ],
    "CanNm": [
        ("CanNmGlobalConfig", [
            "CanNmMainFunctionPeriod", "CanNmTimeoutTime", "CanNmEcuName"]),
        ("CanNmChannelConfig", [
            "CanNmChannelId", "CanNmMsgCycleTime", "CanNmNodeId",
            "CanNmWaitBusSleepTime"]),
    ],
    "CanTp": [
        ("CanTpConfig", ["CanTpChannel", "CanTpNSdu"]),
        ("CanTpGeneral", ["CanTpDevErrorDetect", "CanTpMainFunctionPeriod"]),
        ("CanTpChannel", [
            "CanTpChannelId", "CanTpChannelMode", "CanTpBs", "CanTpSTmin"]),
        ("CanTpNSdu", [
            "CanTpNSduId", "CanTpNSduDirection", "CanTpTxNSduRef", "CanTpNas"]),
    ],
    "CanTrcv": [
        ("CanTrcvConfigSet", ["CanTrcvChannel"]),
    ],
    "LinIf": [
        ("LinIfGlobalConfig", ["LinIfChannel", "LinIfFrame", "LinIfScheduleTable"]),
        ("LinIfGeneral", ["LinIfDevErrorDetect", "LinIfMainFunctionPeriod"]),
        ("LinIfChannel", ["LinIfChannelId", "LinIfBaudRate"]),
    ],
    "LinSM": [
        ("LinSMConfigSet", ["LinSMChannel", "LinSMScheduleTable"]),
        ("LinSMGeneral", ["LinSMDevErrorDetect", "LinSMMainFunctionPeriod"]),
    ],
    "LinTp": [
        ("LinTpGlobalConfig", ["LinTpChannel", "LinTpNSdu"]),
        ("LinTpGeneral", ["LinTpDevErrorDetect"]),
    ],
    "FrIf": [
        ("FrIfConfig", ["FrIfCluster", "FrIfController", "FrIfPdu"]),
        ("FrIfGeneral", ["FrIfDevErrorDetect"]),
    ],
    "FrSM": [
        ("FrSMConfig", ["FrSMCluster"]),
        ("FrSMGeneral", ["FrSMDevErrorDetect"]),
    ],
    "FrNm": [
        ("FrNmGlobalConfig", ["FrNmChannelConfig"]),
        ("FrNmGeneral", ["FrNmMainFunctionPeriod"]),
    ],
    "FrTp": [
        ("FrTpGlobalConfig", ["FrTpConnection"]),
        ("FrTpGeneral", ["FrTpDevErrorDetect"]),
    ],
    "EthIf": [
        ("EthIfConfigSet", ["EthIfController", "EthIfFrameOwnerConfig"]),
        ("EthIfGeneral", ["EthIfDevErrorDetect", "EthIfMaxTrcvsTotal"]),
        ("EthIfController", ["EthIfCtrlIdx", "EthIfCtrlMtu"]),
    ],
    "EthSM": [
        ("EthSMConfiguration", ["EthSMNetwork"]),
        ("EthSMGeneral", ["EthSMDevErrorDetect", "EthSMMainFunctionPeriod"]),
    ],
    "Eth": [
        ("EthConfigSet", ["EthCtrlConfig"]),
        ("EthGeneral", ["EthDevErrorDetect", "EthIndex"]),
    ],
    "TcpIp": [
        ("TcpIpConfig", ["TcpIpCtrl", "TcpIpLocalAddr", "TcpIpSocketOwner"]),
        ("TcpIpGeneral", ["TcpIpDevErrorDetect"]),
    ],
    "SoAd": [
        ("SoAdConfig", ["SoAdSocketConnection", "SoAdPduRoute", "SoAdRoutingGroup"]),
        ("SoAdGeneral", ["SoAdDevErrorDetect"]),
    ],
    "DoIP": [
        ("DoIPConfigSet", ["DoIPConnections", "DoIPChannel"]),
        ("DoIPGeneral", ["DoIPDevErrorDetect", "DoIPVin"]),
    ],
    "Sd": [
        ("SdConfig", ["SdInstance", "SdClientService", "SdServerService"]),
        ("SdGeneral", ["SdDevErrorDetect"]),
    ],
    "SomeIpXf": [
        ("SomeIpXfTransformation", ["SomeIpXfDeployment", "SomeIpXfISignalProps"]),
    ],
    "Nm": [
        ("NmGlobalConfig", [
            "NmChannelConfig", "NmCycleTimeMainFunction",
            "NmDevErrorDetect", "NmCoordinatorSupportEnabled"]),
        ("NmChannelConfig", ["NmComMChannelRef", "NmPassiveModeEnabled"]),
    ],
    "UdpNm": [
        ("UdpNmGlobalConfig", [
            "UdpNmChannelConfig", "UdpNmMainFunctionPeriod"]),
        ("UdpNmChannelConfig", [
            "UdpNmChannelId", "UdpNmMsgCycleTime", "UdpNmNetworkTimeout"]),
    ],
    "Dcm": [
        ("DcmConfigSet", ["DcmDsd", "DcmDsl", "DcmDsp"]),
        ("DcmGeneral", [
            "DcmDevErrorDetect", "DcmVersionInfoApi", "DcmRespondAllRequest"]),
        ("DcmDslProtocol", [
            "DcmDslProtocolID", "DcmDslProtocolBuffer",
            "DcmDslProtocolPriority"]),
        ("DcmDsdServiceTable", [
            "DcmDsdSidTabServiceId", "DcmDsdSidTabSubfuncAvail"]),
        ("DcmDspSession", ["DcmDspSessionLevel", "DcmDspSessionP2ServerMax"]),
        ("DcmDspSecurity", ["DcmDspSecurityLevel", "DcmDspSecurityADRSize"]),
    ],
    "Dem": [
        ("DemConfigSet", [
            "DemEventParameter", "DemOperationCycle", "DemDTCClass"]),
        ("DemGeneral", [
            "DemDevErrorDetect", "DemAgingCycleCounterThreshold"]),
        ("DemEventParameter", [
            "DemEventId", "DemEventKind", "DemDTCClassRef",
            "DemEventFailureCycleCounterThreshold"]),
        ("DemOperationCycle", [
            "DemOperationCycleId", "DemOperationCycleType"]),
        ("DemDTCClass", ["DemDtcValue", "DemDTCSeverity"]),
    ],
    "Fim": [
        ("FimConfig", ["FimFid", "FimInhibitionConfiguration"]),
        ("FimFid", ["FimFidId", "FimInhibitionMask"]),
    ],
    "Xcp": [
        ("XcpConfig", ["XcpEventChannel", "XcpDao"]),
    ],
    "NvM": [
        ("NvMBlockDescriptor", [
            "NvMNvramBlockIdentifier", "NvMBlockJobPriority",
            "NvMBlockLength", "NvMBlockManagementType"]),
        ("NvMCommon", [
            "NvMDevErrorDetect", "NvMMainFunctionPeriod", "NvMApiConfigClass"]),
    ],
    "MemIf": [
        ("MemIfGeneral", [
            "MemIfNumberOfDevices", "MemIfDevErrorDetect",
            "MemIfVersionInfoApi"]),
    ],
    "Fee": [
        ("FeeGeneral", [
            "FeeDevErrorDetect", "FeeVirtualPageSize",
            "FeeMainFunctionPeriod"]),
        ("FeeBlockConfiguration", [
            "FeeBlockNumber", "FeeBlockSize", "FeeImmediateData"]),
    ],
    "Ea": [
        ("EaGeneral", ["EaDevErrorDetect", "EaVirtualPageSize"]),
        ("EaBlockConfiguration", ["EaBlockNumber", "EaBlockSize"]),
    ],
    "WdgM": [
        ("WdgMConfigSet", ["WdgMMode", "WdgMSupervisedEntity"]),
        ("WdgMGeneral", ["WdgMDevErrorDetect", "WdgMMainFunctionPeriod"]),
    ],
    "WdgIf": [
        ("WdgIfConfigSet", ["WdgIfDevice"]),
        ("WdgIfGeneral", ["WdgIfDevErrorDetect"]),
    ],
    "Crc": [
        ("CrcGeneral", ["Crc_8_Mode", "Crc_16_Mode", "Crc_32_Mode"]),
    ],
    "Crypto": [
        ("CryptoGeneral", ["CryptoDevErrorDetect"]),
        ("CryptoDriverObjects", ["CryptoDriverObject"]),
        ("CryptoKeyElements", ["CryptoKeyElement"]),
    ],
    "Csm": [
        ("CsmJobs", ["CsmJob"]),
        ("CsmKeys", ["CsmKey"]),
        ("CsmGeneral", ["CsmDevErrorDetect", "CsmMainFunctionPeriod"]),
    ],
    "SecOC": [
        ("SecOCGeneral", [
            "SecOCDevErrorDetect", "SecOCMainFunctionPeriod",
            "SecOCQueryFreshnessValue"]),
        ("SecOCRxPduProcessing", [
            "SecOCAuthDataFreshnessLen", "SecOCAuthInfoTruncLength",
            "SecOCRxPduRef"]),
        ("SecOCTxPduProcessing", [
            "SecOCAuthInfoTxLength", "SecOCTxPduRef"]),
    ],
    "E2EXf": [
        ("E2EXfTransformation", ["E2EXfProfile", "E2EXfContainer"]),
    ],
    "StbM": [
        ("StbMSynchronizedTimeBase", [
            "StbMTimeBaseId", "StbMIsSystemWideGlobalTimeMaster"]),
        ("StbMGeneral", ["StbMDevErrorDetect", "StbMMainFunctionPeriod"]),
    ],
    "Tm": [
        ("TmGeneral", ["TmMainFunctionPeriod", "TmDevErrorDetect"]),
    ],
    "Mirror": [
        ("MirrorConfig", ["MirrorNetwork", "MirrorSourceNetwork"]),
        ("MirrorGeneral", ["MirrorDevErrorDetect"]),
    ],
}


def stub_module(name: str, ecu_name: str = "Ecu") -> arxmlparse.EcucModel:
    """Build a starter ECUC module tree for *name* (schema-driven when present)."""
    from _shared import arxml_ecuc_schema
    model = arxml_ecuc_schema.stub_from_schema(name, ecu_name=ecu_name)
    if model is not None:
        return model
    containers_spec = _MODULE_CONTAINERS.get(name)
    if not containers_spec:
        containers_spec = [
            ("%sGeneral" % name, ["%sDevErrorDetect" % name]),
            ("%sConfig" % name, ["%sMainFunctionPeriod" % name]),
        ]
    children = []
    for cname, params in containers_spec:
        children.append(arxmlparse.EcucContainer(
            name=cname,
            definition="/AUTOSAR/EcucDefs/%s/%s" % (name, cname),
            params=[
                arxmlparse.EcucParam(
                    p,
                    "false" if "Detect" in p or "Api" in p else (
                        "10" if "Period" in p or "Time" in p else ""),
                    "/AUTOSAR/EcucDefs/%s/%s/%s" % (name, cname, p),
                    kind=(
                        "boolean" if "Detect" in p or "Api" in p
                        else ("numerical" if "Period" in p or "Time" in p
                              or "Size" in p else "string")),
                )
                for p in params
            ],
        ))
    if name in ("CanNm", "Nm", "UdpNm") and children:
        children[0].params.append(arxmlparse.EcucParam(
            "%sEcuName" % name, ecu_name,
            "/AUTOSAR/EcucDefs/%s/EcuName" % name, kind="string"))
    mod = arxmlparse.EcucModule(
        name=name,
        definition="/AUTOSAR/EcucDefs/%s" % name,
        containers=children,
    )
    return arxmlparse.EcucModel(
        package="Ecuc_%s" % name, modules=[mod], derived_from_com=False)


def stub_all_modules(ecu_name: str = "Ecu") -> Dict[str, arxmlparse.EcucModel]:
    return {n: stub_module(n, ecu_name=ecu_name) for n in module_names()}


def sync_com_into_bsw(
        bsw: Dict[str, arxmlparse.EcucModel],
        com_model: arxmlparse.ArxmlModel,
        ecu_name: str = "Ecu") -> Dict[str, arxmlparse.EcucModel]:
    """Replace Com/CanIf/PduR/CanNm stubs with COM-derived ECUC-lite."""
    from _shared import arxml_ecuc_schema
    derived = arxmlparse.derive_ecuc_from_com(com_model, ecu_name=ecu_name)
    out = dict(bsw)
    for mod in derived.modules:
        model = arxmlparse.EcucModel(
            package="Ecuc_%s" % mod.name,
            modules=[mod],
            derived_from_com=True,
        )
        # Keep schema singleton General/Public containers if derive omitted them
        sch = arxml_ecuc_schema.load_schema(mod.name)
        if sch and model.modules:
            existing = {
                arxml_ecuc_schema.container_type_of(c)
                for c in model.modules[0].containers}
            for cdef in arxml_ecuc_schema.root_container_defs(sch):
                tname = cdef.get("shortName") or ""
                _lo, up = arxml_ecuc_schema.multiplicity(cdef)
                if up == 1 and tname and tname not in existing:
                    model.modules[0].containers.insert(
                        0,
                        arxml_ecuc_schema.make_container_instance(
                            mod.name, cdef, tname))
        out[mod.name] = model
    # Seed EcuC PDU collection from COM
    ecuc_mod = stub_module("EcuC", ecu_name=ecu_name)
    pdu_children = []
    for pdu in com_model.ipdus:
        pdu_children.append(arxmlparse.EcucContainer(
            name=pdu.name,
            definition="/AUTOSAR/EcucDefs/EcuC/EcucPduCollection/EcucPdu",
            link_pdu=pdu.name,
            params=[
                arxmlparse.EcucParam(
                    "EcucPduId", str(pdu.can_id),
                    "/AUTOSAR/EcucDefs/EcuC/EcucPduId", kind="numerical"),
                arxmlparse.EcucParam(
                    "EcucPduLength", str(pdu.dlc),
                    "/AUTOSAR/EcucDefs/EcuC/EcucPduLength", kind="numerical"),
            ],
        ))
    if ecuc_mod.modules and ecuc_mod.modules[0].containers:
        for c in ecuc_mod.modules[0].containers:
            # Prefer EcucPduCollection under EcucConfigSet
            if arxml_ecuc_schema.container_type_of(c) == "EcucPduCollection":
                c.children = pdu_children
                break
            for ch in c.children:
                if arxml_ecuc_schema.container_type_of(ch) == "EcucPduCollection":
                    ch.children = pdu_children
                    break
    out["EcuC"] = ecuc_mod
    return out


def validate_bsw_set(
        bsw: Dict[str, arxmlparse.EcucModel],
        com: arxmlparse.ArxmlModel) -> List[dict]:
    """Cross-module consistency for enabled BSW ECUC trees."""
    findings: List[dict] = []

    def add(level, rule, message, fix="", location="bsw"):
        sev = {"error": "error", "warn": "warning"}.get(level, "info")
        findings.append({
            "level": level, "severity": sev, "rule": rule,
            "message": message, "fix": fix, "artifact": "bsw",
            "pdu": "", "signal": "", "location": location,
        })

    if not bsw:
        add("warn", "bsw_empty",
            "No BSW modules initialized",
            fix="Project → Init All BSW Modules")
        return findings

    com_names = {p.name for p in com.ipdus}

    # CanIf Tx refs must exist in COM when CanIf present
    if "CanIf" in bsw:
        for mod in bsw["CanIf"].modules:
            def walk(c: arxmlparse.EcucContainer):
                for p in c.params:
                    if p.name in ("CanIfTxPduRef", "CanIfRxPduRef") and p.value:
                        if p.value not in com_names:
                            add("error", "canif_dangling",
                                "CanIf refs missing PDU %s" % p.value,
                                fix="Sync BSW from COM", location=c.name)
                for ch in c.children:
                    walk(ch)
            for c in mod.containers:
                walk(c)

    # PduR route refs
    if "PduR" in bsw and com_names:
        for mod in bsw["PduR"].modules:
            def walk_pdur(c: arxmlparse.EcucContainer):
                for p in c.params:
                    if p.name in ("PduRSrcPduRef", "PduRDestPduRef") and p.value:
                        if p.value not in com_names:
                            add("warn", "pdur_dangling",
                                "PduR refs missing PDU %s" % p.value,
                                fix="Sync BSW from COM", location=c.name)
                for ch in c.children:
                    walk_pdur(ch)
            for c in mod.containers:
                walk_pdur(c)

    # Dcm PDU refs when present
    if "Dcm" in bsw and com_names:
        for mod in bsw["Dcm"].modules:
            def walk_dcm(c: arxmlparse.EcucContainer):
                for p in c.params:
                    if p.name.endswith("PduRef") and p.value and p.value not in com_names:
                        add("warn", "diag_pdu_ref",
                            "Dcm refs missing PDU %s" % p.value,
                            fix="Point at an existing I-PDU",
                            location=c.name)
                for ch in c.children:
                    walk_dcm(ch)
            for c in mod.containers:
                walk_dcm(c)

    # Os task name uniqueness
    if "Os" in bsw:
        seen = set()
        for mod in bsw["Os"].modules:
            for c in mod.containers:
                if c.name.startswith("OsTask") or "Task" in c.definition:
                    for ch in ([c] + list(c.children)):
                        if ch.name in seen:
                            add("warn", "os_task_dup",
                                "Duplicate Os SHORT-NAME %s" % ch.name,
                                fix="Rename one OsTask container",
                                location=ch.name)
                        seen.add(ch.name)

    # Required communication modules when COM has PDUs
    if com.ipdus:
        for req in ("Com", "CanIf", "PduR"):
            if req not in bsw:
                add("warn", "bsw_comm_missing",
                    "COM has PDUs but %s module not enabled" % req,
                    fix="Enable %s or Sync BSW from COM" % req,
                    location=req)

    # Schema multiplicity / enum / range / refs
    try:
        from _shared import arxml_ecuc_schema
        for name, model in bsw.items():
            findings.extend(
                arxml_ecuc_schema.schema_validate_module(name, model, com))
    except Exception:
        pass

    if not findings:
        add("info", "ok", "No BSW issues")
    return findings
