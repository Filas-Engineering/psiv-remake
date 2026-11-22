#!/usr/bin/env python3
"""Writes the two asset directories scripts/stacked_screens.txt and scripts/small_screens.txt play.

STACK, a copy of MENU (make_menu_assets.py's set): an Extras menu that also lists the codex, which the field menu's
entries leave out; title entries with `when` conditions (Continue only with a save, Load only with two or more); and
alt/controls_no_extras.json, a Controls layout whose `list_extras` is 0.

SMALL, a copy of SMALL_SET (make_layout_assets.py --small, 320x224): without its pause file, so the built-in pause lays
itself out by the styles' sizes; a name entry whose pad takes its cell and text sizes from `pad_*` params; and
alt/styles_badge.json, its styles with the developer badge at the bottom left (`dev_badge_x`, `dev_badge_y`), the tools
at half size with the bitmap font shrunk too (`dev_scale_bitmap`), and music cut without a fade (`music_fade_ms` 0).

Usage: make_screens_assets.py EXPORT MENU SMALL_SET STACK SMALL
"""

import json
import shutil
import sys
from pathlib import Path


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def ui(out, export, name):
	path = out / "ui" / f"{name}.json"
	return load(path if path.exists() else export / "ui" / f"{name}.json")


def stack(export, menu, out):
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(menu, out)
	extras = ui(out, export, "extras")
	extras["entries"].append({"page": "codex"})
	save(out / "ui" / "extras.json", extras)
	title = ui(out, export, "title")
	title["options"] = [{"id": "continue", "when": "has_save"}, "new_game", {"id": "load", "when": "save_count > 1"}, "options", "quit"]
	save(out / "ui" / "title.json", title)
	controls = ui(out, export, "controls")
	controls["params"] = dict(controls.get("params", {}), list_extras=0)
	save(out / "alt" / "controls_no_extras.json", controls)


def small(small_set, out):
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(small_set, out)
	(out / "ui" / "pause.json").unlink()
	save(out / "ui" / "name_entry.json", {"version": 1, "params": {"backdrop_dim": 0.4, "pad_box_h": 22, "pad_gap": 4, "pad_cell_w": 22, "pad_cell_h": 17,
		"pad_entry_size": 16, "pad_label_size": 16, "pad_text_x": 4, "pad_text_y": 3, "pad_label_y": 1}, "windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "box", "x": 4, "y": 4, "w": "W - 8", "h": "H - 8", "frame": "panel", "padding": [6, 6, 6, 6], "elements": [
			{"type": "image", "image": "portrait", "w": 24, "h": 24},
			{"type": "text", "key": "ui.name_entry.prompt", "args": ["member_name"], "x": 30, "y": 4, "color": "accent", "size": "size_small", "scaled": False},
			{"type": "slot", "slot": "entry", "x": 0, "y": 28, "w": "cw", "h": "entry_h"},
			{"type": "hints", "value": "hint", "y": "ch - 10", "w": "cw"},
		]},
	]})
	styles = load(out / "ui" / "styles.json")
	styles["params"] = dict(styles.get("params", {}), dev_badge_x=4, dev_badge_y=420, dev_scale=50, dev_scale_bitmap=1)
	styles["timings"] = dict(styles.get("timings", {}), music_fade_ms=0)
	save(out / "alt" / "styles_badge.json", styles)


def main():
	export, menu, small_set, stack_out, small_out = (Path(a) for a in sys.argv[1:6])
	stack(export, menu, stack_out)
	small(small_set, small_out)


if __name__ == "__main__":
	main()
