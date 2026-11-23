#!/usr/bin/env python3
"""Writes the asset directory scripts/options_flows.txt plays (docs/ui-layout.md, "The system page's picker, speed levels
and macro slots"): a copy of BASE (the test assets, or with --small the small set at 320x224) whose layouts, taken from
BASE where it has its own and from EXPORT's built-in files otherwise, are changed so:

* menu: the entries Status, Items and System;
* menu_system: the rows save, text speed, battle speed, controls and quit (`rows`), labels without values
  (`row_value` "hide") in a framed page half the area wide, and a settings row's levels in a picker window beside it
  (`settings` "picker", `picker_rows`, `picker_list`, `picker_title`), its prompt in the page's message line; Confirm
  returns to the rows (`picker_confirm` "rows"); alt/menu_system_close.json closes the menu instead;
* battle_front: eight lettered macro slots (`macro_slots`), drawn in a window of their own in place of the
  command window while the macro list is up, each row colored by its state (`option_states`: `empty` dim, `set` good),
  and the highlighted macro's steps in a window beside it (`macro_lines`);
* broken copies' files under alt/: menu_system_broken.json (a `settings` word that is no word of it and a `rows` entry
  that is no row) and battle_broken.json (nine macro slots), which the Makefile checks on copies.

With --hold SET OUT it instead copies SET to OUT with alt/dialogue_hold.json: SET's dialogue file with `reveal_confirm`
"hold", `reveal_hold_cps` 200 and `dialogue_open_sound` "brass_alarm" (the reveal hold runs, reveal_hold_*.txt).

Usage: make_options_assets.py EXPORT BASE OUT [--small]
       make_options_assets.py --hold SET OUT
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


def hold(source, out):
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(source, out)
	dialogue = load(out / "ui" / "dialogue.json") if (out / "ui" / "dialogue.json").exists() else load(Path(sys.argv[0]).parent.parent / "ui" / "dialogue.json")
	dialogue["params"] = dict(dialogue.get("params", {}), reveal_confirm="hold", reveal_hold_cps=200, dialogue_open_sound="brass_alarm")
	save(out / "alt" / "dialogue_hold.json", dialogue)


def main():
	if sys.argv[1] == "--hold":
		hold(Path(sys.argv[2]), Path(sys.argv[3]))
		return
	export, base, out = (Path(a) for a in sys.argv[1:4])
	small = "--small" in sys.argv[4:]
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(base, out)

	def layout(name):
		own = base / "ui" / f"{name}.json"
		return load(own if own.exists() else export / "ui" / f"{name}.json")

	menu = layout("menu")
	menu["entries"] = [{"page": "status"}, {"page": "items"}, {"page": "system"}]
	save(out / "ui" / "menu.json", menu)

	row_h = "t(size_small)" if small else "t(30)"
	row_style = "small" if small else "body"
	pad = 6 if small else 20
	system = {
		"version": 1,
		"params": {"settings": "picker", "picker_confirm": "rows", "row_value": "hide"},
		"rows": ["save", "text_speed", "battle_speed", "controls", "quit"],
		"windows": [
			{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w / 2", "h": "area_h", "frame": "panel", "padding": [pad, pad, pad, pad], "elements": [
				{"type": "list", "items": "rows", "state": "list", "active": "!picking", "w": "cw", "h": f"ch - 2 * {row_h}", "row_height": row_h, "row": [
					{"type": "fill", "visible": "selected", "x": -4, "y": -2, "w": "cw - 4", "h": f"{row_h} - 2", "color": "row"},
					{"type": "text", "value": "label", "x": 4, "style": row_style, "color": "selected ? 'accent' : 'text'"}]},
				{"type": "text", "value": "description", "x": 4, "y": f"ch - 2 * {row_h}", "wrap": True, "max_width": "cw - 8", "lines": 2,
					"line_height": row_h if small else "t(size_small + 4)", "style": "small_dim"}]},
			{"name": "picker", "visible": "picking", "x": "area_x + area_w / 2 + 4", "y": "area_y", "w": "area_w / 2 - 4",
				"h": f"len(picker_rows) * {row_h} + {row_h} + {2 * pad}", "frame": "panel", "padding": [pad, pad, pad, pad], "elements": [
				{"type": "text", "value": "picker_title", "x": 4, "style": "small_dim"},
				{"type": "list", "items": "picker_rows", "state": "picker_list", "active": "picking", "y": row_h, "w": "cw", "h": f"ch - {row_h}", "row_height": row_h, "row": [
					{"type": "fill", "visible": "selected", "x": -4, "y": -2, "w": "cw - 4", "h": f"{row_h} - 2", "color": "row"},
					{"type": "text", "value": "text", "x": 4, "style": row_style, "color": "selected ? 'accent' : current ? 'good' : 'text'"}]}]}]}
	save(out / "ui" / "menu_system.json", system)
	close = json.loads(json.dumps(system))
	close["params"]["picker_confirm"] = "close"
	save(out / "alt" / "menu_system_close.json", close)
	broken = json.loads(json.dumps(system))
	broken["params"]["settings"] = "wheel"
	broken["rows"] = ["save", "volume"]
	save(out / "alt" / "menu_system_broken.json", broken)

	slot_w = 120 if small else 220
	line_w = 170 if small else 420
	step = "t(size_small + 2)" if small else "t(24)"
	for name in ("battle_front",):
		battle = layout(name)
		battle["macro_slots"] = ["A", "B", "C", "D", "E", "F", "G", "H"]
		battle["option_states"] = {"empty": "dim", "set": "good"}
		for w in battle["windows"]:
			if w["name"] == "command":
				w["visible"] = f"({w['visible']}) && list_kind != 'macro'"
		at = next(i for i, w in enumerate(battle["windows"]) if w["name"] == "command") + 1
		battle["windows"][at:at] = [
			{"name": "slots", "visible": "menu_shown && list_kind == 'macro'", "x": 8, "y": "strip.y - h - 4", "w": slot_w,
				"h": f"len(menu_items) * {step} + {8 if small else 16}", "frame": "battle", "elements": [
				{"type": "repeater", "items": "menu_items", "x": 14, "y": 4 if small else 8, "dy": step, "template": [
					{"type": "pointer", "visible": "i == cursor", "x": -12, "y": 0},
					{"type": "text", "value": "slot_label", "style": "small", "color": "i == cursor ? 'accent' : 'text'"},
					{"type": "text", "value": "state == 'set' ? macro : '-'", "x": 16 if small else 24, "style": "small",
						"color": "i == cursor ? 'accent' : state_color != '' ? state_color : 'text'"}]}]},
			{"name": "macro_detail", "visible": "menu_shown && list_kind == 'macro'", "x": f"8 + {slot_w} + 6", "y": "slots.y", "w": line_w,
				"h": f"max(1, len(macro_lines)) * {step} + 16", "frame": "battle", "elements": [
				{"type": "text", "visible": "len(macro_lines) == 0", "value": "'-'", "x": 10, "y": 8, "style": "small_dim"},
				{"type": "repeater", "items": "macro_lines", "x": 10, "y": 8, "dy": step, "template": [
					{"type": "text", "value": "fit(text, cw - 20, size_small)", "style": "small"}]}]}]
		save(out / "ui" / f"{name}.json", battle)
		broken = json.loads(json.dumps(battle))
		broken["macro_slots"] = ["A", "B", "C", "D", "E", "F", "G", "H", "I"]
		save(out / "alt" / "battle_broken.json", broken)


if __name__ == "__main__":
	main()
