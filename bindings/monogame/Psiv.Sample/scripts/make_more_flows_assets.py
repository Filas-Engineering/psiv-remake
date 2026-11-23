#!/usr/bin/env python3
"""Writes the asset directory scripts/more_flows.txt plays (and with --small the one small_more_flows.txt plays): a copy
of BASE with layout files that use the items docs/ui-layout.md describes under "More layout flows":

* inn: `flow` welcome, choice, decline, farewell: Leave shows the keeper's decline line, a stay the farewell;
* shop: `flow` list, question, confirm, result: the keeper's question alone, then the YES/NO on the next Confirm;
  alt/shop_held.json the same with `question_ms` 500;
* title: an `intro` of three steps (`studio`, `story`, `logo`) and `after_defeat` "intro:logo"; the story step scrolls a
  picture of the manifest's `pictures` through an image element's `src` with `repeat`, the logo step crops a title art
  layer's frame, and the menu shows the right half of a PNG of the asset directory (`ui/more_sign.png`);
* load_game: the saves as blocks in a grid two wide (`navigation` "grid", `columns` 2) with every manual slot, an empty
  one drawn by its own template (`show_empty` 1), each block with its party's standing sprites and levels;
  alt/load_row.json the same in one row (`navigation` "row");
* travel: from the field menu the list over the menu's windows (`over_menu` 1); one place to go goes at once
  (`one_destination` "go") but a town use lists even its one town and the way out goes at once (`one_destination_for`);
  alt/travel_id.json the Homing Shell going at once by its id;
* battle_front: a `result_sequence` whose level-ups go two lines a page (`level_page_lines` 2) in a window of a fixed
  size, the page count and, while more follows, the UI skin's page arrow cropped by `src`;
* battle_front_vehicle (not with --small): the vehicle's rows attack, weapons (a grouped row) and escape, the weapons
  in a window of their own beside the command window, which keeps the first menu;
* broken copies' files under alt/: title_broken.json (`after_defeat` naming no intro step), shop_broken.json (a question
  without a confirm), inn_broken.json (decline after farewell), load_broken.json (a bad `navigation`), which the Makefile
  checks on copies.

The pictures are generated here: a strip of four color bands (`pictures/more_strip.png`), a sign half teal and half
orange (`ui/more_sign.png`), a title art layer of two frames (`title/more_layer.png`) and a page arrow
(`ui/more_arrow.png`).

Usage: make_more_flows_assets.py EXPORT BASE OUT [--small]

EXPORT is the placeholder export (its ui/ files are the built-in layouts), BASE the asset directory to copy (the test
assets, or with --small the small example set, whose own title and battle files the layouts extend).
"""

import json
import shutil
import struct
import sys
import zlib
from pathlib import Path

BANDS = [(220, 60, 60), (60, 200, 90), (70, 110, 230), (240, 210, 60)]
TEAL, ORANGE, PURPLE, GREY = (40, 180, 170), (240, 140, 40), (150, 70, 200), (120, 120, 120)


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def write_png(path, width, height, pixel):
	raw = b"".join(b"\0" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))

	def chunk(kind, body):
		return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
		+ chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def pictures(out):
	write_png(out / "pictures" / "more_strip.png", 32, 16, lambda x, y: (*BANDS[x // 8], 255))
	write_png(out / "ui" / "more_sign.png", 16, 16, lambda x, y: (*(TEAL if x < 8 else ORANGE), 255))
	# Two 16x8 frames side by side: each a purple left half and a grey right half, the second with a white top row.
	write_png(out / "title" / "more_layer.png", 32, 8, lambda x, y: (255, 255, 255, 255) if x >= 16 and y == 0 else (*(PURPLE if x % 16 < 8 else GREY), 255))
	# A down-pointing arrow over a teal bar: the arrow in the top half, the bar in the bottom half.
	write_png(out / "ui" / "more_arrow.png", 16, 16,
		lambda x, y: (250, 250, 250, 255) if y < 8 and y // 2 <= x - 4 <= 7 - y // 2 else (*TEAL, 255) if y >= 10 else (0, 0, 0, 0))
	manifest = load(out / "assets.json")
	manifest["pictures"] = dict(manifest.get("pictures", {}), more_strip="pictures/more_strip.png")
	manifest["title_art"] = dict(manifest.get("title_art", {}), layers=[
		{"image": "title/more_layer.png", "frame": [16, 8], "frames": [0, 1], "ms": 400, "at": [0, 0], "scale": 1}])
	manifest["ui"] = dict(manifest.get("ui", {"enabled": True}), page_arrow="ui/more_arrow.png")
	save(out / "assets.json", manifest)


def metrics(small):
	if small:
		return {"rh": 11, "pad": 4, "body": "small", "big": "small", "titled": False, "pic": 64, "sprite": 12, "member_dx": 33, "block_h": 48}
	return {"rh": 30, "pad": 24, "body": "body", "big": "title", "titled": True, "pic": 128, "sprite": 32, "member_dx": 84, "block_h": 150}


def inn(m):
	rh, pad = m["rh"], m["pad"]
	return {"version": 1, "flow": ["welcome", "choice", "decline", "farewell"], "windows": [
		{"name": "box", "x": pad // 2, "y": "top", "w": f"W - {pad}", "h": f"4 * {rh} + {2 * pad}", "frame": "panel",
			**({"title": {"value": "title"}} if m["titled"] else {}), "padding": [pad, pad, pad, pad], "elements": [
			{"type": "text", "value": "voice_line", "w": "cw", "wrap": True, "lines": 2, "style": m["body"]},
			{"type": "rows", "visible": "step == 'choice'", "items": "options", "selected": "cursor", "x": rh, "y": 2 * rh, "row_height": rh, "height": rh - 2},
			{"type": "text", "value": "step", "y": 3 * rh, "w": "cw", "align": "right", "style": "small_dim"}]},
	]}


def shop(m):
	rh, pad = m["rh"], m["pad"]
	return {"version": 1, "flow": ["list", "question", "confirm", "result"], "params": {"backdrop_dim": 0.4}, "windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "list", "visible": "open", "x": pad // 2, "y": pad // 2, "w": f"W - {pad}", "h": f"H - {pad}", "frame": "panel",
			"padding": [pad, pad, pad, pad], "elements": [
			{"type": "text", "value": "money", "w": "cw", "align": "right", "style": m["body"]},
			{"type": "list", "items": "rows", "state": "list", "probe": False, "y": rh, "w": "cw", "h": f"ch - {2 * rh}", "row_height": rh, "active": "flow_step == 'list'",
				"row": [
				{"type": "fill", "visible": "selected", "x": -2, "y": -2, "w": "cw - 4", "h": rh - 2, "color": "row"},
				{"type": "text", "value": "text", "x": 4, "color": "selected ? 'accent' : 'text'", "style": m["body"]},
				{"type": "text", "value": "price", "w": "cw - 8", "align": "right", "style": m["body"]}]}]},
		# The question alone: the keeper's line, no YES/NO.
		{"name": "question", "visible": "open && flow_step == 'question'", "anchor": "center", "w": "W * 2 / 3", "h": f"2 * {rh} + {2 * pad}", "z": 5, "frame": "panel",
			"padding": [pad, pad, pad, pad], "elements": [
			{"type": "text", "value": "voice_line", "w": "cw", "wrap": True, "lines": 2, "style": m["body"]}]},
		{"name": "confirm", "visible": "open && flow_step == 'confirm'", "anchor": "center", "w": "W * 2 / 3", "h": f"4 * {rh} + {2 * pad}", "z": 5, "frame": "panel",
			"padding": [pad, pad, pad, pad], "elements": [
			{"type": "text", "value": "voice_line", "w": "cw", "wrap": True, "lines": 2, "style": m["body"]},
			{"type": "rows", "items": "confirm_options", "selected": "confirm_cursor", "y": 2 * rh, "row_height": rh, "height": rh - 2}]},
		{"name": "result", "visible": "open && flow_step == 'result'", "anchor": "center", "w": "W * 2 / 3", "h": f"2 * {rh} + {2 * pad}", "z": 5, "frame": "panel",
			"padding": [pad, pad, pad, pad], "elements": [
			{"type": "text", "value": "voice_line", "w": "cw", "wrap": True, "lines": 2, "style": m["body"]}]},
		{"name": "toasts", "elements": [{"type": "slot", "slot": "toasts", "x": pad, "y": f"H - {pad}"}]},
	]}


def title(m, base):
	pic, pad = m["pic"], m["pad"]
	data = dict(base)
	data["start"] = True
	data["intro"] = [{"name": "studio", "ms": 600}, {"name": "story", "ms": 900}, {"name": "logo", "ms": 700}]
	data["params"] = dict(data.get("params", {}), after_defeat="intro:logo")
	windows = [dict(w) for w in data["windows"]]
	for w in windows:
		if w["name"] == "box":
			w["visible"] = "!start && !intro"
	windows += [
		{"name": "intro", "visible": "intro", "z": 10, "elements": [
			{"type": "fill", "w": "W", "h": "H", "color": "black"},
			{"type": "text", "visible": "intro_step == 'studio'", "value": "'A test studio'", "y": "H / 2 - 10", "w": "W", "align": "center", "style": m["big"]},
			# The strip's 16 middle pixels, one band further every 225 ms, wrapping past its end.
			{"type": "image", "name": "strip", "visible": "intro_step == 'story'", "image": "'picture:more_strip'", "src": ["intro_ms / 225 * 8", 0, 16, 16], "repeat": True,
				"x": f"(W - {pic}) / 2", "y": f"H / 2 - {pic // 2}", "w": pic, "h": pic},
			# The layer's frame now, its left half.
			{"type": "image", "name": "layer", "visible": "intro_step == 'logo'", "image": "'title:layer:0'", "src": [0, 0, 8, 8],
				"x": f"(W - {pic // 2}) / 2", "y": f"H / 2 - {pic // 4}", "w": pic // 2, "h": pic // 2},
			{"type": "text", "visible": "intro_step == 'logo'", "value": "'logo'", "y": f"H / 2 + {pic // 4 + pad // 2}", "w": "W", "align": "center", "style": m["body"]}]},
		{"name": "press", "visible": "start", "anchor": "center", "w": "W / 2", "h": 2 * m["rh"] + 2 * pad, "frame": "panel", "z": 9, "elements": [
			{"type": "text", "value": "start_hint", "y": pad, "w": "cw", "align": "center", "style": m["body"]}]},
		# The sign's right half, from a PNG of the asset directory, at the top left over the menu.
		{"name": "sign", "visible": "!start && !intro", "x": pad // 2, "y": pad // 2, "w": pic // 4, "h": pic // 2, "z": 8, "elements": [
			{"type": "image", "image": "'ui/more_sign.png'", "src": [8, 0, 8, 16], "w": pic // 4, "h": pic // 2}]},
	]
	data["windows"] = windows
	return data


def load_game(m, navigation):
	rh, pad, sprite, block_h = m["rh"], m["pad"], m["sprite"], m["block_h"]
	member = {"type": "group", "elements": [
		{"type": "image", "image": "sprite", "w": sprite, "h": sprite},
		{"type": "text", "value": "'L' + level", "x": sprite + 2, "y": sprite // 4, "style": "small"}]}
	block = [
		{"type": "box", "frame": "panel", "w": "cw / 2 - 8", "h": block_h - 8},
		{"type": "fill", "visible": "selected", "x": 4, "y": 4, "w": "cw / 2 - 16", "h": block_h - 16, "color": "row"},
		{"type": "text", "value": "number + '  ' + slot", "x": pad // 2 + 4, "y": pad // 2, "color": "selected ? 'accent' : 'text'", "style": "small"},
		# The empty slot's own template.
		{"type": "text", "visible": "empty", "value": "'No journey here yet'", "x": pad // 2 + 4, "y": pad // 2 + rh, "color": "dim", "style": "small"},
		{"type": "text", "visible": "damaged", "value": "detail", "x": pad // 2 + 4, "y": pad // 2 + rh, "color": "bad", "style": "small"},
		{"type": "group", "visible": "!empty && !damaged", "x": pad // 2 + 4, "y": pad // 2 + rh, "elements": [
			{"type": "text", "value": "area + '  ' + play_time", "style": "small"},
			{"type": "repeater", "items": "members", "y": rh + 1, "dx": m["member_dx"], "template": [member]}]}]
	# A page of blocks at a time: four rows of two in the grid, two blocks in the row.
	per = 8 if navigation == "grid" else 2
	block = [{"type": "group", "visible": f"i / {per} == cursor / {per}", "elements": block}]
	return {"version": 1, "params": {"navigation": navigation, "columns": 2, "show_empty": 1}, "windows": [
		{"name": "scene", "elements": [{"type": "slot", "slot": "scene"}, {"type": "fill", "w": "W", "h": "H", "color": "black", "alpha": 0.5}]},
		{"name": "box", "x": pad // 2, "y": pad // 2, "w": f"W - {pad}", "h": f"H - {pad}", "frame": "panel", "padding": [pad, pad, pad, pad],
			**({"title": "ui.saves.load_title"} if m["titled"] else {}), "elements": [
			{"type": "repeater", "items": "slots", "layout": "grid", "columns": "columns", "dx": "cw / 2", "dy": block_h, "y": f"-(cursor / 8) * 4 * {block_h}", "template": block}
			if navigation == "grid" else
			# Two blocks a page, the cursor's page in the window.
			{"type": "repeater", "items": "slots", "layout": "row", "dx": "cw / 2", "x": "-(cursor / 2) * cw", "template": block},
			{"type": "slot", "slot": "toasts", "x": 0, "y": f"ch - {rh}", "w": "cw", "h": 0},
			{"type": "hints", "value": "hint", "y": f"ch - {rh}", "w": "cw"}]},
	]}


def travel(export_travel, by):
	data = dict(export_travel)
	data["params"] = dict(data.get("params", {}), over_menu=1, one_destination="go", backdrop_dim=0.3)
	data["one_destination_for"] = by
	return data


def battle(m, base):
	rh, pad = m["rh"], m["pad"]
	data = dict(base)
	data["params"] = dict(data.get("params", {}), level_page_lines=2)
	data["result_sequence"] = [{"phase": "victory", "ms": 600}, {"phase": "result"}, {"phase": "level_ups"}]
	windows = [dict(w) for w in data["windows"]]
	for w in windows:
		if w["name"] == "result":
			w["visible"] = "result_phase == 'result'"
	arrow = rh if rh > 16 else 8
	windows += [
		{"name": "victory", "visible": "result_phase == 'victory'", "anchor": "center", "w": "W / 2", "h": 2 * rh + 2 * pad, "frame": "battle", "elements": [
			{"type": "text", "value": "outcome", "y": pad, "w": "cw", "align": "center", "style": m["big"]}]},
		# A fixed size: two lines, whatever the page holds.
		{"name": "levels", "visible": "result_phase == 'level_ups'", "anchor": "center", "w": "W * 3 / 4", "h": 4 * rh + 2 * pad, "frame": "battle",
			"padding": [pad, pad, pad, pad], "elements": [
			{"type": "text", "value": "'Level up'", "color": "accent", "style": m["body"]},
			{"type": "text", "value": "level_page + ' / ' + level_pages", "w": "cw", "align": "right", "style": "small_dim"},
			{"type": "repeater", "items": "level_lines", "y": rh, "dy": rh, "template": [
				{"type": "text", "value": "text", "max_width": "cw", "color": "kind == 'learned' ? 'good' : 'text'", "style": m["body"]}]},
			{"type": "image", "name": "more_arrow", "visible": "level_more", "image": "'ui:page_arrow'", "src": [0, 0, 16, 8], "x": f"cw / 2 - {arrow}", "y": 3 * rh,
				"w": 2 * arrow, "h": arrow}]},
	]
	data["windows"] = windows
	return data


def vehicle_battle(base):
	"""The front layout for vehicle battles: the first menu stays in the command window while the weapons list, which a
	grouped row opens, stands in a window of its own beside it."""
	data = json.loads(json.dumps(base))
	data["command_rows"] = {"vehicle": ["attack", "weapons", "escape"]}
	for w in data["windows"]:
		if w["name"] == "command":
			for e in w["elements"]:
				if e.get("type") == "rows":
					e["items"] = "list_kind == 'weapons' ? command_items : menu_items"
					e["selected"] = "list_kind == 'weapons' ? list_index : cursor"
			w["title"] = {"value": "list_kind == 'weapons' ? 'Skimmer' : menu_title"}
	data["windows"].append({"name": "weapons", "visible": "list_kind == 'weapons'", "x": "command.right + 8", "y": "command.y", "w": 280,
		"h": "len(menu_items) * 22 + 36", "frame": "battle", "title": {"value": "menu_title"}, "padding": [18, 18, 18, 18], "elements": [
		{"type": "rows", "items": "menu_items", "selected": "cursor", "text": "name", "enabled": "usable", "w": "cw", "size": "size_small", "row_height": 22, "height": 22},
		{"type": "repeater", "items": "menu_items", "dy": 22, "template": [
			{"type": "text", "value": "uses >= 0 ? uses + '/' + max_uses : ''", "w": "cw", "align": "right", "style": "small_dim"}]}]})
	return data


def main():
	export, base, out = (Path(a) for a in sys.argv[1:4])
	small = "--small" in sys.argv[4:]
	m = metrics(small)
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(base, out)
	pictures(out)
	ui, built_in = out / "ui", export / "ui"

	def own(name):
		return load(ui / name if (ui / name).exists() else built_in / name)

	base_title, base_travel, base_battle = own("title.json"), own("travel.json"), own("battle_front.json")

	save(ui / "inn.json", inn(m))
	save(ui / "shop.json", shop(m))
	held = shop(m)
	held["params"]["question_ms"] = 500
	save(out / "alt/shop_held.json", held)
	save(out / "alt/shop_plain.json", dict(shop(m), flow=["list", "confirm", "result"]))
	save(ui / "title.json", title(m, base_title))
	save(ui / "load_game.json", load_game(m, "grid"))
	save(out / "alt/load_row.json", load_game(m, "row"))
	save(ui / "travel.json", travel(base_travel, {"town": "list", "dungeon_exit": "go"}))
	save(out / "alt/travel_id.json", travel(base_travel, {"town": "list", "dungeon_exit": "go", "homing_shell": "go"}))
	save(ui / "battle_front.json", battle(m, base_battle))
	if not small:
		save(ui / "battle_front_vehicle.json", vehicle_battle(load(built_in / "battle_front.json")))
	broken_title = title(m, base_title)
	broken_title["params"]["after_defeat"] = "intro:credits"
	save(out / "alt/title_broken.json", broken_title)
	save(out / "alt/shop_broken.json", dict(shop(m), flow=["list", "question", "result"]))
	save(out / "alt/inn_broken.json", dict(inn(m), flow=["choice", "farewell", "decline"]))
	broken_load = load_game(m, "grid")
	broken_load["params"]["navigation"] = "spiral"
	save(out / "alt/load_broken.json", broken_load)


if __name__ == "__main__":
	main()
