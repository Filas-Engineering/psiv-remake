#!/usr/bin/env python3
"""Writes the asset directory scripts/menus_more.txt plays: a copy of the test assets with layout files built from the
export's built-in ones and the menu follow-ups docs/ui-layout.md describes ("Menu entries"):

* menu: Items, a System tab whose `children` are the Save, Options and Quests pages, Abilities, Talk and Status, with
  larger tabs (`tab_h`, `tab_pad`, `tab_gap`, `tab_text_y`); alt/menu_broken.json one problem of each kind `children`
  can have, which the Makefile checks on a copy;
* menu_system: the System action on (`system_action`), the rows save, text speed and quit;
* menu_abilities: abilities not usable in the field hidden (`unusable`); alt/menu_abilities_dim.json the built-in file,
  which the run swaps in and reloads;
* menu_options: the party talk on the page (`party_talk`), its lines in a box through the `talk_box` slot.

Usage: make_menus_more_assets.py EXPORT TEST OUT

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

	menu = load(built_in / "menu.json")
	menu["params"] = {"tab_h": 36, "tab_pad": 36, "tab_gap": 12, "tab_text_y": 7}
	menu["entries"] = [
		{"page": "items"},
		{"page": "system", "children": [{"page": "save"}, {"page": "options"}, {"page": "quests", "label": "ui.quests.jobs"}]},
		{"page": "abilities"},
		{"page": "party_talk"},
		{"page": "status"},
	]
	save(ui / "menu.json", menu)
	save(out / "alt/menu_broken.json", dict(menu, entries=[
		{"page": "items", "children": []},
		{"page": "system", "children": [{"page": "save"}, {"page": "teleport"}, {"page": "save"}, {"label": "ui.quests.jobs"}, {"page": "codex", "children": []}]},
		{"page": "status", "children": "save"},
	]))

	system = load(built_in / "menu_system.json")
	system["params"] = {"system_action": 1}
	system["rows"] = ["save", "text_speed", "quit"]
	save(ui / "menu_system.json", system)

	abilities = load(built_in / "menu_abilities.json")
	save(out / "alt/menu_abilities_dim.json", abilities)
	abilities["params"] = {"unusable": "hide"}
	save(ui / "menu_abilities.json", abilities)

	options = load(built_in / "menu_options.json")
	options["params"] = {"party_talk": "page"}
	options["windows"].append({"name": "talk", "visible": "talking", "x": "area_x + 20", "y": "area_y + t(96)", "w": "min(area_w - 40, t(640))", "h": "t(150)",
		"frame": "panel", "elements": [{"type": "slot", "slot": "talk_box", "x": 24, "y": 20, "w": "cw - 48", "h": "ch - 40"}]})
	save(ui / "menu_options.json", options)


if __name__ == "__main__":
	main()
