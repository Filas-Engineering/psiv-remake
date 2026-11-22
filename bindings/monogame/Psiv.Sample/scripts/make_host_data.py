#!/usr/bin/env python3
"""Copies the placeholder data for the host-call runs (host_menus.txt, host_tags.txt, host_reach.txt):

* core's items.json: `host_charm`, an item the bag may not throw away (`discardable` false);
* Tidewater's east exit `fires: always`, the reed path's exits as they are (`from_floor`), for the exit markers;
* Tidewater's stall with a voice whose welcome is two pages with a wait tag;
* control tags in text a screen outside the dialogue shows: a party talk line (`{page}`, `{wait}`, `{var:money}`), the
  Tidewater codex entry (`{page}`), every quest objective (`{wait}`), the Tonic's description (`{page}`) and the discard
  notice (`{wait}`), in both languages.

With --consume core's rules.json says `item_no_reach` "consume" and the Tonic `consume_when_no_effect`, so a Tonic is
spent on an ally it cannot reach instead of refused.

Usage: make_host_data.py GAME_DATA CORE_DATA OUT [--consume]

OUT gets game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import re
import shutil
import sys
from pathlib import Path

STRINGS = {
	"en": {"voice.host_stall.welcome": "Welcome in.{page}Look around {wait:150}as long as you like."},
	"ja": {"voice.host_stall.welcome": "いらっしゃい。{page}ゆっくり{wait:150}見ていって。"},
}


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def tag_strings(path):
	"""Adds the control tags to the string table's lines the runs show outside the dialogue."""
	lines = path.read_text().split("\n")
	for i, line in enumerate(lines):
		key, sep, text = line.partition(" = ")
		if not sep:
			continue
		if key == "talk.chapter_one.1":
			first, _, rest = text.partition(". ")
			lines[i] = f"{key} = {first}.{{page}}{rest} {{wait:200}}({{var:money}})" if rest else f"{key} = {text}{{page}}{{wait:200}}({{var:money}})"
		elif key == "codex.tidewater.text":
			lines[i] = f"{key} = {text}{{page}}{text}"
		elif key.startswith("objective."):
			lines[i] = f"{key} = {{wait:100}}{text}"
	path.write_text("\n".join(lines))


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	consume = "--consume" in sys.argv[4:]
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"
	core_data = out / "core" / "data"

	game_json = load(data / "game.json")
	game_json["prices"]["host_charm"] = 5
	save(data / "game.json", game_json)

	tidewater_file = data / "areas" / "tidewater.json"
	tidewater = load(tidewater_file)
	tidewater["exits"][0]["fires"] = "always"
	tidewater["shops"][0]["voice"] = "voice.host_stall"
	save(tidewater_file, tidewater)

	for language in game_json["languages"]:
		table_file = data / language["text"]
		table = load(table_file)
		table["text"].update(STRINGS[language["code"]])
		table["text"]["desc.item.tonic"] = re.sub(r"\. ", ".{page}", table["text"]["desc.item.tonic"], count=1)
		table["text"]["game.discarded"] = "{wait:100}" + table["text"]["game.discarded"]
		save(table_file, table)
		tag_strings(data / language["strings"])

	items_file = core_data / "items.json"
	items = load(items_file)
	items["items"].append({"id": "host_charm", "name": "Host Charm", "target": "self", "discardable": False})
	if consume:
		next(i for i in items["items"] if i["id"] == "tonic")["consume_when_no_effect"] = True
	save(items_file, items)
	if consume:
		rules = load(core_data / "rules.json")
		rules["item_no_reach"] = "consume"
		save(core_data / "rules.json", rules)


if __name__ == "__main__":
	main()
