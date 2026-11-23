#!/usr/bin/env python3
"""Writes the asset directory arrival_hold.txt plays: a copy of BASE (the test assets) with the built-in layouts of
EXPORT changed as docs/ui-layout.md describes under "The title's music after its intro, events through the map load hold":

* title: an `intro` of one step (`studio`, 600 ms) and `intro_end_music` "victory_bright_sail", the track that takes over
  once the intro ends or is skipped; alt/title_stop.json the same with "stop", alt/title_broken.json with a value that is
  no track id, which the Makefile checks on copies;
* field: `map_hold_events` "wait", so an event waits through the map load hold and its fade-in.

Usage: make_arrival_hold_assets.py EXPORT BASE OUT
"""

import json
import shutil
import sys
from pathlib import Path


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def main():
	export, base, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(base, out)
	ui = export / "ui"

	title = load(ui / "title.json")
	title["intro"] = [{"name": "studio", "ms": 600}]
	title["params"] = dict(title.get("params", {}), intro_end_music="victory_bright_sail")
	for window in title["windows"]:
		window["visible"] = f"({window['visible']}) && !intro" if "visible" in window else "!intro"
	title["windows"].append({"name": "intro", "visible": "intro", "z": 10, "elements": [
		{"type": "text", "value": "'A test studio'", "y": "H / 2 - 10", "w": "W", "align": "center"}]})
	save(out / "ui" / "title.json", title)
	title["params"]["intro_end_music"] = "stop"
	save(out / "alt" / "title_stop.json", title)
	title["params"]["intro_end_music"] = "Victory Sail"
	save(out / "alt" / "title_broken.json", title)

	field = load(ui / "field.json")
	field["params"] = dict(field.get("params", {}), map_hold_events="wait")
	save(out / "ui" / "field.json", field)


if __name__ == "__main__":
	main()
