#!/usr/bin/env python3
"""Writes the asset directory scripts/late_flows.txt plays: a copy of MORE (make_more_flows_assets.py's directory) with
the items docs/ui-layout.md describes under "Late layout flows":

* load_game: the grid's blocks also show each save's money and each member's hit and technique points with their
  maxima (`money`, `hp`, `hp_max`, `tp`, `tp_max`, empty for a save that lacks them) and the hint names the grid's four
  directions (`navigation_hint` 1); alt/load_row.json the same in one row, its hint naming Left and Right;
* travel: over the menu with the open page kept under the list and back as it was after a cancel (`over_page` 1);
* menu_items: Confirm on an item opens its actions (`actions` 1) in a window of their own, which a travel list leaves
  drawn and open;
* battle_front: a defeat shown in a `defeat` step and the result window with `ui.battle.result.defeat` naming the member
  who fell last (`defeat_message`), no outcome banner; alt/battle_quiet.json the same with `game_over_confirm` 0 and a
  timed defeat step, and alt/styles_return.json the styles with `game_over_return_ms` 1500;
* dialogue (alt/dialogue_hold.json): a press never completes a page and holding Confirm reveals it at 200 characters a
  second (`reveal_confirm` "hold", `reveal_hold_cps`), and the box's opening plays `brass_alarm` (`dialogue_open_sound`);
* broken copies' files under alt/: battle_broken.json (`defeat_message` that is no text key), which the Makefile checks
  on a copy.

Usage: make_late_flows_assets.py EXPORT MORE OUT
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


def walk(elements, visit):
	for e in elements:
		visit(e)
		walk(e.get("elements", []), visit)
		walk(e.get("template", []), visit)


def load_game(base):
	data = json.loads(json.dumps(base))
	data["params"]["navigation_hint"] = 1

	def visit(e):
		if e.get("type") == "group" and e.get("visible") == "!empty && !damaged":
			e["elements"][0]["value"] = "area + '  ' + play_time + (money != '' ? '  ' + money + ' M' : '')"
		if e.get("type") == "repeater" and e.get("items") == "members":
			member = e["template"][0]
			sprite = member["elements"][0]["w"]
			member["elements"].append({"type": "text", "visible": "hp != ''", "value": "hp + '/' + hp_max", "x": sprite + 2, "y": sprite // 4 + 12, "style": "small"})
			member["elements"].append({"type": "text", "visible": "tp != ''", "value": "tp + '/' + tp_max", "x": sprite + 2, "y": sprite // 4 + 24, "style": "small_dim"})
	for w in data["windows"]:
		walk(w.get("elements", []), visit)
	return data


def menu_items(base):
	data = json.loads(json.dumps(base))
	data["params"] = dict(data.get("params", {}), actions=1)
	data["windows"].append({"name": "actions", "visible": "acting", "x": "area_x + area_w - 300", "y": "area_y + 20", "w": 260, "h": 110, "z": 5, "frame": "panel",
		"elements": [{"type": "list", "items": "actions", "state": "action_list", "w": "cw", "h": "ch", "active": "acting && !looking"}]})
	return data


def battle(base, quiet):
	data = json.loads(json.dumps(base))
	data["params"]["defeat_message"] = "ui.battle.result.defeat"
	data["result_sequence"] = [{"phase": "victory", "ms": 600}, {"phase": "defeat", "ms": 700 if quiet else 0}, {"phase": "result"}, {"phase": "level_ups"}]
	if quiet:
		data["params"]["game_over_confirm"] = 0
	data["windows"].append({"name": "defeat", "visible": "result_phase == 'defeat'", "anchor": "center", "w": "W * 3 / 4", "h": 108, "frame": "battle",
		"padding": [24, 24, 24, 24], "elements": [{"type": "text", "value": "defeat_text", "w": "cw", "align": "center", "wrap": True, "lines": 2}]})
	return data


def main():
	export, more, out = (Path(a) for a in sys.argv[1:4])
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(more, out)
	ui = out / "ui"
	save(ui / "load_game.json", load_game(load(ui / "load_game.json")))
	save(out / "alt/load_row.json", load_game(load(out / "alt/load_row.json")))
	travel = load(ui / "travel.json")
	travel["params"]["over_page"] = 1
	save(ui / "travel.json", travel)
	save(ui / "menu_items.json", menu_items(load(export / "ui/menu_items.json")))
	base_battle = load(ui / "battle_front.json")
	save(ui / "battle_front.json", battle(base_battle, False))
	save(out / "alt/battle_quiet.json", battle(base_battle, True))
	styles = load(ui / "styles.json" if (ui / "styles.json").exists() else export / "ui/styles.json")
	styles["timings"] = dict(styles.get("timings", {}), game_over_return_ms=1500)
	save(out / "alt/styles_return.json", styles)
	dialogue = load(ui / "dialogue.json" if (ui / "dialogue.json").exists() else export / "ui/dialogue.json")
	dialogue["params"] = dict(dialogue.get("params", {}), reveal_confirm="hold", reveal_hold_cps=200, dialogue_open_sound="brass_alarm")
	save(out / "alt/dialogue_hold.json", dialogue)
	broken = battle(base_battle, False)
	broken["params"]["defeat_message"] = "Fell Last!"
	save(out / "alt/battle_broken.json", broken)


if __name__ == "__main__":
	main()
