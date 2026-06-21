
def float_beat_to_triplet(beat, den):
    den = max(1, int(den))
    if beat < 0.0:
        beat = 0.0
    ticks = int(round(beat * den))
    beat_num = ticks // den
    num = ticks % den
    if num < 0:
        num += den
        beat_num -= 1
    return beat_num, num, den


def chart_to_note(context, lane_x, beat, den, *, clamp_fn):
    lane_w = max(1.0, float(context.get("lane_width", 512.0)))
    lane_x = clamp_fn(lane_x, 0.0, lane_w)
    b, n, d = float_beat_to_triplet(beat, den)
    return {"beat": [b, n, d], "x": int(round(lane_x)), "type": 0}


def normal_note_position_key(note_obj, *, beat_fraction_from_triplet_fn):
    if not isinstance(note_obj, dict):
        return None
    try:
        note_type = int(note_obj.get("type", 0) or 0)
    except Exception:
        note_type = 0
    if note_type != 0:
        return None

    beat = beat_fraction_from_triplet_fn(note_obj.get("beat"))
    if beat is None:
        return None
    try:
        x = int(round(float(note_obj.get("x", note_obj.get("lane_x", 0)))))
    except Exception:
        return None
    return beat, x


def existing_normal_note_position_keys(context, *, normal_note_position_key_fn, os_module, json_module):
    keys = set()
    if not isinstance(context, dict):
        return keys

    raw_positions = context.get("existing_note_positions")
    if isinstance(raw_positions, list):
        for raw in raw_positions:
            key = normal_note_position_key_fn(raw)
            if key is not None:
                keys.add(key)

    if keys:
        return keys

    chart_path = str(context.get("chart_path", "") or "").strip()
    if not chart_path or not os_module.path.exists(chart_path):
        return keys
    try:
        with open(chart_path, "r", encoding="utf-8") as f:
            payload = json_module.load(f)
    except Exception:
        return keys

    notes = payload.get("note") if isinstance(payload, dict) else None
    if not isinstance(notes, list):
        return keys
    for raw in notes:
        key = normal_note_position_key_fn(raw)
        if key is not None:
            keys.add(key)
    return keys


def build_batch_from_curve(context, target_links, callbacks):
    if not isinstance(context, dict):
        return {"add": [], "remove": [], "move": []}

    connected_anchor_segments = callbacks["connected_anchor_segments"]
    normalize_link = callbacks["normalize_link"]
    segment_denominator_for_link = callbacks["segment_denominator_for_link"]
    normalize_samples_by_beat = callbacks["normalize_samples_by_beat"]
    lane_x_at_beat = callbacks["lane_x_at_beat"]
    sample_segment_chart = callbacks["sample_segment_chart"]
    note_position_keys_fn = callbacks["existing_normal_note_position_keys"]
    normal_note_position_key_fn = callbacks["normal_note_position_key"]
    chart_to_note_fn = callbacks["chart_to_note"]

    segments = connected_anchor_segments()
    if target_links is not None:
        target_set = set()
        for raw in target_links:
            if not isinstance(raw, (list, tuple)) or len(raw) != 2:
                continue
            norm = normalize_link(raw[0], raw[1])
            if norm is not None:
                target_set.add(norm)
        if not target_set:
            return {"add": [], "remove": [], "move": []}
        segments = [seg for seg in segments if normalize_link(
            seg[2], seg[3]) in target_set]

    if not segments:
        return {"add": [], "remove": [], "move": []}

    override_den = int(context.get("plugin_time_division_override", 0) or 0)
    default_den = max(1, override_den if override_den >
                      0 else int(context.get("time_division", 4)))

    out = []
    existing = note_position_keys_fn(context)
    seen = set()
    for _i0, _i1, id0, id1, a0, a1 in segments:
        seg_den = segment_denominator_for_link(id0, id1, default_den)
        sampled_segment = sample_segment_chart(a0, a1, 32, id0, id1)
        samples_by_beat = normalize_samples_by_beat(sampled_segment)
        if len(samples_by_beat) < 2:
            continue

        seg_start = float(samples_by_beat[0][1])
        seg_end = float(samples_by_beat[-1][1])
        lo = min(seg_start, seg_end)
        hi = max(seg_start, seg_end)

        start_tick = int(round(float(lo) * seg_den))
        end_tick = int(round(float(hi) * seg_den))
        if end_tick < start_tick:
            start_tick, end_tick = end_tick, start_tick

        for tick in range(start_tick, end_tick + 1):
            beat = float(tick) / float(seg_den)
            if beat < lo or beat > hi:
                continue
            lane_x = lane_x_at_beat(samples_by_beat, beat)
            note = chart_to_note_fn(context, lane_x, beat, seg_den)
            key = normal_note_position_key_fn(note)
            if key is None or key in existing or key in seen:
                continue
            seen.add(key)
            out.append(note)

    out.sort(
        key=lambda n: (
            int((n.get("beat") or [0, 0, 1])[0]),
            int((n.get("beat") or [0, 0, 1])[1]),
            int((n.get("beat") or [0, 0, 1])[2]),
            int(n.get("x", 0)),
        )
    )
    return {"add": out, "remove": [], "move": []}


def build_batch_edit(payload, callbacks):
    ensure_project_context = callbacks["ensure_project_context"]
    seed_missing_segment_denominators = callbacks["seed_missing_segment_denominators"]
    build_batch_from_curve = callbacks["build_batch_from_curve"]
    context_menu_target_links = callbacks["context_menu_target_links"]

    action_id = str((payload or {}).get("action_id", ""))
    context = (payload or {}).get("context", {}) or {}
    ensure_project_context(context)
    seed_missing_segment_denominators(context)

    if action_id not in ("commit_curve_to_notes", "commit_curve_to_notes_sidebar", "commit_context_segments_to_notes"):
        return {"add": [], "remove": [], "move": []}
    if action_id == "commit_context_segments_to_notes":
        return build_batch_from_curve(context, context_menu_target_links())
    return build_batch_from_curve(context)
