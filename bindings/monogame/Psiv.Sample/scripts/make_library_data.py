#!/usr/bin/env python3
"""Copies the placeholder data for the library adoption runs (library_followers.txt, library_shop.txt, exit_block.txt,
field_status.txt) with the newer library keys set:

* game.json `followers_arrive` and `followers_arrive_other` "behind" (the session places the followers after exits and
  after warps alike), and with --bag `bag_slots` 4;
* Tidewater's stall and inn named (`name`: `shop.tidewater_stall`, `inn.tidewater_inn`), the stall with a voice whose
  welcome takes the name as {0};
* two generated areas, `exit_yard_a` and `exit_yard_b` (10x7, test-only), each with a 2x2 block of exits leading onto the
  other's block (marker `block`, an exit cell), so a party arrives standing on an exit; `exit_yard_a` has
  `field_poison`, `exit_yard_b` `field_paralysis`;
* core's rules.json `field_poison_every` 2 and `field_paralysis` (from step 1, a 1 in 1000 roll, surely by step 3),
  paralysis kept outside battle, two battle items that put poison (`test_venom`) and paralysis (`test_numb`) on an ally,
  and a field item that sets an ally's hit points to 1 (`test_drain`), for the field status run.

With --no-autosave game.json also says `autosave` false. With --moving (moving_talk.txt) a generated town `follow_yard`
(12x8, test-only) has an exit with `followers` "behind" and one with "up", both onto the marker `middle` of a second
generated town, `follow_room`, and the
ashwife wanders there from her talk point (3, 2), whose `talk_at` "actor" and `turns` make her answer where she stands
and turn to the leader; her talk point in Ashfall Market stays as it was.

Usage: make_library_data.py GAME_DATA CORE_DATA OUT [--bag] [--no-autosave] [--moving]

OUT gets game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import shutil
import sys
from pathlib import Path

STRINGS = {
	"en": {
		"shop.tidewater_stall": "Gull's Rest Stall",
		"voice.library_stall.welcome": "Welcome to {0}.",
		"inn.tidewater_inn": "The Mooring Inn",
		"area.exit_yard_a": "Exit Yard West",
		"area.exit_yard_b": "Exit Yard East",
		"area.follow_yard": "Follow Yard",
		"area.follow_room": "Follow Room",
	},
	"ja": {
		"shop.tidewater_stall": "カモメの店",
		"voice.library_stall.welcome": "{0}へようこそ。",
		"inn.tidewater_inn": "もやい宿",
		"area.exit_yard_a": "出口の庭 西",
		"area.exit_yard_b": "出口の庭 東",
		"area.follow_yard": "ついて来る庭",
		"area.follow_room": "ついて来る部屋",
	},
}

# Rows of a test yard: walls round a floor, the 2x2 exit block at columns 6 and 7, rows 3 and 4.
YARD = [
	"##########",
	"#........#",
	"#........#",
	"#........#",
	"#........#",
	"#........#",
	"##########",
]
BLOCK = [[6, 3], [7, 3], [6, 4], [7, 4]]


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def yard(area_id, other, status_key):
	return {
		"id": area_id, "kind": "town", "encounter_mode": "random", "step_ms": 250,
		"map": YARD,
		"markers": {"west": [2, 3], "block": [6, 3]},
		"exits": [{"at": cell, "to": other, "marker": "block"} for cell in BLOCK],
		status_key: True,
	}


FOLLOW_YARD = {
	"id": "follow_yard", "kind": "town", "encounter_mode": "random", "step_ms": 250,
	"map": ["############"] + ["#..........#"] * 6 + ["############"],
	"markers": {"middle": [6, 4], "behind_side": [9, 2], "up_side": [9, 6]},
	"exits": [
		{"at": [10, 2], "to": "follow_room", "marker": "middle", "followers": "behind"},
		{"at": [10, 6], "to": "follow_room", "marker": "middle", "followers": "up"},
	],
	"hooks": [{"id": "ashwife", "at": [3, 2], "event": "ashfall_ashwife", "talk_at": "actor", "turns": True}],
}
FOLLOW_ROOM = {
	"id": "follow_room", "kind": "town", "encounter_mode": "random", "step_ms": 250,
	"map": FOLLOW_YARD["map"],
	"markers": {"middle": [6, 4]},
}


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	no_autosave = "--no-autosave" in sys.argv[4:]
	bag = "--bag" in sys.argv[4:]
	moving = "--moving" in sys.argv[4:]
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"
	core_data = out / "core" / "data"

	game_json = load(data / "game.json")
	game_json["followers_arrive"] = "behind"
	game_json["followers_arrive_other"] = "behind"
	if bag:
		game_json["bag_slots"] = 4
	if no_autosave:
		game_json["autosave"] = False
	for area_id, other, key in (("exit_yard_a", "exit_yard_b", "field_poison"), ("exit_yard_b", "exit_yard_a", "field_paralysis")):
		save(data / "areas" / f"{area_id}.json", yard(area_id, other, key))
		game_json["areas"].append(f"areas/{area_id}.json")
	if moving:
		for area in (FOLLOW_YARD, FOLLOW_ROOM):
			save(data / "areas" / f"{area['id']}.json", area)
			game_json["areas"].append(f"areas/{area['id']}.json")
		game_json["actors"] = [{"id": a, "area": "follow_yard", "at": [3, 2], "wander": {"radius": 2, "pause_ms": [400, 800]}} if a == "ashwife" else a
			for a in game_json["actors"]]
	game_json["prices"].update({"test_venom": 1, "test_numb": 1, "test_drain": 1})
	save(data / "game.json", game_json)

	tidewater_file = data / "areas" / "tidewater.json"
	tidewater = load(tidewater_file)
	tidewater["shops"][0]["name"] = "shop.tidewater_stall"
	tidewater["shops"][0]["voice"] = "voice.library_stall"
	tidewater["rests"][0]["name"] = "inn.tidewater_inn"
	save(tidewater_file, tidewater)

	for language in game_json["languages"]:
		table_file = data / language["text"]
		table = load(table_file)
		table["text"].update(STRINGS[language["code"]])
		save(table_file, table)

	rules = load(core_data / "rules.json")
	rules["field_poison_every"] = 2
	rules["field_paralysis"] = {"from": 1, "chance": 1000, "by": 3}
	save(core_data / "rules.json", rules)
	statuses_file = core_data / "statuses.json"
	statuses = load(statuses_file)
	for status in statuses["statuses"] if isinstance(statuses, dict) else statuses:
		if status["id"] == "paralysis":
			status["persists_outside_battle"] = True
	save(statuses_file, statuses)
	items_file = core_data / "items.json"
	items = load(items_file)
	for item_id, name, status in (("test_venom", "Test Venom", "poison"), ("test_numb", "Test Numb", "paralysis")):
		items["items"].append({"id": item_id, "name": name, "target": "one_ally",
			"effects": [{"type": "apply_status", "status": status, "always_lands": True}]})
	items["items"].append({"id": "test_drain", "name": "Test Drain", "target": "one_ally", "field": True,
		"effects": [{"type": "set_hp_percent", "power": 1}]})
	save(items_file, items)


if __name__ == "__main__":
	main()
