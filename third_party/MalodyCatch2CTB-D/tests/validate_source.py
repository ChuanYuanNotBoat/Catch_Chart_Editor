"""Source-tree and plugin-manifest validation; no D compiler required."""
import json
import pathlib

root = pathlib.Path(__file__).resolve().parents[1]
manifest_path = root / 'plugin/malodycatch2ctb-d.plugin.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
assert manifest['pluginApiVersion'] == 2
assert manifest['executable'].startswith('./')
assert 'tool_actions' in manifest['capabilities']
assert (root / 'source/app.d').is_file()
assert (root / 'dub.json').is_file()
assert json.loads((root / 'dub.json').read_text(encoding='utf-8'))['targetName'] == 'malodycatch2ctb-d'
assert 'convert_current_catch_to_osz' in (root / 'source/app.d').read_text(encoding='utf-8')
print('Source layout and CCE manifest: PASS')
