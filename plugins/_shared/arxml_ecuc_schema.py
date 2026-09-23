# -*- coding: utf-8 -*-
"""AUTOSAR CP ECUC schema packs — drive BSW stubs + configurator (no codegen)."""

from __future__ import annotations

import json
import os
import re
from typing import Any, Dict, List, Optional, Tuple

from _shared import arxmlparse

_SCHEMA_DIR = os.path.join(os.path.dirname(__file__), "ecuc_schemas")
_CACHE: Dict[str, dict] = {}


def schema_dir() -> str:
    return _SCHEMA_DIR


def load_schema(module: str) -> Optional[dict]:
    """Load curated EcucDefs-shaped JSON for *module* (cached)."""
    if module in _CACHE:
        return _CACHE[module]
    path = os.path.join(_SCHEMA_DIR, "%s.json" % module)
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    _CACHE[module] = data
    return data


def clear_schema_cache() -> None:
    _CACHE.clear()


def list_schema_modules() -> List[str]:
    if not os.path.isdir(_SCHEMA_DIR):
        return []
    out = []
    for name in os.listdir(_SCHEMA_DIR):
        if name.endswith(".json"):
            out.append(name[:-5])
    return sorted(out)


def container_defs(schema: dict) -> List[dict]:
    return list(schema.get("containers") or [])


def find_cdef(schema: dict, short_name: str) -> Optional[dict]:
    for c in container_defs(schema):
        if c.get("shortName") == short_name:
            return c
    return None


def _parse_mult(val) -> Tuple[int, Optional[int]]:
    """Return (lower, upper) where upper None means unbounded."""
    lower = int(val.get("lowerMultiplicity", 0) if isinstance(val, dict) else 0)
    if isinstance(val, dict):
        raw = val.get("upperMultiplicity", "*")
    else:
        raw = "*"
    if raw in ("*", "n", "N", ""):
        return lower, None
    try:
        return lower, int(raw)
    except (TypeError, ValueError):
        return lower, None


def multiplicity(cdef: dict) -> Tuple[int, Optional[int]]:
    return _parse_mult(cdef)


def def_path(module: str, *parts: str) -> str:
    base = "/AUTOSAR/EcucDefs/%s" % module
    for p in parts:
        if p:
            base = "%s/%s" % (base, p)
    return base


def _default_for_param(pdef: dict) -> str:
    if pdef.get("default") not in (None, ""):
        return str(pdef["default"])
    kind = (pdef.get("kind") or "string").lower()
    name = str(pdef.get("shortName") or "")
    if kind == "boolean" or "Detect" in name or "Api" in name or "Enabled" in name:
        return "false"
    if kind == "numerical" or "Period" in name or "Time" in name or "Size" in name:
        return str(pdef.get("min", 10) if pdef.get("min") is not None else 10)
    if kind == "enumeration":
        lits = pdef.get("literals") or []
        return str(lits[0]) if lits else ""
    return ""


def make_param(module: str, cdef: dict, pdef: dict) -> arxmlparse.EcucParam:
    cname = cdef.get("shortName") or "Container"
    pname = pdef.get("shortName") or "Param"
    definition = pdef.get("definition") or def_path(module, cname, pname)
    kind = (pdef.get("kind") or "string").lower()
    return arxmlparse.EcucParam(
        name=pname,
        value=_default_for_param(pdef),
        definition=definition,
        kind=kind,
    )


def make_container_instance(
        module: str, cdef: dict, instance_name: str = "") -> arxmlparse.EcucContainer:
    cname = cdef.get("shortName") or "Container"
    name = instance_name or cname
    definition = cdef.get("definition") or def_path(module, cname)
    params = [make_param(module, cdef, p) for p in (cdef.get("params") or [])]
    return arxmlparse.EcucContainer(
        name=name, definition=definition, params=params, children=[])


def container_type_of(cont: arxmlparse.EcucContainer) -> str:
    """Best-effort schema type from DEFINITION-REF or SHORT-NAME prefix."""
    if cont.definition:
        parts = [p for p in cont.definition.split("/") if p]
        if parts:
            return parts[-1]
    # OsTask_0 → OsTask
    m = re.match(r"^([A-Za-z][A-Za-z0-9]*?)(?:_\d+)?$", cont.name or "")
    if m:
        return m.group(1)
    return cont.name or ""


def count_type(
        containers: List[arxmlparse.EcucContainer], type_name: str) -> int:
    return sum(1 for c in containers if container_type_of(c) == type_name)


def unique_instance_name(
        containers: List[arxmlparse.EcucContainer], type_name: str) -> str:
    existing = {c.name for c in containers}
    if type_name not in existing and count_type(containers, type_name) == 0:
        # first instance can use bare type name if free
        if type_name not in existing:
            return type_name
    n = 0
    while True:
        cand = "%s_%d" % (type_name, n)
        if cand not in existing:
            return cand
        n += 1


def root_container_defs(schema: dict) -> List[dict]:
    """Containers with no parent (or parent empty) — live under module."""
    out = []
    for c in container_defs(schema):
        parent = c.get("parent") or ""
        if not parent:
            out.append(c)
    return out


def child_defs_for(schema: dict, parent_type: str) -> List[dict]:
    """Child container defs for *parent_type* (parent field + subContainers)."""
    by_name = {
        c.get("shortName"): c for c in container_defs(schema)
        if c.get("shortName")
    }
    parent_cdef = by_name.get(parent_type)
    sub_names = list(parent_cdef.get("subContainers") or []) if parent_cdef else []
    out: List[dict] = []
    seen = set()
    for c in container_defs(schema):
        sn = c.get("shortName") or ""
        if not sn or sn in seen:
            continue
        if (c.get("parent") or "") == parent_type:
            out.append(c)
            seen.add(sn)
    for sn in sub_names:
        if sn in seen:
            continue
        cdef = by_name.get(sn)
        if cdef is not None:
            out.append(cdef)
            seen.add(sn)
    return out


def _seed_children(
        module: str, schema: dict, parent_cdef: dict,
        parent_cont: arxmlparse.EcucContainer) -> None:
    """Seed required / singleton / seed-flagged children (recursive)."""
    type_name = parent_cdef.get("shortName") or "Container"
    for child_def in child_defs_for(schema, type_name):
        cl, cu = multiplicity(child_def)
        cn = cl
        if cn == 0 and child_def.get("seed"):
            cn = 1
        if cn == 0 and cu == 1:
            cn = 1
        if cn <= 0:
            continue
        ct = child_def.get("shortName") or "Child"
        for j in range(cn):
            cname = ct if cn == 1 else "%s_%d" % (ct, j)
            child_cont = make_container_instance(module, child_def, cname)
            _seed_children(module, schema, child_def, child_cont)
            parent_cont.children.append(child_cont)


def stub_from_schema(
        module: str, ecu_name: str = "Ecu",
        schema: Optional[dict] = None) -> Optional[arxmlparse.EcucModel]:
    """Build ECUC tree from schema: create required (lower≥1) root instances."""
    schema = schema or load_schema(module)
    if not schema:
        return None
    roots: List[arxmlparse.EcucContainer] = []
    for cdef in root_container_defs(schema):
        lower, upper = multiplicity(cdef)
        type_name = cdef.get("shortName") or "Container"
        n_create = lower
        if n_create == 0 and cdef.get("seed"):
            n_create = 1
        if n_create == 0 and upper == 1:
            n_create = 1  # singleton containers always present
        for i in range(n_create):
            if n_create == 1:
                iname = type_name
            else:
                iname = "%s_%d" % (type_name, i)
            cont = make_container_instance(module, cdef, iname)
            _seed_children(module, schema, cdef, cont)
            roots.append(cont)
    if not roots and container_defs(schema):
        cdef = container_defs(schema)[0]
        roots.append(make_container_instance(
            module, cdef, cdef.get("shortName") or module))
    if module in ("CanNm", "Nm", "UdpNm") and roots:
        # avoid duplicate EcuName
        if not any(p.name == ("%sEcuName" % module) for p in roots[0].params):
            roots[0].params.append(arxmlparse.EcucParam(
                "%sEcuName" % module, ecu_name,
                def_path(module, "EcuName"), kind="string"))
    mod = arxmlparse.EcucModule(
        name=module,
        definition=schema.get("definition") or def_path(module),
        containers=roots,
    )
    return arxmlparse.EcucModel(
        package="Ecuc_%s" % module, modules=[mod], derived_from_com=False)


def can_add(
        schema: dict, parent_containers: List[arxmlparse.EcucContainer],
        type_name: str) -> bool:
    cdef = find_cdef(schema, type_name)
    if not cdef:
        return False
    _lo, up = multiplicity(cdef)
    n = count_type(parent_containers, type_name)
    if up is None:
        return True
    return n < up


def can_remove(
        schema: dict, parent_containers: List[arxmlparse.EcucContainer],
        type_name: str) -> bool:
    cdef = find_cdef(schema, type_name)
    if not cdef:
        return True
    lower, _up = multiplicity(cdef)
    n = count_type(parent_containers, type_name)
    return n > lower


def add_container(
        model: arxmlparse.EcucModel, module: str, schema: dict,
        parent_path: Tuple[int, ...], type_name: str
) -> Optional[arxmlparse.EcucContainer]:
    """Add a container instance under parent_path (empty = module roots)."""
    if not model.modules:
        return None
    cdef = find_cdef(schema, type_name)
    if not cdef:
        return None
    mod = model.modules[0]
    parent_list, parent_cont = _parent_list(mod, parent_path)
    if parent_list is None:
        return None
    if not can_add(schema, parent_list, type_name):
        return None
    # Validate parent type if schema requires parent
    req_parent = cdef.get("parent") or ""
    if req_parent:
        if parent_cont is None:
            return None
        if container_type_of(parent_cont) != req_parent:
            return None
    elif parent_path:
        # root-only type cannot nest
        return None
    name = unique_instance_name(parent_list, type_name)
    cont = make_container_instance(module, cdef, name)
    _seed_children(module, schema, cdef, cont)
    parent_list.append(cont)
    return cont


def remove_container(
        model: arxmlparse.EcucModel, schema: dict,
        path: Tuple[int, ...]) -> bool:
    if not model.modules or not path:
        return False
    mod = model.modules[0]
    parent_path = path[:-1]
    idx = path[-1]
    parent_list, _pc = _parent_list(mod, parent_path)
    if parent_list is None or idx < 0 or idx >= len(parent_list):
        return False
    target = parent_list[idx]
    tname = container_type_of(target)
    if not can_remove(schema, parent_list, tname):
        return False
    del parent_list[idx]
    return True


def duplicate_container(
        model: arxmlparse.EcucModel, module: str, schema: dict,
        path: Tuple[int, ...]) -> Optional[arxmlparse.EcucContainer]:
    if not model.modules or not path:
        return None
    mod = model.modules[0]
    parent_path = path[:-1]
    idx = path[-1]
    parent_list, _pc = _parent_list(mod, parent_path)
    if parent_list is None or idx < 0 or idx >= len(parent_list):
        return None
    src = parent_list[idx]
    tname = container_type_of(src)
    if not can_add(schema, parent_list, tname):
        return None
    import copy
    clone = copy.deepcopy(src)
    clone.name = unique_instance_name(parent_list, tname)
    parent_list.append(clone)
    return clone


def _parent_list(mod: arxmlparse.EcucModule, parent_path: Tuple[int, ...]):
    containers = mod.containers
    parent_cont = None
    for i in parent_path:
        if i < 0 or i >= len(containers):
            return None, None
        parent_cont = containers[i]
        containers = parent_cont.children
    return containers, parent_cont


def addable_types(
        schema: dict, parent_type: Optional[str]) -> List[dict]:
    """Schema container defs that may be added under parent_type (None=root)."""
    out = []
    for c in container_defs(schema):
        p = c.get("parent") or ""
        if parent_type is None or parent_type == "":
            if not p:
                out.append(c)
        elif p == parent_type:
            out.append(c)
    return out


def tip_rows_from_schema(schema: dict) -> List[dict]:
    """Build BSWMD-lite tip rows for Spec / tip_for_field."""
    module = schema.get("module") or ""
    rows = []

    def walk(cdefs: List[dict]) -> None:
        for cdef in cdefs:
            cname = cdef.get("shortName") or ""
            for pdef in (cdef.get("params") or []):
                pname = pdef.get("shortName") or ""
                if not pname:
                    continue
                fid = "ecuc.%s.%s" % (module.lower(), pname)
                rows.append({
                    "id": fid,
                    "title": "%s / %s" % (cname, pname),
                    "category": "ECUC / %s" % module,
                    "summary": pdef.get("summary") or pname,
                    "detail": pdef.get("detail") or (
                        pdef.get("definition") or ""),
                    "range": pdef.get("range") or "",
                    "definition": pdef.get("definition") or "",
                })
            sub_names = cdef.get("subContainers") or []
            if sub_names:
                by_name = {
                    c.get("shortName"): c for c in container_defs(schema)
                    if c.get("shortName")
                }
                walk([by_name[s] for s in sub_names if s in by_name])

    walk(container_defs(schema))
    return rows


def register_schema_tips(module: str = "") -> int:
    """Load tips from one or all schemas into arxmlparse extra BSWMD."""
    mods = [module] if module else list_schema_modules()
    rows: List[dict] = []
    for m in mods:
        sch = load_schema(m)
        if not sch:
            continue
        rows.extend(tip_rows_from_schema(sch))
    return arxmlparse.merge_bswmd_tips(rows)


def schema_validate_module(
        module: str, model: arxmlparse.EcucModel,
        com: Optional[arxmlparse.ArxmlModel] = None) -> List[dict]:
    """Schema-aware checks: required containers, enum/range, refs."""
    findings: List[dict] = []
    schema = load_schema(module)
    if not schema or not model.modules:
        return findings
    mod = model.modules[0]
    com_names = {p.name for p in (com.ipdus if com else [])}

    def walk(containers: List[arxmlparse.EcucContainer], parent_type: str):
        # required root/child types
        needed = (
            root_container_defs(schema) if not parent_type
            else child_defs_for(schema, parent_type))
        for cdef in needed:
            tname = cdef.get("shortName") or ""
            lower, _up = multiplicity(cdef)
            n = count_type(containers, tname)
            if n < lower:
                findings.append({
                    "level": "warn", "severity": "warning",
                    "rule": "ecuc_mult_low",
                    "message": "%s needs ≥%d %s (have %d)" % (
                        module, lower, tname, n),
                    "fix": "Add %s" % tname,
                    "artifact": "bsw", "pdu": "", "signal": "",
                    "location": module,
                    "module": module,
                    "container_type": tname,
                    "parent_type": parent_type or "",
                })
        for c in containers:
            tname = container_type_of(c)
            cdef = find_cdef(schema, tname)
            if cdef:
                pmap = {
                    p.get("shortName"): p for p in (cdef.get("params") or [])}
                for p in c.params:
                    pdef = pmap.get(p.name)
                    if not pdef:
                        continue
                    kind = (pdef.get("kind") or "").lower()
                    if kind == "enumeration":
                        lits = [str(x) for x in (pdef.get("literals") or [])]
                        if lits and p.value and p.value not in lits:
                            findings.append({
                                "level": "warn", "severity": "warning",
                                "rule": "ecuc_enum",
                                "message": "%s.%s value %r not in %s" % (
                                    c.name, p.name, p.value, lits),
                                "fix": "Pick a listed literal",
                                "artifact": "bsw", "pdu": "", "signal": "",
                                "location": c.name,
                            })
                    if kind == "numerical" and p.value not in ("",):
                        try:
                            v = float(p.value)
                            if pdef.get("min") is not None and v < float(pdef["min"]):
                                findings.append({
                                    "level": "warn", "severity": "warning",
                                    "rule": "ecuc_range",
                                    "message": "%s.%s below min" % (c.name, p.name),
                                    "fix": "Set ≥ %s" % pdef["min"],
                                    "artifact": "bsw", "pdu": "", "signal": "",
                                    "location": c.name,
                                })
                            if pdef.get("max") is not None and v > float(pdef["max"]):
                                findings.append({
                                    "level": "warn", "severity": "warning",
                                    "rule": "ecuc_range",
                                    "message": "%s.%s above max" % (c.name, p.name),
                                    "fix": "Set ≤ %s" % pdef["max"],
                                    "artifact": "bsw", "pdu": "", "signal": "",
                                    "location": c.name,
                                })
                        except ValueError:
                            findings.append({
                                "level": "warn", "severity": "warning",
                                "rule": "ecuc_num",
                                "message": "%s.%s not numeric" % (c.name, p.name),
                                "fix": "Enter a number",
                                "artifact": "bsw", "pdu": "", "signal": "",
                                "location": c.name,
                            })
                    if kind == "reference" and p.value and com_names:
                        dest = (pdef.get("destination") or "").lower()
                        if "pdu" in dest or p.name.endswith("PduRef"):
                            if p.value not in com_names:
                                findings.append({
                                    "level": "warn", "severity": "warning",
                                    "rule": "ecuc_ref",
                                    "message": "%s.%s missing PDU %s" % (
                                        c.name, p.name, p.value),
                                    "fix": "Clear ref or Sync from COM",
                                    "artifact": "bsw", "pdu": p.value,
                                    "signal": "", "location": c.name,
                                    "module": module,
                                    "param": p.name,
                                    "container": c.name,
                                })
            walk(c.children, tname)

    walk(mod.containers, "")
    return findings


def apply_live_fix(
        module: str, model: arxmlparse.EcucModel, finding: dict,
        schema: Optional[dict] = None) -> Tuple[bool, str]:
    """Apply a one-click fix for a BSW live finding. Returns (ok, note)."""
    schema = schema or load_schema(module)
    if not schema or not model.modules:
        return False, "No schema/model"
    rule = finding.get("rule") or ""
    if rule == "ecuc_mult_low":
        tname = finding.get("container_type") or ""
        parent_type = finding.get("parent_type") or ""
        if not tname:
            return False, "Missing container type"
        # Find a parent path: prefer first matching parent_type, else root.
        mod = model.modules[0]
        parent_path: Tuple[int, ...] = ()
        if parent_type:
            def find_parent(
                    containers, base: Tuple[int, ...]
            ) -> Optional[Tuple[int, ...]]:
                for i, c in enumerate(containers):
                    if container_type_of(c) == parent_type:
                        return base + (i,)
                    hit = find_parent(c.children, base + (i,))
                    if hit is not None:
                        return hit
                return None
            found = find_parent(mod.containers, ())
            if found is None:
                return False, "Parent %s not found" % parent_type
            parent_path = found
        cont = add_container(model, module, schema, parent_path, tname)
        if cont is None:
            return False, "Cannot add %s" % tname
        return True, "Added %s" % cont.name
    if rule in ("ecuc_ref", "canif_dangling", "pdur_dangling", "diag_pdu_ref"):
        target = finding.get("container") or finding.get("location") or ""
        param_name = finding.get("param") or ""
        cleared = 0

        def walk(containers):
            nonlocal cleared
            for c in containers:
                if target and c.name == target:
                    for p in c.params:
                        if param_name and p.name != param_name:
                            continue
                        if (p.name.endswith("PduRef")
                                or p.name in (
                                    "CanIfTxPduRef", "CanIfRxPduRef",
                                    "PduRSrcPduRef", "PduRDestPduRef",
                                    "ComPduIdRef")):
                            if p.value:
                                p.value = ""
                                cleared += 1
                walk(c.children)

        walk(model.modules[0].containers)
        if cleared:
            return True, "Cleared %d dangling ref(s)" % cleared
        return False, "Ref target not found"
    return False, "No fix for %s" % rule


def collect_ref_choices(
        module: str, pdef: dict, document) -> List[str]:
    """COM I-PDU names + sibling SHORT-NAMEs for reference pickers."""
    names: List[str] = []
    dest = (pdef.get("destination") or "").lower()
    pname = pdef.get("shortName") or ""
    want_pdu = "pdu" in dest or pname.endswith("PduRef") or "Pdu" in pname
    if want_pdu and hasattr(document, "model"):
        for pdu in document.model.ipdus:
            if pdu.name and pdu.name not in names:
                names.append(pdu.name)
    # Sibling container short names in active module
    bsw = getattr(document, "bsw", None) or {}
    model = bsw.get(module)
    if model and model.modules:
        def walk(containers):
            for c in containers:
                if c.name and c.name not in names:
                    names.append(c.name)
                walk(c.children)
        walk(model.modules[0].containers)
    # CanIf PDU short names when editing other modules
    if want_pdu and "CanIf" in bsw and module != "CanIf":
        cif = bsw["CanIf"]
        if cif.modules:
            def walk_cif(containers):
                for c in containers:
                    if c.name and c.name not in names:
                        names.append(c.name)
                    walk_cif(c.children)
            walk_cif(cif.modules[0].containers)
    return names


def build_schema_dict(
        module: str,
        containers: List[Dict[str, Any]],
        release: str = "R20-11") -> dict:
    return {
        "module": module,
        "definition": def_path(module),
        "autosarRelease": release,
        "containers": containers,
    }


def param_def(
        short: str, kind: str = "string", default: str = "",
        summary: str = "", literals: Optional[List[str]] = None,
        min_v=None, max_v=None, destination: str = "",
        detail: str = "", range_s: str = "",
        post_build: Optional[bool] = None,
        condition: str = "") -> dict:
    d: Dict[str, Any] = {
        "shortName": short,
        "kind": kind,
        "default": default,
        "summary": summary or short,
        "detail": detail,
        "range": range_s,
    }
    if literals is not None:
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


def cont_def(
        short: str, params: List[dict], parent: str = "",
        lower: int = 0, upper="*", seed: bool = False,
        sub: Optional[List[str]] = None) -> dict:
    return {
        "shortName": short,
        "definition": "",  # filled on write
        "lowerMultiplicity": lower,
        "upperMultiplicity": upper,
        "parent": parent,
        "seed": seed,
        "params": params,
        "subContainers": sub or [],
    }


def param_condition_met(pdef: dict, param_values: Dict[str, str]) -> bool:
    """Evaluate simple ``condition`` like ``ComIPduDirection==SEND``."""
    cond = (pdef.get("condition") or "").strip()
    if not cond:
        return True
    if "==" in cond:
        left, right = cond.split("==", 1)
        return (param_values.get(left.strip()) or "") == right.strip()
    if "!=" in cond:
        left, right = cond.split("!=", 1)
        return (param_values.get(left.strip()) or "") != right.strip()
    return True


def import_bswmd_schema(path: str, *, write: bool = True) -> Tuple[str, int]:
    """Import EcucDefs-shaped BSWMD JSON as schema *structure* (not tips only).

    Accepts either a single-module file (``module`` + ``containers``) or a
    multi-module pack ``{modules:[{name, containers|params}]}``.

    Returns ``(module_name, params_merged)``. Merges into existing
    ``ecuc_schemas/<Module>.json`` when *write* is True; always updates cache.
    """
    import json
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    modules_in: List[dict] = []
    if data.get("module") and data.get("containers"):
        modules_in.append(data)
    else:
        for mod in data.get("modules") or []:
            mname = str(mod.get("name") or mod.get("module") or "")
            if not mname:
                continue
            containers = list(mod.get("containers") or [])
            # Flat params → synthetic General container
            flat = list(mod.get("params") or [])
            if flat and not containers:
                containers = [{
                    "shortName": "%sGeneral" % mname,
                    "lowerMultiplicity": 1,
                    "upperMultiplicity": 1,
                    "parent": "",
                    "seed": False,
                    "params": [
                        {
                            "shortName": p.get("name") or p.get("shortName"),
                            "kind": p.get("kind") or "string",
                            "default": p.get("default") or "",
                            "summary": p.get("summary") or "",
                            "detail": p.get("detail") or p.get("description") or "",
                            "range": p.get("range") or "",
                            "definition": p.get("definition") or "",
                            "literals": p.get("literals"),
                            "min": p.get("min"),
                            "max": p.get("max"),
                            "destination": p.get("destination") or "",
                        }
                        for p in flat if (p.get("name") or p.get("shortName"))
                    ],
                    "subContainers": [],
                }]
            modules_in.append({
                "module": mname,
                "definition": mod.get("definition") or def_path(mname),
                "autosarRelease": mod.get("autosarRelease") or "R22-11",
                "containers": containers,
            })

    if not modules_in:
        raise ValueError("No module/containers in BSWMD file")

    last_module = ""
    total_merged = 0
    for incoming in modules_in:
        module = str(incoming.get("module") or "")
        last_module = module
        existing = load_schema(module) or {
            "module": module,
            "definition": def_path(module),
            "autosarRelease": "R22-11",
            "containers": [],
        }
        by_c = {
            c.get("shortName"): dict(c)
            for c in (existing.get("containers") or [])
        }
        for c in incoming.get("containers") or []:
            cname = c.get("shortName") or c.get("name")
            if not cname:
                continue
            # Normalize param field names
            params = []
            for p in c.get("params") or []:
                sn = p.get("shortName") or p.get("name")
                if not sn:
                    continue
                params.append({
                    "shortName": sn,
                    "kind": p.get("kind") or "string",
                    "default": p.get("default") if p.get("default") is not None else "",
                    "summary": p.get("summary") or sn,
                    "detail": p.get("detail") or p.get("description") or sn,
                    "range": p.get("range") or "",
                    "definition": p.get("definition") or def_path(
                        module, cname, sn),
                    **({k: p[k] for k in (
                        "literals", "min", "max", "destination",
                        "postBuild", "condition") if k in p and p[k] is not None}),
                })
            if cname in by_c:
                cur = by_c[cname]
                have = {x.get("shortName"): x for x in (cur.get("params") or [])}
                for p in params:
                    if p["shortName"] in have:
                        # Enrich prose
                        old = have[p["shortName"]]
                        for k in ("summary", "detail", "range", "kind",
                                  "literals", "min", "max", "destination",
                                  "postBuild", "condition"):
                            if p.get(k) not in (None, "", []):
                                if k in ("summary", "detail") and (
                                        not old.get(k) or old.get(k) == p["shortName"]):
                                    old[k] = p[k]
                                elif k not in old or old[k] in (None, "", []):
                                    old[k] = p[k]
                        have[p["shortName"]] = old
                    else:
                        have[p["shortName"]] = p
                        total_merged += 1
                cur["params"] = list(have.values())
                by_c[cname] = cur
            else:
                by_c[cname] = {
                    "shortName": cname,
                    "definition": c.get("definition") or def_path(module, cname),
                    "lowerMultiplicity": c.get("lowerMultiplicity", 0),
                    "upperMultiplicity": c.get("upperMultiplicity", "*"),
                    "parent": c.get("parent") or "",
                    "seed": bool(c.get("seed")),
                    "params": params,
                    "subContainers": list(c.get("subContainers") or []),
                }
                total_merged += len(params)
        existing["containers"] = list(by_c.values())
        existing["autosarRelease"] = incoming.get(
            "autosarRelease") or existing.get("autosarRelease") or "R22-11"
        _CACHE[module] = existing
        if write:
            out = os.path.join(schema_dir(), "%s.json" % module)
            with open(out, "w", encoding="utf-8") as f:
                json.dump(existing, f, indent=2, ensure_ascii=False)
                f.write("\n")
        # Also register tips from imported params
        tip_rows = tip_rows_from_schema(existing)
        if tip_rows:
            arxmlparse.merge_bswmd_tips(tip_rows)
    return last_module, total_merged
