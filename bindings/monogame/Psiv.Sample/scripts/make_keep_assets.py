#!/usr/bin/env python3
"""Writes the asset directory scripts/keep_flows.txt plays: a copy of BASE (the test assets) with the built-in layouts of
EXPORT changed as docs/ui-layout.md describes under "The save flow, travel messages and kept choices":

* menu: the entries Items, Techniques and System;
* save_slots: the slot list in a framed window over the right of the system page, whose rows stay drawn under it
  (`over_page` 1), and the steps slots, confirm and saved (`flow`) in a message window of its own (`message`,
  `confirm_options`, `confirm_cursor`);
* travel: no notice for a travel use (`used_toast` 0) and a refusal on the page it came from (`refusal` "page");
* menu_items, menu_abilities: the last use's outcome under the page (`result_text`);
* dialogue: each option's row colored by its state (`option_states`: `new` good, `plain` dim) and the kept choice in a
  window above the box, its picked row marked (`kept_visible`, `kept_prompt`, `kept_rows`, `kept_cursor`);
* broken copies' files under alt/: save_slots_broken.json (a `flow` without `slots`) and dialogue_broken.json (an
  `option_states` color that is no color), which the Makefile checks on copies.

Usage: make_keep_assets.py EXPORT BASE OUT
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


RESULT = {"type": "text", "visible": "result_text != ''", "value": "result_text", "x": 14, "y": "ch - t(20)", "style": "small", "color": "result_ok ? 'good' : 'bad'"}


def main():
	export, base, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(base, out)
	ui = export / "ui"

	menu = load(ui / "menu.json")
	menu["entries"] = [{"page": "items"}, {"page": "techniques"}, {"page": "system"}]
	save(out / "ui" / "menu.json", menu)

	slots = load(ui / "save_slots.json")
	slots["flow"] = ["slots", "confirm", "saved"]
	slots["params"] = {"over_page": 1}
	window = slots["windows"][0]
	window.update({"x": "area_x + area_w / 3", "w": "area_w * 2 / 3", "frame": "panel", "padding": [20, 20, 20, 20]})
	slots["windows"].append({"name": "message", "visible": "flow_step == 'confirm' || flow_step == 'saved'", "anchor": "center", "w": 560, "h": "flow_step == 'confirm' ? t(120) : t(76)",
		"frame": "panel", "padding": [24, 24, 24, 24], "elements": [
			{"type": "text", "value": "message", "w": "cw", "wrap": True, "lines": 2, "color": "message_ok ? 'text' : 'bad'"},
			{"type": "rows", "visible": "flow_step == 'confirm'", "items": "confirm_options", "selected": "confirm_cursor", "x": 20, "y": "t(40)", "row_height": "t(28)"}]})
	save(out / "ui" / "save_slots.json", slots)
	broken = json.loads(json.dumps(slots))
	broken["flow"] = ["confirm", "saved"]
	save(out / "alt" / "save_slots_broken.json", broken)

	travel = load(ui / "travel.json")
	travel["params"] = {"used_toast": 0, "refusal": "page"}
	save(out / "ui" / "travel.json", travel)

	items = load(ui / "menu_items.json")
	items["windows"][0]["elements"].append(RESULT)
	save(out / "ui" / "menu_items.json", items)
	abilities = load(ui / "menu_abilities.json")
	next(w for w in abilities["windows"] if w["name"] == "abilities")["elements"].append(dict(RESULT, color="'bad'"))
	save(out / "ui" / "menu_abilities.json", abilities)

	dialogue = load(ui / "dialogue.json")
	dialogue["option_states"] = {"new": "good", "plain": "dim"}
	box = next(w for w in dialogue["windows"] if w["name"] == "box")
	for e in box["elements"]:
		if e.get("type") == "group" and e.get("visible") == "has_choice":
			rows = next(r for r in e["elements"] if r.get("type") == "rows")
			e["elements"].remove(rows)
			e["elements"].append({"type": "repeater", "items": "option_rows", "x": rows["x"], "y": rows["y"], "dy": rows["row_height"], "template": [
				{"type": "pointer", "visible": "i == cursor", "x": -22, "y": 2},
				{"type": "text", "value": "text", "color": "i == cursor ? 'accent' : state_color != '' ? state_color : 'text'"},
				{"type": "text", "visible": "state == 'new'", "value": "'*'", "x": 360, "color": "'good'"}]})
	kept = {"name": "kept", "visible": "kept_visible", "x": "W - 404", "w": 380, "h": "len(kept_rows) * t(30) + t(60)",
		"y": "top > H / 2 ? top - h - 12 : top + t(176) + 12", "frame": "panel", "elements": [
			{"type": "text", "value": "kept_prompt", "x": 20, "y": "t(12)", "style": "small_dim"},
			{"type": "repeater", "items": "kept_rows", "x": 44, "y": "t(42)", "dy": "t(30)", "template": [
				{"type": "pointer", "visible": "picked", "x": -22, "y": 2},
				{"type": "text", "value": "text", "color": "picked ? 'accent' : state_color != '' ? state_color : 'text'"}]}]}
	dialogue["windows"].insert(dialogue["windows"].index(box), kept)
	save(out / "ui" / "dialogue.json", dialogue)
	broken = json.loads(json.dumps(dialogue))
	broken["option_states"] = {"new": "bright pink"}
	save(out / "alt" / "dialogue_broken.json", broken)


if __name__ == "__main__":
	main()
