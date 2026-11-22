#!/usr/bin/env python3
"""Writes the asset directory the host-call runs play (host_menus.txt, host_tags.txt, host_reach.txt): a copy of the test
assets with layout files built from the export's built-in ones and the fields docs/ui-layout.md describes:

* menu: Items, Talk, a Journal tab (`quests`, `immediate`) whose children are the quest log, the codex hidden by `when`
  and the text log disabled by `enabled`, so the quest log opens at once; a Records tab (`codex`) listing the codex, the
  text log disabled and the save page hidden; then Status, Techniques, Save and Options;
* menu_items: an item's actions with `discard`, Discard's count and confirm in a window (`discard_text`), the
  description when looked at, the outcome on a result line (`result_text`) and the targets greyed by `reachable`;
* menu_options: the party talk on its page (`party_talk` "page") with each speaker's portrait beside the talk box;
* save_slots: `Continue` beside the save `newest` marks, and each row's `write_time`;
* load_game: `select_newest` 1, the Load list opening on the newest save; title: `continue` "choose";
* assets.json: the field's `exit_markers` for exits that fire `always` (a gold ring) and `from_floor` (a blue ring), and
  a palette track on the `shadow` sprite stepping every 100 ms, which the battle steps on its own clock;
* alt/assets_plain.json and alt/assets_host.json: the manifest without and with the two;
* alt/menu_broken.json: a child's `when` reading no field and an `enabled` that is no expression, which the Makefile
  checks on a copy; alt/menu_items_plain.json and alt/load_game_plain.json, the built-in files.

Usage: make_host_assets.py EXPORT TEST OUT

EXPORT is the placeholder export (its ui/ files are the built-in layouts), TEST the test assets (make_test_assets.py).
"""

import json
import shutil
import struct
import sys
import zlib
from pathlib import Path


def png(path, width, height, pixel):
	rows = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))

	def chunk(kind, data):
		return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
		chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def ring(color):
	"""Two frames of a 16x16 ring, the second one pixel wider."""
	def pixel(x, y):
		frame, cx = divmod(x, 16)
		d = ((cx - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
		return color + (255,) if 4.5 + frame <= d <= 6.5 + frame else (0, 0, 0, 0)
	return pixel


def main():
	export, test, out = (Path(a) for a in sys.argv[1:4])
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(test, out)
	ui = out / "ui"
	built_in = export / "ui"

	entries = [{"page": "items"}, {"page": "party_talk"},
		{"page": "quests", "label": "ui.quests.jobs", "immediate": True, "children": [
			{"page": "quests"}, {"page": "codex", "when": "macro_count > 50"}, {"page": "log", "enabled": "money_value > 999999"}]},
		{"page": "codex", "children": [{"page": "codex"}, {"page": "log", "enabled": "money_value > 999999"}, {"page": "save", "when": "item_count > 99"}]},
		{"page": "status"}, {"page": "techniques"}, {"page": "save"}, {"page": "options"}]
	menu = load(built_in / "menu.json")
	save(ui / "menu.json", dict(menu, entries=entries))
	broken = [dict(e) for e in entries]
	broken[2] = dict(broken[2], children=[{"page": "quests", "when": "nonsense > 1"}, {"page": "codex", "enabled": {"on": True}}])
	save(out / "alt/menu_broken.json", dict(menu, entries=broken))

	items = load(built_in / "menu_items.json")
	save(out / "alt/menu_items_plain.json", items)
	items["params"] = {"actions": 1, "discard": 1}
	page = items["windows"][0]["elements"]
	next(e for e in page if e["type"] == "list")["active"] = "!picking && !acting"
	items["windows"] += [
		{"name": "actions", "visible": "acting && !discarding", "x": "area_x + area_w - 300", "y": "area_y + t(20)", "w": 260, "h": "t(140)", "z": 5,
			"frame": "panel", "padding": [16, 14, 16, 14], "elements": [
			{"type": "list", "items": "actions", "state": "action_list", "w": "cw", "h": "ch", "active": "acting && !looking"}]},
		{"name": "discard", "visible": "discarding", "x": "area_x + 40", "y": "area_y + t(150)", "w": "area_w - 80", "h": "t(70)", "z": 6, "frame": "panel",
			"padding": [20, 16, 20, 16], "elements": [
			{"type": "text", "value": "discard_text", "w": "cw", "color": "discard_step == 'confirm' ? 'bad' : 'accent'"},
			{"type": "bar", "visible": "discard_step == 'count'", "value": "discard_count", "max": "discard_max", "y": "t(30)", "w": "cw", "h": 6}]},
		{"name": "look", "visible": "looking", "x": "area_x + 40", "y": "area_y + t(150)", "w": "area_w - 80", "h": "t(110)", "z": 6, "frame": "panel",
			"padding": [20, 16, 20, 16], "elements": [
			{"type": "text", "value": "look_text", "w": "cw", "wrap": True, "lines": 3, "line_height": "t(24)"}]},
		{"name": "result", "visible": "result_text != ''", "x": "area_x + 14", "y": "area_y + area_h - t(86)", "w": "area_w - 28", "h": "t(26)", "elements": [
			{"type": "text", "value": "result_text", "style": "small", "color": "result_ok ? 'good' : 'bad'"}]},
	]
	save(ui / "menu_items.json", items)

	options = load(built_in / "menu_options.json")
	options["params"] = {"party_talk": "page"}
	options["windows"].append({"name": "talk", "visible": "talking", "x": "area_x + 20", "y": "area_y + t(96)", "w": "min(area_w - 40, t(700))", "h": "t(190)",
		"frame": "panel", "elements": [
		{"type": "repeater", "items": "talk", "layout": "column", "x": 16, "y": 16, "dy": 52, "template": [
			{"type": "image", "image": "portrait", "w": 44, "h": 44}]},
		{"type": "slot", "slot": "talk_box", "x": 76, "y": 16, "w": "cw - 92", "h": "ch - 32"}]})
	save(ui / "menu_options.json", options)

	slots = load(built_in / "save_slots.json")
	row = slots["windows"][0]["elements"][0]["row"]
	row.append({"type": "text", "visible": "newest", "key": "ui.title.continue", "x": "cw - 150", "w": 120, "align": "right", "style": "small",
		"color": "'good'"})
	row.append({"type": "text", "visible": "write_time > 0 && !empty", "value": "write_time", "x": "cw - 150", "y": "t(26)", "w": 120, "align": "right",
		"style": "small_dim"})
	save(ui / "save_slots.json", slots)

	load_game = load(built_in / "load_game.json")
	save(out / "alt/load_game_plain.json", load_game)
	save(ui / "load_game.json", dict(load_game, params={"select_newest": 1}))
	title = load(built_in / "title.json")
	save(ui / "title.json", dict(title, params={"continue": "choose"}))

	png(out / "overlays/exit_always.png", 32, 16, ring((236, 196, 64)))
	png(out / "overlays/exit_from_floor.png", 32, 16, ring((84, 156, 236)))
	png(out / "sprites/shadow_host.png", 16, 16, lambda x, y: (32, 32, 32, 255) if 2 <= x <= 13 and y >= 10 else (0, 0, 0, 0))
	m = load(out / "assets.json")
	save(out / "alt/assets_plain.json", m)
	m.setdefault("field", {})["exit_markers"] = {
		"always": {"image": "overlays/exit_always.png", "frame": [16, 16], "frames": [0, 1], "ms": 250},
		"from_floor": {"image": "overlays/exit_from_floor.png", "frame": [16, 16], "frames": [0, 1], "ms": 400},
	}
	m.setdefault("sprites", {})["shadow"] = {"image": "sprites/shadow_host.png",
		"palettes": [{"colors": {"band": "#202020"}, "steps": [{}, {"band": "#602020"}, {"band": "#206020"}], "ms": 100}]}
	save(out / "assets.json", m)
	save(out / "alt/assets_host.json", m)


if __name__ == "__main__":
	main()
