#!/usr/bin/env python3
"""Copies the second flows' data copy (make_more_flows_data.py's OUT) for late_flows.txt, adding an encounter the party
loses in its first round by the enemies' own hits (`late_crushers`: five `test_crusher`s, fast and strong, in no zone
table: the script starts it with `dev battle`), so the defeat comes from CombatantDefeated events; and `late_wisps`,
three one-hit-point `test_wisp`s the party fells in its first round, for counting the battle's cues. game.json's
presentation holds the screen dark 400 ms after an area change (`map_load_hold_ms`) and plays `brass_alarm` on a return
to a town and on opening a chest (`sounds`).

Usage: make_late_flows_data.py MORE_FLOWS_DATA OUT
"""

import json
import shutil
import sys
from pathlib import Path


def main():
	more, out = (Path(a) for a in sys.argv[1:3])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(more, out)
	enemies_file = out / "core" / "data" / "enemies.json"
	enemies = json.loads(enemies_file.read_text())
	enemies["enemies"].append({
		"id": "test_crusher", "name": "Test Crusher",
		"stats": {"hp": 999, "tp": 0, "strength": 999, "mental": 1, "agility": 999, "dexterity": 999, "defense": 999, "level": 1},
		"basic_attack": "bite", "slots": ["bite"] * 8, "experience": 1, "currency": 1,
	})
	enemies["enemies"].append({
		"id": "test_wisp", "name": "Test Wisp",
		"stats": {"hp": 1, "tp": 0, "strength": 1, "mental": 1, "agility": 1, "dexterity": 1, "defense": 0, "level": 1},
		"basic_attack": "bite", "slots": ["bite"] * 8, "experience": 1, "currency": 1,
	})
	enemies["encounters"].append({"id": "late_crushers", "enemies": ["test_crusher"] * 5})
	enemies["encounters"].append({"id": "late_wisps", "enemies": ["test_wisp"] * 3})
	enemies_file.write_text(json.dumps(enemies, indent="\t", ensure_ascii=False) + "\n")
	game_file = out / "game" / "data" / "game.json"
	game = json.loads(game_file.read_text())
	game["presentation"]["map_load_hold_ms"] = 400
	game["presentation"]["sounds"] = {"travel": {"town": "brass_alarm"}, "chest": "brass_alarm"}
	game_file.write_text(json.dumps(game, indent="\t", ensure_ascii=False) + "\n")


if __name__ == "__main__":
	main()
