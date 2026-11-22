#!/usr/bin/env python3
"""Copies the placeholder data for the party talk speaker runs (talk_speakers.txt, small_talk.txt): the journal's
`chapter_one` party talk only in Tidewater, and there three speakers in place of its one line: the mystic in two pages
(`{page}`), the medic with a control tag that selects a portrait expression (`{portrait:medic:worried}`) and a wait, and
the scout in one page, in both languages. Away from Tidewater, before the warden falls, nobody talks.

Usage: make_talk_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import shutil
import sys
from pathlib import Path

LINES = {
	"en": {
		"talk.speakers.1": "The tide is turning.{page}We should move before the bell rings again.",
		"talk.speakers.2": "{portrait:medic:worried}Count the bandages {wait:20}twice. We are running low again.",
		"talk.speakers.3": "The lookout is clear. Nothing moves on the water tonight, and the gulls have gone quiet over the old pier by the kilns.",
	},
	"ja": {
		"talk.speakers.1": "潮が変わる。{page}鐘がまた鳴る前に行こう。",
		"talk.speakers.2": "{portrait:medic:worried}包帯を{wait:20}二度数えて。もう少ない。",
		"talk.speakers.3": "見張りは静か。水の上には何もない。",
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
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"

	game_json = load(data / "game.json")
	journal_file = data / game_json["journal"]
	journal = load(journal_file)
	talk = next(t for t in journal["party_talk"] if t["id"] == "chapter_one")
	talk["area"] = "tidewater"
	talk["lines"] = [{"speaker": speaker, "text": f"talk.speakers.{n}"} for n, speaker in enumerate(["mystic", "medic", "scout"], 1)]
	save(journal_file, journal)

	# The set's old line goes with it: the data check refuses a string nothing uses.
	for language in game_json["languages"]:
		strings = data / language["strings"]
		kept = [line for line in strings.read_text().splitlines() if not line.startswith("talk.chapter_one.1 ")]
		strings.write_text("\n".join(kept + [f"{k} = {v}" for k, v in LINES[language["code"]].items()]) + "\n")


if __name__ == "__main__":
	main()
