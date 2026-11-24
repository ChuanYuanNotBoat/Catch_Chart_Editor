"""Summarize NoteSoundTimingProbe JSON; no external packages or chart writes."""
import argparse
import json
import statistics
import bisect
from pathlib import Path


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    index = (len(values) - 1) * .95
    low = int(index)
    p95 = values[low] + (values[min(low + 1, len(values) - 1)] - values[low]) * (index - low)
    return {"count": len(values), "min_ms": round(values[0], 3),
            "median_ms": round(statistics.median(values), 3),
            "p95_ms": round(p95, 3),
            "max_ms": round(values[-1], 3)}


def pulse_timing(data):
    if not data.get("stereo_pulse_detection"):
        return None
    music = data["music_onsets_wall_ms"]
    hits = data["hit_onsets_wall_ms"]
    result = {}
    if not data.get("transport"):
        period_ms = data["synthetic_interval_ms"] / data["speed"]
        for name, onsets in (("music", music), ("hit", hits)):
            if len(onsets) > 1:
                errors = [b - a - period_ms for a, b in zip(onsets, onsets[1:])]
                result[f"{name}_abs_interval_error_wall_ms"] = distribution(map(abs, errors))
                result[f"{name}_end_minus_start_error_wall_ms"] = round(sum(errors), 3)
    # Pair by order only when both channels contain the same number of pulses.
    # Verify the recording is isolated before interpreting the phase results.
    if music and len(music) == len(hits):
        phases = [hit - marker for marker, hit in zip(music, hits)]
        result["phase_hit_minus_music_wall_ms"] = distribution(phases)
        result["first_phase_wall_ms"] = round(phases[0], 3)
        result["last_phase_wall_ms"] = round(phases[-1], 3)
        if len(phases) >= 41:
            result["last20_minus_first20_steady_phase_wall_ms"] = round(
                statistics.median(phases[-20:]) - statistics.median(phases[1:21]), 3)
    return result


def summarize(path):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    rate = data["speed"]
    calls = data["play_calls"]
    notes = data.get("note_times_ms")
    if notes is None and not data.get("chart") and path.name.startswith("notesound_ab_"):
        # Original A/B captures used this known synthetic sequence.
        notes = [137 + 437 * i for i in range(len(calls))]
    submission = None
    if notes and len(calls) <= len(notes):
        if data.get("submission_policy") == "latest_due_per_wakeup":
            assigned = [notes[max(0, bisect.bisect_right(notes, call["predicted_ms"] + 1e-6) - 1)]
                        for call in calls]
        else:
            assigned = notes[:len(calls)]
        submission = distribution([(call["predicted_ms"] - note) / rate
                                   for call, note in zip(calls, assigned)])
    observations = [item for item in data["observations"]
                    if item["raw_ms"] > data.get("start_ms", 0)]
    intervals = [b["wall_ms"] - a["wall_ms"]
                 for a, b in zip(data["ticks"], data["ticks"][1:])]
    buffers = data.get("decoded_buffers", [])
    return {"path": str(path), "speed": rate, "device": data.get("device"),
            "first_decoded_start_ms": buffers[0]["start_ms"] if buffers else None,
            "first_progress_wall_ms": observations[0]["wall_ms"] if observations else None,
            "trigger_count": len(calls), "frame_intervals": distribution(intervals),
            "submission_lateness_wall_ms": submission,
            "observed_minus_prediction_wall_ms": distribution([
                (v["raw_ms"] - v["predicted_ms"]) / rate for v in observations]),
            "loopback_hit_count": len(data["hit_onsets_wall_ms"]),
            "loopback_music_count": len(data["music_onsets_wall_ms"]),
            "loopback_discontinuities": data.get("loopback_discontinuities"),
            "loopback_pulse_timing": pulse_timing(data)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path, nargs="+")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps([summarize(path) for path in args.captures], ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(result + "\n", encoding="utf-8")
    else:
        print(result)
