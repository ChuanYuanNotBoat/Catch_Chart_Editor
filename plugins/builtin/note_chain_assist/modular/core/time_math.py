import math
from fractions import Fraction


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def distance(x1, y1, x2, y2):
    return math.hypot(x1 - x2, y1 - y2)


def rect_normalized(x1, y1, x2, y2):
    return min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2)


def point_in_rect(px, py, rect):
    x0, y0, x1, y1 = rect
    return x0 <= px <= x1 and y0 <= py <= y1


def cubic_point(a, b, c, d, t):
    u = 1.0 - t
    tt = t * t
    uu = u * u
    uuu = uu * u
    ttt = tt * t
    # Support both scalar interpolation and 2D point tuple/list interpolation.
    if isinstance(a, (list, tuple)):
        ax, ay = float(a[0]), float(a[1])
        bx, by = float(b[0]), float(b[1])
        cx, cy = float(c[0]), float(c[1])
        dx, dy = float(d[0]), float(d[1])
        x = uuu * ax + 3.0 * uu * t * bx + 3.0 * u * tt * cx + ttt * dx
        y = uuu * ay + 3.0 * uu * t * by + 3.0 * u * tt * cy + ttt * dy
        return (x, y)
    return uuu * a + 3.0 * uu * t * b + 3.0 * u * tt * c + ttt * d


def float_to_triplet(beat, den):
    d = max(1, int(den))
    value = float(beat)
    beat_num = int(math.floor(value + 1e-9))
    frac = value - beat_num
    num = int(round(frac * d))
    if num >= d:
        beat_num += num // d
        num = num % d
    if num < 0:
        borrow = int(math.ceil(abs(num) / float(d)))
        beat_num -= borrow
        num += borrow * d
    g = math.gcd(num, d)
    return [int(beat_num), int(num // g), int(d // g)]


def triplet_to_float(tri):
    if not isinstance(tri, list) or len(tri) < 3:
        return None
    try:
        beat_num = int(tri[0])
        num = int(tri[1])
        den = int(tri[2])
    except Exception:
        return None
    if den == 0:
        return None
    return float(beat_num) + float(num) / float(den)


def context_dims(context):
    cw = max(1.0, float(context.get("canvas_width", 1200.0)))
    ch = max(1.0, float(context.get("canvas_height", 800.0)))
    lane_w = max(1.0, float(context.get("lane_width", 512.0)))

    # Host currently provides left/right margins. Keep this path as primary to
    # preserve the legacy coordinate mapping used before modularization.
    has_margin_fields = isinstance(context, dict) and (
        "left_margin" in context or "right_margin" in context)
    if has_margin_fields:
        l = float(context.get("left_margin", 0.0))
        r_margin = float(context.get("right_margin", 0.0))
        available = max(1.0, cw - l - r_margin)
        r = l + available
    else:
        # Fallback for contexts that expose lane bounds directly.
        l = float(context.get("lane_left", 64.0))
        r = float(context.get("lane_right", cw - 64.0))
        available = max(1.0, r - l)
    return cw, ch, l, r, lane_w, available


def canvas_to_chart(context, x, y):
    _, ch, l, _, lane_w, available = context_dims(context)
    xx = clamp(float(x), l, l + available)
    lane_x = ((xx - l) / available) * lane_w

    scroll = float(context.get("scroll_beat", 0.0))
    vr = max(1e-6, float(context.get("visible_beat_range", 8.0)))
    vertical_flip = bool(context.get("vertical_flip", False))
    t = 1.0 - (float(y) / ch) if vertical_flip else (float(y) / ch)
    beat = scroll + t * vr
    return lane_x, beat


def chart_to_canvas(context, lane_x, beat):
    _, ch, l, _, lane_w, available = context_dims(context)
    lane_x = clamp(float(lane_x), 0.0, lane_w)
    x = l + (lane_x / lane_w) * available

    scroll = float(context.get("scroll_beat", 0.0))
    vr = max(1e-6, float(context.get("visible_beat_range", 8.0)))
    vertical_flip = bool(context.get("vertical_flip", False))
    t = (float(beat) - scroll) / vr
    y = ch - t * ch if vertical_flip else t * ch
    return x, y


def snap_chart_point(context, lane_x, beat, snap_beat=True, snap_lane=True):
    lane_w = max(1.0, float(context.get("lane_width", 512.0)))
    grid_snap = bool(context.get("grid_snap", False))
    grid_div = max(1, int(context.get("grid_division", 8)))
    time_div = max(1, int(context.get("time_division", 1)))

    lane_x = clamp(float(lane_x), 0.0, lane_w)
    if snap_lane and grid_snap and grid_div > 0:
        lane_x = round((lane_x / lane_w) * grid_div) * \
            (lane_w / float(grid_div))

    if snap_beat:
        beat = round(float(beat) * time_div) / float(time_div)
    return lane_x, beat


def distance_point_to_segment(px, py, x1, y1, x2, y2):
    vx = x2 - x1
    vy = y2 - y1
    wx = px - x1
    wy = py - y1
    c1 = vx * wx + vy * wy
    if c1 <= 0:
        return distance(px, py, x1, y1)
    c2 = vx * vx + vy * vy
    if c2 <= 1e-12:
        return distance(px, py, x1, y1)
    t = clamp(c1 / c2, 0.0, 1.0)
    qx = x1 + t * vx
    qy = y1 + t * vy
    return distance(px, py, qx, qy)


def beat_fraction_from_triplet(triplet):
    if not isinstance(triplet, list) or len(triplet) < 3:
        return None
    try:
        beat_num = int(triplet[0])
        num = int(triplet[1])
        den = int(triplet[2])
    except Exception:
        return None
    if den <= 0:
        return None
    return Fraction(beat_num, 1) + Fraction(num, den)
