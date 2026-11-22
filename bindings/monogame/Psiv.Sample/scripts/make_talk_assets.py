#!/usr/bin/env python3
"""Writes the asset directories the party talk speaker runs play (talk_speakers.txt at 1280x720, small_talk.txt on the
320x224 example set with --small): a copy of BASE with

* menu: Items, Talk (`party_talk`, `immediate`: the first speaker comes up as the tab opens), Status and Options;
* menu_options: `party_talk` "speakers" and a `talk_speaker` window at the page's foot, shaped as the set's dialogue
  box: the portrait (`talk_portrait`), the name (`talk_speaker_name`), the box's text (`talk_text`, revealed by
  `talk_shown`, as many rows as the set's dialogue box), the speaker and box numbers and the page arrow while more
  follows (`talk_more`, `page_arrow`); the page's own row hidden while the speakers run;
* extras: the party talk first, then Options and Controls;
* assets.json: the medic's portrait with an expression, `worried`, which the data copy's line selects by a tag;
* alt/menu_options_hide.json: the same with `talk_hide_menu` 1, and alt/menu_options_speakers.json without, to swap
  back; alt/menu_plain.json, the set's menu (no Talk tab); alt/menu_options_broken.json: a `party_talk` word that is
  none of the three, which the Makefile checks on a copy.

Usage: make_talk_assets.py EXPORT BASE OUT [--small]

EXPORT is the placeholder export (its ui/ files are the built-in layouts), BASE the test assets (make_test_assets.py) or,
with --small, the small example set (make_layout_assets.py --small).
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


def face(skin, brow):
	"""A 32x32 face: a round head on a teal ground, eyes, and a mouth that is level or, with `brow`, turned down under
	slanted brows."""
	def pixel(x, y):
		dx, dy = x - 15.5, y - 16.5
		if dx * dx + dy * dy > 13 * 13:
			return (40, 96, 104, 255)
		if y in (13, 14) and x in (10, 11, 20, 21):
			return (24, 24, 32, 255)
		if brow and ((y == 10 - (x - 9) // 2 and 9 <= x <= 12) or (y == 10 - (22 - x) // 2 and 19 <= x <= 22)):
			return (24, 24, 32, 255)
		if brow and 11 <= x <= 20 and y == 22 + (1 if x in (11, 12, 19, 20) else 0):
			return (120, 32, 32, 255)
		if not brow and 11 <= x <= 20 and y == 22:
			return (120, 32, 32, 255)
		return skin + (255,)
	return pixel


SPEAKER = {
	"large": {"name": "talk_speaker", "visible": "talk_count > 0", "x": "area_x", "y": "area_y + area_h - h", "w": "area_w", "h": "t(176)", "frame": "panel",
		"elements": [
			{"type": "role", "role": "dialogue"},
			{"type": "plate", "value": "talk_speaker_name", "x": 18, "y": "-t(16)", "size": "size_body"},
			{"type": "fill", "x": 20, "y": "t(34) - 2", "w": 100, "h": 100, "color": "panel_edge"},
			{"type": "fill", "x": 22, "y": "t(34)", "w": 96, "h": 96, "color": "track"},
			{"type": "image", "name": "portrait", "image": "talk_portrait", "x": 30, "y": "t(34) + 8", "w": 80, "h": 80},
			{"type": "text", "name": "talk_text", "value": "talk_text", "x": 144, "y": "t(30)", "max_width": "cw - 174", "wrap": True, "lines": 4,
				"reveal": "talk_shown", "line_height": "t(size_line)"},
			{"type": "role", "role": "menu"},
			{"type": "text", "value": "talk_index + '/' + talk_count + '   ' + talk_page + '/' + talk_pages", "y": "ch - t(32)", "w": "cw - 70",
				"align": "right", "style": "small_dim"},
			{"type": "image", "visible": "talk_more", "image": "page_arrow", "x": "cw - 48", "y": "ch - 40 + clock_ms / 260 % 2 * 3", "w": 24, "h": 24},
		]},
	"small": {"name": "talk_speaker", "visible": "talk_count > 0", "x": "area_x", "y": "area_y + area_h - h", "w": "area_w", "h": "t(70)", "frame": "panel",
		"padding": [6, 5, 6, 4], "elements": [
			{"type": "role", "role": "dialogue"},
			{"type": "fill", "w": 34, "h": 34, "color": "panel_edge"},
			{"type": "fill", "x": 1, "y": 1, "w": 32, "h": 32, "color": "track"},
			{"type": "image", "name": "portrait", "image": "talk_portrait", "x": 1, "y": 1, "w": 32, "h": 32},
			{"type": "text", "value": "talk_speaker_name", "x": 40, "color": "accent"},
			{"type": "text", "name": "talk_text", "value": "talk_text", "x": 40, "y": "t(11)", "max_width": "cw - 42", "wrap": True, "lines": 2,
				"reveal": "talk_shown", "line_height": "t(size_line) + 2"},
			{"type": "role", "role": "menu"},
			{"type": "text", "value": "talk_index + '/' + talk_count + ' ' + talk_page + '/' + talk_pages", "w": "cw", "align": "right", "style": "small_dim"},
			{"type": "image", "visible": "talk_more", "image": "page_arrow", "x": "cw - 8", "y": "ch - 9 + clock_ms / 260 % 2", "w": 8, "h": 8},
		]},
}


def main():
	export, base, out = (Path(a) for a in sys.argv[1:4])
	small = "--small" in sys.argv[4:]
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(base, out)
	ui = out / "ui"
	built_in = export / "ui"

	menu = load(ui / "menu.json") if (ui / "menu.json").exists() else load(built_in / "menu.json")
	save(out / "alt/menu_plain.json", menu)
	save(ui / "menu.json", dict(menu, entries=[{"page": "items"}, {"page": "party_talk", "immediate": True}, {"page": "status"}, {"page": "options"}]))

	options = load(built_in / "menu_options.json")
	page = options["windows"][0]
	page["visible"] = "talk_count == 0"
	if small:
		page["elements"] = [{"type": "rows", "items": "options", "selected": 0, "x": 8, "y": 4, "w": "cw - 16", "size": "size_small", "row_height": "t(14)",
			"height": "t(13)"}]
	options["params"] = {"party_talk": "speakers"}
	options["windows"] = [page, SPEAKER["small" if small else "large"]]
	save(ui / "menu_options.json", options)
	save(out / "alt/menu_options_speakers.json", options)
	save(out / "alt/menu_options_hide.json", dict(options, params={"party_talk": "speakers", "talk_hide_menu": 1}))
	save(out / "alt/menu_options_broken.json", dict(options, params={"party_talk": "dialogue"}))

	extras = load(ui / "extras.json") if (ui / "extras.json").exists() else load(built_in / "extras.json")
	save(ui / "extras.json", dict(extras, entries=[{"page": "party_talk"}, {"page": "options"}, {"page": "controls"}]))

	png(out / "portraits/talk_medic.png", 32, 32, face((236, 200, 160), False))
	png(out / "portraits/talk_medic_worried.png", 32, 32, face((236, 200, 160), True))
	m = load(out / "assets.json")
	m.setdefault("portraits", {})["medic"] = {"image": "portraits/talk_medic.png", "expressions": {"worried": "portraits/talk_medic_worried.png"}}
	save(out / "assets.json", m)


if __name__ == "__main__":
	main()
