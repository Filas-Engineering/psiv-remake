#!/usr/bin/env python3
"""Copies the placeholder data for scripts/battle_front.txt, scripts/battle_changes.txt and scripts/fells.txt and adds
three encounters: six enemies, more than any of the placeholder's encounters has, so the front battle layout's two ranks
can be checked; a lone Twin Ooze, which has no partner to merge with, for an instant defeat that lands on it; and a
scrap crusher that answers a basic attack with a blow no member survives, so a fallen machine member stands up after a
won battle.

Usage: make_front_data.py GAME_DATA CORE_DATA OUT [--magic-defense] [--tile-hook]

With --magic-defense the copy's rules.json also sets `magic_defense`, for scripts/magic_defense.txt. With --tile-hook
the copy's Cogway also has a row of soft ground pads (map character `:`, which a tile step hook names) that play the
music box once the hall is powered, for scripts/assets_cogway.txt (the placeholder has no tile step hook).

OUT gets game/data and core/data side by side, where game.json's core_data looks. The added encounters, `horde`,
`lone_ooze` and `crusher`, are in no zone table: the scripts start them with the `dev battle` command.
"""

import json
import shutil
import sys
from pathlib import Path


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	magic_defense = "--magic-defense" in sys.argv[4:]
	tile_hook = "--tile-hook" in sys.argv[4:]
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
	enemies["enemies"].append({
		"id": "scrap_crusher", "name": "Scrap Crusher", "kind": "machine",
		"stats": {"hp": 999, "tp": 0, "strength": 1, "mental": 1, "agility": 1, "dexterity": 1, "defense": 0, "level": 1},
		"basic_attack": "bite", "abilities": ["crush"], "slots": ["bite"] * 8,
		"overrides": [{"when": "hit_by_attack", "ability": "crush", "target": "last_attacker"}],
		"experience": 1, "currency": 1,
	})
	enemies["encounters"].append({"id": "crusher", "enemies": ["scrap_crusher"]})
	enemies_file.write_text(json.dumps(enemies, indent="\t") + "\n")
	abilities_file = out / "core" / "data" / "abilities.json"
	abilities = json.loads(abilities_file.read_text())
	abilities["abilities"].append({"id": "crush", "name": "Crush", "kind": "technique", "cost": 0, "target": "one_enemy",
		"effects": [{"type": "physical_damage", "power": 999}]})
	abilities_file.write_text(json.dumps(abilities, indent="\t") + "\n")
	if tile_hook:
		cogway_file = out / "game" / "data" / "areas" / "cogway.json"
		cogway = json.loads(cogway_file.read_text())
		cogway["map"][5] = cogway["map"][5][:7] + ":::" + cogway["map"][5][10:]
		cogway["hooks"].append({"id": "cogway_pads", "trigger": "step", "tile": ":", "event": "cogway_jukebox", "require_flag": "cogway.open"})
		cogway_file.write_text(json.dumps(cogway, indent="\t") + "\n")
	if magic_defense:
		rules_file = out / "core" / "data" / "rules.json"
		rules = json.loads(rules_file.read_text())
		rules["magic_defense"] = True
		rules_file.write_text(json.dumps(rules, indent="\t") + "\n")


if __name__ == "__main__":
	main()
