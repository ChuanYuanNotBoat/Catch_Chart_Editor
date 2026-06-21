
def normalize_shape_name(shape):
    return "polyline" if str(shape or "").strip().lower() == "polyline" else "curve"


def normalize_link(id_a, id_b, anchor_index_map):
    a = int(id_a)
    b = int(id_b)
    if a <= 0 or b <= 0 or a == b:
        return None
    if a not in anchor_index_map or b not in anchor_index_map:
        return None
    return (a, b) if anchor_index_map[a] <= anchor_index_map[b] else (b, a)


def link_exists(links, id_a, id_b, anchor_index_map):
    norm = normalize_link(id_a, id_b, anchor_index_map)
    if norm is None:
        return False
    for raw in links:
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        cur = normalize_link(raw[0], raw[1], anchor_index_map)
        if cur == norm:
            return True
    return False


def link_key(id_a, id_b, anchor_index_map):
    norm = normalize_link(id_a, id_b, anchor_index_map)
    if norm is None:
        return ""
    return f"{norm[0]}:{norm[1]}"


def segment_denominator_for_link(segment_denominators, curve_density_mode_by_link, id_a, id_b, anchor_index_map, fallback_den=4):
    key = link_key(id_a, id_b, anchor_index_map)
    if not key:
        return max(1, int(fallback_den))
    if isinstance(curve_density_mode_by_link, dict):
        mode = str(curve_density_mode_by_link.get(
            key, "") or "").strip().lower()
        if mode == "follow":
            return max(1, int(fallback_den))
    if isinstance(segment_denominators, dict):
        try:
            den = int(segment_denominators.get(key, 0))
            if den > 0:
                return den
        except Exception:
            pass
    return max(1, int(fallback_den))


def set_segment_denominator(segment_denominators, curve_density_mode_by_link, id_a, id_b, den, anchor_index_map):
    key = link_key(id_a, id_b, anchor_index_map)
    if not key:
        return False, segment_denominators, curve_density_mode_by_link
    try:
        val = int(den)
    except Exception:
        val = 0

    seg_map = dict(segment_denominators) if isinstance(
        segment_denominators, dict) else {}
    density_mode = dict(curve_density_mode_by_link) if isinstance(
        curve_density_mode_by_link, dict) else {}
    prev = int(seg_map.get(key, 0) or 0)

    if val <= 0:
        density_mode[key] = "follow"
        if key in seg_map:
            seg_map.pop(key, None)
            return True, seg_map, density_mode
        return False, seg_map, density_mode

    seg_map[key] = val
    density_mode[key] = "fixed"
    return prev != val, seg_map, density_mode


def segment_shape_for_link(segment_shapes, id_a, id_b, anchor_index_map, fallback_shape="curve"):
    key = link_key(id_a, id_b, anchor_index_map)
    if not key:
        return normalize_shape_name(fallback_shape)
    if isinstance(segment_shapes, dict) and key in segment_shapes:
        return normalize_shape_name(segment_shapes.get(key))
    return normalize_shape_name(fallback_shape)


def set_segment_shape(segment_shapes, id_a, id_b, shape, anchor_index_map):
    key = link_key(id_a, id_b, anchor_index_map)
    if not key:
        return False, segment_shapes

    val = normalize_shape_name(shape)
    seg_map = dict(segment_shapes) if isinstance(segment_shapes, dict) else {}
    prev = normalize_shape_name(seg_map.get(key, "curve"))
    if val == "curve":
        changed = key in seg_map and prev != "curve"
        seg_map.pop(key, None)
    else:
        changed = prev != val
        seg_map[key] = val
    return changed, seg_map
