from modular.ui import input_handler as input_ui
from modular.ui import overlay as overlay_ui
from modular.runtime import workspace as ws_runtime
from modular.runtime import protocol_io as proto_io
from modular.runtime.plugin_loop import run_plugin_loop as run_protocol_loop
from modular.actions import batch_commit as bc
from modular.actions import tool_actions as ta
from modular.core import i18n as i18n_core
from modular.core import sidecar_v3 as scv3
from modular.core import curve_model as cm
from modular.core import time_math as tm
from modular.core import state as state_core
from modular.core.state import (
    CTRL_MODIFIER_MASK,
    CURVE_CHECKPOINT_PREFIX,
    CURVE_SIDECAR_FORMAT_VERSION,
    DEFAULT_CURVE_GROUP_ID,
    DEFAULT_NODE_GROUP_ID,
    KEY_A,
    KEY_BACKSPACE,
    KEY_DELETE,
    LEFT_BUTTON,
    MAX_HISTORY,
    RIGHT_BUTTON,
    SERIALIZE_DEN,
    SHIFT_MODIFIER_MASK,
    STATE,
    STYLE_PRESETS,
)
import json
import locale
import math
import os
import shutil
import sys
import time
import uuid

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)


TRANSLATIONS = {
    "anchor_mode_smooth": {"en": "S", "zh": "平", "ja": "滑"},
    "anchor_mode_corner": {"en": "C", "zh": "角", "ja": "角"},
    "overlay_summary": {
        "en": "Dens: {dens} | Place: 1/{den} | Anchor: {anchor_mode}",
        "zh": "密度: {dens} | 放置: 1/{den} | 锚点: {anchor_mode}",
        "ja": "密度: {dens} | 配置: 1/{den} | アンカー: {anchor_mode}",
    },
    "anchor_on": {"en": "ON", "zh": "开", "ja": "ON"},
    "anchor_off": {"en": "OFF", "zh": "关", "ja": "OFF"},
    "dragging_handle": {
        "en": "Dragging {handle_kind} handle A{index}",
        "zh": "正在拖动 A{index} 的{handle_kind}控制柄",
        "ja": "A{index} の{handle_kind}ハンドルをドラッグ中",
    },
    "handle_kind_in": {"en": "in", "zh": "入侧", "ja": "内側"},
    "handle_kind_out": {"en": "out", "zh": "出侧", "ja": "外側"},
    "anchor_mode_changed": {
        "en": "Anchor A{index} -> {mode}",
        "zh": "锚点 A{index} 已切换为{mode}",
        "ja": "アンカー A{index} を{mode}に切り替えました",
    },
    "mode_smooth": {"en": "smooth", "zh": "平滑", "ja": "スムーズ"},
    "mode_corner": {"en": "corner", "zh": "折角", "ja": "コーナー"},
    "dragging_anchor": {"en": "Dragging anchor A{index}", "zh": "正在拖动锚点 A{index}", "ja": "アンカー A{index} をドラッグ中"},
    "anchor_place_off_hint": {
        "en": "Anchor placement is OFF (toggle Anchor Place first)",
        "zh": "锚点放置已关闭，请先切换“放置锚点”",
        "ja": "アンカー配置は OFF です（先に「アンカー配置」を切り替えてください）",
    },
    "anchor_added": {"en": "Anchor A{index} added", "zh": "已添加锚点 A{index}", "ja": "アンカー A{index} を追加しました"},
    "editing_anchor": {"en": "Editing A{index}", "zh": "正在编辑 A{index}", "ja": "A{index} を編集中"},
    "curve_edit_applied": {"en": "Curve edit applied", "zh": "曲线编辑已应用", "ja": "曲線編集を適用しました"},
    "interaction_cancelled": {"en": "Interaction cancelled", "zh": "交互已取消", "ja": "操作をキャンセルしました"},
    "anchor_enabled": {"en": "Anchor placement enabled", "zh": "已启用锚点放置", "ja": "アンカー配置を有効にしました"},
    "anchor_disabled": {"en": "Anchor placement disabled", "zh": "已禁用锚点放置", "ja": "アンカー配置を無効にしました"},
    "curve_undo": {"en": "Curve undo", "zh": "已撤销曲线编辑", "ja": "曲線編集を元に戻しました"},
    "curve_redo": {"en": "Curve redo", "zh": "已重做曲线编辑", "ja": "曲線編集をやり直しました"},
    "action_commit_curve": {"en": "Commit Curve", "zh": "提交曲线", "ja": "曲線を確定"},
    "action_commit_curve_desc": {
        "en": "Generate normal notes from current pen curve",
        "zh": "根据当前钢笔曲线生成普通 note",
        "ja": "現在のペン曲線から通常ノートを生成します",
    },
    "action_commit_context_segments": {
        "en": "Place Notes for Segment(s)",
        "zh": "放置曲线段 Note",
        "ja": "セグメントのノートを配置",
    },
    "action_commit_context_segments_desc": {
        "en": "Generate normal notes from the right-clicked segment, or all selected segments",
        "zh": "根据右键曲线段生成普通 note；多选曲线段时生成所有选中段",
        "ja": "右クリックしたセグメント、または選択中のセグメントから通常ノートを生成",
    },
    "action_anchor_place": {"en": "Anchor Place", "zh": "放置锚点", "ja": "アンカー配置"},
    "action_anchor_place_desc": {
        "en": "Toggle anchor placement mode to prevent misclick additions",
        "zh": "切换锚点放置模式，避免误触新增锚点",
        "ja": "アンカー配置モードを切り替えて誤操作での追加を防ぎます",
    },
    "action_curve_visible": {"en": "Show Curve (with Nodes)", "zh": "显示曲线（含节点）", "ja": "カーブ表示（ノード含む）"},
    "action_curve_visible_desc": {"en": "Toggle curve and node overlay visibility", "zh": "切换曲线与节点叠加层显示", "ja": "カーブとノードのオーバーレイ表示を切替"},
    "action_polyline_mode": {"en": "Polyline Link", "zh": "折线连接", "ja": "ポリライン接続"},
    "action_polyline_mode_desc": {
        "en": "Toggle selected connection lines between Bezier curves and straight segments; with no selection this sets the shape for new links",
        "zh": "切换选中连接线的曲线/折线状态；未选中连接线时设置新连接线的默认状态",
        "ja": "選択した接続線のベジェ/直線状態を切り替えます。未選択時は新規接続線の既定状態を設定します",
    },
    "action_polyline_context_desc": {
        "en": "Toggle the right-clicked connection line, or all selected connection lines, between curve and polyline",
        "zh": "切换右键连接线或所有选中连接线的曲线/折线状态",
        "ja": "右クリックした接続線、または選択中の接続線のカーブ/ポリライン状態を切り替えます",
    },
    "action_toggle_context_shape": {"en": "Toggle Curve/Polyline", "zh": "切换曲线/折线", "ja": "カーブ/ポリライン切替"},
    "action_note_curve_snap": {"en": "Snap Notes to Curve", "zh": "Note 吸附到曲线", "ja": "ノートを曲線に吸着"},
    "action_note_curve_snap_desc": {
        "en": "Use the curve as a note drag snap reference; curve and anchor selection is disabled while enabled",
        "zh": "拖拽 note 时以曲线作为吸附参考；启用时不可选中曲线和节点",
        "ja": "ノートドラッグ時に曲線を吸着基準にします。有効時は曲線とノードを選択できません",
    },
    "status_curve_shape_changed": {"en": "Curve shape: {shape}", "zh": "曲线形态：{shape}", "ja": "曲線形状: {shape}"},
    "shape_curve": {"en": "Curve", "zh": "曲线", "ja": "カーブ"},
    "shape_polyline": {"en": "Polyline", "zh": "折线", "ja": "ポリライン"},
    "action_undo_curve": {"en": "Undo Curve", "zh": "撤销曲线", "ja": "曲線を元に戻す"},
    "action_undo_curve_desc": {
        "en": "Undo latest curve anchor/handle edit",
        "zh": "撤销最近一次曲线锚点或控制柄编辑",
        "ja": "直前の曲線アンカーまたはハンドル編集を取り消します",
    },
    "action_redo_curve": {"en": "Redo Curve", "zh": "重做曲线", "ja": "曲線をやり直す"},
    "action_redo_curve_desc": {
        "en": "Redo latest curve anchor/handle edit",
        "zh": "重做最近一次曲线锚点或控制柄编辑",
        "ja": "直前の曲線アンカーまたはハンドル編集をやり直します",
    },
    "action_reset_curve": {"en": "Reset Curve", "zh": "重置曲线", "ja": "曲線をリセット"},
    "action_reset_curve_desc": {
        "en": "Reset all anchors and handles",
        "zh": "重置全部锚点与控制柄",
        "ja": "すべてのアンカーとハンドルをリセットします",
    },
    "action_cycle_density": {"en": "Cycle Density", "zh": "切换密度", "ja": "密度を切替"},
    "action_cycle_density_desc": {
        "en": "Rotate predefined denominator sequence styles",
        "zh": "轮换预设的分母序列样式",
        "ja": "定義済みの分母シーケンススタイルを切り替えます",
    },
    "action_export_style": {"en": "Export Style", "zh": "导出样式", "ja": "スタイルを書き出す"},
    "action_export_style_desc": {"en": "Export current style", "zh": "导出当前样式", "ja": "現在のスタイルを書き出します"},
    "action_import_style": {"en": "Import Style", "zh": "导入样式", "ja": "スタイルを読み込む"},
    "action_import_style_desc": {
        "en": "Import style from shared style file",
        "zh": "从共享样式文件导入样式",
        "ja": "共有スタイルファイルからスタイルを読み込みます",
    },
    "action_density_follow": {"en": "Density: Follow", "zh": "分度：跟随", "ja": "分度: 追従"},
    "action_density_follow_desc": {
        "en": "Follow current placement denominator",
        "zh": "跟随当前放置分度",
        "ja": "現在の配置分度に追従します",
    },
    "action_density_fixed": {"en": "Density 1/{den}", "zh": "分度 1/{den}", "ja": "分度 1/{den}"},
    "action_density_fixed_desc": {
        "en": "Set selected segment density to 1/{den}",
        "zh": "将选中曲线段分度设为 1/{den}",
        "ja": "選択セグメントの分度を 1/{den} に設定します",
    },
    "action_density_mixed": {"en": "Density: Mixed", "zh": "分度：混合", "ja": "分度: 混在"},
    "action_density_mixed_desc": {
        "en": "Selected segments have mixed density settings",
        "zh": "当前选中曲线段分度状态不一致",
        "ja": "選択セグメントの分度設定が混在しています",
    },
}

TRANSLATIONS.update(
    {
        "status_segment_selected": {"en": "Segment selected", "zh": "已选中曲线段", "ja": "セグメントを選択しました"},
        "status_box_selecting": {"en": "Box selecting", "zh": "正在框选", "ja": "範囲選択中"},
        "status_selection_cleared": {"en": "Selection cleared", "zh": "已清除选择", "ja": "選択を解除しました"},
        "status_box_selection_applied": {"en": "Box selection applied", "zh": "框选已应用", "ja": "範囲選択を適用しました"},
        "status_box_selection_cleared": {"en": "Box selection cleared", "zh": "框选已清除", "ja": "範囲選択をクリアしました"},
        "status_selection_deleted": {"en": "Selection deleted", "zh": "已删除所选对象", "ja": "選択対象を削除しました"},
        "action_toggle_select_anchors": {"en": "Selectable: Anchors", "zh": "可选中：节点", "ja": "選択可能: ノード"},
        "action_toggle_select_anchors_desc": {"en": "Enable/disable anchor selection", "zh": "启用/禁用节点选择", "ja": "ノード選択の有効/無効を切替"},
        "action_toggle_select_segments": {"en": "Selectable: Segments", "zh": "可选中：曲线段", "ja": "選択可能: セグメント"},
        "action_toggle_select_segments_desc": {"en": "Enable/disable segment selection", "zh": "启用/禁用曲线段选择", "ja": "セグメント選択の有効/無効を切替"},
        "action_toggle_select_notes": {"en": "Selectable: Notes", "zh": "可选中：音符", "ja": "選択可能: ノーツ"},
        "action_toggle_select_notes_desc": {
            "en": "Allow selecting host notes (disable for curve-only selection)",
            "zh": "允许选择主程序音符（关闭后为仅曲线选择）",
            "ja": "ホストノーツ選択を許可（無効でカーブのみ選択）",
        },
        "action_connect_selected": {"en": "Connect Selected", "zh": "连接选中节点", "ja": "選択ノードを接続"},
        "action_connect_selected_desc": {"en": "Connect selected anchors by time order", "zh": "按时间顺序连接选中节点", "ja": "時間順に選択ノードを接続"},
        "action_disconnect_selected_segments": {"en": "Delete Selected Segment", "zh": "删除选中曲线段", "ja": "選択セグメントを削除"},
        "action_disconnect_selected_segments_desc": {"en": "Disconnect selected curve segments", "zh": "断开选中的曲线段连接", "ja": "選択セグメントの接続を解除"},
        "status_link_dragging": {"en": "Shift-drag to another anchor to connect", "zh": "按住 Shift 拖到另一个节点即可连接", "ja": "Shiftドラッグで別ノードに接続"},
        "status_link_drag_target": {"en": "Release to connect A{from_idx} -> A{to_idx}", "zh": "松开即可连接 A{from_idx} -> A{to_idx}", "ja": "離して接続 A{from_idx} -> A{to_idx}"},
        "status_link_connected": {"en": "Connected A{from_idx} -> A{to_idx}", "zh": "已连接 A{from_idx} -> A{to_idx}", "ja": "接続済み A{from_idx} -> A{to_idx}"},
        "status_link_already_connected": {"en": "A{from_idx} and A{to_idx} are already connected", "zh": "A{from_idx} 与 A{to_idx} 已连接", "ja": "A{from_idx} と A{to_idx} は既に接続済み"},
        "status_link_cancelled": {"en": "Connect drag cancelled", "zh": "连接拖拽已取消", "ja": "接続ドラッグをキャンセル"},
    }
)


def _clone(v):
    return json.loads(json.dumps(v))


def _write(msg):
    proto_io.write_message(msg, json_module=json, sys_module=sys)


def _respond(req_id, result):
    proto_io.respond(req_id, result, write_fn=_write)


def _distance(x1, y1, x2, y2):
    return tm.distance(x1, y1, x2, y2)


def _mods_has_shift(mods):
    try:
        value = int(mods)
    except Exception:
        return False
    # Prefer Qt::ShiftModifier; keep low-bit fallback for host/tooling variance.
    return (value & SHIFT_MODIFIER_MASK) != 0 or (value & 0x1) != 0


def _event_has_shift(event):
    if isinstance(event, dict):
        if "shift_down" in event:
            return bool(event.get("shift_down", False))
        if "shiftDown" in event:
            return bool(event.get("shiftDown", False))
    mods = event.get("modifiers", 0) if isinstance(event, dict) else 0
    return _mods_has_shift(mods)


def _clamp(v, lo, hi):
    return tm.clamp(v, lo, hi)


def normalize_lang(value, default="en"):
    return i18n_core.normalize_lang(value, default)


def detect_lang(default="en", context=None):
    return i18n_core.detect_lang(
        default=default,
        context=context,
        state=STATE,
        os_module=os,
        locale_module=locale,
        normalize_lang_fn=normalize_lang,
    )


def tr(context, key, **kwargs):
    return i18n_core.tr(
        context,
        key,
        translations=TRANSLATIONS,
        detect_lang_fn=detect_lang,
        **kwargs,
    )


def _configure_stdio_utf8():
    for stream in (sys.stdin, sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def _capture_snapshot():
    return state_core.capture_snapshot(
        STATE,
        clone_fn=_clone,
        active_link_shape_fn=_active_link_shape,
    )


def _restore_snapshot(snapshot):
    state_core.restore_snapshot(
        STATE,
        snapshot,
        clone_fn=_clone,
        default_node_group_id=DEFAULT_NODE_GROUP_ID,
        default_curve_group_id=DEFAULT_CURVE_GROUP_ID,
        ensure_anchor_ids_fn=_ensure_anchor_ids,
        cleanup_links_and_selection_fn=_cleanup_links_and_selection,
        seed_missing_segment_denominators_fn=_seed_missing_segment_denominators,
        invalidate_curve_cache_fn=_invalidate_curve_cache,
    )


def _invalidate_curve_cache():
    STATE["curve_revision"] = int(STATE.get("curve_revision", 0)) + 1
    STATE["curve_samples_cache"] = {}


def _anchor_index_map():
    out = {}
    anchors = STATE.get("anchors", [])
    for idx, a in enumerate(anchors):
        aid = int(a.get("id", 0))
        if aid > 0:
            out[aid] = idx
    return out


def _ensure_anchor_ids():
    anchors = STATE.get("anchors", [])
    used = set()
    next_id = max(1, int(STATE.get("next_anchor_id", 1)))
    for a in anchors:
        aid = int(a.get("id", 0)) if isinstance(a, dict) else 0
        if aid <= 0 or aid in used:
            aid = next_id
            next_id += 1
            a["id"] = aid
        used.add(aid)
        if aid >= next_id:
            next_id = aid + 1
    STATE["next_anchor_id"] = next_id


def _new_anchor_id():
    _ensure_anchor_ids()
    aid = int(STATE.get("next_anchor_id", 1))
    STATE["next_anchor_id"] = aid + 1
    return aid


def _normalize_link(id_a, id_b):
    return cm.normalize_link(id_a, id_b, _anchor_index_map())


def _link_exists(id_a, id_b):
    return cm.link_exists(STATE.get("links", []), id_a, id_b, _anchor_index_map())


def _link_key(id_a, id_b):
    return cm.link_key(id_a, id_b, _anchor_index_map())


def _segment_denominator_for_link(id_a, id_b, fallback_den=4):
    return cm.segment_denominator_for_link(
        STATE.get("segment_denominators", {}),
        STATE.get("curve_density_mode_by_link", {}),
        id_a,
        id_b,
        _anchor_index_map(),
        fallback_den=fallback_den,
    )


def _set_segment_denominator(id_a, id_b, den):
    changed, seg_map, density_mode = cm.set_segment_denominator(
        STATE.get("segment_denominators", {}),
        STATE.get("curve_density_mode_by_link", {}),
        id_a,
        id_b,
        den,
        _anchor_index_map(),
    )
    STATE["segment_denominators"] = seg_map
    STATE["curve_density_mode_by_link"] = density_mode
    return changed


def _normalize_shape_name(shape):
    return cm.normalize_shape_name(shape)


def _active_link_shape():
    return _normalize_shape_name(STATE.get("active_link_shape", STATE.get("curve_shape", "curve")))


def _active_link_shape_is_polyline():
    return _active_link_shape() == "polyline"


def _segment_shape_for_link(id_a, id_b, fallback_shape="curve"):
    return cm.segment_shape_for_link(
        STATE.get("segment_shapes", {}),
        id_a,
        id_b,
        _anchor_index_map(),
        fallback_shape=fallback_shape,
    )


def _set_segment_shape(id_a, id_b, shape):
    changed, seg_map = cm.set_segment_shape(
        STATE.get("segment_shapes", {}),
        id_a,
        id_b,
        shape,
        _anchor_index_map(),
    )
    STATE["segment_shapes"] = seg_map
    return changed


def _context_default_segment_denominator(context=None):
    ctx = context if isinstance(context, dict) else {}
    if not ctx:
        ctx = STATE.get("last_context", {}) if isinstance(
            STATE.get("last_context", {}), dict) else {}
    override_den = int(ctx.get("plugin_time_division_override", 0)
                       or 0) if isinstance(ctx, dict) else 0
    if override_den > 0:
        return max(1, override_den)
    if isinstance(ctx, dict):
        try:
            time_div = int(ctx.get("time_division", 0) or 0)
            if time_div > 0:
                return time_div
        except Exception:
            pass
    dens = _sanitize_denominators(
        STATE.get("style", {}).get("denominators", [4]), ctx)
    if dens:
        return max(1, int(dens[0]))
    return 4


def _seed_missing_segment_denominators(context=None):
    links = STATE.get("links", [])
    seg_map = STATE.get("segment_denominators", {})
    if not isinstance(seg_map, dict):
        seg_map = {}
    default_den = _context_default_segment_denominator(context)
    density_mode = STATE.get("curve_density_mode_by_link", {})
    if not isinstance(density_mode, dict):
        density_mode = {}
    changed = False
    for raw in links:
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        norm = _normalize_link(raw[0], raw[1])
        if norm is None:
            continue
        key = f"{norm[0]}:{norm[1]}"
        if str(density_mode.get(key, "") or "").strip().lower() == "follow":
            continue
        try:
            cur = int(seg_map.get(key, 0))
        except Exception:
            cur = 0
        if cur > 0:
            continue
        seg_map[key] = default_den
        changed = True
    if changed or not isinstance(STATE.get("segment_denominators"), dict):
        STATE["segment_denominators"] = seg_map
    return changed


def _add_link(id_a, id_b):
    norm = _normalize_link(id_a, id_b)
    if norm is None:
        return False
    if _link_exists(norm[0], norm[1]):
        return False
    links = STATE.get("links", [])
    if not isinstance(links, list):
        links = []
    links.append([norm[0], norm[1]])
    STATE["links"] = links
    _set_segment_denominator(
        norm[0], norm[1], _context_default_segment_denominator())
    _set_segment_shape(norm[0], norm[1], _active_link_shape())
    return True


def _remove_link(id_a, id_b):
    norm = _normalize_link(id_a, id_b)
    if norm is None:
        return False
    changed = False
    kept = []
    for raw in STATE.get("links", []):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        cur = _normalize_link(raw[0], raw[1])
        if cur == norm:
            changed = True
            continue
        if cur is not None:
            kept.append([cur[0], cur[1]])
    if changed:
        STATE["links"] = kept
        _set_segment_denominator(norm[0], norm[1], 0)
        _set_segment_shape(norm[0], norm[1], "curve")
    return changed


def _cleanup_links_and_selection():
    idx_map = _anchor_index_map()
    valid_ids = set(idx_map.keys())

    dedup = set()
    cleaned_links = []
    for raw in STATE.get("links", []):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        norm = _normalize_link(raw[0], raw[1])
        if norm is None:
            continue
        if norm in dedup:
            continue
        dedup.add(norm)
        cleaned_links.append([norm[0], norm[1]])
    STATE["links"] = cleaned_links
    valid_link_keys = set(f"{a}:{b}" for (a, b) in dedup)
    seg_map_raw = STATE.get("segment_denominators", {})
    seg_cleaned = {}
    if isinstance(seg_map_raw, dict):
        for key, raw_val in seg_map_raw.items():
            if key not in valid_link_keys:
                continue
            try:
                den = int(raw_val)
            except Exception:
                continue
            if den > 0:
                seg_cleaned[key] = den
    STATE["segment_denominators"] = seg_cleaned

    seg_shapes_raw = STATE.get("segment_shapes", {})
    seg_shapes_cleaned = {}
    if isinstance(seg_shapes_raw, dict):
        for key, raw_val in seg_shapes_raw.items():
            if key not in valid_link_keys:
                continue
            shape = _normalize_shape_name(raw_val)
            if shape == "polyline":
                seg_shapes_cleaned[key] = shape
    STATE["segment_shapes"] = seg_shapes_cleaned
    density_mode_raw = STATE.get("curve_density_mode_by_link", {})
    density_mode_cleaned = {}
    if isinstance(density_mode_raw, dict):
        for key, raw_val in density_mode_raw.items():
            if key not in valid_link_keys:
                continue
            mode = str(raw_val or "").strip().lower()
            if mode in ("fixed", "follow"):
                density_mode_cleaned[key] = mode
    STATE["curve_density_mode_by_link"] = density_mode_cleaned

    selected_anchor_ids = [int(aid) for aid in STATE.get(
        "selected_anchor_ids", []) if int(aid) in valid_ids]
    if not _selection_enabled("anchors"):
        selected_anchor_ids = []
    STATE["selected_anchor_ids"] = selected_anchor_ids

    selected_links = []
    for raw in STATE.get("selected_links", []):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        norm = _normalize_link(raw[0], raw[1])
        if norm is None:
            continue
        if not _link_exists(norm[0], norm[1]):
            continue
        selected_links.append([norm[0], norm[1]])
    if not _selection_enabled("segments"):
        selected_links = []
    STATE["selected_links"] = selected_links

    pending = int(STATE.get("pending_connect_anchor_id", -1))
    if pending not in valid_ids:
        STATE["pending_connect_anchor_id"] = -1


def _default_links_for_anchors():
    anchors = STATE.get("anchors", [])
    if len(anchors) < 2:
        return []
    out = []
    for i in range(len(anchors) - 1):
        id0 = int(anchors[i].get("id", 0))
        id1 = int(anchors[i + 1].get("id", 0))
        norm = _normalize_link(id0, id1)
        if norm is not None:
            out.append([norm[0], norm[1]])
    return out


def _connected_anchor_segments():
    idx_map = _anchor_index_map()
    anchors = STATE.get("anchors", [])
    segments = []
    for raw in STATE.get("links", []):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        norm = _normalize_link(raw[0], raw[1])
        if norm is None:
            continue
        id0, id1 = norm
        i0 = idx_map.get(id0, -1)
        i1 = idx_map.get(id1, -1)
        if i0 < 0 or i1 < 0 or i0 == i1:
            continue
        a0 = anchors[i0]
        a1 = anchors[i1]
        segments.append((i0, i1, id0, id1, a0, a1))
    segments.sort(key=lambda s: (min(s[0], s[1]), max(s[0], s[1])))
    return segments


def _set_single_selected_anchor(anchor_id):
    aid = int(anchor_id)
    STATE["selected_anchor_ids"] = [aid] if aid > 0 else []


def _add_selected_anchor(anchor_id):
    aid = int(anchor_id)
    if aid <= 0:
        return
    selected = [int(v) for v in STATE.get(
        "selected_anchor_ids", []) if int(v) > 0]
    if aid not in selected:
        selected.append(aid)
    STATE["selected_anchor_ids"] = selected


def _set_single_selected_link(id_a, id_b):
    norm = _normalize_link(id_a, id_b)
    if norm is None:
        STATE["selected_links"] = []
        return
    STATE["selected_links"] = [[norm[0], norm[1]]]


def _distance_point_to_segment(px, py, x1, y1, x2, y2):
    return tm.distance_point_to_segment(px, py, x1, y1, x2, y2)


def _find_segment_hit(context, cx, cy, threshold=14.0):
    best = None
    best_d = 1e9
    for _i0, _i1, id0, id1, a0, a1 in _connected_anchor_segments():
        sampled = _sample_segment_chart(a0, a1, 24, id0, id1)
        if len(sampled) < 2:
            continue
        prev = _chart_to_canvas(context, sampled[0][0], sampled[0][1])
        for lane_x, beat in sampled[1:]:
            cur = _chart_to_canvas(context, lane_x, beat)
            d = _distance_point_to_segment(
                cx, cy, prev[0], prev[1], cur[0], cur[1])
            if d < threshold and d < best_d:
                best_d = d
                best = (id0, id1)
            prev = cur
    return best


def _toggle_selected_anchor(anchor_id):
    aid = int(anchor_id)
    selected = [int(v) for v in STATE.get("selected_anchor_ids", [])]
    if aid in selected:
        selected = [v for v in selected if v != aid]
    elif aid > 0:
        selected.append(aid)
    STATE["selected_anchor_ids"] = selected


def _toggle_selected_link(id_a, id_b):
    norm = _normalize_link(id_a, id_b)
    if norm is None:
        return
    selected = []
    exists = False
    for raw in STATE.get("selected_links", []):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        cur = _normalize_link(raw[0], raw[1])
        if cur is None:
            continue
        if cur == norm:
            exists = True
            continue
        selected.append([cur[0], cur[1]])
    if not exists:
        selected.append([norm[0], norm[1]])
    STATE["selected_links"] = selected


def _selection_enabled(kind):
    if bool(STATE.get("note_curve_snap_enabled", False)) and kind in ("anchors", "segments"):
        return False
    targets = STATE.get("selection_targets", {})
    if not isinstance(targets, dict):
        return True
    return bool(targets.get(kind, True))


def _connect_selected_anchors(context):
    idx_map = _anchor_index_map()
    selected = [int(v) for v in STATE.get(
        "selected_anchor_ids", []) if int(v) in idx_map]
    try:
        print(f"[note_chain_assist] _connect_selected_anchors selected={selected}", file=sys.stderr)
    except Exception:
        pass
    if len(selected) < 2:
        try:
            print(f"[note_chain_assist] _connect_selected_anchors -> insufficient selected ({len(selected)})", file=sys.stderr)
        except Exception:
            pass
        return False
    selected.sort(key=lambda aid: idx_map.get(aid, 1 << 30))
    changed = False
    for i in range(len(selected) - 1):
        changed = _add_link(selected[i], selected[i + 1]) or changed
    if changed:
        _cleanup_links_and_selection()
        _invalidate_curve_cache()
        _record_history_state(context)
    try:
        print(f"[note_chain_assist] _connect_selected_anchors -> changed={changed}", file=sys.stderr)
    except Exception:
        pass
    return changed


def _disconnect_selected_segments(context):
    changed = False
    for raw in list(STATE.get("selected_links", [])):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        changed = _remove_link(raw[0], raw[1]) or changed
    if changed:
        _cleanup_links_and_selection()
        _invalidate_curve_cache()
        _record_history_state(context)
    try:
        print(f"[note_chain_assist] _disconnect_selected_segments -> changed={changed}", file=sys.stderr)
    except Exception:
        pass
    return changed


def _delete_selected_anchors(context):
    selected = set(int(v) for v in STATE.get("selected_anchor_ids", []))
    try:
        print(f"[note_chain_assist] _delete_selected_anchors selected={sorted(list(selected))}", file=sys.stderr)
    except Exception:
        pass
    if not selected:
        try:
            print("[note_chain_assist] _delete_selected_anchors -> no selection", file=sys.stderr)
        except Exception:
            pass
        return False
    anchors = STATE.get("anchors", [])
    kept = [a for a in anchors if int(a.get("id", 0)) not in selected]
    if len(kept) == len(anchors):
        try:
            print("[note_chain_assist] _delete_selected_anchors -> no anchors removed", file=sys.stderr)
        except Exception:
            pass
        return False
    STATE["anchors"] = kept
    _cleanup_links_and_selection()
    _invalidate_curve_cache()
    _record_history_state(context)
    try:
        print(f"[note_chain_assist] _delete_selected_anchors -> removed {len(anchors)-len(kept)} anchors", file=sys.stderr)
    except Exception:
        pass
    return True


def _rect_normalized(x1, y1, x2, y2):
    return tm.rect_normalized(x1, y1, x2, y2)


def _point_in_rect(px, py, rect):
    return tm.point_in_rect(px, py, rect)


def _apply_box_selection(context):
    box = STATE.get("box_select", {})
    if not bool(box.get("active", False)):
        return False
    sx, sy = box.get("start", [0.0, 0.0])
    ex, ey = box.get("end", [0.0, 0.0])
    rect = _rect_normalized(float(sx), float(sy), float(ex), float(ey))
    append = bool(box.get("append", False))

    selected_anchor_ids = set(int(v) for v in STATE.get(
        "selected_anchor_ids", [])) if append else set()
    selected_links = set()
    if append:
        for raw in STATE.get("selected_links", []):
            if isinstance(raw, list) and len(raw) == 2:
                norm = _normalize_link(raw[0], raw[1])
                if norm is not None:
                    selected_links.add(norm)

    for a in STATE.get("anchors", []):
        cx, cy = _chart_to_canvas(context, a["lane_x"], a["beat"])
        if _point_in_rect(cx, cy, rect):
            selected_anchor_ids.add(int(a.get("id", 0)))

    for _i0, _i1, id0, id1, a0, a1 in _connected_anchor_segments():
        hit = False
        for lane_x, beat in _sample_segment_chart(a0, a1, 24, id0, id1):
            cx, cy = _chart_to_canvas(context, lane_x, beat)
            if _point_in_rect(cx, cy, rect):
                hit = True
                break
        if hit:
            selected_links.add((id0, id1))

    STATE["selected_anchor_ids"] = [
        aid for aid in sorted(selected_anchor_ids) if aid > 0]
    STATE["selected_links"] = [[a, b] for (a, b) in sorted(selected_links)]
    STATE["box_select"] = {"active": False, "start": [
        0.0, 0.0], "end": [0.0, 0.0], "append": False}
    _cleanup_links_and_selection()
    return True


def _push_history():
    return state_core.push_history(
        STATE,
        _capture_snapshot(),
        max_history=MAX_HISTORY,
    )


def _record_history_state(context):
    changed = _push_history()
    if changed:
        _mark_dirty(context, flush=False)
    return changed


def _undo_history_from_host(context):
    return state_core.undo_history(
        STATE,
        restore_snapshot_fn=_restore_snapshot,
        mark_dirty_fn=_mark_dirty,
        context=context,
    )


def _redo_history_from_host(context):
    return state_core.redo_history(
        STATE,
        restore_snapshot_fn=_restore_snapshot,
        mark_dirty_fn=_mark_dirty,
        context=context,
    )


def _is_curve_checkpoint_action(action_text):
    if not isinstance(action_text, str):
        return False
    return action_text.strip().startswith(CURVE_CHECKPOINT_PREFIX)


def _register_host_undo_action(context, action_id):
    tokens = STATE.get("host_undo_action_tokens", [])
    if not isinstance(tokens, list):
        tokens = []

    title = ""
    if isinstance(context, dict):
        title = str(context.get("action_title", "") or "").strip()
    if not title:
        if action_id == "reset_curve":
            title = tr(STATE.get("last_context", {}), "action_reset_curve")
        elif action_id in ("connect_selected_nodes", "connect_selected_nodes_ctx"):
            title = tr(STATE.get("last_context", {}),
                       "action_connect_selected")
        elif action_id in ("disconnect_selected_segments", "disconnect_selected_segments_ctx"):
            title = tr(STATE.get("last_context", {}),
                       "action_disconnect_selected_segments")
    if not title:
        return

    if title in tokens:
        tokens = [v for v in tokens if isinstance(v, str) and v != title]
    tokens.append(title)
    if len(tokens) > 64:
        tokens = tokens[-64:]
    STATE["host_undo_action_tokens"] = tokens


def _extract_host_action_title(action_text):
    text = str(action_text or "").strip()
    if not text:
        return ""
    # Host action text is usually "Plugin Action: <title>" (localized prefix possible).
    sep_idx = -1
    for sep in (":", "\uFF1A"):
        idx = text.find(sep)
        if idx >= 0:
            sep_idx = idx
            break
    if sep_idx >= 0:
        return text[sep_idx + 1:].strip()
    return text


def _is_registered_host_undo_action(action_text):
    title = _extract_host_action_title(action_text)
    if not title:
        return False
    for token in STATE.get("host_undo_action_tokens", []):
        if not isinstance(token, str):
            continue
        cur = token.strip()
        if cur and cur == title:
            return True
    return False


def _context_density_menu_state():
    links = _context_menu_target_links()
    dens = _sanitize_denominators(STATE.get("style", {}).get(
        "denominators", [4, 8, 12, 16]), STATE.get("last_context", {}))
    if not links:
        return {"has_target": False, "mixed": False, "mode": "", "den": 0, "denominators": dens}

    signatures = []
    density_mode_map = STATE.get("curve_density_mode_by_link", {})
    for id0, id1 in links:
        key = _link_key(id0, id1)
        mode = str(density_mode_map.get(key, "") if isinstance(
            density_mode_map, dict) else "").strip().lower()
        if mode == "follow":
            signatures.append(("follow", 0))
        else:
            den = _segment_denominator_for_link(
                id0, id1, _context_default_segment_denominator(STATE.get("last_context", {})))
            signatures.append(("fixed", int(den)))
    first = signatures[0] if signatures else ("", 0)
    mixed = any(sig != first for sig in signatures[1:])
    return {
        "has_target": True,
        "mixed": bool(mixed),
        "mode": str(first[0]),
        "den": int(first[1]),
        "denominators": dens,
    }


def _safe_denominator_set(context):
    values = context.get("safe_denominators") if isinstance(
        context, dict) else None
    out = set()
    if isinstance(values, list):
        for v in values:
            try:
                iv = int(v)
            except Exception:
                continue
            if iv > 0:
                out.add(iv)
    if not out:
        out = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 192, 288}
    return out


def _sanitize_denominators(values, context):
    safe = _safe_denominator_set(context)
    cleaned = []
    if isinstance(values, list):
        for d in values:
            try:
                iv = int(d)
            except Exception:
                continue
            if iv in safe and iv > 0:
                cleaned.append(iv)
    if not cleaned:
        cleaned = [4, 8, 12, 16]
    return cleaned


def _float_to_triplet(beat, den):
    return tm.float_to_triplet(beat, den)


def _triplet_to_float(tri):
    return tm.triplet_to_float(tri)


def _context_dims(context):
    return tm.context_dims(context)


def _canvas_to_chart(context, x, y):
    return tm.canvas_to_chart(context, x, y)


def _chart_to_canvas(context, lane_x, beat):
    return tm.chart_to_canvas(context, lane_x, beat)


def _snap_chart_point(context, lane_x, beat, snap_beat=True, snap_lane=True):
    return tm.snap_chart_point(context, lane_x, beat, snap_beat=snap_beat, snap_lane=snap_lane)


def _first_style_path(context):
    if not isinstance(context, dict):
        return ""
    paths = context.get("style_library_paths")
    if not isinstance(paths, list):
        return ""
    for p in paths:
        if isinstance(p, str) and p.strip():
            folder = p.strip()
            os.makedirs(folder, exist_ok=True)
            return os.path.join(folder, "note_chain_default.nca_style.json")
    return ""


def _save_style(context):
    path = _first_style_path(context)
    if not path:
        return False
    payload = {
        "format_version": 1,
        "style_id": "default",
        "style_name": str(STATE.get("style", {}).get("style_name", "custom")),
        "denominators": list(STATE.get("style", {}).get("denominators", [4, 8, 12, 16])),
    }
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)
        return True
    except Exception:
        return False


def _load_style(context):
    path = _first_style_path(context)
    if not path or not os.path.exists(path):
        return False
    try:
        with open(path, "r", encoding="utf-8") as f:
            payload = json.load(f)
        dens = _sanitize_denominators(payload.get("denominators"), context)
        STATE["style"] = {"style_name": str(payload.get(
            "style_name", "imported")), "denominators": dens}
        return True
    except Exception:
        return False


def _anchor_in_abs_chart(a):
    return a["lane_x"] + a["in"][0], a["beat"] + a["in"][1]


def _anchor_out_abs_chart(a):
    return a["lane_x"] + a["out"][0], a["beat"] + a["out"][1]


def _set_anchor_in_abs_chart(a, lane_x, beat, mirror=False):
    a["in"] = [float(lane_x) - a["lane_x"], float(beat) - a["beat"]]
    if mirror and a.get("smooth", True):
        a["out"] = [-a["in"][0], -a["in"][1]]


def _set_anchor_out_abs_chart(a, lane_x, beat, mirror=False):
    a["out"] = [float(lane_x) - a["lane_x"], float(beat) - a["beat"]]
    if mirror and a.get("smooth", True):
        a["in"] = [-a["out"][0], -a["out"][1]]


def _default_anchors():
    return []


def _find_anchor_hit(context, cx, cy, threshold=16.0):
    best = -1
    best_dist = 1e9
    for i, a in enumerate(STATE["anchors"]):
        ax, ay = _chart_to_canvas(context, a["lane_x"], a["beat"])
        d = _distance(cx, cy, ax, ay)
        if d < threshold and d < best_dist:
            best = i
            best_dist = d
    return best


def _find_handle_hit(context, cx, cy, threshold=14.0):
    best = ("", -1)
    best_dist = 1e9
    for i, a in enumerate(STATE["anchors"]):
        ilx, ib = _anchor_in_abs_chart(a)
        olx, ob = _anchor_out_abs_chart(a)
        ix, iy = _chart_to_canvas(context, ilx, ib)
        ox, oy = _chart_to_canvas(context, olx, ob)

        d1 = _distance(cx, cy, ix, iy)
        if d1 < threshold and d1 < best_dist:
            best = ("in", i)
            best_dist = d1
        d2 = _distance(cx, cy, ox, oy)
        if d2 < threshold and d2 < best_dist:
            best = ("out", i)
            best_dist = d2
    return best


def _cubic_point(a, b, c, d, t):
    return tm.cubic_point(a, b, c, d, t)


def _note_curve_snap_enabled():
    return bool(STATE.get("note_curve_snap_enabled", False))


def _segment_point_chart(a0, a1, t, id0=None, id1=None):
    p0 = (a0["lane_x"], a0["beat"])
    p3 = (a1["lane_x"], a1["beat"])
    t = _clamp(float(t), 0.0, 1.0)
    if id0 is not None and id1 is not None:
        shape = _segment_shape_for_link(id0, id1)
    else:
        shape = _active_link_shape()
    if shape == "polyline":
        return p0[0] + (p3[0] - p0[0]) * t, p0[1] + (p3[1] - p0[1]) * t

    p1 = _anchor_out_abs_chart(a0)
    p2 = _anchor_in_abs_chart(a1)
    return _cubic_point(p0, p1, p2, p3, t)


def _sample_segment_chart(a0, a1, samples_per_segment=24, id0=None, id1=None):
    pts = []
    samples = max(1, int(samples_per_segment))
    for j in range(samples + 1):
        pts.append(_segment_point_chart(a0, a1, j / float(samples), id0, id1))
    return pts


def _sample_curve_chart(samples_per_segment=24):
    segments = _connected_anchor_segments()
    if not segments:
        return []

    pts = []
    for _i0, _i1, id0, id1, a0, a1 in segments:
        sampled = _sample_segment_chart(a0, a1, samples_per_segment, id0, id1)
        if pts and sampled:
            sampled = sampled[1:]
        pts.extend(sampled)
    return pts


def _sample_curve_chart_cached(samples_per_segment=24):
    cache = STATE.get("curve_samples_cache")
    if not isinstance(cache, dict):
        cache = {}
        STATE["curve_samples_cache"] = cache
    key = f"{int(STATE.get('curve_revision', 0))}:{int(samples_per_segment)}"
    cached = cache.get(key)
    if isinstance(cached, list):
        return cached
    sampled = _sample_curve_chart(samples_per_segment)
    cache[key] = sampled
    return sampled


def _visible_beat_window(context, ratio_margin=0.6, min_margin=2.0):
    if not isinstance(context, dict):
        return -1e9, 1e9
    scroll = float(context.get("scroll_beat", 0.0))
    vr = max(1e-6, float(context.get("visible_beat_range", 8.0)))
    margin = max(float(min_margin), vr * float(ratio_margin))
    return scroll - margin, scroll + vr + margin


def _segment_intersects_beat_window(p0, p1, lo, hi):
    b0 = float(p0[1])
    b1 = float(p1[1])
    mn = min(b0, b1)
    mx = max(b0, b1)
    return not (mx < lo or mn > hi)


def _normalize_samples_by_beat(samples):
    if not samples:
        return []
    ordered = sorted(samples, key=lambda p: (p[1], p[0]))
    out = []
    eps = 1e-6
    for lane_x, beat in ordered:
        bx = float(lane_x)
        bb = float(beat)
        if not out:
            out.append([bx, bb])
            continue
        if abs(bb - out[-1][1]) <= eps:
            out[-1][0] = (out[-1][0] + bx) * 0.5
        else:
            out.append([bx, bb])
    return out


def _lane_x_at_beat(samples_by_beat, beat):
    if not samples_by_beat:
        return 0.0
    if beat <= samples_by_beat[0][1]:
        return samples_by_beat[0][0]
    if beat >= samples_by_beat[-1][1]:
        return samples_by_beat[-1][0]

    for i in range(len(samples_by_beat) - 1):
        a = samples_by_beat[i]
        b = samples_by_beat[i + 1]
        if a[1] <= beat <= b[1]:
            db = b[1] - a[1]
            if abs(db) <= 1e-9:
                return a[0]
            t = (beat - a[1]) / db
            return a[0] + (b[0] - a[0]) * t
    return samples_by_beat[-1][0]


def _enforce_anchor_time_order(index, context):
    if not (0 <= index < len(STATE["anchors"])):
        return
    den = max(1, int(context.get("time_division", 4)))
    eps = 1.0 / float(den * 4)
    cur = STATE["anchors"][index]
    cur_id = int(cur.get("id", 0))
    lower = None
    upper = None
    for _i0, _i1, id0, id1, a0, a1 in _connected_anchor_segments():
        if cur_id <= 0:
            break
        other = None
        if id0 == cur_id:
            other = a1
        elif id1 == cur_id:
            other = a0
        if other is None:
            continue
        other_b = float(other.get("beat", 0.0))
        if other_b < float(cur.get("beat", 0.0)):
            candidate = other_b + eps
            lower = candidate if lower is None else max(lower, candidate)
        elif other_b > float(cur.get("beat", 0.0)):
            candidate = other_b - eps
            upper = candidate if upper is None else min(upper, candidate)
    if lower is not None:
        cur["beat"] = max(float(cur.get("beat", 0.0)), float(lower))
    if upper is not None:
        cur["beat"] = min(float(cur.get("beat", 0.0)), float(upper))


def _enforce_handle_time_constraints(index, context):
    if not (0 <= index < len(STATE["anchors"])):
        return
    den = max(1, int(context.get("time_division", 4)))
    eps = 1.0 / float(den * 4)
    a = STATE["anchors"][index]
    cur_id = int(a.get("id", 0))
    cur_beat = float(a.get("beat", 0.0))
    out_upper = None
    in_lower = None
    for _i0, _i1, id0, id1, a0, a1 in _connected_anchor_segments():
        if cur_id <= 0:
            break
        other = None
        if id0 == cur_id:
            other = a1
        elif id1 == cur_id:
            other = a0
        if other is None:
            continue
        other_b = float(other.get("beat", 0.0))
        if other_b > cur_beat:
            candidate = max(0.0, other_b - cur_beat - eps)
            out_upper = candidate if out_upper is None else min(
                out_upper, candidate)
        elif other_b < cur_beat:
            candidate = min(0.0, other_b - cur_beat + eps)
            in_lower = candidate if in_lower is None else max(
                in_lower, candidate)

    if out_upper is not None:
        a["out"][1] = _clamp(a["out"][1], 0.0, out_upper)
    if in_lower is not None:
        a["in"][1] = _clamp(a["in"][1], in_lower, 0.0)


def _connected_neighbor_anchor_indices(anchor_index=None, anchor_id=None):
    anchors = STATE.get("anchors", [])
    idx = int(anchor_index) if isinstance(anchor_index, int) else -1
    if anchor_id is None:
        if not (0 <= idx < len(anchors)):
            return []
        anchor_id = int(anchors[idx].get("id", 0))
    else:
        anchor_id = int(anchor_id)

    if anchor_id <= 0:
        return []

    out = set()
    for i0, i1, id0, id1, _a0, _a1 in _connected_anchor_segments():
        if id0 == anchor_id:
            out.add(int(i1))
        elif id1 == anchor_id:
            out.add(int(i0))
    if 0 <= idx < len(anchors) and idx in out:
        out.remove(idx)
    return [i for i in sorted(out) if 0 <= i < len(anchors)]


def _enforce_anchor_and_connected_handle_constraints(index, context):
    if not (0 <= index < len(STATE["anchors"])):
        return
    neighbors = _connected_neighbor_anchor_indices(anchor_index=index)
    if not neighbors:
        # Unconnected anchors should not trigger any curve constraint pass.
        return
    _enforce_anchor_time_order(index, context)
    _enforce_handle_time_constraints(index, context)
    for neighbor_idx in neighbors:
        _enforce_handle_time_constraints(neighbor_idx, context)


def _serialize_anchor(a):
    return {
        "id": int(a.get("id", 0)),
        "lane_x": float(a.get("lane_x", 0.0)),
        "beat": _float_to_triplet(float(a.get("beat", 0.0)), SERIALIZE_DEN),
        "in": {
            "lane_dx": float((a.get("in") or [0.0, 0.0])[0]),
            "beat_delta": _float_to_triplet(float((a.get("in") or [0.0, 0.0])[1]), SERIALIZE_DEN),
        },
        "out": {
            "lane_dx": float((a.get("out") or [0.0, 0.0])[0]),
            "beat_delta": _float_to_triplet(float((a.get("out") or [0.0, 0.0])[1]), SERIALIZE_DEN),
        },
        "smooth": bool(a.get("smooth", True)),
    }


def _deserialize_anchor(raw, context):
    if not isinstance(raw, dict):
        return None

    # New chart-space format.
    if "lane_x" in raw:
        anchor_id = int(raw.get("id", 0))
        lane_x = float(raw.get("lane_x", 0.0))
        beat = _triplet_to_float(raw.get("beat"))
        if beat is None:
            beat = float(raw.get("beat_float", 0.0))

        in_raw = raw.get("in")
        out_raw = raw.get("out")

        if isinstance(in_raw, dict):
            in_dx = float(in_raw.get("lane_dx", 0.0))
            in_db = _triplet_to_float(in_raw.get("beat_delta"))
            if in_db is None:
                in_db = float(in_raw.get("beat_delta_float", 0.0))
        else:
            in_arr = in_raw if isinstance(in_raw, list) else [0.0, 0.0]
            in_dx = float(in_arr[0])
            in_db = float(in_arr[1])

        if isinstance(out_raw, dict):
            out_dx = float(out_raw.get("lane_dx", 0.0))
            out_db = _triplet_to_float(out_raw.get("beat_delta"))
            if out_db is None:
                out_db = float(out_raw.get("beat_delta_float", 0.0))
        else:
            out_arr = out_raw if isinstance(out_raw, list) else [0.0, 0.0]
            out_dx = float(out_arr[0])
            out_db = float(out_arr[1])

        lane_x, beat = _snap_chart_point(context, lane_x, beat)
        return {
            "id": anchor_id,
            "lane_x": lane_x,
            "beat": beat,
            "in": [in_dx, in_db],
            "out": [out_dx, out_db],
            "smooth": bool(raw.get("smooth", True)),
        }

    # Legacy canvas-space format migration.
    if "x" in raw and "y" in raw:
        lane_x, beat = _canvas_to_chart(context, float(
            raw.get("x", 0.0)), float(raw.get("y", 0.0)))
        lane_x, beat = _snap_chart_point(context, lane_x, beat)
        return {
            "id": int(raw.get("id", 0)),
            "lane_x": lane_x,
            "beat": beat,
            "in": [float((raw.get("in") or [0.0, 0.0])[0]), float((raw.get("in") or [0.0, 0.0])[1])],
            "out": [float((raw.get("out") or [0.0, 0.0])[0]), float((raw.get("out") or [0.0, 0.0])[1])],
            "smooth": bool(raw.get("smooth", True)),
        }

    return None


def _parse_int(value, fallback=0):
    return scv3.parse_int(value, fallback)


def _parse_float(value, fallback=0.0):
    return scv3.parse_float(value, fallback)


def _triplet_from_any(value, fallback_float=0.0):
    return scv3.triplet_from_any(
        value,
        fallback_float,
        triplet_to_float=_triplet_to_float,
        float_to_triplet=_float_to_triplet,
        serialize_den=SERIALIZE_DEN,
    )


def _beat_from_any(value, fallback_float=0.0):
    return scv3.beat_from_any(value, fallback_float, triplet_to_float=_triplet_to_float)


def _clone_dict_or_empty(value):
    return scv3.clone_dict_or_empty(value, clone=_clone)


def _unique_positive_int_list(value, default_id):
    return scv3.unique_positive_int_list(value, default_id)


def _next_available_positive(used, start=1):
    return scv3.next_available_positive(used, start)


def _default_group_entry(group_id, name):
    return scv3.default_group_entry(group_id, name)


def _dedupe_group_names(groups):
    scv3.dedupe_group_names(groups)


def _normalize_group_entries(entries, default_group_id, default_name):
    return scv3.normalize_group_entries(entries, default_group_id, default_name, state=STATE, clone=_clone)


def _ensure_groups_contain_ids(groups_key, required_ids, default_prefix):
    scv3.ensure_groups_contain_ids(
        groups_key, required_ids, default_prefix, state=STATE)


def _cleanup_v3_metadata():
    valid_link_keys = set()
    for raw in STATE.get("links", []):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        norm = _normalize_link(raw[0], raw[1])
        if norm is None:
            continue
        valid_link_keys.add(f"{norm[0]}:{norm[1]}")

    valid_anchor_ids = set(int(a.get("id", 0))
                           for a in STATE.get("anchors", []) if isinstance(a, dict))

    for key in ("anchor_group_ids", "anchor_reserved", "anchor_compat_handles"):
        raw_map = STATE.get(key, {})
        cleaned = {}
        if isinstance(raw_map, dict):
            for raw_id, raw_val in raw_map.items():
                aid = _parse_int(raw_id, 0)
                if aid <= 0 or aid not in valid_anchor_ids:
                    continue
                cleaned[str(aid)] = _clone(raw_val)
        STATE[key] = cleaned

    for key in (
        "curve_id_by_link",
        "curve_no_by_link",
        "curve_group_ids_by_link",
        "curve_density_mode_by_link",
        "curve_reserved_by_link",
        "curve_special_joystick_by_link",
    ):
        raw_map = STATE.get(key, {})
        cleaned = {}
        if isinstance(raw_map, dict):
            for raw_link_key, raw_val in raw_map.items():
                if raw_link_key not in valid_link_keys:
                    continue
                cleaned[raw_link_key] = _clone(raw_val)
        STATE[key] = cleaned

    required_node_group_ids = set()
    for aid in valid_anchor_ids:
        key = str(aid)
        gids = _unique_positive_int_list(
            STATE.get("anchor_group_ids", {}).get(key, []), DEFAULT_NODE_GROUP_ID)
        STATE["anchor_group_ids"][key] = gids
        required_node_group_ids.update(gids)
        STATE["anchor_reserved"][key] = _clone_dict_or_empty(
            STATE.get("anchor_reserved", {}).get(key))

    required_curve_group_ids = set()
    density_mode = STATE.get("curve_density_mode_by_link", {})
    if not isinstance(density_mode, dict):
        density_mode = {}
    for link_key in valid_link_keys:
        gids = _unique_positive_int_list(STATE.get(
            "curve_group_ids_by_link", {}).get(link_key, []), DEFAULT_CURVE_GROUP_ID)
        STATE["curve_group_ids_by_link"][link_key] = gids
        required_curve_group_ids.update(gids)
        mode = str(density_mode.get(link_key, "") or "").strip().lower()
        if mode not in ("fixed", "follow"):
            den = _parse_int(
                STATE.get("segment_denominators", {}).get(link_key, 0), 0)
            density_mode[link_key] = "fixed" if den > 0 else "follow"
        STATE["curve_reserved_by_link"][link_key] = _clone_dict_or_empty(
            STATE.get("curve_reserved_by_link", {}).get(link_key))
        STATE["curve_special_joystick_by_link"][link_key] = _clone_dict_or_empty(
            STATE.get("curve_special_joystick_by_link", {}).get(link_key)
        )
    STATE["curve_density_mode_by_link"] = density_mode

    STATE["node_groups"] = _normalize_group_entries(
        STATE.get("node_groups", []), DEFAULT_NODE_GROUP_ID, "base")
    STATE["curve_groups"] = _normalize_group_entries(
        STATE.get("curve_groups", []), DEFAULT_CURVE_GROUP_ID, "base")
    _ensure_groups_contain_ids(
        "node_groups", required_node_group_ids, "node_group")
    _ensure_groups_contain_ids(
        "curve_groups", required_curve_group_ids, "curve_group")


def _ensure_curve_identity_and_numbers():
    _cleanup_links_and_selection()
    _cleanup_v3_metadata()

    ordered_keys = []
    for raw in STATE.get("links", []):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        norm = _normalize_link(raw[0], raw[1])
        if norm is None:
            continue
        ordered_keys.append(f"{norm[0]}:{norm[1]}")

    existing_id_map = _clone_dict_or_empty(STATE.get("curve_id_by_link", {}))
    existing_no_map = _clone_dict_or_empty(STATE.get("curve_no_by_link", {}))
    id_out = {}
    no_out = {}

    used_ids = set()
    next_curve_id = max(1, _parse_int(STATE.get("next_curve_id", 1), 1))
    for key in ordered_keys:
        cid = _parse_int(existing_id_map.get(key, 0), 0)
        if cid <= 0 or cid in used_ids:
            cid = _next_available_positive(used_ids, next_curve_id)
        used_ids.add(cid)
        id_out[key] = cid
        if cid >= next_curve_id:
            next_curve_id = cid + 1

    used_numbers = set()
    for key in ordered_keys:
        cno = _parse_int(existing_no_map.get(key, 0), 0)
        if cno <= 0 or cno in used_numbers:
            cno = _next_available_positive(used_numbers, 1)
        used_numbers.add(cno)
        no_out[key] = cno

    STATE["curve_id_by_link"] = id_out
    STATE["curve_no_by_link"] = no_out
    STATE["next_curve_id"] = max(1, next_curve_id)


def _serialize_node_for_v3(anchor):
    aid = int(anchor.get("id", 0))
    key = str(aid)
    out_handle = anchor.get("out", [0.0, 0.0])
    out_dx = _parse_float(out_handle[0] if isinstance(
        out_handle, list) and len(out_handle) >= 1 else 0.0, 0.0)
    out_db = _parse_float(out_handle[1] if isinstance(
        out_handle, list) and len(out_handle) >= 2 else 0.0, 0.0)
    compat_raw = STATE.get("anchor_compat_handles", {}).get(key, {})
    compat = _clone_dict_or_empty(compat_raw)
    if not compat:
        compat = {
            "in": {
                "lane_dx": _parse_float(anchor.get("in", [0.0, 0.0])[0] if isinstance(anchor.get("in"), list) else 0.0, 0.0),
                "beat_delta": _triplet_from_any(anchor.get("in", [0.0, 0.0])[1] if isinstance(anchor.get("in"), list) else 0.0, 0.0),
            },
            "out": {
                "lane_dx": out_dx,
                "beat_delta": _triplet_from_any(out_db, out_db),
            },
        }
    return {
        "node_id": aid,
        "lane_x": _parse_float(anchor.get("lane_x", 0.0), 0.0),
        "beat": _triplet_from_any(anchor.get("beat", 0.0), 0.0),
        "joystick": {"lane_dx": out_dx, "beat_delta": _triplet_from_any(out_db, out_db)},
        "group_ids": _unique_positive_int_list(STATE.get("anchor_group_ids", {}).get(key, []), DEFAULT_NODE_GROUP_ID),
        "reserved": _clone_dict_or_empty(STATE.get("anchor_reserved", {}).get(key)),
        "compat_handles": compat,
        "smooth": bool(anchor.get("smooth", True)),
    }


def _serialize_curve_for_v3(norm):
    key = f"{norm[0]}:{norm[1]}"
    den = _parse_int(STATE.get("segment_denominators", {}).get(key, 0), 0)
    density_mode = str(STATE.get("curve_density_mode_by_link",
                       {}).get(key, "") or "").strip().lower()
    if density_mode == "follow":
        density = {"mode": "follow"}
    elif den > 0:
        density = {"mode": "fixed", "denominator": den}
    else:
        density = {"mode": "follow"}
    shape = _normalize_shape_name(
        STATE.get("segment_shapes", {}).get(key, "curve"))
    return {
        "curve_id": _parse_int(STATE.get("curve_id_by_link", {}).get(key, 0), 0),
        "curve_no": _parse_int(STATE.get("curve_no_by_link", {}).get(key, 0), 0),
        "node_ids": [int(norm[0]), int(norm[1])],
        "density": density,
        "style_category": shape,
        "group_ids": _unique_positive_int_list(STATE.get("curve_group_ids_by_link", {}).get(key, []), DEFAULT_CURVE_GROUP_ID),
        "special_joystick_reserved": _clone_dict_or_empty(STATE.get("curve_special_joystick_by_link", {}).get(key)),
        "reserved": _clone_dict_or_empty(STATE.get("curve_reserved_by_link", {}).get(key)),
    }


def _build_v3_payload():
    _ensure_curve_identity_and_numbers()
    anchors = STATE.get("anchors", [])
    nodes = [_serialize_node_for_v3(a) for a in anchors]

    curves = []
    for raw in STATE.get("links", []):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        norm = _normalize_link(raw[0], raw[1])
        if norm is None:
            continue
        curves.append(_serialize_curve_for_v3(norm))

    revision = max(0, _parse_int(STATE.get("project_revision", 0), 0))
    file_uuid = str(STATE.get("project_file_uuid", "") or "").strip()
    if not file_uuid:
        file_uuid = uuid.uuid4().hex
        STATE["project_file_uuid"] = file_uuid
    return {
        "format_version": CURVE_SIDECAR_FORMAT_VERSION,
        "coordinate_space": "chart",
        "revision": revision,
        "file_uuid": file_uuid,
        "updated_at": int(time.time() * 1000),
        "last_writer_instance": str(STATE.get("instance_id", "") or ""),
        "nodes": nodes,
        "curves": curves,
        "node_groups": _clone(STATE.get("node_groups", [])),
        "curve_groups": _clone(STATE.get("curve_groups", [])),
        "style": STATE.get("style", {"denominators": [4, 8, 12, 16], "style_name": "balanced"}),
        "active_link_shape": _active_link_shape(),
        "note_curve_snap_enabled": _note_curve_snap_enabled(),
    }


def _set_save_error(code, detail=""):
    scv3.set_save_error(STATE, code, detail)
    if code:
        try:
            sys.stderr.write(
                f"[note_chain_assist] save failed: {code} {detail}\n")
            sys.stderr.flush()
        except Exception:
            pass


def _read_disk_payload(path):
    return scv3.read_disk_payload(path, json_module=json, os_module=os)


def _save_project(path, context=None):
    if bool(STATE.get("project_load_failed", False)) and str(STATE.get("project_path", "") or "") == str(path or ""):
        _set_save_error("load_failed_blocked",
                        "sidecar load failed; save blocked to protect existing file")
        return False
    return scv3.save_project(
        STATE,
        path,
        context,
        os_module=os,
        json_module=json,
        time_module=time,
        read_disk_payload_fn=_read_disk_payload,
        build_v3_payload_fn=_build_v3_payload,
        parse_int_fn=_parse_int,
        set_save_error_fn=_set_save_error,
    )


def _load_project_v2_payload(payload, context):
    anchors_raw = payload.get("anchors")
    parsed = []
    if isinstance(anchors_raw, list):
        for raw in anchors_raw:
            a = _deserialize_anchor(raw, context)
            if a is not None:
                parsed.append(a)
    STATE["anchors"] = parsed
    _ensure_anchor_ids()

    links_raw = payload.get("links")
    if isinstance(links_raw, list):
        STATE["links"] = _clone(links_raw)
    else:
        STATE["links"] = _default_links_for_anchors()
    seg_dens = payload.get("segment_denominators")
    STATE["segment_denominators"] = _clone(
        seg_dens) if isinstance(seg_dens, dict) else {}
    seg_shapes = payload.get("segment_shapes")
    if isinstance(seg_shapes, dict):
        STATE["segment_shapes"] = _clone(seg_shapes)
    else:
        legacy_shape = _normalize_shape_name(
            payload.get("curve_shape", "curve"))
        STATE["segment_shapes"] = {}
        if legacy_shape == "polyline":
            for raw in STATE.get("links", []):
                if isinstance(raw, list) and len(raw) == 2:
                    norm = _normalize_link(raw[0], raw[1])
                    if norm is not None:
                        STATE["segment_shapes"][f"{norm[0]}:{norm[1]}"] = "polyline"

    STATE["anchor_group_ids"] = {}
    STATE["anchor_reserved"] = {}
    STATE["anchor_compat_handles"] = {}
    for a in STATE.get("anchors", []):
        aid = int(a.get("id", 0))
        if aid <= 0:
            continue
        key = str(aid)
        STATE["anchor_group_ids"][key] = [DEFAULT_NODE_GROUP_ID]
        STATE["anchor_reserved"][key] = {}
        STATE["anchor_compat_handles"][key] = {
            "in": {
                "lane_dx": _parse_float(a.get("in", [0.0, 0.0])[0] if isinstance(a.get("in"), list) else 0.0, 0.0),
                "beat_delta": _triplet_from_any(a.get("in", [0.0, 0.0])[1] if isinstance(a.get("in"), list) else 0.0, 0.0),
            },
            "out": {
                "lane_dx": _parse_float(a.get("out", [0.0, 0.0])[0] if isinstance(a.get("out"), list) else 0.0, 0.0),
                "beat_delta": _triplet_from_any(a.get("out", [0.0, 0.0])[1] if isinstance(a.get("out"), list) else 0.0, 0.0),
            },
        }

    STATE["curve_id_by_link"] = {}
    STATE["curve_no_by_link"] = {}
    STATE["curve_group_ids_by_link"] = {}
    STATE["curve_density_mode_by_link"] = {}
    STATE["curve_reserved_by_link"] = {}
    STATE["curve_special_joystick_by_link"] = {}
    STATE["node_groups"] = [_default_group_entry(
        DEFAULT_NODE_GROUP_ID, "base")]
    STATE["curve_groups"] = [_default_group_entry(
        DEFAULT_CURVE_GROUP_ID, "base")]

    style = payload.get("style")
    if isinstance(style, dict):
        STATE["style"] = {
            "style_name": str(style.get("style_name", "loaded")),
            "denominators": _sanitize_denominators(style.get("denominators"), context),
        }
    active_shape = payload.get(
        "active_link_shape", payload.get("curve_shape", "curve"))
    STATE["active_link_shape"] = _normalize_shape_name(active_shape)
    STATE["note_curve_snap_enabled"] = bool(
        payload.get("note_curve_snap_enabled", False))
    STATE["project_revision"] = 0
    STATE["project_file_uuid"] = str(
        STATE.get("project_file_uuid", "") or "") or uuid.uuid4().hex
    STATE["project_last_writer_instance"] = str(
        payload.get("last_writer_instance", "") or "")
    _cleanup_links_and_selection()
    _seed_missing_segment_denominators(context)
    _ensure_curve_identity_and_numbers()


def _load_project_v3_payload(payload, context):
    nodes = payload.get("nodes")
    parsed_anchors = []
    anchor_group_ids = {}
    anchor_reserved = {}
    anchor_compat = {}
    if isinstance(nodes, list):
        for raw in nodes:
            if not isinstance(raw, dict):
                continue
            aid = _parse_int(raw.get("node_id", raw.get("id", 0)), 0)
            if aid <= 0:
                continue
            lane_x = _parse_float(raw.get("lane_x", 0.0), 0.0)
            beat = _beat_from_any(raw.get("beat"), 0.0)
            joystick_raw = raw.get("joystick")
            if isinstance(joystick_raw, dict):
                joy_dx = _parse_float(joystick_raw.get("lane_dx", 0.0), 0.0)
                joy_db = _beat_from_any(joystick_raw.get("beat_delta"), 0.0)
            else:
                compat_raw = raw.get("compat_handles", {})
                out_raw = compat_raw.get("out", {}) if isinstance(
                    compat_raw, dict) else {}
                joy_dx = _parse_float(out_raw.get("lane_dx", 0.0), 0.0)
                joy_db = _beat_from_any(out_raw.get("beat_delta"), 0.0)

            lane_x, beat = _snap_chart_point(context, lane_x, beat)
            parsed_anchors.append({
                "id": aid,
                "lane_x": lane_x,
                "beat": beat,
                "in": [-joy_dx, -joy_db],
                "out": [joy_dx, joy_db],
                "smooth": bool(raw.get("smooth", True)),
            })
            key = str(aid)
            anchor_group_ids[key] = _unique_positive_int_list(
                raw.get("group_ids"), DEFAULT_NODE_GROUP_ID)
            anchor_reserved[key] = _clone_dict_or_empty(raw.get("reserved"))
            compat_payload = raw.get("compat_handles")
            if isinstance(compat_payload, dict) and compat_payload:
                anchor_compat[key] = _clone_dict_or_empty(compat_payload)
            else:
                anchor_compat[key] = {
                    "in": {"lane_dx": -joy_dx, "beat_delta": _triplet_from_any(-joy_db, -joy_db)},
                    "out": {"lane_dx": joy_dx, "beat_delta": _triplet_from_any(joy_db, joy_db)},
                }

    parsed_anchors.sort(key=lambda a: (float(a.get("beat", 0.0)), float(
        a.get("lane_x", 0.0)), int(a.get("id", 0))))
    STATE["anchors"] = parsed_anchors
    _ensure_anchor_ids()

    curve_id_by_link = {}
    curve_no_by_link = {}
    curve_group_ids_by_link = {}
    curve_density_mode_by_link = {}
    curve_reserved_by_link = {}
    curve_special_joystick_by_link = {}
    links = []
    dedup_links = set()
    curves = payload.get("curves")
    if isinstance(curves, list):
        for raw in curves:
            if not isinstance(raw, dict):
                continue
            node_ids = raw.get("node_ids")
            if not isinstance(node_ids, list) or len(node_ids) != 2:
                continue
            norm = _normalize_link(node_ids[0], node_ids[1])
            if norm is None or norm in dedup_links:
                continue
            dedup_links.add(norm)
            links.append([norm[0], norm[1]])
            key = f"{norm[0]}:{norm[1]}"
            curve_id_by_link[key] = _parse_int(raw.get("curve_id", 0), 0)
            curve_no_by_link[key] = _parse_int(raw.get("curve_no", 0), 0)
            curve_group_ids_by_link[key] = _unique_positive_int_list(
                raw.get("group_ids"), DEFAULT_CURVE_GROUP_ID)
            curve_reserved_by_link[key] = _clone_dict_or_empty(
                raw.get("reserved"))
            curve_special_joystick_by_link[key] = _clone_dict_or_empty(
                raw.get("special_joystick_reserved"))

            density = raw.get("density", {})
            mode = str(density.get("mode", "follow") if isinstance(
                density, dict) else "follow").strip().lower()
            if mode == "fixed":
                den = _parse_int(density.get("denominator", 0),
                                 0) if isinstance(density, dict) else 0
                if den > 0:
                    STATE["segment_denominators"][key] = den
                    curve_density_mode_by_link[key] = "fixed"
            else:
                curve_density_mode_by_link[key] = "follow"
            style_category = raw.get("style_category", "curve")
            if _normalize_shape_name(style_category) == "polyline":
                STATE["segment_shapes"][key] = "polyline"

    STATE["links"] = links if links else _default_links_for_anchors()
    STATE["anchor_group_ids"] = anchor_group_ids
    STATE["anchor_reserved"] = anchor_reserved
    STATE["anchor_compat_handles"] = anchor_compat
    STATE["curve_id_by_link"] = curve_id_by_link
    STATE["curve_no_by_link"] = curve_no_by_link
    STATE["curve_group_ids_by_link"] = curve_group_ids_by_link
    STATE["curve_density_mode_by_link"] = curve_density_mode_by_link
    STATE["curve_reserved_by_link"] = curve_reserved_by_link
    STATE["curve_special_joystick_by_link"] = curve_special_joystick_by_link
    STATE["node_groups"] = _normalize_group_entries(
        payload.get("node_groups", []), DEFAULT_NODE_GROUP_ID, "base")
    STATE["curve_groups"] = _normalize_group_entries(
        payload.get("curve_groups", []), DEFAULT_CURVE_GROUP_ID, "base")

    style = payload.get("style")
    if isinstance(style, dict):
        STATE["style"] = {
            "style_name": str(style.get("style_name", "loaded")),
            "denominators": _sanitize_denominators(style.get("denominators"), context),
        }
    active_shape = payload.get(
        "active_link_shape", payload.get("curve_shape", "curve"))
    STATE["active_link_shape"] = _normalize_shape_name(active_shape)
    STATE["note_curve_snap_enabled"] = bool(
        payload.get("note_curve_snap_enabled", False))
    STATE["project_revision"] = max(
        0, _parse_int(payload.get("revision", 0), 0))
    STATE["project_file_uuid"] = str(payload.get(
        "file_uuid", "") or "").strip() or uuid.uuid4().hex
    STATE["project_last_writer_instance"] = str(
        payload.get("last_writer_instance", "") or "")
    _cleanup_links_and_selection()
    _seed_missing_segment_denominators(context)
    _ensure_curve_identity_and_numbers()


def _load_project(path, context):
    return scv3.load_project(
        STATE,
        path,
        context,
        os_module=os,
        json_module=json,
        parse_int_fn=_parse_int,
        set_save_error_fn=_set_save_error,
        load_project_v2_payload_fn=_load_project_v2_payload,
        load_project_v3_payload_fn=_load_project_v3_payload,
        invalidate_curve_cache_fn=_invalidate_curve_cache,
        format_version_threshold=CURVE_SIDECAR_FORMAT_VERSION,
    )


def _ensure_project_context(context):
    path = ""
    if isinstance(context, dict):
        path = str(context.get("curve_project_path", "") or "").strip()
    if not path:
        return

    path_changed = STATE["project_path"] != path
    if path_changed or bool(STATE.get("project_load_failed", False)):
        if path_changed and STATE["project_path"] and STATE["project_dirty"]:
            # Follow host save/discard flow: do not auto-persist when switching chart/project.
            STATE["project_dirty"] = False
        if path_changed:
            STATE["project_path"] = path
            # Reset default link shape when switching chart/project.
            STATE["active_link_shape"] = "curve"
        STATE["project_load_failed"] = False
        STATE["suppress_persist_once"] = False
        _try_seed_curve_project_from_source(context, path)
        if not _load_project(path, context):
            STATE["anchors"] = _default_anchors()
            _ensure_anchor_ids()
            STATE["links"] = _default_links_for_anchors()
            STATE["segment_denominators"] = {}
            STATE["segment_shapes"] = {}
            STATE["anchor_group_ids"] = {}
            STATE["anchor_reserved"] = {}
            STATE["anchor_compat_handles"] = {}
            STATE["curve_id_by_link"] = {}
            STATE["curve_no_by_link"] = {}
            STATE["curve_group_ids_by_link"] = {}
            STATE["curve_density_mode_by_link"] = {}
            STATE["curve_reserved_by_link"] = {}
            STATE["curve_special_joystick_by_link"] = {}
            STATE["node_groups"] = [_default_group_entry(
                DEFAULT_NODE_GROUP_ID, "base")]
            STATE["curve_groups"] = [_default_group_entry(
                DEFAULT_CURVE_GROUP_ID, "base")]
            STATE["next_curve_id"] = 1
            STATE["next_group_id"] = 2
            STATE["project_revision"] = 0
            STATE["project_file_uuid"] = uuid.uuid4().hex
            STATE["project_last_writer_instance"] = ""
            file_exists = os.path.exists(path)
            if file_exists:
                # Existing sidecar failed to load: keep error and block save to avoid overwrite.
                STATE["project_load_failed"] = True
                STATE["project_dirty"] = False
            else:
                _set_save_error("", "")
                STATE["project_dirty"] = True
            _invalidate_curve_cache()
        # Always start a chart/project with curve as the default shape for
        # newly created links, regardless of persisted sidecar preference.
        STATE["active_link_shape"] = "curve"
        STATE["history"] = []
        STATE["history_index"] = -1
        _push_history()


def _try_seed_curve_project_from_source(context, target_curve_path):
    if not isinstance(context, dict):
        return False
    if not isinstance(target_curve_path, str) or not target_curve_path.strip():
        return False
    if os.path.exists(target_curve_path):
        return True

    source_chart = str(context.get("chart_path_source", "") or "").strip()
    if not source_chart:
        return False
    if not os.path.exists(source_chart):
        return False

    source_stem = os.path.splitext(os.path.basename(source_chart))[0]
    source_curve = os.path.join(os.path.dirname(
        source_chart), ".mcce-plugin", source_stem + ".curve_tbd.json")
    if not os.path.exists(source_curve):
        return False

    try:
        os.makedirs(os.path.dirname(target_curve_path), exist_ok=True)
        shutil.copy2(source_curve, target_curve_path)
        return True
    except Exception:
        return False


def _sync_project_sidecar_to_chart(chart_path):
    if bool(STATE.get("project_load_failed", False)):
        return False
    source_sidecar = str(STATE.get("project_path", "") or "").strip()
    if not source_sidecar or not os.path.exists(source_sidecar):
        return False
    chart_path = str(chart_path or "").strip()
    if not chart_path:
        return False
    target_dir = os.path.join(os.path.dirname(chart_path), ".mcce-plugin")
    target_name = os.path.splitext(os.path.basename(chart_path))[
        0] + ".curve_tbd.json"
    target_sidecar = os.path.join(target_dir, target_name)
    if os.path.normcase(os.path.abspath(source_sidecar)) == os.path.normcase(os.path.abspath(target_sidecar)):
        return True
    try:
        os.makedirs(target_dir, exist_ok=True)
        shutil.copy2(source_sidecar, target_sidecar)
        return True
    except Exception:
        return False


def _mark_dirty(context, flush=False):
    STATE["project_dirty"] = True
    if not flush:
        return
    if bool(STATE.get("suppress_persist_once", False)):
        return
    if isinstance(context, dict):
        _ensure_project_context(context)
    if STATE["project_path"]:
        _save_project(STATE["project_path"], context)


def _build_overlay(context):
    # listCanvasOverlays 请求来自 C++ 侧 ExternalProcessPlugin::canvasOverlays，
    # 该方法忽略了 context 参数并始终向 Python 发送空的 QJsonObject。
    # 回退到 handleCanvasInput 保存的最后完整上下文，以确保
    # chart_to_canvas 能获取正确的 scroll_beat / visible_beat_range / margin 等参数。
    if not isinstance(context, dict) or not context:
        context = STATE.get("last_context", {})
    callbacks = {
        "normalize_link": _normalize_link,
        "connected_anchor_segments": _connected_anchor_segments,
        "sample_segment_chart": _sample_segment_chart,
        "note_curve_snap_enabled": _note_curve_snap_enabled,
        "anchor_in_abs_chart": _anchor_in_abs_chart,
        "anchor_out_abs_chart": _anchor_out_abs_chart,
        "tr": tr,
        "rect_normalized": _rect_normalized,
        "anchor_index_map": _anchor_index_map,
        "chart_to_canvas": _chart_to_canvas,
        "state": STATE,
    }
    return overlay_ui.build_overlay(context, callbacks)


def _reset_anchors(context):
    STATE["anchors"] = _default_anchors()
    STATE["links"] = []
    STATE["segment_denominators"] = {}
    STATE["segment_shapes"] = {}
    STATE["anchor_group_ids"] = {}
    STATE["anchor_reserved"] = {}
    STATE["anchor_compat_handles"] = {}
    STATE["curve_id_by_link"] = {}
    STATE["curve_no_by_link"] = {}
    STATE["curve_group_ids_by_link"] = {}
    STATE["curve_density_mode_by_link"] = {}
    STATE["curve_reserved_by_link"] = {}
    STATE["curve_special_joystick_by_link"] = {}
    STATE["node_groups"] = [_default_group_entry(
        DEFAULT_NODE_GROUP_ID, "base")]
    STATE["curve_groups"] = [_default_group_entry(
        DEFAULT_CURVE_GROUP_ID, "base")]
    STATE["next_curve_id"] = 1
    STATE["next_group_id"] = 2
    STATE["drag"] = {"mode": "", "index": -1}
    STATE["selected_anchor_ids"] = []
    STATE["selected_links"] = []
    STATE["context_menu_links"] = []
    STATE["link_drag"] = {"active": False, "source_anchor_id": -
                          1, "hover_anchor_id": -1, "x": 0.0, "y": 0.0}
    STATE["pending_connect_anchor_id"] = -1
    _invalidate_curve_cache()
    _record_history_state(context)


def _append_anchor(context, cx, cy):
    lane_x, beat = _canvas_to_chart(context, cx, cy)
    lane_x, beat = _snap_chart_point(context, lane_x, beat)
    anchors = STATE["anchors"]

    if not anchors:
        anchors.append({"id": _new_anchor_id(), "lane_x": lane_x, "beat": beat,
                       "in": [-16.0, 0.0], "out": [16.0, 0.0], "smooth": True})
        return len(anchors) - 1

    idx = len(anchors)
    for i, a in enumerate(anchors):
        if beat < float(a.get("beat", 0.0)):
            idx = i
            break

    ref = None
    if idx > 0 and idx < len(anchors):
        prev = anchors[idx - 1]
        nxt = anchors[idx]
        prev_dist = abs(beat - float(prev.get("beat", beat)))
        next_dist = abs(float(nxt.get("beat", beat)) - beat)
        ref = prev if prev_dist <= next_dist else nxt
    elif idx > 0:
        ref = anchors[idx - 1]
    elif idx < len(anchors):
        ref = anchors[idx]

    if ref is None:
        ol, ob = 16.0, 0.0
    else:
        if beat <= float(ref.get("beat", beat)):
            dl = float(ref.get("lane_x", lane_x)) - lane_x
            db = float(ref.get("beat", beat)) - beat
        else:
            dl = lane_x - float(ref.get("lane_x", lane_x))
            db = beat - float(ref.get("beat", beat))
        # Split axis scaling to avoid overly weak handles on lane axis.
        ol = _clamp(dl * 0.25, -96.0, 96.0)
        ob = _clamp(db * 0.25, -2.0, 2.0)

    anchors.insert(idx, {"id": _new_anchor_id(), "lane_x": lane_x,
                   "beat": beat, "in": [-ol, -ob], "out": [ol, ob], "smooth": True})
    _enforce_anchor_and_connected_handle_constraints(idx, context)
    _cleanup_links_and_selection()
    _invalidate_curve_cache()
    return idx


def _float_beat_to_triplet(beat, den):
    return bc.float_beat_to_triplet(beat, den)


def _chart_to_note(context, lane_x, beat, den):
    return bc.chart_to_note(context, lane_x, beat, den, clamp_fn=_clamp)


def _beat_fraction_from_triplet(triplet):
    return tm.beat_fraction_from_triplet(triplet)


def _normal_note_position_key(note_obj):
    return bc.normal_note_position_key(note_obj, beat_fraction_from_triplet_fn=_beat_fraction_from_triplet)


def _existing_normal_note_position_keys(context):
    return bc.existing_normal_note_position_keys(
        context,
        normal_note_position_key_fn=_normal_note_position_key,
        os_module=os,
        json_module=json,
    )


def _sample_single_segment_chart(a0, a1, samples_per_segment=24, id0=None, id1=None):
    return _sample_segment_chart(a0, a1, samples_per_segment, id0, id1)


def _connected_curve_segment_groups():
    segments = _connected_anchor_segments()
    if not segments:
        return []

    by_anchor = {}
    for seg in segments:
        id0 = int(seg[2])
        id1 = int(seg[3])
        by_anchor.setdefault(id0, []).append(seg)
        by_anchor.setdefault(id1, []).append(seg)

    visited_anchor_ids = set()
    grouped = []
    for anchor_id in by_anchor.keys():
        if anchor_id in visited_anchor_ids:
            continue
        queue = [anchor_id]
        comp_anchor_ids = set()
        comp_segments = []
        seen_seg_keys = set()
        while queue:
            cur = int(queue.pop())
            if cur in comp_anchor_ids:
                continue
            comp_anchor_ids.add(cur)
            for seg in by_anchor.get(cur, []):
                id0 = int(seg[2])
                id1 = int(seg[3])
                key = (id0, id1)
                if key not in seen_seg_keys:
                    seen_seg_keys.add(key)
                    comp_segments.append(seg)
                if id0 not in comp_anchor_ids:
                    queue.append(id0)
                if id1 not in comp_anchor_ids:
                    queue.append(id1)
        visited_anchor_ids.update(comp_anchor_ids)
        comp_segments.sort(key=lambda s: (min(float(s[4]["beat"]), float(
            s[5]["beat"])), max(float(s[4]["beat"]), float(s[5]["beat"]))))
        grouped.append(comp_segments)
    return grouped


def _build_batch_from_curve(context, target_links=None):
    return bc.build_batch_from_curve(
        context,
        target_links,
        {
            "connected_anchor_segments": _connected_anchor_segments,
            "normalize_link": _normalize_link,
            "segment_denominator_for_link": _segment_denominator_for_link,
            "normalize_samples_by_beat": _normalize_samples_by_beat,
            "lane_x_at_beat": _lane_x_at_beat,
            "sample_segment_chart": _sample_single_segment_chart,
            "existing_normal_note_position_keys": _existing_normal_note_position_keys,
            "normal_note_position_key": _normal_note_position_key,
            "chart_to_note": _chart_to_note,
        },
    )


def _sync_anchor_placement_with_host_mode(context):
    input_ui.sync_anchor_placement_with_host_mode(context, STATE)


def _host_controls_anchor_mode(context):
    return input_ui.host_controls_anchor_mode(context)


def _selected_target_links():
    selected_links = []
    for raw in STATE.get("selected_links", []):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        norm = _normalize_link(raw[0], raw[1])
        if norm is None:
            continue
        selected_links.append(norm)

    if selected_links:
        return selected_links

    selected_anchor_ids = [int(v) for v in STATE.get(
        "selected_anchor_ids", []) if int(v) > 0]
    if len(selected_anchor_ids) == 2:
        norm = _normalize_link(selected_anchor_ids[0], selected_anchor_ids[1])
        if norm is not None and _link_exists(norm[0], norm[1]):
            return [norm]
    return []


def _selected_links_all_polyline():
    links = _selected_target_links()
    if not links:
        return _active_link_shape_is_polyline()
    return all(_segment_shape_for_link(id0, id1) == "polyline" for id0, id1 in links)


def _toggle_polyline_for_active_or_selected(context):
    links = _selected_target_links()
    # With selected targets, only mutate those segments and keep the default
    # shape for future links unchanged.
    if links:
        target = "polyline"
        if all(_segment_shape_for_link(id0, id1) == "polyline" for id0, id1 in links):
            target = "curve"
        changed = False
        for id0, id1 in links:
            changed = _set_segment_shape(id0, id1, target) or changed
        if changed:
            _invalidate_curve_cache()
            _mark_dirty(context, flush=False)
        return True

    # No selection: this is an explicit default-shape toggle for new links.
    prev_active = _active_link_shape()
    target = "curve" if _active_link_shape_is_polyline() else "polyline"
    STATE["active_link_shape"] = target
    if prev_active != target:
        _mark_dirty(context, flush=False)
    return True


def _set_context_menu_links_for_hit(context, x, y):
    hit = _find_segment_hit(context, x, y)
    selected = _selected_target_links()
    selected_set = set(selected)

    target = []
    if hit is not None:
        norm = _normalize_link(hit[0], hit[1])
        if norm is not None:
            if norm in selected_set and len(selected) > 1:
                target = selected
            else:
                target = [norm]
    elif selected:
        target = selected

    STATE["context_menu_links"] = [[int(a), int(b)] for a, b in target]
    return STATE["context_menu_links"]


def _context_menu_target_links():
    target = []
    for raw in STATE.get("context_menu_links", []):
        if not isinstance(raw, list) or len(raw) != 2:
            continue
        norm = _normalize_link(raw[0], raw[1])
        if norm is not None and _link_exists(norm[0], norm[1]):
            target.append(norm)
    if target:
        return target
    return _selected_target_links()


def _context_links_all_polyline():
    links = _context_menu_target_links()
    return bool(links) and all(_segment_shape_for_link(id0, id1) == "polyline" for id0, id1 in links)


def _toggle_polyline_for_context_links(context):
    links = _context_menu_target_links()
    if not links:
        return False

    target = "curve" if all(_segment_shape_for_link(
        id0, id1) == "polyline" for id0, id1 in links) else "polyline"
    changed = False
    for id0, id1 in links:
        changed = _set_segment_shape(id0, id1, target) or changed
    if changed:
        _invalidate_curve_cache()
        _mark_dirty(context, flush=False)
    return True


def _set_density_for_selected_segments(context, target_den):
    links = _context_menu_target_links()
    if not links:
        return False
    den = int(target_den)
    if den <= 0:
        den = _context_default_segment_denominator(context)
    changed = False
    for id0, id1 in links:
        changed = _set_segment_denominator(id0, id1, den) or changed
    if changed:
        _mark_dirty(context, flush=False)
    return changed


def _sync_anchor_selection_from_host_notes(context):
    callbacks = {
        "selection_enabled": _selection_enabled,
        "anchor_index_map": _anchor_index_map,
        "state": STATE,
    }
    input_ui.sync_anchor_selection_from_host_notes(context, callbacks)


def _handle_canvas_input(payload):
    return input_ui.handle_canvas_input(
        payload,
        {
            "state": STATE,
            "tr": tr,
            "checkpoint_prefix": CURVE_CHECKPOINT_PREFIX,
            "right_button": RIGHT_BUTTON,
            "left_button": LEFT_BUTTON,
            "ctrl_modifier_mask": CTRL_MODIFIER_MASK,
            "key_a": KEY_A,
            "key_delete": KEY_DELETE,
            "key_backspace": KEY_BACKSPACE,
            "key_shift": 0x01000020,
            "key_escape": 0x01000000,
            "now_ms": lambda: int(time.time() * 1000),
            "build_overlay": _build_overlay,
            "note_curve_snap_enabled": _note_curve_snap_enabled,
            "ensure_project_context": _ensure_project_context,
            "seed_missing_segment_denominators": _seed_missing_segment_denominators,
            "sync_anchor_placement_with_host_mode": _sync_anchor_placement_with_host_mode,
            "sync_anchor_selection_from_host_notes": _sync_anchor_selection_from_host_notes,
            "event_has_shift": _event_has_shift,
            "selection_enabled": _selection_enabled,
            "host_controls_anchor_mode": _host_controls_anchor_mode,
            "find_handle_hit": _find_handle_hit,
            "find_anchor_hit": _find_anchor_hit,
            "find_segment_hit": _find_segment_hit,
            "set_context_menu_links_for_hit": _set_context_menu_links_for_hit,
            "toggle_selected_anchor": _toggle_selected_anchor,
            "add_selected_anchor": _add_selected_anchor,
            "toggle_selected_link": _toggle_selected_link,
            "set_single_selected_link": _set_single_selected_link,
            "append_anchor": _append_anchor,
            "add_link": _add_link,
            "set_single_selected_anchor": _set_single_selected_anchor,
            "cleanup_links_and_selection": _cleanup_links_and_selection,
            "invalidate_curve_cache": _invalidate_curve_cache,
            "record_history_state": _record_history_state,
            "mark_dirty": _mark_dirty,
            "disconnect_selected_segments": _disconnect_selected_segments,
            "delete_selected_anchors": _delete_selected_anchors,
            "anchor_index_map": _anchor_index_map,
            "canvas_to_chart": _canvas_to_chart,
            "snap_chart_point": _snap_chart_point,
            "enforce_anchor_time_order": _enforce_anchor_time_order,
            "enforce_handle_time_constraints": _enforce_handle_time_constraints,
            "enforce_anchor_and_connected_handle_constraints": _enforce_anchor_and_connected_handle_constraints,
            "set_anchor_in_abs_chart": _set_anchor_in_abs_chart,
            "set_anchor_out_abs_chart": _set_anchor_out_abs_chart,
            "apply_box_selection": _apply_box_selection,
        },
    )


def _list_tool_actions():
    return ta.list_tool_actions(
        {
            "state": STATE,
            "tr": tr,
            "active_link_shape_is_polyline": _active_link_shape_is_polyline,
            "note_curve_snap_enabled": _note_curve_snap_enabled,
            "context_links_all_polyline": _context_links_all_polyline,
            "context_density_menu_state": _context_density_menu_state,
        }
    )


def _run_tool_action(payload):
    return ta.run_tool_action(
        payload,
        {
            "state": STATE,
            "ensure_project_context": _ensure_project_context,
            "reset_anchors": _reset_anchors,
            "save_project": _save_project,
            "mark_dirty": _mark_dirty,
            "toggle_polyline_for_active_or_selected": _toggle_polyline_for_active_or_selected,
            "toggle_polyline_for_context_links": _toggle_polyline_for_context_links,
            "note_curve_snap_enabled": _note_curve_snap_enabled,
            "set_density_for_selected_segments": _set_density_for_selected_segments,
            "connect_selected_anchors": _connect_selected_anchors,
            "disconnect_selected_segments": _disconnect_selected_segments,
            "delete_selected_anchors": _delete_selected_anchors,
            "save_style": _save_style,
            "load_style": _load_style,
            "register_host_undo_action": _register_host_undo_action,
        },
    )


def _workspace_config(_payload):
    return ws_runtime.workspace_config(_payload)


def _build_batch_edit(payload):
    return bc.build_batch_edit(
        payload,
        {
            "ensure_project_context": _ensure_project_context,
            "seed_missing_segment_denominators": _seed_missing_segment_denominators,
            "build_batch_from_curve": _build_batch_from_curve,
            "context_menu_target_links": _context_menu_target_links,
        },
    )


def run_one_shot(action_id):
    return ta.run_one_shot(action_id)


def run_plugin_loop():
    run_protocol_loop({
        "state": STATE,
        "normalize_lang": normalize_lang,
        "is_curve_checkpoint_action": _is_curve_checkpoint_action,
        "is_registered_host_undo_action": _is_registered_host_undo_action,
        "undo_history_from_host": _undo_history_from_host,
        "redo_history_from_host": _redo_history_from_host,
        "list_tool_actions": _list_tool_actions,
        "run_tool_action": _run_tool_action,
        "build_overlay": _build_overlay,
        "handle_canvas_input": _handle_canvas_input,
        "workspace_config": _workspace_config,
        "build_batch_edit": _build_batch_edit,
        "respond": _respond,
        "save_project": _save_project,
        "sync_sidecar_to_chart": _sync_project_sidecar_to_chart,
    })


def main():
    _configure_stdio_utf8()
    args = sys.argv[1:]
    if "--run-tool-action" in args:
        i = args.index("--run-tool-action")
        action_id = args[i + 1] if i + 1 < len(args) else ""
        raise SystemExit(run_one_shot(action_id))

    if "--plugin" in args:
        run_plugin_loop()
        return

    print("Use --plugin for protocol mode", file=sys.stderr)


if __name__ == "__main__":
    main()
