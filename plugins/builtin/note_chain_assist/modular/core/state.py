import os
import uuid

LEFT_BUTTON = 1
RIGHT_BUTTON = 2
CTRL_MODIFIER_MASK = 0x04000000
SHIFT_MODIFIER_MASK = 0x02000000
KEY_A = 0x41
KEY_DELETE = 0x01000007
KEY_BACKSPACE = 0x01000003
MAX_HISTORY = 128
SERIALIZE_DEN = 288
CURVE_CHECKPOINT_PREFIX = "Plugin Curve Edit"
CURVE_SIDECAR_FORMAT_VERSION = 3
DEFAULT_NODE_GROUP_ID = 1
DEFAULT_CURVE_GROUP_ID = 1

STYLE_PRESETS = [
    [4, 8, 12, 16],
    [8, 8, 12, 16, 24],
    [4, 6, 8, 12, 16, 24],
    [12, 16, 24, 32],
]


def build_initial_state():
    return {
        # Anchors are stored in chart space:
        # lane_x in [0..lane_width], beat in timeline beats.
        "anchors": [],
        "links": [],
        "drag": {"mode": "", "index": -1},
        "selected_anchor_ids": [],
        "selected_links": [],
        "selection_targets": {"anchors": True, "segments": True, "notes": False},
        "segment_denominators": {},
        "segment_shapes": {},
        "context_menu_links": [],
        "box_select": {"active": False, "start": [0.0, 0.0], "end": [0.0, 0.0], "append": False},
        "pending_connect_anchor_id": -1,
        "next_anchor_id": 1,
        "last_context": {},
        "last_click_anchor": -1,
        "last_click_ms": 0,
        "style": {"denominators": [4, 8, 12, 16], "style_name": "balanced"},
        "curve_visible": True,
        "active_link_shape": "curve",
        "note_curve_snap_enabled": False,
        "anchor_placement_enabled": False,
        "project_path": "",
        "project_dirty": False,
        "history": [],
        "history_index": -1,
        "lang": "en",
        "curve_revision": 0,
        "curve_samples_cache": {},
        "suppress_persist_once": False,
        "link_drag": {"active": False, "source_anchor_id": -1, "hover_anchor_id": -1, "x": 0.0, "y": 0.0},
        "shift_down": False,
        "last_host_selected_note_ids": [],
        "anchor_group_ids": {},
        "anchor_reserved": {},
        "anchor_compat_handles": {},
        "curve_id_by_link": {},
        "curve_no_by_link": {},
        "curve_group_ids_by_link": {},
        "curve_density_mode_by_link": {},
        "curve_reserved_by_link": {},
        "curve_special_joystick_by_link": {},
        "node_groups": [{"group_id": DEFAULT_NODE_GROUP_ID, "group_name": "base", "reserved": {}}],
        "curve_groups": [{"group_id": DEFAULT_CURVE_GROUP_ID, "group_name": "base", "reserved": {}}],
        "next_curve_id": 1,
        "next_group_id": 2,
        "project_revision": 0,
        "project_file_uuid": "",
        "project_last_writer_instance": "",
        "project_load_failed": False,
        "last_save_error": "",
        "last_save_error_detail": "",
        "host_undo_action_tokens": [],
        "instance_id": f"{os.getpid()}-{uuid.uuid4().hex[:12]}",
    }


STATE = build_initial_state()


def capture_snapshot(state, *, clone_fn, active_link_shape_fn):
    return {
        "anchors": clone_fn(state.get("anchors", [])),
        "links": clone_fn(state.get("links", [])),
        "style": clone_fn(state.get("style", {})),
        "segment_denominators": clone_fn(state.get("segment_denominators", {})),
        "segment_shapes": clone_fn(state.get("segment_shapes", {})),
        "anchor_group_ids": clone_fn(state.get("anchor_group_ids", {})),
        "anchor_reserved": clone_fn(state.get("anchor_reserved", {})),
        "anchor_compat_handles": clone_fn(state.get("anchor_compat_handles", {})),
        "curve_id_by_link": clone_fn(state.get("curve_id_by_link", {})),
        "curve_no_by_link": clone_fn(state.get("curve_no_by_link", {})),
        "curve_group_ids_by_link": clone_fn(state.get("curve_group_ids_by_link", {})),
        "curve_density_mode_by_link": clone_fn(state.get("curve_density_mode_by_link", {})),
        "curve_reserved_by_link": clone_fn(state.get("curve_reserved_by_link", {})),
        "curve_special_joystick_by_link": clone_fn(state.get("curve_special_joystick_by_link", {})),
        "node_groups": clone_fn(state.get("node_groups", [])),
        "curve_groups": clone_fn(state.get("curve_groups", [])),
        "selection_targets": clone_fn(state.get("selection_targets", {"anchors": True, "segments": True, "notes": False})),
        "curve_visible": bool(state.get("curve_visible", True)),
        "active_link_shape": active_link_shape_fn(),
        "note_curve_snap_enabled": bool(state.get("note_curve_snap_enabled", False)),
        "anchor_placement_enabled": bool(state.get("anchor_placement_enabled", False)),
        "next_anchor_id": int(state.get("next_anchor_id", 1)),
        "next_curve_id": int(state.get("next_curve_id", 1)),
        "next_group_id": int(state.get("next_group_id", 2)),
        "pending_connect_anchor_id": int(state.get("pending_connect_anchor_id", -1)),
    }


def restore_snapshot(
    state,
    snapshot,
    *,
    clone_fn,
    default_node_group_id,
    default_curve_group_id,
    ensure_anchor_ids_fn,
    cleanup_links_and_selection_fn,
    seed_missing_segment_denominators_fn,
    invalidate_curve_cache_fn,
):
    if not isinstance(snapshot, dict):
        return
    state["anchors"] = clone_fn(snapshot.get("anchors", [])) if isinstance(
        snapshot.get("anchors"), list) else []
    state["links"] = clone_fn(snapshot.get("links", [])) if isinstance(
        snapshot.get("links"), list) else []
    style = snapshot.get("style")
    state["style"] = clone_fn(style) if isinstance(style, dict) else {
        "denominators": [4, 8, 12, 16], "style_name": "balanced"}
    seg_dens = snapshot.get("segment_denominators")
    state["segment_denominators"] = clone_fn(
        seg_dens) if isinstance(seg_dens, dict) else {}
    seg_shapes = snapshot.get("segment_shapes")
    state["segment_shapes"] = clone_fn(
        seg_shapes) if isinstance(seg_shapes, dict) else {}
    anchor_group_ids = snapshot.get("anchor_group_ids")
    state["anchor_group_ids"] = clone_fn(
        anchor_group_ids) if isinstance(anchor_group_ids, dict) else {}
    anchor_reserved = snapshot.get("anchor_reserved")
    state["anchor_reserved"] = clone_fn(
        anchor_reserved) if isinstance(anchor_reserved, dict) else {}
    anchor_compat = snapshot.get("anchor_compat_handles")
    state["anchor_compat_handles"] = clone_fn(
        anchor_compat) if isinstance(anchor_compat, dict) else {}
    curve_id_by_link = snapshot.get("curve_id_by_link")
    state["curve_id_by_link"] = clone_fn(
        curve_id_by_link) if isinstance(curve_id_by_link, dict) else {}
    curve_no_by_link = snapshot.get("curve_no_by_link")
    state["curve_no_by_link"] = clone_fn(
        curve_no_by_link) if isinstance(curve_no_by_link, dict) else {}
    curve_group_ids_by_link = snapshot.get("curve_group_ids_by_link")
    state["curve_group_ids_by_link"] = clone_fn(
        curve_group_ids_by_link) if isinstance(curve_group_ids_by_link, dict) else {}
    curve_density_mode_by_link = snapshot.get("curve_density_mode_by_link")
    state["curve_density_mode_by_link"] = clone_fn(
        curve_density_mode_by_link) if isinstance(curve_density_mode_by_link, dict) else {}
    curve_reserved_by_link = snapshot.get("curve_reserved_by_link")
    state["curve_reserved_by_link"] = clone_fn(
        curve_reserved_by_link) if isinstance(curve_reserved_by_link, dict) else {}
    curve_special_js = snapshot.get("curve_special_joystick_by_link")
    state["curve_special_joystick_by_link"] = clone_fn(
        curve_special_js) if isinstance(curve_special_js, dict) else {}
    node_groups = snapshot.get("node_groups")
    state["node_groups"] = clone_fn(node_groups) if isinstance(node_groups, list) else [
        {"group_id": default_node_group_id, "group_name": "base", "reserved": {}}]
    curve_groups = snapshot.get("curve_groups")
    state["curve_groups"] = clone_fn(curve_groups) if isinstance(curve_groups, list) else [
        {"group_id": default_curve_group_id, "group_name": "base", "reserved": {}}]
    targets = snapshot.get("selection_targets")
    if isinstance(targets, dict):
        state["selection_targets"] = {
            "anchors": bool(targets.get("anchors", True)),
            "segments": bool(targets.get("segments", True)),
            "notes": bool(targets.get("notes", False)),
        }
    else:
        state["selection_targets"] = {
            "anchors": True, "segments": True, "notes": False}
    state["curve_visible"] = bool(snapshot.get("curve_visible", True))
    active_shape = str(snapshot.get("active_link_shape", snapshot.get(
        "curve_shape", "curve")) or "curve").strip().lower()
    state["active_link_shape"] = "polyline" if active_shape == "polyline" else "curve"
    state["note_curve_snap_enabled"] = bool(
        snapshot.get("note_curve_snap_enabled", False))
    state["anchor_placement_enabled"] = bool(
        snapshot.get("anchor_placement_enabled", False))
    state["drag"] = {"mode": "", "index": -1}
    state["selected_anchor_ids"] = []
    state["selected_links"] = []
    state["box_select"] = {"active": False, "start": [
        0.0, 0.0], "end": [0.0, 0.0], "append": False}
    state["link_drag"] = {"active": False, "source_anchor_id": -
                          1, "hover_anchor_id": -1, "x": 0.0, "y": 0.0}
    state["shift_down"] = False
    state["pending_connect_anchor_id"] = int(
        snapshot.get("pending_connect_anchor_id", -1))
    state["next_anchor_id"] = max(1, int(snapshot.get("next_anchor_id", 1)))
    state["next_curve_id"] = max(1, int(snapshot.get("next_curve_id", 1)))
    state["next_group_id"] = max(2, int(snapshot.get("next_group_id", 2)))
    ensure_anchor_ids_fn()
    cleanup_links_and_selection_fn()
    seed_missing_segment_denominators_fn()
    invalidate_curve_cache_fn()


def push_history(state, snapshot, *, max_history):
    hist = state.get("history", [])
    idx = int(state.get("history_index", -1))
    if 0 <= idx < len(hist):
        if hist[idx] == snapshot:
            return False
        hist = hist[: idx + 1]
    else:
        hist = []

    hist.append(snapshot)
    if len(hist) > max_history:
        hist = hist[-max_history:]
    state["history"] = hist
    state["history_index"] = len(hist) - 1
    return True


def undo_history(state, *, restore_snapshot_fn, mark_dirty_fn, context):
    hist = state.get("history", [])
    idx = int(state.get("history_index", -1))
    if not hist or idx <= 0:
        return False
    idx -= 1
    state["history_index"] = idx
    restore_snapshot_fn(hist[idx])
    mark_dirty_fn(context, flush=False)
    return True


def redo_history(state, *, restore_snapshot_fn, mark_dirty_fn, context):
    hist = state.get("history", [])
    idx = int(state.get("history_index", -1))
    if not hist or idx >= len(hist) - 1:
        return False
    idx += 1
    state["history_index"] = idx
    restore_snapshot_fn(hist[idx])
    mark_dirty_fn(context, flush=False)
    return True
