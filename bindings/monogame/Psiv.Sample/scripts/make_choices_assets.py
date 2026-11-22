#!/usr/bin/env python3
"""Writes the asset directory scripts/layout_choices.txt plays: a copy of the test assets with layout files built from
the export's built-in ones and the choices docs/ui-layout.md describes:

* title: Continue opens the Load list (`continue` "choose"); alt/title_newest.json the built-in file, which the run
  swaps in;
* battle_front: the worn pieces before the bag's items in the Item list (`item_order` "equipment_then_items") and the
  auto-battle macro first in the macro list (`macro_auto_first`); alt/battle_front_plain.json the built-in file, which
  the run swaps in;
* menu_items: an item's actions (`actions`) in a window beside the list, its description in a window when looked at,
  and the uses' outcomes on a line over the description (`result_text`);
* alt/title_broken.json and alt/menu_items_broken.json: a bad `continue` word and a non-number `actions`, which the
  Makefile checks on a copy.

Usage: make_choices_assets.py EXPORT TEST OUT

EXPORT is the placeholder export (its ui/ files are the built-in layouts), TEST the test assets (make_test_assets.py).
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


def main():
	export, test, out = (Path(a) for a in sys.argv[1:4])
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(test, out)
	ui = out / "ui"
	built_in = export / "ui"

	title = load(built_in / "title.json")
	save(out / "alt/title_newest.json", title)
	save(out / "alt/title_broken.json", dict(title, params={"continue": "latest"}))
	save(ui / "title.json", dict(title, params={"continue": "choose"}))

	front = load(built_in / "battle_front.json")
	save(out / "alt/battle_front_plain.json", front)
	front["params"].update({"item_order": "equipment_then_items", "macro_auto_first": 1})
	save(ui / "battle_front.json", front)

	items = load(built_in / "menu_items.json")
	save(out / "alt/menu_items_broken.json", dict(items, params={"actions": "yes"}))
	items["params"] = {"actions": 1}
	page = items["windows"][0]["elements"]
	next(e for e in page if e["type"] == "list")["active"] = "!picking && !acting"
	items["windows"] += [
		{"name": "actions", "visible": "acting", "x": "area_x + area_w - 300", "y": "area_y + t(20)", "w": 260, "h": "t(110)", "z": 5, "frame": "panel",
			"padding": [16, 14, 16, 14], "elements": [
			{"type": "list", "items": "actions", "state": "action_list", "w": "cw", "h": "ch", "active": "acting && !looking"}]},
		{"name": "look", "visible": "looking", "x": "area_x + 40", "y": "area_y + t(150)", "w": "area_w - 80", "h": "t(110)", "z": 6, "frame": "panel",
			"padding": [20, 16, 20, 16], "elements": [
			{"type": "text", "value": "look_text", "w": "cw", "wrap": True, "lines": 3, "line_height": "t(24)"}]},
		{"name": "result", "visible": "result_text != ''", "x": "area_x + 14", "y": "area_y + area_h - t(86)", "w": "area_w - 28", "h": "t(26)", "elements": [
			{"type": "text", "value": "result_text", "style": "small", "color": "result_ok ? 'good' : 'bad'"}]},
	]
	save(ui / "menu_items.json", items)


if __name__ == "__main__":
	main()
