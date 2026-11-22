#!/usr/bin/env python3
"""Writes the asset directory scripts/flows.txt plays (and with --small the one small_flows.txt plays): a copy of BASE
with layout files that use the flow items docs/ui-layout.md describes ("Layout flows"):

* menu: `navigation` "list": the entries in a list chosen with Up, Down and Confirm, the page under it only once
  confirmed; a System entry with children; menu_system: the System action (`system_action` 1); extras: the Quests page
  (an entry) and the Codex (not one);
* menu_items: `stack_rows` "single" (one row per copy), each row's `name` with its `count_text` at the right
  (`count_format` "x{0}", `count_format_one` ""), the target choice headed by the item and its count;
* shop: `flow` welcome, mode, list, confirm, result, farewell, the keeper's line over each step, the rows' counts held;
* inn: `flow` welcome, price, choice, result, farewell, the step and the keeper's line in the box;
* title: an `intro` of two steps before the start state, and `after_defeat` "intro";
* travel (not with --small): `one_destination` "go", no Cancel row (`cancel_row` 0), the destinations as plain names and
  regions, a message before leaving and after arriving (`leave_key`, `arrive_key`); alt/travel_timed.json the same with
  `message_ms` 400;
* battle_front: `command_open_delay` 600, a `result_sequence` of a victory banner alone (800 ms), the result window, then
  the level-ups (until Confirm), each a window shown by `result_phase`, and item rows' `count_text`;
* battle_front_vehicle (not with --small): the vehicle battle's own front layout: the command window at the top right,
  an outline frame in teal and the commands in teal;
* alt/menu_items_stacked.json: the same Items page with `stack_rows` "stacked"; alt/title_broken.json (a bad
  `after_defeat`, an intro step of 0 ms, one listed twice and one without a name), alt/title_no_intro.json
  (`after_defeat` "intro" without an `intro`) and alt/menu_broken.json (a bad `navigation`), which the Makefile checks
  on copies.

Usage: make_flows_assets.py EXPORT BASE OUT [--small]

EXPORT is the placeholder export (its ui/ files are the built-in layouts), BASE the asset directory to copy (the test
assets, or with --small the small example set, whose own battle file the battle layout extends).
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


def metrics(small):
	"""Row height, padding, the count column's room, the text styles, the keeper's box and whether windows carry title
	plates, at either resolution."""
	if small:
		return {"rh": 11, "pad": 4, "count_x": 40, "body": "small", "big": "small", "keeper_h": 30, "titled": False}
	return {"rh": 30, "pad": 24, "count_x": 120, "body": "body", "big": "title", "keeper_h": 96, "titled": True}


def titled(m):
	"""The window's title plate, its `title` field, where the plates fit."""
	return {"title": {"value": "title"}} if m["titled"] else {}


def menu(m):
	rh, pad = m["rh"], m["pad"]
	return {"version": 1, "params": {"navigation": "list", "backdrop_dim": 0.4},
		"entries": [{"page": "items"}, {"page": "status"}, {"page": "system", "children": [{"page": "save"}, {"page": "options"}]}, {"page": "quests"}],
		"windows": [
			{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
			{"name": "entries", "x": pad // 2, "y": pad // 2, "w": "W / 4", "h": f"len(tabs) * {rh} + {2 * pad}", "frame": "panel", "padding": [pad, pad, pad, pad], "elements": [
				{"type": "rows", "items": "tabs", "selected": "list_cursor", "x": pad // 2, "row_height": rh, "height": rh - 2, "selected_color": "page_open ? 'dim' : 'accent'"}]},
			{"name": "money", "x": pad // 2, "y": f"entries.bottom + {pad // 2}", "w": "W / 4", "h": rh + pad, "frame": "panel", "padding": [pad, pad // 2, pad, 0], "elements": [
				{"type": "text", "value": "money_value", "format": "N0", "style": m["body"]}]},
			{"name": "preview", "visible": "!page_open", "x": f"W / 4 + {pad}", "y": pad // 2, "w": f"W - W / 4 - {pad + pad // 2}", "h": rh + pad, "frame": "panel",
				"padding": [pad, pad // 2, pad, 0], "elements": [
				{"type": "text", "value": "'Open ' + entry_label + '?'", "color": "dim", "style": m["body"]}]},
			{"name": "box", "visible": "page_open", "x": f"W / 4 + {pad}", "y": pad // 2, "w": f"W - W / 4 - {pad + pad // 2}", "h": f"H - {pad + rh + pad // 2}", "frame": "panel",
				"elements": [{"type": "slot", "slot": "page", "x": pad, "y": pad, "w": f"cw - {2 * pad}", "h": f"ch - {2 * pad}"}]},
			{"name": "hint", "x": 0, "y": f"H - {rh + pad // 2}", "h": rh + pad // 2, "elements": [
				{"type": "fill", "w": "W", "h": rh + pad // 2, "color": "black", "alpha": 0.6},
				{"type": "hints", "value": "hint", "x": pad // 2, "y": 2, "w": f"W - {pad}"}]},
			{"name": "toasts", "elements": [{"type": "slot", "slot": "toasts", "y": f"H - {rh + pad}", "w": f"W - {pad}", "h": 0}]},
		]}


def menu_items(m, stack):
	rh = m["rh"]
	return {"version": 1, "params": {"stack_rows": stack, "count_format": "x{0}", "count_format_one": ""}, "windows": [
		{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			{"type": "text", "value": "item_count + ' kinds, ' + item_total + ' in all'", "w": "cw", "align": "right", "style": "small_dim"},
			{"type": "list", "items": "item_rows", "state": "list", "y": rh, "w": "picking ? cw / 2 : cw", "h": f"ch - {3 * rh}", "row_height": rh, "active": "!picking",
				"row": [
				{"type": "fill", "visible": "selected", "x": -2, "y": -2, "w": f"(picking ? area_w / 2 : area_w) - 8", "h": rh - 2, "color": "row"},
				{"type": "text", "value": "name", "x": 4, "color": "selected ? 'accent' : 'text'", "style": m["body"]},
				{"type": "text", "value": "count_text", "w": f"(picking ? area_w / 2 : area_w) - 16", "align": "right", "color": "dim", "style": m["body"]}]},
			{"type": "text", "value": "description", "y": f"ch - {2 * rh}", "w": "cw", "wrap": True, "lines": 2, "style": "small"}]},
		{"name": "target", "visible": "picking", "x": "area_x + area_w / 2 + 4", "y": "area_y", "w": "area_w / 2 - 4", "h": f"len(member_rows) * {rh} + {3 * rh}",
			"frame": "panel", "padding": [m["pad"] // 2, m["pad"] // 2, m["pad"] // 2, 0], "elements": [
			{"type": "text", "value": "target_name + (target_count_text != '' ? ' ' + target_count_text : '')", "color": "accent", "style": m["body"]},
			{"type": "list", "items": "member_rows", "state": "member_list", "enabled": "reachable", "y": rh + 4, "w": "cw", "h": f"len(member_rows) * {rh} + 4", "row_height": rh,
				"height": rh - 2, "size": 16 if rh < 20 else 20}]},
	]}


def shop(m):
	rh, pad = m["rh"], m["pad"]
	return {"version": 1, "flow": ["welcome", "mode", "list", "confirm", "result", "farewell"],
		"params": {"count_format": "x{0}", "count_format_one": "one", "backdrop_dim": 0.4}, "windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "keeper", "visible": "open", "x": pad // 2, "y": pad // 2, "w": f"W - {pad}", "h": m["keeper_h"], "frame": "panel", "padding": [pad, pad // 2, pad, pad // 2],
			"elements": [
			{"type": "text", "value": "voice_line", "w": f"cw - {m['count_x']}", "wrap": True, "lines": 2, "style": m["body"]},
			{"type": "text", "value": "money", "w": "cw", "align": "right", "style": m["body"]},
			{"type": "text", "visible": m["titled"], "value": "flow_step", "y": m["keeper_h"] - pad - rh // 2, "w": "cw", "align": "right", "style": "small_dim"}]},
		{"name": "mode", "visible": "open && flow_step == 'mode'", "x": pad // 2, "y": f"keeper.bottom + {pad // 2}", "w": "W / 3", "h": f"2 * {rh} + {2 * pad}", "frame": "panel",
			"padding": [pad, pad, pad, pad], "elements": [{"type": "rows", "items": "choices", "selected": "choice_cursor", "row_height": rh, "height": rh - 2}]},
		{"name": "list", "visible": "open && (flow_step == 'list' || flow_step == 'confirm' || flow_step == 'result')", "x": pad // 2, "y": f"keeper.bottom + {pad // 2}",
			"w": f"W - {pad}", "h": f"H - keeper.bottom - {pad}", "frame": "panel", **titled(m), "padding": [pad, pad, pad, pad], "elements": [
			{"type": "list", "items": "rows", "state": "list", "probe": False, "w": "cw", "h": f"ch - {2 * rh}", "row_height": rh, "active": "flow_step == 'list'", "row": [
				{"type": "fill", "visible": "selected", "x": -2, "y": -2, "w": "cw - 4", "h": rh - 2, "color": "row"},
				{"type": "text", "value": "name", "x": 4, "color": "selected ? 'accent' : 'text'", "style": m["body"]},
				{"type": "text", "value": "price", "w": f"cw - {m['count_x'] + 8}", "align": "right", "style": m["body"]},
				{"type": "text", "value": "count_text", "w": "cw - 8", "align": "right", "color": "dim", "style": m["body"]}]}]},
		{"name": "confirm", "visible": "open && flow_step == 'confirm'", "anchor": "center", "w": "W / 3", "h": f"2 * {rh} + {2 * pad}", "z": 5, "frame": "panel",
			"padding": [pad, pad, pad, pad], "elements": [{"type": "rows", "items": "confirm_options", "selected": "confirm_cursor", "row_height": rh, "height": rh - 2}]},
		{"name": "result", "visible": "open && flow_step == 'result'", "anchor": "center", "w": "W / 2", "h": f"2 * {rh} + {2 * pad}", "z": 5, "frame": "panel",
			"padding": [pad, pad, pad, pad], "elements": [
			{"type": "text", "value": "result_text", "w": "cw", "wrap": True, "lines": 2, "color": "result_ok ? 'text' : 'bad'", "style": m["body"]}]},
		{"name": "toasts", "elements": [{"type": "slot", "slot": "toasts", "x": pad, "y": f"H - {pad}"}]},
	]}


def inn(m):
	rh, pad = m["rh"], m["pad"]
	return {"version": 1, "flow": ["welcome", "price", "choice", "result", "farewell"], "windows": [
		{"name": "box", "x": pad // 2, "y": "top", "w": f"W - {pad}", "h": f"4 * {rh} + {2 * pad}", "frame": "panel", **titled(m),
			"padding": [pad, pad, pad, pad], "elements": [
			{"type": "text", "value": "voice_line", "w": "cw", "wrap": True, "lines": 2, "style": m["body"]},
			{"type": "text", "visible": "step == 'price'", "key": "ui.inn.cost", "args": ["cost", "money"], "y": 2 * rh, "style": "small_dim"},
			{"type": "rows", "visible": "step == 'choice'", "items": "options", "selected": "cursor", "x": rh, "y": 2 * rh, "row_height": rh, "height": rh - 2},
			{"type": "text", "value": "step", "y": 3 * rh, "w": "cw", "align": "right", "style": "small_dim"}]},
	]}


def title(m, export_title):
	rh, pad = m["rh"], m["pad"]
	data = dict(export_title)
	data["start"] = True
	data["intro"] = [{"name": "studio", "ms": 600}, {"name": "story", "ms": 900}]
	data["params"] = dict(data.get("params", {}), after_defeat="intro")
	windows = [dict(w) for w in data["windows"]]
	for w in windows:
		if w["name"] == "box":
			w["visible"] = "!start && !intro"
	windows += [
		{"name": "intro", "visible": "intro", "z": 10, "elements": [
			{"type": "fill", "w": "W", "h": "H", "color": "black"},
			{"type": "text", "visible": "intro_step == 'studio'", "value": "'A test studio'", "y": "H / 2 - 10", "w": "W", "align": "center", "style": m["big"],
				"alpha": "min(1, intro_ms * 3.0 / max(1, intro_step_ms))"},
			{"type": "text", "visible": "intro_step == 'story'", "value": "'Long ago, a test began.'", "y": "H / 2 - 10", "w": "W", "align": "center", "color": "accent",
				"style": m["body"]},
			{"type": "bar", "y": f"H - {rh}", "w": "W", "h": 3, "value": "intro_ms", "max": "max(1, intro_step_ms)", "color": "accent"}]},
		{"name": "press", "visible": "start", "anchor": "center", "w": "W / 2", "h": 2 * rh + 2 * pad, "frame": "panel", "z": 9, "elements": [
			{"type": "text", "value": "start_hint", "y": pad, "w": "cw", "align": "center", "style": m["body"]}]},
	]
	data["windows"] = windows
	return data


def travel():
	return {"version": 1, "params": {"one_destination": "go", "cancel_row": 0, "leave_key": "ui.travel.leaving", "arrive_key": "ui.travel.arrived", "backdrop_dim": 0.3},
		"windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "box", "visible": "phase == 'list'", "x": 60, "y": 80, "w": 520, "h": "len(destinations) * 34 + 90", "frame": "panel", "title": {"value": "title"},
			"padding": [24, 20, 24, 20], "elements": [
			{"type": "text", "value": "question", "color": "accent"},
			{"type": "list", "items": "destinations", "state": "list", "y": 40, "w": "cw", "h": "len(destinations) * 34", "row_height": 34, "row": [
				{"type": "fill", "visible": "selected", "x": -4, "y": -3, "w": "cw - 4", "h": 30, "color": "row"},
				{"type": "text", "value": "name", "x": 8, "color": "selected ? 'accent' : 'text'"},
				{"type": "text", "value": "region", "w": "cw - 16", "align": "right", "style": "small_dim"}]}]},
		{"name": "message", "visible": "phase != 'list'", "x": 120, "y": "H - 200", "w": "W - 240", "h": 110, "frame": "panel", "padding": [28, 24, 28, 24], "elements": [
			{"type": "text", "value": "message", "w": "cw", "wrap": True, "lines": 2},
			{"type": "text", "value": "phase", "y": 40, "w": "cw", "align": "right", "style": "small_dim"}]},
		{"name": "toasts", "elements": [{"type": "slot", "slot": "toasts", "x": 48, "y": "H - 92"}]},
	]}


def battle(m, base):
	rh, pad = m["rh"], m["pad"]
	data = dict(base)
	data["params"] = dict(data.get("params", {}), command_open_delay=600, count_format="x{0}")
	data["result_sequence"] = [{"phase": "victory", "ms": 800}, {"phase": "result"}, {"phase": "level_ups"}]
	windows = [dict(w) for w in data["windows"]]
	for w in windows:
		if w["name"] == "result":
			w["visible"] = "result_phase == 'result'"
	frame = "battle"
	windows += [
		{"name": "victory", "visible": "result_phase == 'victory'", "anchor": "center", "w": "W / 2", "h": 2 * rh + 2 * pad, "frame": frame, "elements": [
			{"type": "text", "value": "outcome", "y": pad, "w": "cw", "align": "center", "style": m["big"]}]},
		{"name": "levels", "visible": "result_phase == 'level_ups'", "anchor": "center", "w": "W * 2 / 3", "h": f"len(level_lines) * {rh} + {2 * pad + rh}", "frame": frame,
			"padding": [pad, pad, pad, pad], "elements": [
			{"type": "text", "value": "'Level up'", "color": "accent", "style": m["body"]},
			{"type": "repeater", "items": "level_lines", "y": rh, "dy": rh, "template": [
				{"type": "text", "value": "text", "max_width": "cw", "color": "kind == 'learned' ? 'good' : 'text'", "style": m["body"]}]}]},
	]
	data["windows"] = windows
	return data


def vehicle_battle(base):
	"""The front layout for vehicle battles: the command window moved to the top right, an outline frame and teal rows."""
	data = json.loads(json.dumps(base))
	for w in data["windows"]:
		if w["name"] == "command":
			w["x"] = "W - w - 16"
			w["y"] = 16
			w["frame"] = "vehicle"
			for e in w["elements"]:
				if e.get("type") == "rows":
					e["selected_color"] = "#5ccfc0"
		if w["name"] in ("strip", "result"):
			w["frame"] = "vehicle"
	return data


def main():
	export, base, out = (Path(a) for a in sys.argv[1:4])
	small = "--small" in sys.argv[4:]
	m = metrics(small)
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(base, out)
	ui, built_in = out / "ui", export / "ui"
	save(ui / "menu.json", menu(m))
	system = load(built_in / "menu_system.json")
	save(ui / "menu_system.json", dict(system, params=dict(system.get("params", {}), system_action=1)))
	extras = load(built_in / "extras.json")
	save(ui / "extras.json", dict(extras, entries=[{"page": "quests"}, {"page": "codex"}, {"page": "options"}]))
	save(ui / "menu_items.json", menu_items(m, "single"))
	save(out / "alt/menu_items_stacked.json", menu_items(m, "stacked"))
	save(ui / "shop.json", shop(m))
	save(ui / "inn.json", inn(m))
	own_title = ui / "title.json" if (ui / "title.json").exists() else built_in / "title.json"
	save(ui / "title.json", title(m, load(own_title)))
	own_battle = ui / "battle_front.json" if (ui / "battle_front.json").exists() else built_in / "battle_front.json"
	save(ui / "battle_front.json", battle(m, load(own_battle)))
	broken_title = title(m, load(own_title))
	broken_title["params"]["after_defeat"] = "credits"
	broken_title["intro"] = [{"name": "studio", "ms": 0}, {"name": "studio", "ms": 500}, {"ms": 500}]
	save(out / "alt/title_broken.json", broken_title)
	no_intro = title(m, load(own_title))
	del no_intro["intro"]
	save(out / "alt/title_no_intro.json", no_intro)
	save(out / "alt/menu_broken.json", dict(menu(m), params={"navigation": "grid"}))
	if not small:
		save(ui / "travel.json", travel())
		timed = travel()
		timed["params"]["message_ms"] = 400
		save(out / "alt/travel_timed.json", timed)
		save(ui / "battle_front_vehicle.json", vehicle_battle(load(built_in / "battle_front.json")))
		styles = load(ui / "styles.json") if (ui / "styles.json").exists() else load(built_in / "styles.json")
		styles["frames"]["vehicle"] = {"type": "outline", "color": "#5ccfc0", "thickness": 3}
		save(ui / "styles.json", styles)


if __name__ == "__main__":
	main()
