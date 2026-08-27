# Plugin System

This folder is the source-side plugin SDK and host integration code.

## Structure

- `PluginInterface.h`: host plugin interface contract
- `PluginManager.h/.cpp`: plugin lifecycle and dispatch
- `ExternalProcessPlugin.h/.cpp`: process-plugin adapter (multi-language)
- `docs/`: protocol and capability documentation

## Supported plugin types

- Native plugin (`.dll/.so/.dylib`)
- Process plugin (`*.plugin.json` + stdin/stdout JSON line protocol)

## UI extension points

Plugin tool actions can be mounted to:

- `tools_menu`
- `top_toolbar`
- `left_sidebar`

All discovered tool actions are also grouped by plugin in the host-rendered,
dockable `Plugin Tools` panel. Actions declaring `scope_selector: note_range`
receive a persistent selected/range/all scope GUI with `LongRangeSelector`
inputs. Native floating panels remain supported and join the same vertically
split tool column by default. Tool blocks detach into floating windows and
return as simultaneously visible split sections instead of switching tabs.

## References

- [Plugin docs index](docs/README.md)
- [Process plugin protocol](docs/PROCESS_PLUGIN_PROTOCOL.md)
- [Canvas interaction protocol](docs/CANVAS_INTERACTION_PROTOCOL.md)
- [Minimal process example](docs/PROCESS_PLUGIN_MINIMAL_EXAMPLE.md)
- [Advanced color editor capability](docs/ADVANCED_COLOR_EDITOR_PLUGIN.md)
- [Native plugin template](docs/PLUGIN_TEMPLATE.md)
- [Runtime samples](../../plugins/samples/README.md)

The former `builtin.note_chain_assist` process plugin is not an active SDK
example. It is skipped by the host because the authoritative implementation is
the native module under `src/editor/NoteChain`.
