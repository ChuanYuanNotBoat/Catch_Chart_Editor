# Note Color Formatter (Builtin)

This plugin formats note timing division values used by color grouping logic.

The toolbar action opens and focuses the host's dockable Plugin Tools panel. The
panel provides a persistent scope selector, beat-range editor, selection count,
and an explicit Format button. It can format selected Normal/Rain notes, notes
whose start beat lies in an explicit beat range, or the entire chart. Sound notes
are not changed. The host reloads the result as one undoable action.

The panel is generated from the plugin's contextual tool-action metadata, so it
can be docked, tabbed, detached, closed, and restored like built-in tool blocks.
The Plugins menu action keeps the one-shot scope dialog as a compact fallback.
