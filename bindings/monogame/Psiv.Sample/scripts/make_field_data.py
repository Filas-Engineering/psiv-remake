#!/usr/bin/env python3
"""Copies the placeholder data for scripts/field_actors.txt and adds a script actor named after the marsh encounter,
which the sample draws as that encounter's field enemy, standing on its talk point in Tidewater Landing. Talking to it
starts `field_creature`, which plays the sample's built-in animations on it (hop, tremble, a move at twice the walking
pace, fade out, fade in, blink out and dissolve), each followed by the `hide` that removes it, and `game.json` lists
those animation names, as scripts may name only listed ones.

Usage: make_field_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import shutil
import sys
from pathlib import Path

EVENT = """
# Test-only (scripts/make_field_data.py): an actor named after an encounter, drawn as its field enemy, through the
# sample's built-in animations; each that ends out of sight is followed by the hide that removes the actor.
event field_creature skippable
	lock player
	anim marsh hop
	anim marsh tremble
	move marsh to 6 0 4 speed 2
	anim marsh fade_out
	hide marsh
	move marsh to 10 0 6
	show marsh nowait
	anim marsh fade_in
	anim marsh blink_out
	hide marsh
	show marsh nowait
	anim marsh fade_in
	anim marsh dissolve
	hide marsh
	unlock player
end
"""

ANIMATIONS = ["blink_out", "dissolve", "fade_in", "fade_out", "hop", "tremble"]


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"
	(data / "events" / "field_creature.ev").write_text(EVENT.lstrip("\n"))
	game_file = data / "game.json"
	game_json = json.loads(game_file.read_text())
	game_json["events"].append("events/field_creature.ev")
	game_json["actors"] = sorted(game_json["actors"] + ["marsh"])
	game_json["nameless_actors"] = sorted(game_json.get("nameless_actors", []) + ["marsh"])
	presentation = game_json["presentation"]
	presentation["animations"] = sorted(set(presentation["animations"]) | set(ANIMATIONS))
	game_file.write_text(json.dumps(game_json, indent="\t", ensure_ascii=False) + "\n")
	area_file = data / "areas" / "tidewater.json"
	area = json.loads(area_file.read_text())
	area["hooks"].append({"id": "marsh", "at": [10, 6], "event": "field_creature"})
	area_file.write_text(json.dumps(area, indent="\t", ensure_ascii=False) + "\n")


if __name__ == "__main__":
	main()
