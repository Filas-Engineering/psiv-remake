#!/usr/bin/env python3
"""Copies the placeholder data for scripts/battle_front.txt and scripts/battle_changes.txt and adds two encounters: six
enemies, more than any of the placeholder's encounters has, so the front battle layout's two ranks can be checked, and
a lone Twin Ooze, which has no partner to merge with, for an instant defeat that lands on it.

Usage: make_front_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks. The added encounters, `horde` and
`lone_ooze`, are in no zone table: the scripts start them with the `dev battle` command.
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
	enemies["encounters"].append({"id": "lone_ooze", "enemies": ["twin_ooze"]})
	enemies_file.write_text(json.dumps(enemies, indent="\t") + "\n")


if __name__ == "__main__":
	main()
