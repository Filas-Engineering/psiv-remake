#!/usr/bin/env python3
"""Writes the asset directory scripts/shop_steps.txt plays: a copy of TEST (the test assets) with a shop layout in steps
(`steps` 1): a buy or sell choice, the list, a quantity window with the unit price and the total, and a result window,
the keeper's line (`voice_line`) over them and the trades' outcomes in `result_text` instead of the notices; and
alt/shop_tabs.json, the export's tab layout with a `result_text` line under the list, for the same outcomes without the
steps.

Usage: make_shop_assets.py EXPORT TEST OUT
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


STEPS = {"version": 1, "params": {"steps": 1, "backdrop_dim": 0.4}, "windows": [
	{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
	{"name": "keeper", "visible": "open", "x": 80, "y": 40, "w": "W - 160", "h": 96, "frame": "panel", "padding": [16, 14, 16, 14], "elements": [
		{"type": "image", "image": "keeper", "w": 64, "h": 64},
		{"type": "text", "value": "voice_line", "x": 84, "y": 4, "w": "cw - 260", "wrap": True, "lines": 2},
		{"type": "text", "value": "money", "w": "cw", "align": "right", "y": 4},
	]},
	{"name": "choose", "visible": "open && step == 'choose'", "x": 80, "y": 152, "w": 240, "h": 110, "frame": "panel", "title": {"value": "title"},
		"open": {"kind": "grow_y", "ms": 120}, "elements": [
		{"type": "rows", "items": "choices", "selected": "choice_cursor", "x": 30, "y": 24},
	]},
	{"name": "list", "visible": "open && step != 'choose'", "x": 80, "y": 152, "w": "W - 160", "h": "H - 240", "frame": "panel", "title": {"value": "title"}, "elements": [
		{"type": "list", "items": "rows", "state": "list", "active": "step != 'choose'", "x": 24, "y": 24, "w": "cw - 48", "h": "ch - 120", "row_height": "t(30)", "row": [
			{"type": "fill", "visible": "selected", "x": -6, "y": -3, "w": "cw - 36", "h": "t(28)", "color": "row"},
			{"type": "text", "value": "text", "x": 14, "color": "selected ? 'accent' : 'text'"},
			{"type": "text", "value": "price", "w": "cw - 180", "align": "right", "color": "selected ? 'accent' : 'text'"},
			{"type": "text", "key": "ui.common.count", "args": ["have"], "x": "cw - 140", "color": "dim"},
		]},
		{"type": "text", "value": "description", "x": 24, "y": "ch - 80", "w": "cw - 48", "wrap": True, "lines": 2, "style": "small"},
	]},
	{"name": "quantity", "visible": "open && step == 'quantity'", "anchor": "center", "w": 420, "h": 150, "z": 5, "frame": "panel",
		"open": {"kind": "fade", "ms": 120}, "padding": [20, 18, 20, 18], "elements": [
		{"type": "text", "value": "'x ' + quantity + ' / ' + quantity_max", "w": "cw", "align": "center", "style": "title"},
		{"type": "text", "value": "unit_price + ' each'", "y": 46, "w": "cw", "align": "center", "style": "small_dim"},
		{"type": "text", "value": "'Total ' + total", "y": 76, "w": "cw", "align": "center", "color": "accent"},
	]},
	{"name": "result", "visible": "open && step == 'result'", "anchor": "center", "w": 520, "h": 110, "z": 5, "frame": "panel", "padding": [20, 18, 20, 18], "elements": [
		{"type": "text", "value": "result_text", "w": "cw", "wrap": True, "lines": 2, "color": "result_ok ? 'text' : 'bad'"},
	]},
	{"name": "toasts", "elements": [{"type": "slot", "slot": "toasts", "x": 24, "y": "H - 60"}]},
]}


def main():
	export, test, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(test, out)
	save(out / "ui" / "shop.json", STEPS)
	tabs = load(export / "ui" / "shop.json")
	group = tabs["windows"][1]["elements"][0]["elements"]
	group.insert(-2, {"type": "text", "name": "result", "visible": "result_text != ''", "value": "result_text", "x": 54, "y": "H - 190 - t(80) - t(26)", "style": "small",
		"color": "result_ok ? 'good' : 'bad'"})
	save(out / "alt" / "shop_tabs.json", tabs)


if __name__ == "__main__":
	main()
