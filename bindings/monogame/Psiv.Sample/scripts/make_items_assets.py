#!/usr/bin/env python3
"""Writes the asset directory the layout item runs play (scripts/items_battle.txt, items_menus.txt and
items_dialogue.txt): a copy of the test assets with alt/assets_more.json as the manifest (its still pictures), a cue and
a music track that only the dialogue's tags play, and layout files built from the export's built-in ones with the
optional fields docs/ui-layout.md describes:

* battle_front: the per-member status flags and the fallen flag on the strip, the enemies' status icons following a
  statuses table, the list's kind and index sizing a window, the acting member's slot, a name window shown only for a
  command chosen from a list, the command rows as a row of pictures walked with Left and Right (`command_axis` row), the top-level step, and
  the result window paged two lines at a time;
* battle_side: the enemies' status icons kept off the table (`enemy_statuses` 0);
* title: a start state before the menu, and the menu's entries chosen and ordered (erase listed, so hidden);
* dialogue: a tag whose values act by range and by value, and the still pictures kept as a stack;
* field: no arrival notice and no area banner (`arrive_toast`, `area_banner` 0);
* save_slots, menu_equip, menu_order, menu_status: the Save page's system menu and numbered slots, the Equip page
  opening on the first member's places with its own places and the stats shown only as a preview, the order page
  without numbers, and the status page's stats without the bonus, a portrait at twice its size and profile rows.

Usage: make_items_assets.py EXPORT TEST OUT

EXPORT is the placeholder export (its ui/ files are the built-in layouts), TEST the test assets (make_test_assets.py).
"""

import copy
import json
import shutil
import sys
from pathlib import Path


def load(path):
	return json.loads(path.read_text())


def save(path, data):
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def window(layout, name):
	return next(w for w in layout["windows"] if w["name"] == name)


def battle_front(layout):
	layout = copy.deepcopy(layout)
	layout["params"]["command_axis"] = "row"
	layout["params"]["command_top"] = 1
	layout["params"]["result_page_lines"] = 2
	layout["statuses"] = {
		"sleep": {"color": "tp", "priority": 2},
		"poison": {"color": "good", "icon": "none", "priority": 1},
	}
	strip = window(layout, "strip")
	slot = next(e for e in strip["elements"] if e["type"] == "repeater")
	slot["template"] += [
		{"type": "plate", "visible": "status_sleep", "value": "'Zz'", "x": 180, "y": -14, "color": "tp"},
		{"type": "plate", "visible": "fallen", "value": "'down'", "x": 120, "y": -14, "color": "bad"},
	]
	command = window(layout, "command")
	command["visible"] = "menu_shown && phase != 'command'"
	command["h"] = "list_kind == 'item' ? 160 : list_kind == 'top' ? 94 : 114"
	layout["windows"] += [
		# The command rows as a row of pictures, the one under the cursor framed.
		{"name": "icons", "visible": "phase == 'command'", "x": "(W - w) / 2", "y": "strip.y - h - 4", "w": "len(menu_items) * 40 + 16", "h": 72,
			"frame": "battle", "padding": [8, 8, 8, 8], "elements": [
				{"type": "repeater", "items": "menu_items", "dx": 40, "template": [
					{"type": "outline", "visible": "i == cursor", "x": -2, "y": -2, "w": 36, "h": 36, "color": "accent"},
					{"type": "image", "image": "icon", "w": 32, "h": 32}
				]},
				{"type": "text", "value": "'row ' + cursor", "y": 38, "w": "cw", "align": "center", "style": "small"}
			]},
		# The list's kind and the command row it came from, and the member choosing.
		{"name": "list_info", "visible": "list_index >= 0 || actor_slot >= 0", "x": 16, "y": 16, "w": 300, "h": 54, "frame": "battle", "padding": [10, 6, 10, 6],
			"elements": [
				{"type": "text", "value": "'list ' + list_kind + ' ' + list_index", "style": "small"},
				{"type": "text", "value": "'slot ' + actor_slot", "y": 20, "style": "small"}
			]},
		# The action's name only for a command chosen from a list.
		{"name": "chosen", "visible": "phase == 'playback' && action_chosen", "x": "W - 16 - w", "y": 16, "w": 220, "h": 34, "frame": "battle",
			"padding": [10, 6, 10, 6], "elements": [
				{"type": "text", "value": "'chosen ' + action_command", "style": "small", "color": "accent"}
			]},
		{"name": "pages", "visible": "phase == 'ended' && result_pages > 1", "x": "result.x + result.w - w - 12", "y": "result.y + result.h - h - 8", "w": 80,
			"h": 24, "elements": [
				{"type": "text", "value": "result_page + '/' + result_pages + (result_more ? ' >' : '')", "w": "cw", "align": "right", "style": "small_dim"}
			]},
	]
	return layout


def battle_side(layout):
	layout = copy.deepcopy(layout)
	layout["params"]["enemy_statuses"] = 0
	layout["statuses"] = {"sleep": {"color": "tp", "icon": "none", "priority": 2}}
	return layout


def title(layout):
	layout = copy.deepcopy(layout)
	layout["start"] = True
	layout["options"] = ["continue", "new_game", "load", "erase", "options", "quit"]
	window(layout, "box")["visible"] = "!start"
	layout["windows"].insert(1, {"name": "press", "visible": "start", "anchor": "center", "w": 420, "h": 120, "frame": "panel", "elements": [
		{"type": "text", "value": "title", "y": 24, "w": "cw", "align": "center", "style": "title"},
		{"type": "text", "visible": "clock_ms / 500 % 2 == 0", "value": "start_hint", "y": 76, "w": "cw", "align": "center", "color": "accent"}
	]})
	return layout


def dialogue(layout):
	layout = copy.deepcopy(layout)
	layout.setdefault("params", {})["picture_stack"] = 1
	layout["tags"] = {
		"snd": {"cases": [
			{"value": "0", "action": "music_stop"},
			{"from": 1, "to": 2, "action": "music", "ids": ["tag_theme", "title"]},
			{"from": 10, "to": 19, "action": "cue", "id": "tag_gong"},
		]},
	}
	# Every picture of the stack as a small card along the top, the top one as the built-in window shows it.
	layout["windows"].insert(1, {"name": "stack", "visible": "len(pictures) > 1", "x": 16, "y": 16, "w": "len(pictures) * 84 + 8", "h": 92, "frame": "panel",
		"elements": [
			{"type": "repeater", "items": "pictures", "x": 8, "y": 8, "dx": 84, "template": [
				{"type": "image", "image": "picture", "x": "(76 - min(76, picture_w * 76 / max(1, picture_h))) / 2", "w": "min(76, picture_w * 76 / max(1, picture_h))", "h": 76}
			]}
		]})
	return layout


def field(layout):
	layout = copy.deepcopy(layout)
	layout["params"] = {"arrive_toast": 0, "area_banner": 0}
	return layout


def save_slots(layout):
	layout = copy.deepcopy(layout)
	layout["params"] = {"system_menu": 1}
	slots = window(layout, "slots")
	slots["visible"] = "!system"
	listing = slots["elements"][0]
	listing["row_height"] = "t(32)"
	listing["row"] = [
		{"type": "fill", "visible": "selected", "x": -6, "y": -3, "w": "cw - 14", "h": "t(28)", "color": "row"},
		{"type": "text", "value": "number + '.'", "x": 8, "color": "selected ? 'accent' : 'dim'"},
		{"type": "text", "value": "empty ? '-' : damaged ? 'damaged' : area + '   ' + play_time + '   ' + leader + ' ' + level", "x": 48,
			"max_width": "cw - 60", "color": "!enabled ? 'dim' : selected ? 'accent' : 'text'"}
	]
	layout["windows"].append({"name": "system", "visible": "system", "x": "area_x", "y": "area_y", "w": 260, "h": "len(system_rows) * t(32) + 20", "frame": "panel",
		"padding": [16, 10, 16, 10], "elements": [
			{"type": "rows", "items": "system_rows", "selected": "system_cursor", "x": 20, "w": "cw - 20", "row_height": "t(32)"}
		]})
	return layout


def menu_equip(layout):
	layout = copy.deepcopy(layout)
	layout["params"] = {"members_first": 0, "stats_preview_only": 1}
	layout["places"] = [{"slot": "body"}, {"slot": "weapon"}, {"slot": "head"}, {"slot": "accessory", "label": "ui.equip_slot.accessory"}]
	page = window(layout, "page")
	page["elements"].append({"type": "text", "visible": "view != 'members'", "value": "(member_index + 1) + '/' + member_count",
		"w": "cw / 2 - 10", "align": "right", "style": "small_dim"})
	return layout


def menu_order(layout):
	layout = copy.deepcopy(layout)
	page = window(layout, "page")
	listing = next(e for e in page["elements"] if e["type"] == "list")
	listing["items"] = "lineup"
	listing["text"] = "(picked ? '> ' : '') + name + ' L' + level + ' ' + hp + '/' + max_hp + (statuses != '' ? ' ' + statuses : '')"
	return layout


def menu_status(layout):
	layout = copy.deepcopy(layout)
	layout["params"] = {"stat_bonus": 0}
	layout["profile"] = ["name", "level", "kind", "experience_next", "attack_element"]
	layout["windows"].append({"name": "profile", "visible": "view == 'stats'", "x": "area_x + area_w - w", "y": "area_y", "w": 280, "h": "portrait_h * 2 + 24 + len(profile) * t(22)",
		"frame": "panel", "padding": [12, 12, 12, 12], "elements": [
			{"type": "image", "image": "portrait", "w": "portrait_w * 2", "h": "portrait_h * 2"},
			{"type": "repeater", "items": "profile", "y": "portrait_h * 2 + 8", "dy": "t(22)", "template": [
				{"type": "text", "value": "label", "style": "small_dim"},
				{"type": "text", "value": "value", "w": "cw", "align": "right", "style": "small"}
			]}
		]})
	return layout


def main():
	export, test, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(test, out)
	manifest = load(out / "alt" / "assets_more.json")
	shutil.copy(out / "audio" / "confirm.wav", out / "audio" / "tag_gong.wav")
	manifest["audio"] += [
		{"kind": "cue", "id": "tag_gong", "file": "audio/tag_gong.wav"},
		{"kind": "music", "id": "tag_theme", "file": "audio/town.wav"},
	]
	save(out / "assets.json", manifest)
	builtin = export / "ui"
	for name, make in [("battle_front", battle_front), ("battle_side", battle_side), ("title", title), ("dialogue", dialogue), ("field", field), ("save_slots", save_slots),
			("menu_equip", menu_equip), ("menu_order", menu_order), ("menu_status", menu_status)]:
		save(out / "ui" / f"{name}.json", make(load(builtin / f"{name}.json")))


if __name__ == "__main__":
	main()
