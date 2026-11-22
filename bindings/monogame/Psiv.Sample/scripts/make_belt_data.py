#!/usr/bin/env python3
"""Copies the placeholder data for scripts/belts.txt and adds Belt Yard, reached by a developer warp: an area whose step
time has a fraction, with two belts on the same four cells, one running east (187.5 ms a cell) until the flag
`belt.reversed` is set and one running west (233.5 ms a cell) once it is, so the belt reverses when the flag flips.
game.json also lists a script animation as an object with its seconds.

Usage: make_belt_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import shutil
import sys
from pathlib import Path

CELLS = [[2, 2], [3, 2], [4, 2], [5, 2]]

AREA = {
	"id": "belt_yard",
	"kind": "dungeon",
	"encounter_mode": "random",
	"step_ms": 262.5,
	"region": "coast",
	"map": [
		"#########",
		"#.......#",
		"#.......#",
		"#.......#",
		"#########",
	],
	"markers": {"yard_west": [1, 2], "yard_east": [6, 2]},
	"conveyors": [
		{"cells": CELLS, "direction": "east", "step_ms": 187.5, "block_flag": "belt.reversed"},
		{"cells": CELLS, "direction": "west", "step_ms": 233.5, "require_flag": "belt.reversed"},
	],
}


# Test-only: the warp that leads into the yard, which the run reaches by the developer console instead.
EVENT = """
event belt_way skippable
	warp belt_yard yard_west
end
"""


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"
	(data / "areas" / "belt_yard.json").write_text(json.dumps(AREA, indent="\t") + "\n")
	game_file = data / "game.json"
	game_json = json.loads(game_file.read_text())
	game_json["areas"].append("areas/belt_yard.json")
	(data / "events" / "belt.ev").write_text(EVENT.lstrip("\n"))
	game_json["events"].append("events/belt.ev")
	for language in game_json["languages"]:
		text_file = data / language["text"]
		text = json.loads(text_file.read_text())
		text["text"]["area.belt_yard"] = "Belt Yard"
		text_file.write_text(json.dumps(text, indent="\t", ensure_ascii=False) + "\n")
	game_json["flags"] = sorted(game_json["flags"] + ["belt.reversed"])
	game_json["presentation"]["animations"].append({"id": "belt_bow", "seconds": 0.5})
	game_file.write_text(json.dumps(game_json, indent="\t", ensure_ascii=False) + "\n")


if __name__ == "__main__":
	main()
