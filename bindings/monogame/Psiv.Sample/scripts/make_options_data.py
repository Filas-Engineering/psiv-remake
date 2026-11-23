#!/usr/bin/env python3
"""Copies the placeholder data for options_flows.txt (the system page's picker, five speed levels, a chest's opened notice,
the battle's macro slots):

* game.json `presentation.options`: five text speed levels (`slowest` 12, `slow` 25, `normal` 50, `fast` 100, `instant`
  0, labels `opt.text.*`, default `normal`) and five battle speed levels (`half` 50, `1x` 100, `1.5x` 150, `2x` 200,
  `4x` 400, labels `opt.battle.*`);
* `presentation.chest_opened_notice` on, the Reed Path's cache a `crate` (text key `game.chest_opened.crate`);
* a new game of two at level 10 (the fighter and the mystic) facing the cache, with two macros in party.json (`strike`,
  `guard`), so a battle's slot list shows two set slots and the rest empty.

Usage: make_options_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import shutil
import sys
from pathlib import Path

TEXT = {
	"en": {
		"game.chest_opened.crate": "The crate has been pried open.",
		"opt.text.slowest": "Slowest",
		"opt.text.slow": "Slow",
		"opt.text.normal": "Normal",
		"opt.text.fast": "Fast",
		"opt.text.instant": "Instant",
		"opt.battle.half": "Half",
		"opt.battle.1x": "Normal",
		"opt.battle.1.5x": "Brisk",
		"opt.battle.2x": "Quick",
		"opt.battle.4x": "Fastest",
	},
	"ja": {
		"game.chest_opened.crate": "木箱を開けた。",
		"opt.text.slowest": "とても遅い",
		"opt.text.slow": "遅い",
		"opt.text.normal": "普通",
		"opt.text.fast": "速い",
		"opt.text.instant": "瞬時",
		"opt.battle.half": "半分",
		"opt.battle.1x": "普通",
		"opt.battle.1.5x": "少し速い",
		"opt.battle.2x": "速い",
		"opt.battle.4x": "とても速い",
	},
}


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def levels(prefix, pairs):
	return [{"id": i, "label": f"{prefix}.{i}", "value": v} for i, v in pairs]


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"

	game_json = load(data / "game.json")
	game_json["start"].update({"area": "reed_path", "marker": "cache_front", "facing": "north", "lineup": ["fighter", "mystic"], "level": 10})
	game_json["presentation"]["chest_opened_notice"] = True
	game_json["presentation"]["options"] = {
		"text_speed": {"levels": levels("opt.text", [("slowest", 12), ("slow", 25), ("normal", 50), ("fast", 100), ("instant", 0)]), "default": "normal"},
		"battle_speed": {"levels": levels("opt.battle", [("half", 50), ("1x", 100), ("1.5x", 150), ("2x", 200), ("4x", 400)])},
	}
	save(data / "game.json", game_json)
	for language in game_json["languages"]:
		text_file = data / language["text"]
		text = load(text_file)
		text["text"].update(TEXT[language["code"]])
		save(text_file, text)

	reed_file = data / "areas" / "reed_path.json"
	reed = load(reed_file)
	for chest in reed["chests"]:
		if chest["id"] == "reed_cache":
			chest["kind"] = "crate"
	save(reed_file, reed)

	party_file = out / "core" / "data" / "party.json"
	party = load(party_file)
	party["macros"] = [
		{"name": "strike", "steps": [{"member": "fighter", "command": "attack", "target": {"enemy": 0}}, {"member": "mystic", "command": "attack", "target": {"enemy": 0}}]},
		{"name": "guard", "steps": [{"member": "fighter", "command": "defend"}, {"member": "mystic", "command": "defend"}]},
	]
	save(party_file, party)


if __name__ == "__main__":
	main()
