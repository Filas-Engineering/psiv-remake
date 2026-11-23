#!/usr/bin/env python3
"""Copies the placeholder data for vehicle_sounds.txt: the reed skimmer's move sound.

* game.json `presentation.sounds.vehicles.reed_skimmer`: `move` "lookout_creak" with `move_ms` from --move-ms (default
  0: one on every step aboard).

Usage: make_vehicle_sound_data.py GAME_DATA CORE_DATA OUT [--move-ms N]

OUT gets game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import shutil
import sys
from pathlib import Path


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	move_ms = int(sys.argv[sys.argv.index("--move-ms") + 1]) if "--move-ms" in sys.argv else 0
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game/data")
	shutil.copytree(core, out / "core/data")
	path = out / "game/data/game.json"
	data = json.loads(path.read_text())
	data["core_data"] = "../../core/data"
	data["presentation"].setdefault("sounds", {}).setdefault("vehicles", {})["reed_skimmer"] = {"move": "lookout_creak", "move_ms": move_ms}
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


if __name__ == "__main__":
	main()
