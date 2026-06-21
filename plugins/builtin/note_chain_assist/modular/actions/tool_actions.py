
def list_tool_actions(callbacks):
    state = callbacks["state"]
    tr = callbacks["tr"]
    active_link_shape_is_polyline = callbacks["active_link_shape_is_polyline"]
    note_curve_snap_enabled = callbacks["note_curve_snap_enabled"]
    context_links_all_polyline = callbacks["context_links_all_polyline"]
    context_density_menu_state = callbacks["context_density_menu_state"]

    density_state = context_density_menu_state()
    density_denominators = list(density_state.get("denominators", []))
    density_mixed = bool(density_state.get("mixed", False))
    density_mode = str(density_state.get("mode", ""))
    density_den = int(density_state.get("den", 0) or 0)
    density_has_target = bool(density_state.get("has_target", False))
    right_panel_target_shape_key = "shape_curve" if active_link_shape_is_polyline(
    ) else "shape_polyline"
    context_target_shape_key = "shape_curve" if context_links_all_polyline() else "shape_polyline"
    right_panel_toggle_title = (
        f"{tr(state.get('last_context', {}), 'action_polyline_mode')}: "
        f"{tr(state.get('last_context', {}), right_panel_target_shape_key)}"
    )
    context_toggle_title = (
        f"{tr(state.get('last_context', {}), 'action_toggle_context_shape')}: "
        f"{tr(state.get('last_context', {}), context_target_shape_key)}"
    )

    actions = [
        {
            "action_id": "commit_curve_to_notes",
            "title": tr(state.get("last_context", {}), "action_commit_curve"),
            "description": tr(state.get("last_context", {}), "action_commit_curve_desc"),
            "placement": "top_toolbar",
            "requires_undo_snapshot": True,
        },
        {
            "action_id": "toggle_anchor_placement",
            "title": tr(state.get("last_context", {}), "action_anchor_place"),
            "description": tr(state.get("last_context", {}), "action_anchor_place_desc"),
            "placement": "right_note_panel",
            "requires_undo_snapshot": False,
            "checkable": True,
            "checked": bool(state.get("anchor_placement_enabled", False)),
            "sync_plugin_tool_mode_with_checked": True,
        },
        {
            "action_id": "toggle_curve_visible",
            "title": tr(state.get("last_context", {}), "action_curve_visible"),
            "description": tr(state.get("last_context", {}), "action_curve_visible_desc"),
            "placement": "right_note_panel",
            "requires_undo_snapshot": False,
            "checkable": True,
            "checked": bool(state.get("curve_visible", True)),
        },
        {
            "action_id": "toggle_polyline_mode",
            "title": right_panel_toggle_title,
            "description": tr(state.get("last_context", {}), "action_polyline_mode_desc"),
            "placement": "right_note_panel",
            "requires_undo_snapshot": False,
            "checkable": True,
            "checked": active_link_shape_is_polyline(),
        },
        {
            "action_id": "toggle_note_curve_snap",
            "title": tr(state.get("last_context", {}), "action_note_curve_snap"),
            "description": tr(state.get("last_context", {}), "action_note_curve_snap_desc"),
            "placement": "right_note_panel",
            "requires_undo_snapshot": False,
            "checkable": True,
            "checked": note_curve_snap_enabled(),
        },
        {
            "action_id": "toggle_select_anchors",
            "title": tr(state.get("last_context", {}), "action_toggle_select_anchors"),
            "description": tr(state.get("last_context", {}), "action_toggle_select_anchors_desc"),
            "placement": "right_note_panel",
            "requires_undo_snapshot": False,
            "checkable": True,
            "checked": bool(state.get("selection_targets", {}).get("anchors", True)),
        },
        {
            "action_id": "toggle_select_segments",
            "title": tr(state.get("last_context", {}), "action_toggle_select_segments"),
            "description": tr(state.get("last_context", {}), "action_toggle_select_segments_desc"),
            "placement": "right_note_panel",
            "requires_undo_snapshot": False,
            "checkable": True,
            "checked": bool(state.get("selection_targets", {}).get("segments", True)),
        },
        {
            "action_id": "toggle_select_notes",
            "title": tr(state.get("last_context", {}), "action_toggle_select_notes"),
            "description": tr(state.get("last_context", {}), "action_toggle_select_notes_desc"),
            "placement": "right_note_panel",
            "requires_undo_snapshot": False,
            "checkable": True,
            "checked": bool(state.get("selection_targets", {}).get("notes", False)),
        },
        {
            "action_id": "commit_curve_to_notes_sidebar",
            "title": tr(state.get("last_context", {}), "action_commit_curve"),
            "description": tr(state.get("last_context", {}), "action_commit_curve_desc"),
            "placement": "left_sidebar",
            "requires_undo_snapshot": True,
        },
        {
            "action_id": "commit_context_segments_to_notes",
            "title": tr(state.get("last_context", {}), "action_commit_context_segments"),
            "description": tr(state.get("last_context", {}), "action_commit_context_segments_desc"),
            "placement": "plugin_context_menu",
            "requires_undo_snapshot": True,
        },
        {
            "action_id": "toggle_context_polyline_mode",
            "title": context_toggle_title,
            "description": tr(state.get("last_context", {}), "action_polyline_context_desc"),
            "placement": "plugin_context_menu",
            "requires_undo_snapshot": False,
        },
        {
            "action_id": "reset_curve",
            "title": tr(state.get("last_context", {}), "action_reset_curve"),
            "description": tr(state.get("last_context", {}), "action_reset_curve_desc"),
            "placement": "left_sidebar",
            "requires_undo_snapshot": True,
        },
        {
            "action_id": "connect_selected_nodes",
            "title": tr(state.get("last_context", {}), "action_connect_selected"),
            "description": tr(state.get("last_context", {}), "action_connect_selected_desc"),
            "placement": "left_sidebar",
            "requires_undo_snapshot": False,
        },
        {
            "action_id": "disconnect_selected_segments",
            "title": tr(state.get("last_context", {}), "action_disconnect_selected_segments"),
            "description": tr(state.get("last_context", {}), "action_disconnect_selected_segments_desc"),
            "placement": "left_sidebar",
            "requires_undo_snapshot": True,
        },
        {
            "action_id": "connect_selected_nodes_ctx",
            "title": tr(state.get("last_context", {}), "action_connect_selected"),
            "description": tr(state.get("last_context", {}), "action_connect_selected_desc"),
            "placement": "plugin_context_menu",
            "requires_undo_snapshot": False,
        },
        {
            "action_id": "disconnect_selected_segments_ctx",
            "title": tr(state.get("last_context", {}), "status_selection_deleted"),
            "description": tr(state.get("last_context", {}), "status_selection_deleted"),
            "placement": "plugin_context_menu",
            "requires_undo_snapshot": True,
        },
        {
            "action_id": "export_style_preset",
            "title": tr(state.get("last_context", {}), "action_export_style"),
            "description": tr(state.get("last_context", {}), "action_export_style_desc"),
            "placement": "tools_menu",
            "requires_undo_snapshot": False,
        },
        {
            "action_id": "import_style_preset",
            "title": tr(state.get("last_context", {}), "action_import_style"),
            "description": tr(state.get("last_context", {}), "action_import_style_desc"),
            "placement": "tools_menu",
            "requires_undo_snapshot": False,
        },
    ]
    if density_has_target:
        actions.append(
            {
                "action_id": "set_segment_density_follow",
                "title": tr(state.get("last_context", {}), "action_density_follow"),
                "description": tr(state.get("last_context", {}), "action_density_follow_desc"),
                "placement": "plugin_context_menu",
                "requires_undo_snapshot": False,
                "checkable": True,
                "checked": (not density_mixed) and density_mode == "follow",
            }
        )
        for den in density_denominators:
            actions.append(
                {
                    "action_id": f"set_segment_density_{int(den)}",
                    "title": tr(state.get("last_context", {}), "action_density_fixed", den=int(den)),
                    "description": tr(state.get("last_context", {}), "action_density_fixed_desc", den=int(den)),
                    "placement": "plugin_context_menu",
                    "requires_undo_snapshot": False,
                    "checkable": True,
                    "checked": (not density_mixed) and density_mode == "fixed" and density_den == int(den),
                }
            )
        if density_mixed:
            actions.append(
                {
                    "action_id": "segment_density_mixed_info",
                    "title": tr(state.get("last_context", {}), "action_density_mixed"),
                    "description": tr(state.get("last_context", {}), "action_density_mixed_desc"),
                    "placement": "plugin_context_menu",
                    "requires_undo_snapshot": False,
                    "checkable": True,
                    "checked": True,
                }
            )
    return actions


def run_tool_action(payload, callbacks):
    state = callbacks["state"]
    ensure_project_context = callbacks["ensure_project_context"]
    reset_anchors = callbacks["reset_anchors"]
    save_project = callbacks["save_project"]
    mark_dirty = callbacks["mark_dirty"]
    toggle_polyline_for_active_or_selected = callbacks["toggle_polyline_for_active_or_selected"]
    toggle_polyline_for_context_links = callbacks["toggle_polyline_for_context_links"]
    note_curve_snap_enabled = callbacks["note_curve_snap_enabled"]
    set_density_for_selected_segments = callbacks["set_density_for_selected_segments"]
    connect_selected_anchors = callbacks["connect_selected_anchors"]
    disconnect_selected_segments = callbacks["disconnect_selected_segments"]
    delete_selected_anchors = callbacks["delete_selected_anchors"]
    save_style = callbacks["save_style"]
    load_style = callbacks["load_style"]
    register_host_undo_action = callbacks["register_host_undo_action"]

    action_id = str((payload or {}).get("action_id", ""))
    context = (payload or {}).get("context", {}) or {}
    ensure_project_context(context)

    if action_id == "reset_curve":
        reset_anchors(context)
        register_host_undo_action(context, action_id)
        return True
    if action_id in ("commit_curve_to_notes", "commit_curve_to_notes_sidebar", "commit_context_segments_to_notes"):
        if state["project_path"] and state["project_dirty"]:
            if not save_project(state["project_path"], context):
                return False
        return True
    if action_id == "toggle_anchor_placement":
        state["anchor_placement_enabled"] = not bool(
            state.get("anchor_placement_enabled", False))
        # UI mode toggle only: avoid polluting plugin/host undo stacks.
        return True
    if action_id == "toggle_curve_visible":
        state["curve_visible"] = not bool(state.get("curve_visible", True))
        return True
    if action_id == "toggle_polyline_mode":
        return toggle_polyline_for_active_or_selected(context)
    if action_id == "toggle_context_polyline_mode":
        return toggle_polyline_for_context_links(context)
    if action_id == "toggle_note_curve_snap":
        state["note_curve_snap_enabled"] = not note_curve_snap_enabled()
        mark_dirty(context, flush=False)
        return True
    if action_id == "toggle_select_anchors":
        cur = bool(state.get("selection_targets", {}).get("anchors", True))
        state["selection_targets"]["anchors"] = not cur
        if not state["selection_targets"]["anchors"]:
            state["selected_anchor_ids"] = []
        return True
    if action_id == "toggle_select_segments":
        cur = bool(state.get("selection_targets", {}).get("segments", True))
        state["selection_targets"]["segments"] = not cur
        if not state["selection_targets"]["segments"]:
            state["selected_links"] = []
        return True
    if action_id == "toggle_select_notes":
        cur = bool(state.get("selection_targets", {}).get("notes", False))
        state["selection_targets"]["notes"] = not cur
        return True
    if action_id == "set_segment_density_follow":
        return set_density_for_selected_segments(context, 0)
    if action_id.startswith("set_segment_density_"):
        suffix = action_id[len("set_segment_density_"):]
        try:
            den = int(suffix)
        except Exception:
            return False
        return set_density_for_selected_segments(context, den)
    if action_id == "segment_density_mixed_info":
        return True
    if action_id in ("connect_selected_nodes", "connect_selected_nodes_ctx"):
        changed = connect_selected_anchors(context)
        if changed:
            register_host_undo_action(context, action_id)
        return changed
    if action_id == "disconnect_selected_segments":
        changed = disconnect_selected_segments(context)
        if changed:
            register_host_undo_action(context, action_id)
        return changed
    if action_id == "disconnect_selected_segments_ctx":
        changed_segments = disconnect_selected_segments(context)
        changed_anchors = delete_selected_anchors(context)
        changed = changed_segments or changed_anchors
        if changed:
            register_host_undo_action(context, action_id)
        return changed
    if action_id == "export_style_preset":
        return save_style(context)
    if action_id == "import_style_preset":
        ok = load_style(context)
        if ok:
            mark_dirty(context, flush=False)
        return ok
    return False


def run_one_shot(action_id):
    if isinstance(action_id, str):
        if action_id in ("set_segment_density_follow", "segment_density_mixed_info") or action_id.startswith("set_segment_density_"):
            return 0
    known = {
        "reset_curve",
        "commit_curve_to_notes",
        "commit_curve_to_notes_sidebar",
        "commit_context_segments_to_notes",
        "toggle_anchor_placement",
        "toggle_curve_visible",
        "toggle_polyline_mode",
        "toggle_context_polyline_mode",
        "toggle_note_curve_snap",
        "toggle_select_anchors",
        "toggle_select_segments",
        "toggle_select_notes",
        "connect_selected_nodes",
        "disconnect_selected_segments",
        "connect_selected_nodes_ctx",
        "disconnect_selected_segments_ctx",
        "segment_density_mixed_info",
        "export_style_preset",
        "import_style_preset",
    }
    return 0 if action_id in known else 1
