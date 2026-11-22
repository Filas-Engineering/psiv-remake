#!/usr/bin/env python3
"""Copies the placeholder data for scripts/narration.txt and scripts/narration_ja.txt and adds one event, `narration`,
on no hook: five narration lines (the first two show still pictures with `picture` tags), then a speaker's line, in
English and Japanese. The scripts start it with the `dev event` command.

Usage: make_narration_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import shutil
import sys
from pathlib import Path

EVENT = """
# Test-only (scripts/make_narration_data.py): narration lines that gather in a window without a box.
event narration skippable
	narrate @narration.fog
	narrate @narration.tower
	narrate @narration.bell
	narrate @narration.breath
	narrate @narration.lift
	say guide @narration.guide
end
"""

STRINGS = {
	"en": {
		"narration.fog": "{picture:chart}The fog came in off the sea before dawn.",
		"narration.tower": "{picture:tower}By noon the bell tower was a grey shape.",
		"narration.bell": "Nobody rang the bell, yet everyone heard it.",
		"narration.breath": "The harbor held its breath.",
		"narration.lift": "Then the fog lifted, all at once.",
		"narration.guide": "{pictures_clear}Did you see that?",
	},
	"ja": {
		"narration.fog": "{picture:chart}夜明け前、海から霧が入ってきた。",
		"narration.tower": "{picture:tower}昼には鐘楼も灰色の影になった。",
		"narration.bell": "誰も鐘を鳴らさないのに、皆がその音を聞いた。",
		"narration.breath": "港は息をひそめた。",
		"narration.lift": "そして霧は一度に晴れた。",
		"narration.guide": "{pictures_clear}今の、見た？",
	},
}


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"
	(data / "events" / "narration.ev").write_text(EVENT.lstrip("\n"))
	game_file = data / "game.json"
	game_json = json.loads(game_file.read_text())
	game_json["events"].append("events/narration.ev")
	game_file.write_text(json.dumps(game_json, indent="\t", ensure_ascii=False) + "\n")
	for language in game_json["languages"]:
		table = data / language["strings"]
		lines = "".join(f"{key} = {text}\n" for key, text in STRINGS[language["code"]].items())
		table.write_text(table.read_text().rstrip("\n") + "\n\n" + lines)


if __name__ == "__main__":
	main()
