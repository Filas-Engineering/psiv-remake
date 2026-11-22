#!/usr/bin/env python3
"""Copies the placeholder data for scripts/items_dialogue.txt and adds one event, `dialogue_items`, whose first line shows while
the screen is faded out (`fade out 0`, then `fade in 0`), and whose next lines use a
`snd` tag that make_items_assets.py's dialogue layout maps by value (0 stops the music, 1 and 2 play two tracks, 10 to 19
ring a cue only the tags play, 7 matches no case) and still pictures shown, hidden and cleared as a stack; and leaves
out the party's macros, so the battle's top-level Macro has none to offer.

Usage: make_items_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks. The event is on no hook: the script
starts it with the `dev event` command.
"""

import json
import shutil
import sys
from pathlib import Path

EVENT = """
# Test-only (scripts/make_items_data.py): a line while faded out, a tag acting by value, and pictures as a stack.
event dialogue_items skippable
	fade out 0
	say guide @items.dark
	fade in 0
	say guide @items.stack
	say guide @items.uncover
	narrate @items.clear
end
"""

STRINGS = {
	"en": {
		"items.dark": "Who put the lamps out? I can still see you.",
		"items.stack": "{picture:chart}Here is the old chart.{picture:tower} And the tower drawn over it.{snd:1} Hear the tune?",
		"items.uncover": "{picture_hide:tower}The tower goes and the chart is back.{snd:12} A gong.{snd:2} Another tune.",
		"items.clear": "{snd:0}Quiet now.{pictures_clear} Nothing left to see.{snd:7}",
	},
	"ja": {
		"items.dark": "誰が灯りを消したの？まだ見えるよ。",
		"items.stack": "{picture:chart}古い海図だ。{picture:tower}その上に鐘楼の絵。{snd:1}曲が聞こえる？",
		"items.uncover": "{picture_hide:tower}鐘楼が消えて海図が戻る。{snd:12}銅鑼の音。{snd:2}別の曲。",
		"items.clear": "{snd:0}静かになった。{pictures_clear}もう何もない。{snd:7}",
	},
}


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"
	(data / "events" / "dialogue_items.ev").write_text(EVENT.lstrip("\n"))
	party_file = out / "core" / "data" / "party.json"
	party = json.loads(party_file.read_text())
	party["macros"] = []
	party_file.write_text(json.dumps(party, indent="\t", ensure_ascii=False) + "\n")
	game_file = data / "game.json"
	game_json = json.loads(game_file.read_text())
	game_json["events"].append("events/dialogue_items.ev")
	game_file.write_text(json.dumps(game_json, indent="\t", ensure_ascii=False) + "\n")
	for language in game_json["languages"]:
		table = data / language["strings"]
		lines = "".join(f"{key} = {text}\n" for key, text in STRINGS[language["code"]].items())
		table.write_text(table.read_text().rstrip("\n") + "\n\n" + lines)


if __name__ == "__main__":
	main()
