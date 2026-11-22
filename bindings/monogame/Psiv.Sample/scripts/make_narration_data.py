#!/usr/bin/env python3
"""Copies the placeholder data for scripts/narration.txt and scripts/narration_ja.txt and adds one event, `narration`,
on no hook: five narration lines (the first two show still pictures with `picture` tags), then a speaker's line, in
English and Japanese. The scripts start it with the `dev event` command.

With --routing (scripts/narration_route.txt) a second event, `routing`, plays lines that test where narration goes:
a plain line, one starting with `{narration}`, a speaker's line with `{narration:on}`, a plain one with
`{narration:off}`, lines under the still pictures `chart` and `tower` with `picture_hide` taking the top one at a time,
and a `tale.` key.

With --nowait (scripts/narration_nowait.txt) an event `nowait` plays two narration lines tagged
`{narration:nowait}`, then a plain one, then a speaker's line.

Usage: make_narration_data.py GAME_DATA CORE_DATA OUT [--routing] [--nowait]

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


ROUTING_EVENT = """
# Test-only (scripts/make_narration_data.py --routing): lines for each narration_when mode and the picture stack.
event routing skippable
	narrate @route.plain
	narrate @route.tagged
	say guide @route.forced
	narrate @route.boxed
	narrate @route.chart
	narrate @route.tower
	narrate @route.pop
	narrate @tale.close
	say guide @route.clear
end
"""

ROUTING_STRINGS = {"en": {
	"route.plain": "A plain line without a speaker.",
	"route.tagged": "{narration}A line tagged as narration.",
	"route.forced": "{narration:on}The guide speaks, sent to narration.",
	"route.boxed": "{narration:off}A plain line kept in the box.",
	"route.chart": "{picture:chart}The chart goes up.",
	"route.tower": "{picture:tower}The tower goes over it.",
	"route.pop": "{picture_hide}The tower comes down; the chart stays.",
	"tale.close": "{picture_hide}A tale line, the chart gone too.",
	"route.clear": "{pictures_clear}That was the whole stack.",
}, "ja": {
	"route.plain": "話し手のいない普通の行。",
	"route.tagged": "{narration}語りの印のある行。",
	"route.forced": "{narration:on}案内人の言葉を語りに回す。",
	"route.boxed": "{narration:off}箱に残す普通の行。",
	"route.chart": "{picture:chart}海図が上がる。",
	"route.tower": "{picture:tower}その上に鐘楼。",
	"route.pop": "{picture_hide}鐘楼が下り、海図が残る。",
	"tale.close": "{picture_hide}物語の行、海図も消えた。",
	"route.clear": "{pictures_clear}これで全部だった。",
}}


NOWAIT_EVENT = """
# Test-only (scripts/make_narration_data.py --nowait): lines that go on by themselves under the press mode.
event nowait skippable
	narrate @nowait.tide
	narrate @nowait.gulls
	narrate @nowait.still
	say guide @nowait.guide
end
"""

NOWAIT_STRINGS = {"en": {
	"nowait.tide": "{narration:nowait}The tide turned at noon.",
	"nowait.gulls": "{narration:nowait}The gulls went quiet.",
	"nowait.still": "Then nothing moved at all.",
	"nowait.guide": "Strange weather.",
}, "ja": {
	"nowait.tide": "{narration:nowait}正午に潮が変わった。",
	"nowait.gulls": "{narration:nowait}カモメが静かになった。",
	"nowait.still": "それから何も動かなかった。",
	"nowait.guide": "妙な天気だ。",
}}


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	routing = "--routing" in sys.argv[4:]
	nowait = "--nowait" in sys.argv[4:]
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"
	(data / "events" / "narration.ev").write_text(EVENT.lstrip("\n") + (ROUTING_EVENT if routing else "") + (NOWAIT_EVENT if nowait else ""))
	game_file = data / "game.json"
	game_json = json.loads(game_file.read_text())
	game_json["events"].append("events/narration.ev")
	game_file.write_text(json.dumps(game_json, indent="\t", ensure_ascii=False) + "\n")
	for language in game_json["languages"]:
		table = data / language["strings"]
		strings = dict(STRINGS[language["code"]], **(ROUTING_STRINGS[language["code"]] if routing else {}), **(NOWAIT_STRINGS[language["code"]] if nowait else {}))
		lines = "".join(f"{key} = {text}\n" for key, text in strings.items())
		table.write_text(table.read_text().rstrip("\n") + "\n\n" + lines)


if __name__ == "__main__":
	main()
