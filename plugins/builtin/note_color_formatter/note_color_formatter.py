import json
import locale
import math
import os
import sys
import tempfile
import zipfile

TRANSLATIONS = {
    "processed": {
        "zh": "处理成功: {file}",
        "en": "Processed: {file}",
        "ja": "処理成功: {file}",
    },
    "failed": {
        "zh": "处理失败: {file} | 错误: {error}",
        "en": "Failed: {file} | Error: {error}",
        "ja": "処理失敗: {file} | エラー: {error}",
    },
    "action_title": {
        "zh": "格式化音符颜色",
        "en": "Format Note Colors",
        "ja": "ノート色を整形",
    },
    "action_desc": {
        "zh": "按支持分母规范化颜色节奏序列。",
        "en": "Normalize timing divisions to supported color sequence set.",
        "ja": "対応分母セットに合わせて色リズム分割を正規化します。",
    },
    "action_confirm": {
        "zh": "将对当前谱面的音符颜色节奏进行格式化处理，是否继续？",
        "en": "This will format note color timing divisions in current chart. Continue?",
        "ja": "現在の譜面のノート色リズム分割を整形します。続行しますか？",
    },
}


def normalize_lang(value, default="zh"):
    if not isinstance(value, str) or not value.strip():
        return default
    lower = value.strip().lower()
    if lower.startswith("zh"):
        return "zh"
    if lower.startswith("en"):
        return "en"
    if lower.startswith("ja"):
        return "ja"
    return default


def detect_lang(default="zh"):
    sys_locale = locale.getlocale()[0]
    if sys_locale and sys_locale.lower().startswith("zh"):
        return "zh"
    if sys_locale and sys_locale.lower().startswith("en"):
        return "en"
    if sys_locale and sys_locale.lower().startswith("ja"):
        return "ja"
    return default


def tr(lang, key, **kwargs):
    text = TRANSLATIONS.get(key, {}).get(
        lang, TRANSLATIONS.get(key, {}).get("en", key))
    return text.format(**kwargs)


def adjust_denominator(numerator, denominator):
    gcd_val = math.gcd(numerator, denominator)
    return numerator // gcd_val, denominator // gcd_val


def process_beat(beat):
    if not isinstance(beat, list) or len(beat) != 3:
        return beat
    measure, num, den = beat
    if not isinstance(num, int) or not isinstance(den, int) or den == 0:
        return beat
    new_num, new_den = adjust_denominator(num, den)
    return [measure, new_num, new_den]


def simplify_mc_beats(mc_path):
    with open(mc_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    changed = False
    for note in data.get("note", []):
        beat = note.get("beat")
        if beat is None:
            continue
        new_beat = process_beat(beat)
        if new_beat != beat:
            note["beat"] = new_beat
            changed = True

    if changed:
        with open(mc_path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
    return True


def simplify_beats_path(path):
    if path.lower().endswith(".mc"):
        simplify_mc_beats(path)
        return True
    if path.lower().endswith(".mcz"):
        with tempfile.TemporaryDirectory() as tmpdir:
            with zipfile.ZipFile(path, "r") as zip_ref:
                zip_ref.extractall(tmpdir)
            for root, _, files in os.walk(tmpdir):
                for file_name in files:
                    if file_name.lower().endswith(".mc"):
                        simplify_mc_beats(os.path.join(root, file_name))
            with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zip_write:
                for folder, _, files in os.walk(tmpdir):
                    for file_name in files:
                        full_path = os.path.join(folder, file_name)
                        arcname = os.path.relpath(full_path, tmpdir)
                        zip_write.write(full_path, arcname)
        return True
    return False


def resolve_chart_path(context):
    if not isinstance(context, dict):
        return ""
    candidates = []
    for key in ("chart_path", "chart_path_native", "chart_path_canonical"):
        value = context.get(key)
        if isinstance(value, str) and value:
            candidates.append(value)

    for p in candidates:
        if os.path.exists(p):
            return p
    if candidates:
        return candidates[0]
    return ""


def _send_response(req_id, result):
    payload = {"type": "response", "id": req_id, "result": result}
    line = json.dumps(payload, ensure_ascii=False) + "\n"
    if hasattr(sys.stdout, "buffer"):
        sys.stdout.buffer.write(line.encode("utf-8"))
        sys.stdout.buffer.flush()
    else:
        sys.stdout.write(line)
        sys.stdout.flush()


def _configure_stdio_utf8():
    for stream in (sys.stdin, sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def run_process_plugin():
    _configure_stdio_utf8()
    lang = normalize_lang(os.environ.get("MALODY_LOCALE", "zh_CN"), "zh")

    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue

        try:
            msg = json.loads(raw)
        except Exception:
            continue

        msg_type = msg.get("type")
        if msg_type == "notify":
            event = msg.get("event")
            payload = msg.get("payload", {}) or {}
            if event == "initialize":
                locale_name = str(payload.get("locale", "zh_CN"))
                lang = normalize_lang(locale_name, lang)
            elif event == "shutdown":
                break
            continue

        if msg_type != "request":
            continue

        req_id = msg.get("id", "")
        method = msg.get("method", "")
        payload = msg.get("payload", {}) or {}

        if method == "listToolActions":
            _send_response(
                req_id,
                [
                    {
                        "action_id": "format_note_colors",
                        "title": tr(lang, "action_title"),
                        "description": tr(lang, "action_desc"),
                        "confirm_message": tr(lang, "action_confirm"),
                        "placement": "top_toolbar",
                        "requires_undo_snapshot": True,
                    },
                    {
                        "action_id": "format_note_colors_sidebar",
                        "title": tr(lang, "action_title"),
                        "description": tr(lang, "action_desc"),
                        "confirm_message": tr(lang, "action_confirm"),
                        "placement": "left_sidebar",
                        "requires_undo_snapshot": True,
                    },
                ],
            )
            continue

        if method == "runToolAction":
            action_id = str(payload.get("action_id", ""))
            context = payload.get("context", {}) or {}
            chart_path = resolve_chart_path(context)
            if action_id not in ("format_note_colors", "format_note_colors_sidebar") or not chart_path:
                _send_response(req_id, False)
                continue
            try:
                _send_response(req_id, simplify_beats_path(chart_path))
            except Exception as exc:
                print(tr(lang, "failed", file=os.path.basename(
                    chart_path), error=str(exc)), file=sys.stderr)
                _send_response(req_id, False)
            continue

        if method in ("openAdvancedColorEditor", "normalizeCurrentChart"):
            chart_path = payload.get("chart_path")
            if isinstance(chart_path, str) and chart_path:
                _send_response(req_id, simplify_beats_path(chart_path))
            else:
                _send_response(req_id, False)
            continue

        _send_response(req_id, False)

    return 0


def run_standalone():
    _configure_stdio_utf8()
    lang = detect_lang("zh")
    cwd = os.getcwd()
    targets = [os.path.join(cwd, n) for n in os.listdir(
        cwd) if n.lower().endswith((".mc", ".mcz"))]
    if not targets:
        return 1
    for path in targets:
        try:
            ok = simplify_beats_path(path)
            if ok:
                print(tr(lang, "processed", file=os.path.basename(path)))
        except Exception as exc:
            print(tr(lang, "failed", file=os.path.basename(
                path), error=str(exc)), file=sys.stderr)
    return 0


if __name__ == "__main__":
    if "--plugin" in sys.argv:
        raise SystemExit(run_process_plugin())
    raise SystemExit(run_standalone())
