import json
import sys
import time
import traceback


def run_plugin_loop(callbacks):
    state = callbacks["state"]
    normalize_lang = callbacks["normalize_lang"]
    is_curve_checkpoint_action = callbacks["is_curve_checkpoint_action"]
    is_registered_host_undo_action = callbacks["is_registered_host_undo_action"]
    undo_history_from_host = callbacks["undo_history_from_host"]
    redo_history_from_host = callbacks["redo_history_from_host"]
    list_tool_actions = callbacks["list_tool_actions"]
    run_tool_action = callbacks["run_tool_action"]
    build_overlay = callbacks["build_overlay"]
    handle_canvas_input = callbacks["handle_canvas_input"]
    workspace_config = callbacks["workspace_config"]
    build_batch_edit = callbacks["build_batch_edit"]
    respond = callbacks["respond"]
    save_project = callbacks["save_project"]
    sync_sidecar_to_chart = callbacks["sync_sidecar_to_chart"]

    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        try:
            msg = json.loads(raw)
        except Exception:
            continue

        mtype = msg.get("type")
        if mtype == "notify":
            try:
                event = msg.get("event")
                payload = msg.get("payload", {}) or {}
                if event == "initialize":
                    locale_name = str(payload.get("locale", "") or "")
                    language_name = str(payload.get("language", "") or "")
                    if locale_name.strip():
                        state["lang"] = normalize_lang(locale_name, state.get("lang", "en"))
                    elif language_name.strip():
                        state["lang"] = normalize_lang(language_name, state.get("lang", "en"))
                elif event == "onHostDiscardChanges":
                    state["suppress_persist_once"] = True
                    state["project_dirty"] = False
                elif event == "onChartSaved":
                    state["suppress_persist_once"] = False
                    if state["project_path"] and state["project_dirty"]:
                        save_project(state["project_path"], state.get("last_context", {}))
                    chart_path = str(payload.get("chart_path", "") or "").strip()
                    if chart_path:
                        sync_sidecar_to_chart(chart_path)
                elif event == "shutdown":
                    if (
                        (not bool(state.get("suppress_persist_once", False)))
                        and state["project_path"]
                        and state["project_dirty"]
                    ):
                        save_project(state["project_path"], state.get("last_context", {}))
                    break
                elif event == "onHostUndo":
                    action_text = str(payload.get("action_text", "") or "")
                    if is_curve_checkpoint_action(action_text) or is_registered_host_undo_action(action_text):
                        undo_history_from_host(state.get("last_context", {}))
                elif event == "onHostRedo":
                    action_text = str(payload.get("action_text", "") or "")
                    if is_curve_checkpoint_action(action_text) or is_registered_host_undo_action(action_text):
                        redo_history_from_host(state.get("last_context", {}))
            except Exception:
                traceback.print_exc(file=sys.stderr)
            continue

        if mtype != "request":
            continue

        req_id = msg.get("id", str(int(time.time() * 1000)))
        method = msg.get("method", "")
        payload = msg.get("payload", {}) or {}

        try:
            if method == "listToolActions":
                respond(req_id, list_tool_actions())
            elif method == "runToolAction":
                respond(req_id, bool(run_tool_action(payload)))
            elif method == "listCanvasOverlays":
                respond(req_id, build_overlay(payload if isinstance(payload, dict) else {}))
            elif method == "handleCanvasInput":
                respond(req_id, handle_canvas_input(payload))
            elif method == "getPanelWorkspaceConfig":
                respond(req_id, workspace_config(payload))
            elif method == "buildBatchEdit":
                respond(req_id, build_batch_edit(payload))
            else:
                respond(req_id, False)
        except Exception:
            traceback.print_exc(file=sys.stderr)
            respond(req_id, False)
