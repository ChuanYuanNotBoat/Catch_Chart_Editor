
def parse_int(value, fallback=0):
    try:
        return int(value)
    except Exception:
        return int(fallback)


def parse_float(value, fallback=0.0):
    try:
        return float(value)
    except Exception:
        return float(fallback)


def triplet_from_any(value, fallback_float, *, triplet_to_float, float_to_triplet, serialize_den):
    beat = triplet_to_float(value)
    if beat is None:
        beat = parse_float(value, fallback_float)
    return float_to_triplet(float(beat), serialize_den)


def beat_from_any(value, fallback_float, *, triplet_to_float):
    beat = triplet_to_float(value)
    if beat is None:
        beat = parse_float(value, fallback_float)
    return float(beat)


def clone_dict_or_empty(value, *, clone):
    return clone(value) if isinstance(value, dict) else {}


def unique_positive_int_list(value, default_id):
    out = []
    used = set()
    if isinstance(value, list):
        for raw in value:
            iv = parse_int(raw, 0)
            if iv <= 0 or iv in used:
                continue
            used.add(iv)
            out.append(iv)
    if not out and default_id > 0:
        out = [int(default_id)]
    return out


def next_available_positive(used, start=1):
    value = max(1, int(start))
    while value in used:
        value += 1
    return value


def default_group_entry(group_id, name):
    return {"group_id": int(group_id), "group_name": str(name), "reserved": {}}


def dedupe_group_names(groups):
    used_names = set()
    for g in groups:
        base = str(g.get("group_name", "") or "").strip()
        if not base:
            base = f"group_{int(g.get('group_id', 0))}"
        name = base
        suffix = 2
        while name.casefold() in used_names:
            name = f"{base}_{suffix}"
            suffix += 1
        used_names.add(name.casefold())
        g["group_name"] = name


def normalize_group_entries(entries, default_group_id, default_name, *, state, clone):
    raw_entries = entries if isinstance(entries, list) else []
    normalized = []
    used_ids = set()
    auto_id = max(2, int(state.get("next_group_id", 2)))

    for raw in raw_entries:
        if not isinstance(raw, dict):
            continue
        gid = parse_int(raw.get("group_id", 0), 0)
        if gid <= 0 or gid in used_ids:
            gid = next_available_positive(used_ids, auto_id)
            auto_id = gid + 1
        used_ids.add(gid)
        normalized.append({
            "group_id": gid,
            "group_name": str(raw.get("group_name", "") or "").strip(),
            "reserved": clone_dict_or_empty(raw.get("reserved"), clone=clone),
        })

    if default_group_id not in used_ids:
        normalized.insert(0, default_group_entry(
            default_group_id, default_name))
        used_ids.add(default_group_id)

    dedupe_group_names(normalized)
    max_id = max(used_ids) if used_ids else 1
    state["next_group_id"] = max(
        int(state.get("next_group_id", 2)), max_id + 1)
    return normalized


def ensure_groups_contain_ids(groups_key, required_ids, default_prefix, *, state):
    groups = state.get(groups_key, [])
    if not isinstance(groups, list):
        groups = []
    used_ids = set()
    for g in groups:
        if not isinstance(g, dict):
            continue
        gid = parse_int(g.get("group_id", 0), 0)
        if gid > 0:
            used_ids.add(gid)
    for gid in required_ids:
        if gid in used_ids or gid <= 0:
            continue
        groups.append(default_group_entry(gid, f"{default_prefix}_{gid}"))
        used_ids.add(gid)
    dedupe_group_names(groups)
    state[groups_key] = groups
    if used_ids:
        state["next_group_id"] = max(
            int(state.get("next_group_id", 2)), max(used_ids) + 1)


def set_save_error(state, code, detail=""):
    state["last_save_error"] = str(code or "")
    state["last_save_error_detail"] = str(detail or "")


def read_disk_payload(path, *, json_module, os_module):
    if not os_module.path.exists(path):
        return None
    with open(path, "r", encoding="utf-8-sig") as f:
        return json_module.load(f)


def save_project(
    state,
    path,
    context,
    *,
    os_module,
    json_module,
    time_module,
    read_disk_payload_fn,
    build_v3_payload_fn,
    parse_int_fn,
    set_save_error_fn,
):
    if not isinstance(path, str) or not path.strip():
        set_save_error_fn("invalid_path")
        return False
    try:
        os_module.makedirs(os_module.path.dirname(path), exist_ok=True)

        disk_payload = None
        if os_module.path.exists(path):
            try:
                disk_payload = read_disk_payload_fn(path)
            except Exception as ex:
                set_save_error_fn("read_existing_failed", str(ex))
                return False

        disk_revision = 0
        if isinstance(disk_payload, dict):
            disk_revision = max(0, parse_int_fn(
                disk_payload.get("revision", 0), 0))
        state_revision = max(0, parse_int_fn(
            state.get("project_revision", 0), 0))
        if disk_revision != state_revision:
            set_save_error_fn(
                "revision_conflict", "file updated by another instance, please refresh")
            return False

        payload = build_v3_payload_fn()
        payload["revision"] = disk_revision + 1
        payload["updated_at"] = int(time_module.time() * 1000)
        payload["last_writer_instance"] = str(
            state.get("instance_id", "") or "")

        tmp_path = f"{path}.tmp.{os_module.getpid()}.{int(time_module.time() * 1000)}"
        with open(tmp_path, "w", encoding="utf-8") as f:
            json_module.dump(payload, f, ensure_ascii=False, indent=2)
        os_module.replace(tmp_path, path)

        state["project_dirty"] = False
        state["project_revision"] = int(payload.get("revision", 0))
        state["project_file_uuid"] = str(payload.get("file_uuid", "") or "")
        state["project_last_writer_instance"] = str(
            payload.get("last_writer_instance", "") or "")
        set_save_error_fn("", "")
        return True
    except Exception as ex:
        set_save_error_fn("write_failed", str(ex))
        return False


def load_project(
    state,
    path,
    context,
    *,
    os_module,
    json_module,
    parse_int_fn,
    set_save_error_fn,
    load_project_v2_payload_fn,
    load_project_v3_payload_fn,
    invalidate_curve_cache_fn,
    format_version_threshold,
):
    if not isinstance(path, str) or not path.strip() or not os_module.path.exists(path):
        return False
    try:
        with open(path, "r", encoding="utf-8-sig") as f:
            payload = json_module.load(f)

        state["segment_denominators"] = {}
        state["segment_shapes"] = {}
        format_version = parse_int_fn(payload.get("format_version", 0), 0)
        if format_version >= format_version_threshold or ("nodes" in payload and "curves" in payload):
            load_project_v3_payload_fn(payload, context)
        else:
            load_project_v2_payload_fn(payload, context)

        invalidate_curve_cache_fn()
        state["project_dirty"] = False
        set_save_error_fn("", "")
        return True
    except Exception as ex:
        set_save_error_fn("load_failed", str(ex))
        return False
