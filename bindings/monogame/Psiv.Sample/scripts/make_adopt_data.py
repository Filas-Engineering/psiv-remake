#!/usr/bin/env python3
"""Copies the placeholder data for scripts/adopt.txt and gives Reed Path a name variant: once `reed.scout_joined` is set
it shows as `area.reed_path_known` ("Reed Path Lookout" in English, a placeholder in Japanese), else as before; and
an event `lead_walk` on no hook walks the leader five cells east along Reed Path's open row and three back.

Usage: make_adopt_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import shutil
import sys
from pathlib import Path

EVENT = """
# Test-only (scripts/make_adopt_data.py): a script walks the leader along Reed Path's open row and back.
event lead_walk skippable
	move fighter to 9 0 4
	move fighter to 6 0 4
end
"""

STRINGS = {"en": "Reed Path Lookout", "ja": "葦の小道 見張り台"}


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"
	area_file = data / "areas" / "reed_path.json"
	area = json.loads(area_file.read_text())
	area["name"] = [{"flag": "reed.scout_joined", "key": "area.reed_path_known"}, {"key": "area.reed_path"}]
	area_file.write_text(json.dumps(area, indent="\t", ensure_ascii=False) + "\n")
	(data / "events" / "adopt.ev").write_text(EVENT.lstrip("\n"))
	game_json = json.loads((data / "game.json").read_text())
	game_json["events"].append("events/adopt.ev")
	(data / "game.json").write_text(json.dumps(game_json, indent="\t", ensure_ascii=False) + "\n")
	for language in game_json["languages"]:
		table_file = data / language["text"]
		table = json.loads(table_file.read_text())
		table["text"]["area.reed_path_known"] = STRINGS[language["code"]]
		table_file.write_text(json.dumps(table, indent="\t", ensure_ascii=False) + "\n")


if __name__ == "__main__":
	main()
