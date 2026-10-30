"""Smoke tests for compiled D converter. Usage: python tests/smoke.py path/to/exe"""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
import zipfile

BINARY = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv = sys.argv[:1]


def chart(title="Example", offset=125, rain=True):
    notes = [
        {"type": 1, "beat": [0, 0, 1], "sound": "track.ogg", "offset": 125},
        {"type": 0, "beat": [1, 0, 1], "x": 100},
        {"type": 0, "beat": [1, 1, 2], "x": 220},
    ]
    if rain:
        notes.append({"type": 3, "beat": [2, 0, 1], "endbeat": [4, 0, 1]})
    return {
        "meta": {
            "mode": 3, "song": {"title": title, "artist": "Fixture"},
            "creator": "Test", "version": "Hard", "offset": offset,
            "mode_ext": {"speed": 6}
        },
        "time": [{"beat": [0, 0, 1], "bpm": 120},
                 {"beat": [2, 0, 1], "bpm": 150}],
        "note": notes,
    }


@unittest.skipUnless(BINARY is not None and BINARY.exists(), "Supply built D executable path")
class ConverterSmoke(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.work = pathlib.Path(self.tmp.name)

    def call(self, *args, input_data=None):
        return subprocess.run([str(BINARY), *map(str, args)], input=input_data,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, encoding="utf-8", timeout=10)

    def test_mcz_multi_bpm_and_rain(self):
        src = self.work / "input.mcz"
        with zipfile.ZipFile(src, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("0/a.mc", json.dumps(chart()))
            z.writestr("0/track.ogg", b"demo-audio")
            z.writestr("0/unused.png", b"demo-image")
            z.writestr(".mcce-plugin/internal.png", b"should-not-copy")
        result = self.call(src)
        self.assertEqual(result.returncode, 0, result.stderr)
        dest = pathlib.Path(result.stdout.strip())
        with zipfile.ZipFile(dest) as z:
            self.assertIn("track.ogg", z.namelist())
            self.assertNotIn("internal.png", z.namelist())
            osu = z.read(next(n for n in z.namelist() if n.endswith(".osu"))).decode()
            self.assertIn("Mode: 2", osu)
            self.assertIn("ApproachRate:5.5", osu)
            self.assertIn("-125,500.000000,4,2,0,100,1,0", osu)
            self.assertIn("875,400.000000,4,2,0,100,1,0", osu)
            self.assertIn("100,0,375,5,0", osu)
            self.assertIn("220,0,625,1,0", osu)
            self.assertIn("256,0,875,12,0,1675", osu)

    def test_mc_direct(self):
        src = self.work / "single.mc"
        src.write_text(json.dumps(chart(rain=False)), encoding="utf-8")
        (self.work / "track.ogg").write_bytes(b"fake")
        result = self.call(src)
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(result.stdout.strip()) as z:
            self.assertTrue(any(n.endswith(".osu") for n in z.namelist()))

    def test_unicode_bom_and_no_overwrite(self):
        src = self.work / "中文谱面.mc"
        source_bytes = ("\ufeff" + json.dumps(chart(title="中文标题"), ensure_ascii=False)).encode("utf-8")
        src.write_bytes(source_bytes)
        (self.work / "track.ogg").write_bytes(b"audio")
        first = self.call(src)
        second = self.call(src)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertNotEqual(first.stdout.strip(), second.stdout.strip())
        self.assertEqual(src.read_bytes(), source_bytes)
        with zipfile.ZipFile(first.stdout.strip()) as z:
            osu = z.read(next(n for n in z.namelist() if n.endswith(".osu"))).decode()
            self.assertIn("TitleUnicode:中文标题", osu)

    def test_multiple_difficulties_and_resource_collisions(self):
        src = self.work / "package.mcz"
        with zipfile.ZipFile(src, "w", zipfile.ZIP_DEFLATED) as z:
            for index in range(2):
                c = chart()
                c["meta"]["version"] = f"Difficulty {index}"
                c["meta"]["background"] = "bg.png"
                z.writestr(f"{index}/chart.mc", json.dumps(c))
                z.writestr(f"{index}/track.ogg", f"audio-{index}".encode())
                z.writestr(f"{index}/bg.png", f"image-{index}".encode())
        original = src.read_bytes()
        result = self.call(src)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(src.read_bytes(), original)
        with zipfile.ZipFile(result.stdout.strip()) as z:
            maps = [name for name in z.namelist() if name.endswith(".osu")]
            self.assertEqual(len(maps), 2)
            for name in maps:
                osu = z.read(name).decode()
                index = 0 if "Difficulty 0" in osu else 1
                audio_name = next(line.split(": ", 1)[1] for line in osu.splitlines()
                                  if line.startswith("AudioFilename: "))
                bg_name = next(line.split(",")[2] for line in osu.splitlines()
                               if line.startswith("0,0,"))
                self.assertEqual(z.read(audio_name), f"audio-{index}".encode())
                self.assertEqual(z.read(bg_name), f"image-{index}".encode())

    def test_missing_audio(self):
        src = self.work / "silent.mc"
        src.write_text(json.dumps(chart()), encoding="utf-8")
        result = self.call(src)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.work / "ctb-output").exists())

    def test_offset_falls_back_to_sound_note(self):
        c = chart(rain=False)
        del c["meta"]["offset"]
        src = self.work / "fallback.mc"
        src.write_text(json.dumps(c), encoding="utf-8")
        (self.work / "track.ogg").write_bytes(b"audio")
        result = self.call(src)
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(result.stdout.strip()) as z:
            osu = z.read(next(n for n in z.namelist() if n.endswith(".osu"))).decode()
            self.assertIn("100,0,375,5,0", osu)

    def test_dedupe_is_opt_in(self):
        c = chart(rain=False)
        c["note"].append({"type": 0, "beat": [1, 0, 1], "x": 300})
        src = self.work / "overlap.mc"
        src.write_text(json.dumps(c), encoding="utf-8")
        (self.work / "track.ogg").write_bytes(b"audio")
        for args, expected in [((), 3), (("--dedupe",), 2)]:
            result = self.call(src, *args)
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(result.stdout.strip()) as z:
                osu = z.read(next(n for n in z.namelist() if n.endswith(".osu"))).decode()
                hits = osu.split("[HitObjects]", 1)[1].strip().splitlines()
                self.assertEqual(len(hits), expected)

    def test_plugin_exports_working_copy_beside_source(self):
        session = self.work / "session"
        saved = self.work / "saved"
        session.mkdir()
        saved.mkdir()
        src = session / "working.mc"
        src.write_text(json.dumps(chart(title="Current")), encoding="utf-8")
        original = saved / "original.mc"
        original.write_text(json.dumps(chart(title="Old")), encoding="utf-8")
        (session / "track.ogg").write_bytes(b"audio")
        request = {"type": "request", "id": "export", "method": "runToolAction",
                   "payload": {"action_id": "convert_current_catch_to_osz", "context": {
                       "chart_path": str(src), "chart_path_source": str(original)}}}
        result = self.call("--plugin", input_data=json.dumps(request) + "\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)["result"], result.stderr)
        self.assertTrue((saved / "ctb-output" / "Current.osz").is_file())
        self.assertFalse((session / "ctb-output").exists())

    def test_plugin_failure_recovers(self):
        requests = [
            {"type": "request", "id": "missing", "method": "runToolAction", "payload": {
                "action_id": "convert_current_catch_to_osz", "context": {}}},
            {"type": "request", "id": "unknown", "method": "unknown", "payload": {}},
            {"type": "request", "id": "ready", "method": "listToolActions", "payload": {}},
        ]
        result = self.call("--plugin", input_data="\n".join(map(json.dumps, requests)) + "\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([r["id"] for r in responses], ["missing", "unknown", "ready"])
        self.assertFalse(responses[0]["result"])
        self.assertFalse(responses[1]["result"])
        self.assertEqual(responses[2]["result"][0]["action_id"], "convert_current_catch_to_osz")

    def test_reject_non_catch(self):
        src = self.work / "no.mc"
        c = chart()
        c["meta"]["mode"] = 0
        src.write_text(json.dumps(c), encoding="utf-8")
        self.assertNotEqual(self.call(src).returncode, 0)

    def test_process_plugin(self):
        src = self.work / "single.mc"
        src.write_text(json.dumps(chart(rain=False)), encoding="utf-8")
        (self.work / "track.ogg").write_bytes(b"fake")
        lines = [
            {"type": "notify", "event": "initialize", "payload": {"host_api_version": 3}},
            {"type": "request", "id": "a", "method": "listToolActions", "payload": {}},
            {"type": "request", "id": "b", "method": "runToolAction", "payload": {
                "action_id": "convert_current_catch_to_osz", "context": {"chart_path": str(src)}
            }}
        ]
        result = self.call("--plugin", input_data="\n".join(json.dumps(x) for x in lines) + "\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        responses = [json.loads(x) for x in result.stdout.strip().splitlines()]
        self.assertEqual(len(responses), 2)
        self.assertEqual(responses[0]["id"], "a")
        self.assertEqual(responses[0]["result"][0]["action_id"], "convert_current_catch_to_osz")
        self.assertEqual(responses[1], {"type": "response", "id": "b", "result": True})


if __name__ == "__main__":
    unittest.main()
