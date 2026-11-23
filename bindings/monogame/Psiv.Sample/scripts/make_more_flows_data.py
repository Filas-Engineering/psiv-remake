#!/usr/bin/env python3
"""Copies the placeholder data for the second layout flows runs (more_flows.txt, small_more_flows.txt):

* Tidewater's inn with a keeper voice (`voice.more_inn`) whose welcome, confirm, decline and farewell lines differ, in
  both languages, so the inn's `decline` step shows its own line and the farewell another;
* a test enemy (`test_bounty`, one hit point, 2400 experience) and its encounter `more_bounty` (in no zone table: the
  scripts start it with `dev battle`), so a won battle raises every member several levels and the level-ups take more
  than one page.

Usage: make_more_flows_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import shutil
import sys
from pathlib import Path

STRINGS = {
	"en": {
		"voice.more_inn.welcome": "Welcome, travelers.",
		"voice.more_inn.confirm": "A bed for all of you is {0}. Will you stay?",
		"voice.more_inn.decline": "Another night, then. The lamp stays lit.",
		"voice.more_inn.farewell": "Rested and ready. Mind the tide.",
	},
	"ja": {
		"voice.more_inn.welcome": "ようこそ、旅の方。",
		"voice.more_inn.confirm": "全員で{0}です。泊まりますか？",
		"voice.more_inn.decline": "では、またの夜に。灯りはつけておきます。",
		"voice.more_inn.farewell": "よく休めましたね。潮に気をつけて。",
	},
}


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	data, core_data = out / "game" / "data", out / "core" / "data"
	shutil.copytree(game, data)
	shutil.copytree(core, core_data)
	game_json = load(data / "game.json")

	tidewater_file = data / "areas" / "tidewater.json"
	tidewater = load(tidewater_file)
	tidewater["rests"][0]["voice"] = "voice.more_inn"
	save(tidewater_file, tidewater)

	for language in game_json["languages"]:
		table_file = data / language["text"]
		table = load(table_file)
		table["text"].update(STRINGS[language["code"]])
		save(table_file, table)

	enemies_file = core_data / "enemies.json"
	enemies = load(enemies_file)
	enemies["enemies"].append({
		"id": "test_bounty", "name": "Test Bounty",
		"stats": {"hp": 1, "tp": 0, "strength": 1, "mental": 1, "agility": 1, "dexterity": 1, "defense": 0, "level": 1},
		"basic_attack": "bite", "slots": ["bite"] * 8, "experience": 2400, "currency": 1,
	})
	enemies["encounters"].append({"id": "more_bounty", "enemies": ["test_bounty"]})
	save(enemies_file, enemies)


if __name__ == "__main__":
	main()
