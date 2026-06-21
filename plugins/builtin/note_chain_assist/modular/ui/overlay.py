def build_overlay(context, callbacks):
    normalize_link = callbacks["normalize_link"]
    connected_anchor_segments = callbacks["connected_anchor_segments"]
    sample_segment_chart = callbacks["sample_segment_chart"]
    note_curve_snap_enabled = callbacks["note_curve_snap_enabled"]
    anchor_in_abs_chart = callbacks["anchor_in_abs_chart"]
    anchor_out_abs_chart = callbacks["anchor_out_abs_chart"]
    tr = callbacks["tr"]
    rect_normalized = callbacks["rect_normalized"]
    anchor_index_map = callbacks["anchor_index_map"]
    chart_to_canvas = callbacks["chart_to_canvas"]
    state = callbacks["state"]

    if not bool(state.get("curve_visible", True)):
        return []
    toggles = context.get("overlay_toggles", {}) if isinstance(
        context, dict) else {}
    if not bool(toggles.get("overlay_enabled", True)):
        return []

    show_preview = bool(toggles.get("preview", True))
    show_points = bool(toggles.get("control_points", True))
    show_handles = bool(toggles.get("handles", True))
    show_samples = bool(toggles.get("sample_points", True))
    show_labels = bool(toggles.get("labels", True))

    items = []
    anchors = state["anchors"]
    selected_link_set = set()
    for raw in state.get("selected_links", []):
        if isinstance(raw, list) and len(raw) == 2:
            norm = normalize_link(raw[0], raw[1])
            if norm is not None:
                selected_link_set.add(norm)
    selected_anchor_set = set(int(v)
                              for v in state.get("selected_anchor_ids", []))

    if show_preview:
        for _i0, _i1, id0, id1, a0, a1 in connected_anchor_segments():
            is_selected_segment = (id0, id1) in selected_link_set
            seg_color = "#FFD66B" if is_selected_segment else "#33CCFF"
            seg_width = 3.0 if is_selected_segment else 2.0
            sampled = sample_segment_chart(a0, a1, 24, id0, id1)
            for j in range(len(sampled) - 1):
                line_item = {
                    "kind": "line",
                    "coord_space": "chart",
                    "lane_x1": sampled[j][0],
                    "beat1": sampled[j][1],
                    "lane_x2": sampled[j + 1][0],
                    "beat2": sampled[j + 1][1],
                    "color": seg_color,
                    "width": seg_width,
                }
                if note_curve_snap_enabled():
                    line_item["note_snap_reference"] = True
                items.append(line_item)
            if show_samples:
                for lane_x, beat in sampled[::4]:
                    items.append({
                        "kind": "rect",
                        "coord_space": "chart",
                        "lane_x": lane_x,
                        "beat": beat,
                        "w": 4,
                        "h": 4,
                        "rect_anchor": "center",
                        "color": "#88FFFFFF",
                        "fill_color": "#88FFFFFF",
                        "width": 1.0,
                    })

    for i, a in enumerate(anchors):
        is_drag_anchor = state["drag"]["mode"] == "anchor" and state["drag"]["index"] == i
        is_selected_anchor = int(a.get("id", 0)) in selected_anchor_set

        if show_handles:
            ilx, ib = anchor_in_abs_chart(a)
            olx, ob = anchor_out_abs_chart(a)
            items.append({
                "kind": "line",
                "coord_space": "chart",
                "lane_x1": a["lane_x"],
                "beat1": a["beat"],
                "lane_x2": ilx,
                "beat2": ib,
                "color": "#66A0A0A0",
                "width": 1.0,
            })
            items.append({
                "kind": "line",
                "coord_space": "chart",
                "lane_x1": a["lane_x"],
                "beat1": a["beat"],
                "lane_x2": olx,
                "beat2": ob,
                "color": "#66A0A0A0",
                "width": 1.0,
            })
            items.append({
                "kind": "rect",
                "coord_space": "chart",
                "lane_x": ilx,
                "beat": ib,
                "w": 8,
                "h": 8,
                "rect_anchor": "center",
                "color": "#FFFFFFFF",
                "fill_color": "#AAEEAA55",
                "width": 1.0,
            })
            items.append({
                "kind": "rect",
                "coord_space": "chart",
                "lane_x": olx,
                "beat": ob,
                "w": 8,
                "h": 8,
                "rect_anchor": "center",
                "color": "#FFFFFFFF",
                "fill_color": "#AAEEAA55",
                "width": 1.0,
            })

        if show_points:
            items.append({
                "kind": "rect",
                "coord_space": "chart",
                "lane_x": a["lane_x"],
                "beat": a["beat"],
                "w": 12,
                "h": 12,
                "rect_anchor": "center",
                "color": "#FFFFFF",
                "fill_color": "#AAFF9B2F" if is_selected_anchor else ("#AA0077FF" if is_drag_anchor else "#AA00A3FF"),
                "width": 2.5 if is_selected_anchor else 1.5,
            })

        if show_labels:
            mode = tr(context, "anchor_mode_smooth") if a.get(
                "smooth", True) else tr(context, "anchor_mode_corner")
            items.append({
                "kind": "text",
                "coord_space": "chart",
                "lane_x": a["lane_x"] + 8.0,
                "beat": a["beat"],
                "text": f"A{i}({mode})",
                "color": "#FFFFFF",
                "font_px": 12,
            })

    if show_labels:
        dens = state.get("style", {}).get("denominators", [4, 8, 12, 16])
        override_den = int(context.get(
            "plugin_time_division_override", 0) or 0) if isinstance(context, dict) else 0
        effective_den = override_den if override_den > 0 else max(
            1, int(context.get("time_division", 4))) if isinstance(context, dict) else 4
        anchor_mode = tr(context, "anchor_on") if bool(
            state.get("anchor_placement_enabled", False)) else tr(context, "anchor_off")
        label = tr(context, "overlay_summary", dens="/".join(str(d)
                   for d in dens), den=effective_den, anchor_mode=anchor_mode)
        items.append({"kind": "text", "x1": 16, "y1": 18,
                     "text": label, "color": "#DDEEFF", "font_px": 12})

    box = state.get("box_select", {})
    if bool(box.get("active", False)):
        sx, sy = box.get("start", [0.0, 0.0])
        ex, ey = box.get("end", [0.0, 0.0])
        x0, y0, x1, y1 = rect_normalized(
            float(sx), float(sy), float(ex), float(ey))
        items.append({
            "kind": "rect",
            "x": x0,
            "y": y0,
            "w": max(1.0, x1 - x0),
            "h": max(1.0, y1 - y0),
            "color": "#FFCC66",
            "fill_color": "#33FFCC66",
            "width": 1.5,
        })

    link_drag = state.get("link_drag", {})
    if bool(link_drag.get("active", False)):
        idx_map = anchor_index_map()
        source_id = int(link_drag.get("source_anchor_id", -1))
        source_idx = idx_map.get(source_id, -1)
        if 0 <= source_idx < len(anchors):
            src = anchors[source_idx]
            sx, sy = chart_to_canvas(context, src["lane_x"], src["beat"])
            tx = float(link_drag.get("x", sx))
            ty = float(link_drag.get("y", sy))
            hover_id = int(link_drag.get("hover_anchor_id", -1))
            hover_idx = idx_map.get(hover_id, -1)
            if 0 <= hover_idx < len(anchors):
                dst = anchors[hover_idx]
                tx, ty = chart_to_canvas(context, dst["lane_x"], dst["beat"])
                items.append({
                    "kind": "rect",
                    "coord_space": "chart",
                    "lane_x": dst["lane_x"],
                    "beat": dst["beat"],
                    "w": 16,
                    "h": 16,
                    "rect_anchor": "center",
                    "color": "#FFE08A",
                    "fill_color": "#33FFE08A",
                    "width": 2.0,
                })
            items.append({
                "kind": "line",
                "x1": sx,
                "y1": sy,
                "x2": tx,
                "y2": ty,
                "color": "#FFE08A",
                "width": 2.0,
            })

    return items
