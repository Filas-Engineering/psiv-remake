#!/usr/bin/env python3
"""Writes the asset directory the menu runs play (scripts/menus_layout.txt and menus_battle.txt): a copy of the test
assets with layout files built from the export's built-in ones and the optional menu fields docs/ui-layout.md describes:

* menu: its own entries in its own order, with the technique, skill and ability pages, the system page, party talk and
  the Extras entry, its own label for the Quests page, and the Codex, Combos, Log and Order pages left out;
  alt/menu_short.json the system page and the Extras entry only, which the run swaps in and reloads, and
  alt/menu_full.json the first file, to swap back;
* menu_system: the system page's rows (save, the text speed, the flashing level, quit);
* extras: entries for every kind (settings rows, the options and controls, the photosensitivity notice, the developer
  tools, two field menu pages);
* controls: the Extras action listed (`list_extras`);
* battle_front, battle_side: the top-level step with Fight and Run only, and the members' rows without Escape, Macro
  and Auto.

Usage: make_menu_assets.py EXPORT TEST OUT

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
	menu["entries"] = [{"page": p} for p in ["items", "techniques", "skills", "abilities", "status", "equip", "system", "party_talk", "extras", "quests"]]
	menu["entries"][9]["label"] = "ui.quests.jobs"
	save(ui / "menu.json", menu)
	save(out / "alt/menu_full.json", menu)
	save(out / "alt/menu_short.json", dict(menu, entries=[{"page": "system"}, {"page": "extras"}]))

	system = load(built_in / "menu_system.json")
	system["rows"] = ["save", "text_speed", "flashing", "quit"]
	save(ui / "menu_system.json", system)

	extras = load(built_in / "extras.json")
	extras["entries"] = [{"page": p} for p in ["text_speed", "battle_speed", "flashing", "options", "controls", "photosensitivity", "dev", "techniques", "quests"]]
	extras["entries"][3]["label"] = "ui.title.options"
	save(ui / "extras.json", extras)

	controls = load(built_in / "controls.json")
	controls["params"] = {"list_extras": 1}
	save(ui / "controls.json", controls)

	for screen in ["battle_front", "battle_side"]:
		layout = load(built_in / f"{screen}.json")
		layout["params"]["command_top"] = 1
		layout["command_rows"] = {"top": ["fight", "run"], "member": ["attack", "technique", "skill", "item", "defend"]}
		save(ui / f"{screen}.json", layout)


if __name__ == "__main__":
	main()
