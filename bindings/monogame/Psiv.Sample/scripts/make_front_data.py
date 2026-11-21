#!/usr/bin/env python3
"""Copies the placeholder data for scripts/battle_front.txt and adds an encounter of six enemies, more than any of the
placeholder's encounters has, so the front battle layout's two ranks can be checked.

Usage: make_front_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks. The added encounter, `horde`, is in no
zone table: the script starts it with the `dev battle` command.
"""

import json
import shutil
import sys
from pathlib import Path


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	enemies_file = out / "core" / "data" / "enemies.json"
	enemies = json.loads(enemies_file.read_text())
	enemies["encounters"].append({
		"id": "horde",
		"enemies": ["gel_mite", "rust_hound", "gel_mite", "rust_hound", "gel_mite", "brass_warden"],
		"drop": {"item": "tonic", "rate": 50},
	})
	enemies_file.write_text(json.dumps(enemies, indent="\t") + "\n")


if __name__ == "__main__":
	main()
