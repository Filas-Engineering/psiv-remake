#!/usr/bin/env python3
"""Copies the motion data (`make_actor_data ... motion`: wanderers with `stand_percent` and `axis`, a patrol, a pixel path,
platforms) for scripts/shapes.txt and writes in every newer data shape the library accepts, so the sample's own data
reading meets each one:

* game.json `flags`: the first flag and a new `mill.visited` as objects with `reset: "area"`.
* game.json `presentation.pictures` (`mill_card`), `presentation.music.fade_ms` 300, `presentation.music.areas` with
  Wren Mill as a list of `{flag, track}` entries and a plain default, Wren Loft as a plain track.
* Wren Mill: `field_status`, a `name` list (by flag, then the default key), `counters` (a wall cell), its exit with a
  `sound`, a `block_flag` and `closed_tile: "normal"`. (An area's `name` is a list only; the library refuses a string.)

Usage: make_shapes_data.py MOTION_ROOT OUT

MOTION_ROOT and OUT hold game/data and core/data side by side.
"""

import json
import shutil
import sys
from pathlib import Path


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def main():
	source, out = Path(sys.argv[1]), Path(sys.argv[2])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(source, out)
	data = out / "game" / "data"
	game = load(data / "game.json")
	first = game["flags"][0]
	game["flags"][0] = {"id": first["id"] if isinstance(first, dict) else first, "reset": "area"}
	game["flags"].append({"id": "mill.visited", "reset": "area"})
	presentation = game["presentation"]
	presentation["pictures"] = ["mill_card"]
	presentation["music"]["fade_ms"] = 300
	presentation["music"]["areas"] = {
		"wren_mill": [{"flag": "mill.visited", "track": "field_bell_shore"}, "field_marsh_reeds"],
		"wren_loft": "field_brine_flats",
	}
	save(data / "game.json", game)
	mill = load(data / "areas" / "wren_mill.json")
	mill["field_status"] = True
	mill["name"] = [{"flag": "mill.visited", "key": "area.wren_loft"}, {"key": "area.wren_mill"}]
	mill["counters"] = [[3, 0]]
	mill["exits"][0].update({"sound": "lookout_creak", "block_flag": "mill.visited", "closed_tile": "normal"})
	save(data / "areas" / "wren_mill.json", mill)


if __name__ == "__main__":
	main()
