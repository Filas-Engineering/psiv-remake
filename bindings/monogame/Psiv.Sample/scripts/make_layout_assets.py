#!/usr/bin/env python3
"""Builds an example asset directory with a second, complete layout set (docs/ui-layout.md), unlike the built-in one.

  make_layout_assets.py EXPORT FONTS OUT

EXPORT: the placeholder export (--export-placeholders), whose ui/ holds the built-in layout files. FONTS: the bitmap font
directory make_test_assets.py writes. OUT: a copy of FONTS, its manifest turning the layout files on (`ui_layout`), and
ui/*.json: a styles file and every screen's file.

The set draws a 640x480 frame scaled by whole numbers only (`integer`), in the bitmap font at one file pixel a screen
pixel for small and body text and two for titles (the font's `scale` 0.6), so lines are 10 pixels tall and step 13 to
18 pixels apart. Windows are framed panels without title plates: a window's name is its first line, in the accent
color. Where things go:

* Title: a narrow window at the left, with the hero and a creature beside the options; the version at the top right.
* Field: the area's name and the money and play time in a dark card at the top left, the arrival banner over the hint
  line at the bottom, notices from the top right down, the hint line left-aligned.
* Dialogue and the inn: a box at the top of the screen, inset from the sides, with the portrait in a small square.
* Field menu: the pages listed down a column at the left (the selected one highlighted, Left and Right still turn them),
  the money under them, the page in a panel at the right; lists show fewer, shorter rows.
* Shop: the stock on the left, the stall, the description and the comparison in a column at the right.
* Battle, front: the party's status as a column of cards at the left edge (an empty frame for a free place), the stage
  to their right, the command window (one column of four rows, scrolling) beside the member choosing, the log, the
  description and the action's name over the stage. Side: the stage over the top two thirds, the party as one line per
  member at the bottom left, the commands at the bottom right.

With --dev the windows at the top start under the developer badge (the common field `dev`). The pages drawn into
another layout's rectangle (the menu's pages, the save slots, the controls) keep the built-in arrangement on the
smaller panel with shorter rows. The comic panels keep the built-in file, which takes every rectangle from the screen.
"""

import json
import shutil
import sys
from pathlib import Path

HINT_H = "t(16)"
# With --dev the developer badge takes the top middle; windows that would reach it start under it.
TOP = "(dev ? 46 : 8)"


def text(value=None, key=None, **kw):
	e = {"type": "text"}
	if key is not None:
		e["key"] = key
	if value is not None:
		e["value"] = value
	e.update(kw)
	return e


def hint_bar(field="hint"):
	"""The keys' line across the bottom: a dark strip, the hint left-aligned."""
	return {"name": "hint", "x": 0, "y": f"H - {HINT_H}", "h": HINT_H, "elements": [
		{"type": "fill", "w": "W", "h": HINT_H, "color": "black", "alpha": 0.7, "scaled": False},
		{"type": "fill", "w": 3, "h": HINT_H, "color": "accent", "scaled": False},
		text(field, max_width="W - 24", x=12, y="t(3)", style="small_dim", scaled=False),
	]}


def heading(key=None, value=None, y=0):
	"""A window's name as its first line, with a rule under it."""
	return [
		text(value, key=key, y=y, color="accent"),
		{"type": "fill", "y": f"{y} + t(14)", "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
	]


def row_style(width, height="t(15)"):
	"""The ListRow look spelled out for a row template: the highlight, the pointer."""
	return [
		{"type": "fill", "visible": "selected", "x": -6, "y": -3, "w": width, "h": height, "color": "row"},
		{"type": "pointer", "visible": "selected", "x": -4, "y": f"({height} - 8) / 2 - 6", "size": 10},
	]


def styles(builtin):
	s = json.loads(json.dumps(builtin))
	s["resolution"] = {"width": 640, "height": 480}
	s["scaling"] = {"mode": "integer", "safe_area": [0, 0, 0, 0], "filter": "point"}
	s["colors"]["card"] = "#10182c"
	s["colors"]["teal"] = "#5ccfc0"
	s["sizes"]["line"] = 14
	s["text_styles"]["label"] = {"size": "size_small", "color": "teal"}
	s["frames"]["card"] = {"type": "fill", "color": "card"}
	s["frames"]["edge"] = {"type": "outline", "color": "panel_edge", "thickness": 1}
	s["timings"]["toast_ms"] = 2500
	return s


def screens():
	L = {}

	L["title"] = {"windows": [
		{"name": "scene", "elements": [{"type": "slot", "slot": "scene"}]},
		{"name": "box", "anchor": "left", "x": 32, "y": -12, "w": 344, "h": "t(50) + (game_over ? t(16) : 0) + len(options) * t(18) + 18",
			"frame": "panel", "padding": [14, 7, 14, 10], "elements": [
			text("title", style="title", max_width="cw"),
			text("subtitle", y="t(24)", style="small_dim", max_width="cw"),
			{"type": "fill", "y": "t(40)", "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
			text(key="ui.title.fallen", visible="game_over", y="t(46)", color="bad", max_width="cw"),
			{"type": "rows", "items": "options", "selected": "cursor", "x": 6, "y": "t(50) + (game_over ? t(16) : 0)", "w": "cw - 52",
				"row_height": "t(18)", "height": "t(16)"},
			{"type": "box", "frame": "card", "x": "cw - 40", "y": "t(50) + (game_over ? t(16) : 0)", "w": 40, "h": 76},
			{"type": "image", "image": "hero_frame", "x": "cw - 36", "y": "t(50) + (game_over ? t(16) : 0) + 4", "w": 32, "h": 32},
			{"type": "image", "image": "creature_frame", "x": "cw - 36", "y": "t(50) + (game_over ? t(16) : 0) + 40", "w": 32, "h": 32},
		]},
		{"name": "corner", "elements": [
			text("version", w="W - 10", align="right", y=8, style="small_dim"),
			{"type": "slot", "slot": "toasts"},
		]},
		hint_bar(),
	]}

	banner = {"name": "banner", "visible": "banner_visible", "anchor": "bottom", "y": f"{HINT_H} + 12 + banner_drop", "h": 52, "frame": "panel",
		"opacity": "banner_alpha", "w": "max(measure_unscaled(banner_title, size_title), measure_unscaled(banner_kind, size_small)) + 56", "elements": [
		text("banner_kind", w="cw", align="center", y=9, style="small_dim", alpha="banner_alpha", scaled=False),
		text("banner_title", w="cw", align="center", y=22, style="title", alpha="banner_alpha", scaled=False),
	]}
	hud = {"name": "hud", "visible": "hud", "x": 6, "y": 6, "w": "max(measure_unscaled(area, size_body), measure_unscaled(status, size_small)) + 22", "h": 34,
		"elements": [
		{"type": "fill", "w": "cw", "h": "ch", "color": "black", "alpha": 0.6, "scaled": False},
		{"type": "fill", "w": 3, "h": "ch", "color": "accent", "scaled": False},
		text("area", x=11, y=5, scaled=False),
		text("status", x=11, y=19, style="small_dim", scaled=False),
	]}
	L["field"] = {"windows": [
		hud,
		banner,
		{"name": "toasts", "elements": [
			{"type": "repeater", "items": "toasts", "x": 0, "y": TOP, "dy": 26, "template": [
				{"type": "box", "x": "W - 8 - width", "w": "width", "h": 22, "opacity": "box_alpha", "scaled": False},
				text("text", x="W - 8 - width + 14", y=6, style="small", alpha="alpha", scaled=False),
			]},
		]},
		hint_bar(),
	]}

	L["dialogue"] = {"windows": [
		dict(banner),
		{"name": "box", "visible": "box_visible", "anchor": "top", "y": TOP, "w": "W - 80", "h": "has_choice ? t(20) + len(options) * t(17) + 20 : t(92)", "frame": "panel", "padding": [12, 10, 12, 8], "elements": [
			{"type": "role", "role": "dialogue"},
			{"type": "group", "visible": "speaker != ''", "elements": [
				{"type": "fill", "w": 52, "h": 52, "color": "panel_edge"},
				{"type": "fill", "x": 1, "y": 1, "w": 50, "h": 50, "color": "track"},
				{"type": "image", "image": "portrait", "x": 2, "y": 2, "w": 48, "h": 48},
				text("speaker", x=64, color="accent"),
			]},
			{"type": "group", "visible": "has_line", "elements": [
				text("line", x="speaker != '' ? 64 : 4", y="speaker != '' ? t(16) : 2", max_width="cw - (speaker != '' ? 64 : 4) - 18", wrap=True,
					reveal="revealed", line_height="line_step + 4", lines=4),
				text("hint", w="cw - 18", align="right", y="ch - t(10)", style="small_dim"),
				{"type": "image", "visible": "all_shown", "image": "arrow", "x": "cw - 12", "y": "ch - 12 + clock_ms / max(1, next_arrow_ms) % 2 * 2", "w": 12, "h": 12},
			]},
			{"type": "group", "visible": "has_choice", "elements": [
				text("prompt", x=4, color="accent"),
				{"type": "rows", "items": "options", "selected": "cursor", "x": 22, "y": "t(20)", "w": 260, "row_height": "t(17)", "height": "t(15)"},
			]},
			{"type": "role", "role": "menu"},
		]},
	]}

	L["inn"] = {"windows": [
		{"name": "box", "anchor": "top", "y": TOP, "w": "W - 80", "h": "t(92)", "frame": "panel", "padding": [12, 10, 12, 8], "elements": [
			{"type": "box", "frame": "card", "w": 52, "h": 52},
			{"type": "image", "image": "bed", "x": 2, "y": 2, "w": 48, "h": 48},
			*[dict(e, x=64, w="cw - 64") if e["type"] == "fill" else dict(e, x=64) for e in heading(key="ui.inn.title")],
			text(key="ui.inn.cost", args=["cost", "money"], x=64, y="t(20)", max_width="cw - 64"),
			{"type": "rows", "items": "options", "selected": "cursor", "x": 80, "y": "t(40)", "w": 200, "row_height": "t(17)", "height": "t(15)"},
		]},
	]}

	L["confirm"] = {"windows": [
		{"name": "dim", "elements": [{"type": "fill", "w": "W", "h": "H", "color": "black", "alpha": 0.5}]},
		{"name": "box", "anchor": "center", "y": -20, "w": "max(t(240), measure(question, size_body) + 40)", "h": "t(38) + 2 * t(17) + 16", "frame": "panel",
			"padding": [14, 10, 14, 8], "elements": [
			*heading(key="ui.confirm.title"),
			text("question", y="t(20)"),
			{"type": "rows", "items": "options", "selected": "on_yes ? 0 : 1", "x": 18, "y": "t(38)", "w": "t(120)", "row_height": "t(17)", "height": "t(15)"},
		]},
	]}

	L["pause"] = {"windows": [
		{"name": "scene", "elements": [{"type": "slot", "slot": "scene"}]},
		dict(hud),
		{"name": "dim", "elements": [{"type": "fill", "w": "W", "h": "H", "color": "black", "alpha": 0.5}]},
		{"name": "box", "anchor": "right", "x": 28, "w": "t(180)", "h": "t(24) + len(rows) * t(18) + 18", "frame": "panel", "padding": [14, 10, 14, 8], "elements": [
			*heading(key="ui.pause.title"),
			{"type": "rows", "items": "rows", "selected": "cursor", "x": 8, "y": "t(24)", "w": "cw - 10", "row_height": "t(18)", "height": "t(16)"},
		]},
		hint_bar(),
	]}

	L["menu"] = {"windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "nav", "x": 8, "y": TOP, "w": 136, "h": f"H - 16 - {TOP} - {HINT_H}", "frame": "panel", "padding": [12, 10, 10, 10], "elements": [
			*heading(key="ui.menu.title"),
			{"type": "rows", "items": "tabs", "selected": "tab", "x": 6, "y": "t(22)", "w": "cw - 2", "row_height": "t(17)", "height": "t(15)"},
			{"type": "fill", "y": "ch - t(18)", "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
			text("money", y="ch - t(12)", max_width="cw", style="small"),
		]},
		{"name": "box", "x": 152, "y": TOP, "w": "W - 160", "h": f"H - 16 - {TOP} - {HINT_H}", "frame": "panel", "padding": [14, 14, 14, 12], "elements": [
			{"type": "slot", "slot": "page", "w": "cw", "h": "ch"},
		]},
		{"name": "hint", "x": 0, "y": f"H - {HINT_H}", "h": HINT_H, "elements": [
			{"type": "fill", "w": "W", "h": HINT_H, "color": "black", "alpha": 0.7},
			{"type": "fill", "w": 3, "h": HINT_H, "color": "accent"},
			{"type": "hints", "value": "hint", "x": 12, "y": "t(3)", "w": "W - 24"},
		]},
		{"name": "toasts", "elements": [{"type": "slot", "slot": "toasts", "x": 0, "y": f"H - {HINT_H} - 24", "w": "W - 16", "h": 0}]},
	]}

	L["menu_status"] = {"windows": [
		{"name": "members", "visible": "view == 'members'", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			{"type": "list", "items": "members", "state": "list", "w": "cw * 2 / 3", "h": "t(18) * 6", "row_height": "t(18)", "height": "t(16)"},
		]},
		{"name": "header", "visible": "view == 'stats' || view == 'abilities'", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			{"type": "box", "frame": "card", "w": 52, "h": 52},
			{"type": "image", "image": "portrait", "x": 2, "y": 2, "w": 48, "h": 48},
			text(key="ui.status.header", args=["name", "level"], x=62, color="accent", max_width="cw - 62 - 140"),
			text(key="ui.status.exp_next", args=["experience", "experience_next"], x=62, y="t(14)", style="small_dim", max_width="cw - 62"),
			{"type": "value_bar", "key": "ui.stat_short.hp", "value": "hp", "max": "max_hp", "x": 62, "y": "t(28)", "w": "(cw - 62) / 2 - 12",
				"color": "hp * 4 <= max_hp ? 'bad' : 'good'", "gap": "t(12)", "bar_h": 4},
			{"type": "value_bar", "key": "ui.stat_short.tp", "value": "tp", "max": "max_tp", "x": "62 + (cw - 62) / 2 + 4", "y": "t(28)", "w": "(cw - 62) / 2 - 12",
				"color": "tp", "gap": "t(12)", "bar_h": 4},
			{"type": "repeater", "items": "pages", "layout": "row", "x": "cw - 136", "dx": 70, "template": [
				text("text", color="i == page ? 'accent' : 'dim'"),
				{"type": "fill", "visible": "i == page", "y": "t(13)", "w": 62, "h": 2, "color": "accent"},
			]},
		]},
		{"name": "stats", "visible": "view == 'stats'", "x": "area_x", "y": "area_y + max(64, t(46) + 8)", "w": "area_w", "h": "area_h - max(64, t(46) + 8)", "elements": [
			{"type": "fill", "y": -6, "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
			{"type": "list", "items": "stat_rows", "state": "sub_list", "view": True, "h": "ch", "row_height": "t(15)", "row": [
				{"type": "repeater", "items": "cells", "dx": "cw / 2 + 12", "template": [
					text("label", visible="shown && value == ''", style="label"),
					text("label", visible="shown && value != ''", x=10, max_width="cw / 2 - 12 - min(measure(value, size_small), (cw / 2 - 12) / 2) - 30", style="small"),
					text("value", visible="shown && value != ''", x="label == '' ? 20 : cw / 2 - 12 - 16 - tw",
						max_width="label == '' ? cw / 2 - 12 - 30 : cw / 2 - 12 - measure(fit(label, cw / 2 - 12 - min(measure(value, size_small), (cw / 2 - 12) / 2) - 30, size_small), size_small) - 40",
						color="label == '' ? 'dim' : 'text'", style="small"),
				]},
			]},
		]},
		{"name": "abilities", "visible": "view == 'abilities'", "x": "area_x", "y": "area_y + max(64, t(46) + 8)", "w": "area_w", "h": "area_h - max(64, t(46) + 8)", "elements": [
			{"type": "fill", "y": -6, "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
			text(key="ui.status.no_abilities", visible="len(abilities) == 0", x=14, color="dim"),
			{"type": "group", "visible": "len(abilities) > 0", "elements": [
				text(key="ui.status.kind", x="(picking ? cw * 3 / 5 : cw) * 48 / 100", style="small_dim"),
				text(key="ui.status.cost", x="(picking ? cw * 3 / 5 : cw) * 70 / 100", style="small_dim"),
				text(key="ui.status.uses", x="(picking ? cw * 3 / 5 : cw) * 84 / 100", style="small_dim"),
				{"type": "list", "items": "abilities", "state": "sub_list", "active": "!picking", "y": "t(18)", "w": "picking ? cw * 3 / 5 : cw",
					"h": "t(17) * 8", "row_height": "t(17)", "row": [
					*row_style("(picking ? cw * 3 / 5 : cw) - 14"),
					text("label", x=14, max_width="(picking ? cw * 3 / 5 : cw) * 48 / 100 - 24", color="!usable ? 'dim' : selected ? 'accent' : 'text'"),
					text("kind", x="(picking ? cw * 3 / 5 : cw) * 48 / 100", style="small_dim", max_width="(picking ? cw * 3 / 5 : cw) * 22 / 100 - 4"),
					text("cost", x="(picking ? cw * 3 / 5 : cw) * 70 / 100", style="small"),
					text("uses", x="(picking ? cw * 3 / 5 : cw) * 84 / 100", style="small"),
				]},
				{"type": "box", "frame": "card", "y": "t(18) + t(17) * 8 + 6", "w": "(picking ? cw * 3 / 5 : cw) - 8", "h": "t(13) * 3 + 10"},
				text("description", visible="description != ''", x=8, y="t(18) + t(17) * 8 + 11", wrap=True, max_width="(picking ? cw * 3 / 5 : cw) - 24", lines=3,
					line_height="t(13)", style="small"),
				text(key="ui.common.no_description", visible="description == ''", x=8, y="t(18) + t(17) * 8 + 11", wrap=True,
					max_width="(picking ? cw * 3 / 5 : cw) - 24", lines=3, line_height="t(13)", style="small_dim"),
				text(key="ui.status.usable_here", visible="!picking", w="cw - 8", align="right", y="ch - t(12)", style="small_dim"),
			]},
			{"type": "group", "visible": "len(abilities) > 0 && picking", "x": "cw * 3 / 5 + 16", "elements": [
				text(key="ui.menu.use_on_whom", args=["target_name"], color="accent", max_width="cw - cw * 3 / 5 - 16"),
				{"type": "list", "items": "targets", "state": "target_list", "y": "t(20)", "w": "cw - cw * 3 / 5 - 16", "h": "t(18) * 5",
					"row_height": "t(18)", "height": "t(16)"},
			]},
		]},
	]}

	L["menu_items"] = {"windows": [
		{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			text(key="ui.items.sorted_by", args=["sort_name"], style="label"),
			text(key="ui.items.totals", args=["item_count", "item_total"], w="(picking ? cw / 2 - 16 : cw) - 8", align="right", style="small_dim"),
			text(key="ui.items.none", visible="item_count == 0", x=14, y="t(20)", color="dim"),
			{"type": "list", "items": "items", "state": "list", "y": "t(20)", "w": "picking ? cw / 2 - 16 : cw", "h": "t(18) * 9", "active": "!picking",
				"row_height": "t(18)", "height": "t(16)"},
			{"type": "group", "visible": "item_count > 0", "y": "t(20) + t(18) * 9 + 8", "elements": [
				{"type": "box", "frame": "card", "w": "(picking ? cw / 2 - 16 : cw) - 8", "h": "t(13) * 3 + 12"},
				text("description", visible="description != ''", x=8, y=6, wrap=True, max_width="(picking ? cw / 2 - 16 : cw) - 24", lines=3,
					line_height="t(13)", style="small"),
				text(key="ui.common.no_description", visible="description == ''", x=8, y=6, wrap=True, max_width="(picking ? cw / 2 - 16 : cw) - 24",
					lines=3, line_height="t(13)", style="small_dim"),
			]},
			{"type": "group", "visible": "picking", "x": "cw / 2", "elements": [
				{"type": "fill", "x": -10, "w": 1, "h": "ch", "color": "panel_edge", "alpha": 0.35},
				text(key="ui.menu.use_on_whom", args=["target_name"], color="accent", max_width="cw / 2"),
				{"type": "list", "items": "members", "state": "member_list", "y": "t(20)", "w": "cw / 2", "h": "t(18) * 5", "row_height": "t(18)",
					"height": "t(16)"},
			]},
		]},
	]}

	col = "area_w * 46 / 100"
	right_x = f"area_x + {col} + 16"
	right_w = f"area_w - {col} - 16"
	compare = lambda y: {"type": "repeater", "items": "compare_rows", "y": y, "dy": "t(14)", "template": [
		text("label", style="small", max_width="cw * 50 / 100"),
		text("before", x="cw * 55 / 100", style="small_dim"),
		text("'->'", x="cw * 70 / 100", style="small_dim"),
		text("after", x="cw * 82 / 100", color="after > before ? 'good' : after < before ? 'bad' : 'text'", style="small"),
	]}
	L["menu_equip"] = {"windows": [
		{"name": "page", "x": "area_x", "y": "area_y", "w": col, "h": "area_h", "elements": [
			{"type": "list", "visible": "view == 'members'", "items": "members", "state": "list", "h": "t(18) * 6", "row_height": "t(18)", "height": "t(16)"},
			{"type": "group", "visible": "view == 'slots' || view == 'pieces'", "elements": [
				text("member_name", color="accent"),
				{"type": "list", "items": "slot_rows", "state": "slot_list", "active": "view == 'slots'", "y": "t(18)", "h": "t(17) * len(slot_rows)",
					"row_height": "t(17)", "height": "t(15)"},
			]},
			{"type": "group", "visible": "view == 'pieces'", "y": "t(18) + t(17) * len(slot_rows) + t(12)", "elements": [
				{"type": "fill", "y": -6, "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
				text(key="ui.equip.for_slot", args=["slot_name"], style="label"),
				text(key="ui.equip.nothing_fits", visible="len(choices) == 0", x=14, y="t(18)", color="dim"),
				{"type": "list", "items": "choices", "state": "choice_list", "enabled": "enabled", "y": "t(18)", "row_height": "t(17)", "height": "t(15)",
					"h": "min(t(17) * 6, ch - (t(18) + t(17) * len(slot_rows) + t(12)) - t(18))"},
			]},
		]},
		{"name": "worn", "visible": "right == 'worn'", "x": right_x, "y": "area_y", "w": right_w, "h": "area_h", "elements": [
			{"type": "fill", "x": -9, "w": 1, "h": "ch", "color": "panel_edge", "alpha": 0.35},
			text(key="ui.equip.worn_here", color="accent"),
			text("worn_text", y="t(18)", max_width="cw", style="small"),
			{"type": "group", "visible": "has_change", "elements": [
				text("change_text", y="t(42)", max_width="cw", color="accent"),
				compare("t(62)"),
			]},
		]},
		{"name": "piece", "visible": "right == 'piece'", "x": right_x, "y": "area_y", "w": right_w, "h": "area_h", "elements": [
			{"type": "fill", "x": -9, "w": 1, "h": "ch", "color": "panel_edge", "alpha": 0.35},
			text(key="ui.equip.worn_now", style="label"),
			text("worn_text", y="t(14)", max_width="cw", style="small"),
			text(key="ui.equip.after_removing", visible="removing", y="t(36)", color="accent"),
			text("piece_name", visible="has_preview", y="t(36)", max_width="cw", color="accent"),
			{"type": "group", "visible": "has_preview", "y": "t(54)", "elements": [
				text("description", visible="description != ''", wrap=True, max_width="cw", lines=3, line_height="t(13)", style="small"),
				text(key="ui.common.no_description", visible="description == ''", wrap=True, max_width="cw", lines=3, line_height="t(13)", style="small_dim"),
				text("effects", y="t(44)", wrap=True, max_width="cw", line_height="t(13)", style="small"),
				text("wearers", y="t(44) + effect_lines * t(13)", max_width="cw", style="small_dim"),
				text("refusal", visible="!can_wear", y="t(44) + effect_lines * t(13) + t(18)", max_width="cw", color="bad", style="small"),
				text("with_it", visible="can_wear", y="t(44) + effect_lines * t(13) + t(18)", max_width="cw", color="accent"),
			]},
			dict(compare("removing ? t(54) : t(54) + t(44) + effect_lines * t(13) + t(18) + t(20)"), visible="removing || has_preview && can_wear"),
			{"type": "repeater", "visible": "has_preview && can_wear", "items": "notes", "dy": "t(13)",
				"y": "t(54) + t(44) + effect_lines * t(13) + t(18) + t(20) + t(14) * len(compare_rows) + t(4)", "template": [
				text("text", max_width="cw", style="small"),
			]},
		]},
	]}

	L["save_slots"] = {"windows": [
		{"name": "slots", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			{"type": "list", "items": "slots", "state": "list", "active": "active", "row_height": "t(32)", "h": "min(ch, t(32) * 7)", "row": [
				*row_style("cw - 14", "t(30)"),
				text("head", x=14, max_width="cw - 40", color="!enabled ? 'dim' : selected ? 'accent' : 'text'"),
				text("detail", visible="detail != ''", x=28, y="t(14)", max_width="cw - 54", color="detail_color", style="small"),
			]},
		]},
	]}

	L["menu_order"] = {"windows": [
		{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			{"type": "list", "items": "rows", "state": "list", "w": "cw * 2 / 3", "h": "t(18) * 6", "row_height": "t(18)", "height": "t(16)"},
		]},
	]}

	L["menu_options"] = {"windows": [
		{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			text(key="ui.menu.options_about", max_width="cw"),
			{"type": "rows", "items": "options", "selected": 0, "x": 14, "y": "t(24)", "w": "t(200)", "row_height": "t(18)", "height": "t(16)"},
		]},
	]}

	L["menu_quests"] = {"windows": [
		{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			{"type": "list", "items": "rows", "state": "list", "view": True, "row_height": "t(16)", "row": [
				text("text", visible="text != ''", x="heading ? 0 : 16", max_width="cw - 30", color="heading ? 'accent' : 'text'"),
			]},
		]},
	]}

	L["menu_codex"] = {"windows": [
		{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			text(key="ui.codex.none", visible="len(titles) == 0", x=14, color="dim"),
			{"type": "group", "visible": "len(titles) > 0", "elements": [
				{"type": "list", "items": "titles", "state": "list", "w": "cw * 2 / 5", "row_height": "t(17)", "height": "t(15)"},
				{"type": "fill", "x": "cw * 2 / 5 + 8", "w": 1, "h": "ch", "color": "panel_edge", "alpha": 0.35},
				text("title", x="cw * 2 / 5 + 20", color="accent", max_width="cw - cw * 2 / 5 - 20"),
				text("body", x="cw * 2 / 5 + 20", y="t(20)", wrap=True, max_width="cw - cw * 2 / 5 - 20", lines="(ch - t(20)) / t(13)", line_height="t(13)",
					style="small"),
			]},
		]},
	]}

	L["menu_log"] = {"params": {"wrap_width": "area_w - 20", "rows": "max(1, area_h / t(13))"}, "windows": [
		{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			{"type": "repeater", "items": "lines", "dy": "t(13)", "template": [text("text", style="small")]},
			{"type": "scroll_bar", "visible": "total > rows", "x": "cw - 4", "h": "rows * t(13) - 4", "top": "first", "rows": "rows", "count": "total"},
		]},
	]}

	L["quest_log"] = {"windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "box", "anchor": "top", "y": TOP, "w": "W - 96", "h": f"H - 16 - {TOP}", "frame": "panel", "padding": [16, 12, 16, 12], "elements": [
			*heading(key="ui.quest_log.title"),
			{"type": "slot", "slot": "quests", "y": "t(22)", "w": "cw", "h": "ch - t(22)"},
		]},
	]}

	L["text_log"] = {"windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "box", "anchor": "top", "y": TOP, "w": "W - 96", "h": f"H - 16 - {TOP}", "frame": "panel", "padding": [16, 12, 16, 12], "elements": [
			*heading(key="ui.text_log.title"),
			{"type": "slot", "slot": "log", "y": "t(22)", "w": "cw", "h": "ch - t(22)"},
		]},
	]}

	L["menu_macros"] = {"windows": [
		{"name": "macros", "x": "area_x", "y": "area_y", "w": "area_w * 2 / 5 - 8", "h": "area_h", "elements": [
			text(key="ui.macros.count", args=["macro_count"], style="label"),
			{"type": "list", "items": "macro_rows", "state": "list", "active": "stage == 'list'", "y": "t(18)", "h": "t(17) * 8", "row_height": "t(17)",
				"height": "t(15)"},
		]},
		{"name": "detail", "x": "area_x + area_w * 2 / 5 + 8", "y": "area_y", "w": "area_w - area_w * 2 / 5 - 8", "h": "area_h", "elements": [
			{"type": "fill", "x": -8, "w": 1, "h": "ch", "color": "panel_edge", "alpha": 0.35},
			text(key="ui.macros.new_help", visible="!has_macro", y="t(18)", style="small_dim", max_width="cw"),
			text("title", visible="has_macro", max_width="cw", style="label"),
			{"type": "group", "visible": "stage == 'name'", "elements": [
				text("name_prompt", y="t(36)", color="accent", max_width="cw"),
				{"type": "slot", "slot": "name_pad", "y": "t(56)", "w": "cw - 8"},
			]},
			{"type": "group", "visible": "has_macro && stage == 'actions'", "elements": [
				{"type": "list", "items": "actions", "state": "sub_list", "y": "t(18)", "h": "t(17) * 6", "row_height": "t(17)", "height": "t(15)"},
				{"type": "repeater", "items": "step_notes", "x": 14, "y": "t(18) + t(17) * 6 + t(8)", "dy": "t(13)", "template": [
					text("text", max_width="cw - 20", color="ok ? 'dim' : 'bad'", style="small"),
				]},
			]},
			{"type": "list", "visible": "has_macro && stage != 'name' && stage != 'actions'", "items": "steps", "state": "sub_list", "active": "stage == 'steps'",
				"enabled": "enabled", "y": "t(18)", "h": "stage == 'list' ? ch - t(18) : t(17) * min(6, len(steps))", "row_height": "t(17)", "height": "t(15)"},
			{"type": "group", "visible": "has_macro && building", "y": "t(18) + t(17) * min(6, len(steps)) + t(10)", "elements": [
				{"type": "fill", "y": "-t(4)", "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
				text("what", style="label"),
				{"type": "list", "items": "stage_rows", "state": "stage_list", "y": "t(18)", "row_height": "t(17)", "height": "t(15)",
					"h": "ch - (t(18) + t(17) * min(6, len(steps)) + t(10)) - t(18)"},
			]},
		]},
	]}

	L["menu_combos"] = {"windows": [
		{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			text(key="ui.combos.discovered", args=["discovered", "log_total"], style="label"),
			{"type": "list", "items": "rows", "state": "list", "y": "t(18)", "h": "t(17) * 9", "enabled": "enabled", "row_height": "t(17)", "height": "t(15)"},
			{"type": "group", "y": "t(18) + t(17) * 9 + 8", "elements": [
				{"type": "box", "frame": "card", "w": "cw", "h": "t(13) * 3 + 12"},
				{"type": "group", "visible": "has_entry", "x": 8, "y": 6, "elements": [
					text("members_text", max_width="cw - 16", style="small"),
					text("abilities_text", y="t(13)", max_width="cw - 16", style="small"),
					text(key="ui.combos.first_found", args=["found_time", "make_key"], y="t(26)", style="small_dim", max_width="cw - 16"),
				]},
				text(key="ui.combos.not_discovered", visible="!has_entry && total > 0", x=8, y=6, style="small_dim"),
			]},
		]},
	]}

	L["save_prompt"] = {"windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "box", "anchor": "top", "y": TOP, "w": 440, "h": "t(42) + t(32) * 7 + t(28) + 30", "frame": "panel", "padding": [16, 12, 16, 12], "elements": [
			*heading(key="ui.save_prompt.title"),
			text(key="ui.save_prompt.question", y="t(20)", max_width="cw"),
			{"type": "slot", "slot": "slot_list", "x": 8, "y": "t(42)", "w": "cw - 8", "h": "ch - t(42) - t(28)"},
			{"type": "rows", "items": "leave", "selected": "leave_selected ? 0 : -1", "x": 14, "y": "ch - t(16)", "w": "cw - 20", "row_height": "t(18)", "height": "t(16)"},
		]},
	]}

	L["load_game"] = {"windows": [
		{"name": "scene", "elements": [
			{"type": "slot", "slot": "scene"},
			{"type": "fill", "w": "W", "h": "H", "color": "black", "alpha": 0.45},
		]},
		{"name": "box", "x": "W - 16 - w", "y": TOP, "w": 420, "h": f"H - 16 - {TOP} - {HINT_H}", "frame": "panel", "padding": [16, 12, 16, 12], "elements": [
			*heading(key="ui.saves.load_title"),
			text(key="ui.saves.none", visible="!has_saves", x=14, y="t(24)", color="dim"),
			{"type": "slot", "slot": "slot_list", "x": 8, "y": "t(24)", "w": "cw - 8", "h": "ch - t(24)"},
			{"type": "slot", "slot": "toasts", "y": "ch - 4", "w": "cw", "h": 0},
		]},
		{"name": "hint", "x": 0, "y": f"H - {HINT_H}", "h": HINT_H, "elements": [
			{"type": "fill", "w": "W", "h": HINT_H, "color": "black", "alpha": 0.7},
			{"type": "fill", "w": 3, "h": HINT_H, "color": "accent"},
			{"type": "hints", "value": "hint", "x": 12, "y": "t(3)", "w": "W - 24"},
		]},
	]}

	L["sound_test"] = {"windows": [
		{"name": "scene", "elements": [
			{"type": "slot", "slot": "scene"},
			{"type": "fill", "w": "W", "h": "H", "color": "black", "alpha": 0.45},
		]},
		{"name": "box", "x": "W - 16 - w", "y": TOP, "w": 360, "h": f"H - 16 - {TOP} - {HINT_H}", "frame": "panel", "padding": [16, 12, 16, 12], "elements": [
			*heading(key="ui.sound_test.title"),
			{"type": "list", "items": "tracks", "state": "list", "x": 8, "y": "t(24)", "w": "cw - 8", "h": "t(17) * 14", "row_height": "t(17)", "height": "t(15)"},
			text(key="ui.sound_test.playing", args=["playing"], visible="playing != ''", y="ch - t(12)", color="accent", max_width="cw"),
		]},
		{"name": "hint", "x": 0, "y": f"H - {HINT_H}", "h": HINT_H, "elements": [
			{"type": "fill", "w": "W", "h": HINT_H, "color": "black", "alpha": 0.7},
			{"type": "fill", "w": 3, "h": HINT_H, "color": "accent"},
			{"type": "hints", "value": "hint", "x": 12, "y": "t(3)", "w": "W - 24"},
		]},
	]}

	L["options"] = {"windows": [
		{"name": "scene", "visible": "title_mode", "elements": [
			{"type": "slot", "slot": "scene"},
			{"type": "fill", "w": "W", "h": "H", "color": "black", "alpha": 0.45},
		]},
		{"name": "backdrop", "visible": "!title_mode", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "box", "x": 16, "y": TOP, "w": "W - 32", "h": f"H - 8 - {TOP}", "frame": "panel", "padding": [16, 12, 16, 10], "elements": [
			*heading(value="title"),
			{"type": "group", "visible": "!on_controls", "elements": [
				{"type": "list", "items": "rows", "state": "list", "x": 14, "y": "t(24)", "w": "cw - 14", "h": "t(17) * 16", "row_height": "t(17)", "row": [
					*row_style("cw - 28"),
					text("label", x=14, color="!enabled ? 'dim' : selected ? 'accent' : 'text'", max_width="(cw - 14) * 52 / 100 - 20"),
					text("value == '' ? '' : selected && changeable ? '< ' + value + ' >' : value", x="(cw - 14) * 52 / 100",
						color="selected ? 'accent' : 'text'", max_width="(cw - 14) * 48 / 100 - 20"),
				]},
				{"type": "box", "frame": "card", "y": "ch - t(13) * 2 - t(16) - 18", "w": "cw", "h": "t(13) * 2 + 10"},
				text("description", x=8, y="ch - t(13) * 2 - t(16) - 13", wrap=True, max_width="cw - 16", lines=2, line_height="t(13)", style="small_dim"),
			]},
			{"type": "slot", "visible": "on_controls", "slot": "controls", "x": 14, "y": "t(24)", "w": "cw - 14", "h": "ch - t(24) - t(16) - 8"},
			{"type": "slot", "slot": "toasts", "y": "ch - t(16) - 26", "w": "cw", "h": 0},
			{"type": "fill", "y": "ch - t(13)", "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
			{"type": "hints", "value": "hint", "y": "ch - t(10)", "w": "cw"},
		]},
	]}

	L["controls"] = {"windows": [
		{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			text(key="ui.options.keys", x="cw * 34 / 100", style="label"),
			text(key="ui.options.gamepad", x="cw * 66 / 100", style="label"),
			{"type": "list", "items": "actions", "state": "list", "active": "active", "probe": False, "y": "t(18)", "h": "ch - t(18)", "row_height": "t(17)", "row": [
				*row_style("cw - 14"),
				text("label", x=14, color="selected ? 'accent' : 'text'", max_width="cw * 34 / 100 - 24"),
				text("keys", visible="!reset", x="cw * 34 / 100", max_width="cw * 66 / 100 - cw * 34 / 100 - 12", color="selected ? 'accent' : 'text'", style="small"),
				text("buttons", visible="!reset", x="cw * 66 / 100", max_width="cw - cw * 66 / 100 - 20", color="selected ? 'accent' : 'text'", style="small"),
			]},
		]},
		{"name": "action", "visible": "picking", "x": "area_x + area_w - t(220) - 8", "y": "area_y + t(24)", "w": "t(220)", "h": "t(24) + len(choices) * t(17) + 14",
			"frame": "panel", "padding": [14, 10, 14, 8], "elements": [
			*heading(value="action_name"),
			{"type": "rows", "items": "choices", "selected": "choice", "x": 8, "y": "t(24)", "w": "cw - 10", "row_height": "t(17)", "height": "t(15)"},
		]},
		{"name": "capture", "visible": "capturing", "anchor": "center", "w": "measure(ask, size_body) + 40", "h": "t(40) + 8", "frame": "panel", "padding": [20, 10, 20, 8],
			"elements": [
			*heading(key="ui.options.rebind"),
			text("ask", y="t(20)"),
		]},
	]}

	L["name_entry"] = {"windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "box", "anchor": "top", "y": TOP, "w": "min(W - 16, max(560, t(40) * 13 + 40))", "h": "t(20) + 44 + entry_h + t(6) + t(12) + 22",
			"frame": "panel", "padding": [16, 12, 16, 10], "elements": [
			*heading(key="ui.name_entry.title"),
			{"type": "box", "frame": "card", "y": "t(20)", "w": 36, "h": 36},
			{"type": "image", "image": "portrait", "x": 2, "y": "t(20) + 2", "w": 32, "h": 32},
			text(key="ui.name_entry.prompt", args=["member_name"], x=48, y="t(20) + 12", color="accent", max_width="cw - 48"),
			{"type": "slot", "slot": "entry", "y": "t(20) + 44", "w": "cw", "h": "entry_h"},
			{"type": "hints", "value": "hint", "y": "t(20) + 44 + entry_h + t(6)", "w": "cw"},
		]},
	]}

	L["travel"] = {"windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "box", "anchor": "top", "y": TOP, "w": 440, "h": "t(40) + min(len(labels), 8) * t(17) + t(13) * 2 + 34", "frame": "panel", "padding": [16, 12, 16, 10],
			"elements": [
			*heading(value="title"),
			text("question", y="t(20)", max_width="cw"),
			{"type": "list", "items": "labels", "state": "list", "x": 8, "y": "t(40)", "w": "cw - 8", "h": "min(len(labels), 8) * t(17)", "row_height": "t(17)",
				"height": "t(15)"},
			{"type": "box", "frame": "card", "y": "ch - t(13) * 2 - 10", "w": "cw", "h": "t(13) * 2 + 10"},
			text("description", x=8, y="ch - t(13) * 2 - 5", wrap=True, max_width="cw - 16", lines=2, line_height="t(13)", style="small",
				color="description_missing ? 'dim' : 'text'"),
		]},
		hint_bar(),
		{"name": "toasts", "elements": [{"type": "slot", "slot": "toasts", "x": 16, "y": f"H - {HINT_H} - 8"}]},
	]}

	L["shop"] = {"windows": [
		{"name": "backdrop", "elements": [{"type": "slot", "slot": "backdrop"}]},
		{"name": "box", "x": 16, "y": TOP, "w": "W - 32", "h": f"H - 8 - {TOP}", "frame": "panel", "padding": [16, 12, 16, 10], "elements": [
			text("title", color="accent"),
			{"type": "group", "visible": "open", "elements": [
				{"type": "repeater", "items": "tabs", "layout": "row", "x": "measure(title, size_body) + 24", "dx": 72, "template": [
					text("text", color="i == tab ? 'accent' : 'dim'"),
					{"type": "fill", "visible": "i == tab", "y": "t(13)", "w": 64, "h": 2, "color": "accent"},
				]},
				text("money", w="cw", align="right"),
				{"type": "fill", "y": "t(20)", "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
				text(key="ui.shop.price", visible="tab == 0", w=300, align="right", y="t(26)", style="small_dim"),
				text(key="ui.shop.each", visible="tab != 0", w=300, align="right", y="t(26)", style="small_dim"),
				text(key="ui.shop.have", x=316, y="t(26)", style="small_dim"),
				{"type": "list", "items": "rows", "state": "list", "probe": False, "x": 6, "y": "t(42)", "w": 360, "h": "t(18) * 10", "row_height": "t(18)", "row": [
					*row_style(358, "t(16)"),
					text("text", x=14, max_width=220, color="selected ? 'accent' : 'text'"),
					text("price", w=294, align="right", color="selected ? 'accent' : 'text'"),
					text(key="ui.common.count", args=["have"], x=310, color="dim"),
				]},
				{"type": "fill", "x": 384, "y": "t(28)", "w": 1, "h": "ch - t(28) - t(16)", "color": "panel_edge", "alpha": 0.35},
				{"type": "box", "frame": "card", "x": 400, "y": "t(30)", "w": 72, "h": 72},
				{"type": "image", "image": "stall", "x": 404, "y": "t(30) + 4", "w": 64, "h": 64},
				{"type": "group", "visible": "len(rows) > 0", "x": 400, "y": "t(30) + 84", "elements": [
					text("description", wrap=True, max_width="cw - 400", lines=6, line_height="t(13)", style="small", color="description_missing ? 'dim' : 'text'"),
					text("compare", visible="compare != ''", y="t(13) * 6 + 8", wrap=True, max_width="cw - 400", lines=6, line_height="t(13)", style="small_dim"),
				]},
				text(key="ui.shop.nothing_for_sale", visible="len(rows) == 0 && tab == 0", x=20, y="t(42)", color="dim"),
				text(key="ui.shop.nothing_to_sell", visible="len(rows) == 0 && tab != 0", x=20, y="t(42)", color="dim"),
				{"type": "slot", "slot": "toasts", "x": 16, "y": "ch - t(16)"},
				{"type": "fill", "y": "ch - t(13)", "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
				{"type": "hints", "value": "hint", "y": "ch - t(10)", "w": "cw"},
			]},
		]},
	]}

	L["panels"] = None  # the built-in file: every rectangle comes from the screen's items
	L["battle_front"] = battle_front()
	L["battle_side"] = battle_side()
	return L


# The battle's windows both layouts share.
def battle_common(stage_x):
	mid = f"{stage_x} + (W - {stage_x} - w) / 2"
	log = {"name": "log", "visible": "log_shown", "x": stage_x + 8, "y": 8, "w": f"W - {stage_x} - 16", "h": "4 * 13 + 14", "frame": "panel", "opacity": 0.94,
		"elements": [{"type": "repeater", "items": "log_lines", "x": 12, "y": 8, "dy": 13, "template": [
			text("text", size="size_small", color="newest ? 'text' : 'dim'"),
		]}]}
	banner = lambda name, visible, extra: {"name": name, "visible": visible, "x": mid, "y": "log_shown ? log.bottom + 6 : 8",
		"w": "measure(fit(banner, W - 200, banner_big ? size_title : size_body), banner_big ? size_title : size_body) + 32",
		"h": "(banner_big ? 20 : 10) + 14", "elements": [
			{"type": "group", "alpha": "banner_alpha", "elements": [
				{"type": "box", "w": "cw", "h": "ch", "opacity": 0.95},
				*extra,
				text("banner", max_width="W - 200", x=16, y=7, size="banner_big ? size_title : size_body", color="banner_cancel ? 'bad' : 'accent'"),
			]}]}
	description = {"name": "description", "visible": "description_shown", "x": stage_x + 8, "y": "log_shown ? log.bottom + 6 : 8", "w": f"W - {stage_x} - 16",
		"h": "2 * 13 + 12", "frame": "panel", "opacity": 0.92, "elements": [
		text("description", visible="description != ''", x=10, y=6, wrap=True, max_width="cw - 20", lines=2, line_height=13, style="small"),
		text(key="ui.common.no_description", visible="description == ''", x=10, y=6, wrap=True, max_width="cw - 20", lines=2, line_height=13, style="small_dim"),
	]}
	result = {"name": "result", "visible": "phase == 'ended'", "x": mid, "y": "clamp(404 - h, 296, 330)", "w": 380, "h": "52 + len(result_lines) * 14", "frame": "panel",
		"padding": [16, 10, 16, 8], "elements": [
		text(key="ui.battle.result.title", style="label"),
		text("outcome", w="cw", align="center", y=12, style="title"),
		{"type": "fill", "y": 36, "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
		{"type": "repeater", "items": "result_lines", "y": 42, "dy": 14, "template": [
			text("(kind == 'learned' ? '  ' : '') + text", max_width="cw",
				color="kind == 'level_up' ? 'accent' : kind == 'learned' ? 'good' : kind == 'fallen' ? 'bad' : 'text'"),
		]},
	]}
	return log, banner, description, result


def card(w, h):
	"""One member's card: name and status icons, HP and TP side by side with bars, the command chosen or the state."""
	half = (w - 24) // 2
	return [
		{"type": "box", "w": w, "h": h},
		{"type": "fill", "visible": "acting", "x": 4, "y": 4, "w": w - 8, "h": h - 8, "color": "row"},
		{"type": "group", "visible": "hit && hit_flash", "alpha": "hit_fade", "elements": [
			{"type": "fill", "x": 4, "y": 4, "w": w - 8, "h": h - 8, "color": "bad", "alpha": 0.55},
		]},
		{"type": "outline", "visible": "acting || targeted || hit", "w": w, "h": h, "color": "hit ? 'bad' : 'accent'", "alpha": "!hit && targeted ? target_pulse : 1"},
		text("name", x=9, y=6, max_width=f"{w - 18} - icon_count * 22", size="size_small", color="!alive ? 'dim' : acting ? 'accent' : 'text'"),
		{"type": "slot", "slot": "icons", "x": f"{w - 6} - icon_count * 22", "y": 3},
		text(key="ui.battle.hp", x=9, y=19, style="small_dim"),
		text("hp", x=9, w=half, align="right", y=19, style="small"),
		{"type": "bar", "x": 9, "y": 31, "w": half, "h": 3, "value": "hp", "max": "max_hp", "color": "hp_low ? 'bad' : 'good'"},
		text(key="ui.battle.tp", x=15 + half, y=19, style="small_dim"),
		text("tp", x=15 + half, w=half, align="right", y=19, style="small"),
		{"type": "bar", "x": 15 + half, "y": 31, "w": half, "h": 3, "value": "tp", "max": "max_tp", "color": "tp"},
		text("alive && next != '' ? next : state", visible="(alive && next != '' ? next : state) != ''", x=9, y=38, max_width=w - 18, size="size_small",
			color="alive ? 'dim' : 'bad'"),
	]


def battle_front():
	cw, ch, gap = 140, 52, 4
	stage_x = 8 + cw
	log, banner, description, result = battle_common(stage_x)
	return {
		"params": {
			"stage_bottom": 480,
			"party_x": 196, "party_dx": 92, "party_foot": 492, "party_dy": 0, "party_size": 84,
			"vehicle_x": 300, "vehicle_foot": 484, "vehicle_size": 120,
			"step_gap": 6, "step_margin": 12,
			"enemy_x": 392, "enemy_row_max": 4, "enemy_row_span": 440, "enemy_row_spacing_max": 120, "enemy_row_foot": 206, "enemy_row_stagger": 0,
			"enemy_ranks_interleaved": 1, "enemy_rank_span": 440, "enemy_front_foot": 228, "enemy_back_foot": 100,
			"enemy_size": "enemy_count <= 3 ? 104 : enemy_count <= 4 ? 96 : enemy_back ? 56 : 72",
			"log_rows": 4, "log_wrap_margin": 28,
		},
		"windows": [
			{"name": "stage", "elements": [{"type": "slot", "slot": "stage"}]},
			{"name": "strip", "x": 8, "y": 8, "w": cw, "h": f"5 * {ch + gap} - {gap}", "elements": [
				{"type": "repeater", "items": "members", "count": 5, "layout": "column", "dy": ch + gap, "template": [
					*card(cw, ch),
					{"type": "slot", "slot": "anchor", "x": cw // 2 - 48, "y": ch // 2 - 48, "w": 96, "h": 96},
				], "empty": [
					{"type": "fill", "x": 2, "y": 2, "w": cw - 4, "h": ch - 4, "color": "panel", "alpha": 0.45},
					{"type": "outline", "w": cw, "h": ch, "color": "panel_edge", "alpha": 0.35, "thickness": 1},
				]},
			]},
			{"name": "vehicle", "visible": "len(vehicle) > 0", "x": 8, "y": "strip.bottom + 8", "w": cw, "h": ch, "elements": [
				{"type": "repeater", "items": "vehicle", "template": card(cw, ch)},
			]},
			banner("name_plate", "banner != '' && banner_plate", [
				{"type": "outline", "x": 4, "y": 4, "w": "cw - 8", "h": "ch - 8", "color": "accent", "alpha": 0.5, "thickness": 1},
			]),
			{"name": "effects", "elements": [{"type": "slot", "slot": "effects"}]},
			log,
			banner("banner", "banner != '' && !banner_plate", []),
			{"name": "command", "visible": "menu_shown", "w": 156, "h": "4 * 15 + 34", "y": "492 - 84 - 6 - h", "frame": "panel", "padding": [12, 8, 10, 8],
				"x": "command_center >= 0 ? command_center - w / 2 : clamp(actor_center <= W / 2 ? actor_right + 6 : actor_left - 6 - w, 8, W - 8 - w)", "elements": [
				text("menu_title", style="label", max_width="cw - 40"),
				text(key="ui.battle.pages", visible="len(menu_items) > 4", args=["cursor + 1", "len(menu_items)"], w="cw", align="right", style="small_dim"),
				{"type": "fill", "y": 13, "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
				{"type": "rows", "items": "menu_items", "selected": "cursor", "text": "fit(text, cw - 26, size_small)", "enabled": "usable", "x": 6, "y": 20, "w": "cw - 4",
					"size": "size_small", "row_height": 15, "height": 14, "first": "clamp(cursor - 3, 0, max(0, len(menu_items) - 4))", "most": 4},
				{"type": "slot", "slot": "target_pointer"},
			]},
			description,
			{"name": "keys", "x": 0, "y": "H - 16", "w": "W", "h": 16, "elements": [
				{"type": "fill", "w": "W", "h": 16, "color": "black", "alpha": 0.7},
				{"type": "fill", "w": 3, "h": 16, "color": "accent"},
				# With --dev the developer badge stands at the right end of this line.
				text("hint", max_width="dev ? W - 180 : W - 24", x=12, y=3, style="small_dim"),
			]},
			result,
		],
	}


def battle_side():
	stage_bottom = 324
	log, banner, description, result = battle_common(0)
	result["y"] = 40
	menu_w = 200
	return {
		"params": {
			"stage_bottom": stage_bottom,
			"party_x": 470, "party_dx": 28, "party_foot": 176, "party_dy": 30, "party_size": 56,
			"vehicle_x": 520, "vehicle_foot": 250, "vehicle_size": 96,
			"enemy_x": 210, "enemy_row_max": 4, "enemy_row_span": 340, "enemy_row_spacing_max": 100, "enemy_row_foot": 236, "enemy_row_stagger": 18,
			"enemy_ranks_interleaved": 0, "enemy_rank_span": 320, "enemy_rank_spacing_max": 96, "enemy_front_foot": 250, "enemy_back_foot": 186,
			"enemy_size": "enemy_count <= 3 ? 72 : enemy_count <= 5 ? 64 : 56",
			"log_rows": 4, "log_wrap_margin": 28,
		},
		"windows": [
			{"name": "stage", "elements": [{"type": "slot", "slot": "stage"}]},
			{"name": "effects", "elements": [{"type": "slot", "slot": "effects"}]},
			log,
			banner("banner", "banner != ''", []),
			{"name": "party", "x": 8, "y": stage_bottom + 8, "w": f"W - 24 - {menu_w}", "h": f"H - {stage_bottom} - 16", "frame": "panel", "padding": [12, 10, 12, 8],
				"elements": [
				text(key="ui.battle.hp", x="cw * 46 / 100", style="small_dim"),
				text(key="ui.battle.tp", x="cw * 74 / 100", style="small_dim"),
				{"type": "repeater", "items": "party", "y": 14, "dy": "min(26, (ch - 14) / max(1, len(party)))", "template": [
					{"type": "fill", "visible": "acting", "x": -6, "y": -2, "w": "cw + 12", "h": 22, "color": "row"},
					{"type": "outline", "visible": "acting || targeted", "x": -6, "y": -2, "w": "cw + 12", "h": 22, "color": "accent", "alpha": "targeted ? target_pulse : 1",
						"thickness": 1},
					{"type": "box", "frame": "card", "w": 18, "h": 18},
					{"type": "image", "image": "portrait", "x": 1, "y": 1, "w": 16, "h": 16, "tint": "alive ? 'white' : '#50505c'"},
					text("name", x=24, y=1, max_width="cw * 24 / 100 - 4", color="!alive ? 'dim' : acting ? 'accent' : 'text'"),
					{"type": "slot", "slot": "icons", "x": "24 + cw * 24 / 100", "y": -1},
					text("hp + '/' + max_hp", x="cw * 46 / 100", w="cw * 24 / 100", align="right", style="small"),
					{"type": "bar", "x": "cw * 46 / 100", "y": 13, "w": "cw * 24 / 100", "h": 3, "value": "hp", "max": "max_hp", "color": "hp_low ? 'bad' : 'good'"},
					text("tp + '/' + max_tp", x="cw * 74 / 100", w="cw * 16 / 100", align="right", style="small"),
					{"type": "bar", "x": "cw * 74 / 100", "y": 13, "w": "cw * 16 / 100", "h": 3, "value": "tp", "max": "max_tp", "color": "tp"},
					text("alive && next != '' ? next : state", visible="(alive && next != '' ? next : state) != ''", x="cw * 92 / 100", y=1,
						max_width="cw * 8 / 100 + 6", size="size_small", color="alive ? 'dim' : 'bad'"),
				]},
			]},
			{"name": "menu", "x": f"W - 8 - {menu_w}", "y": stage_bottom + 8, "w": menu_w, "h": f"H - {stage_bottom} - 16", "frame": "panel", "padding": [12, 10, 10, 8],
				"elements": [
				text("menu_title", style="label", max_width="cw"),
				{"type": "fill", "y": 13, "w": "cw", "h": 1, "color": "panel_edge", "alpha": 0.35},
				{"type": "rows", "visible": "menu_shown", "items": "menu_items", "selected": "cursor", "text": "fit(text, cw - 26, size_small)", "enabled": "usable",
					"x": 6, "y": 20, "w": "cw - 4", "size": "size_small", "row_height": 16, "height": 14,
					"first": "clamp(cursor - (ch - 24) / 16 + 1, 0, max(0, len(menu_items) - (ch - 24) / 16))", "most": "(ch - 24) / 16"},
			]},
			description,
			{"name": "status", "x": "menu.x", "y": "menu.y", "w": "menu.w", "h": "menu.h", "padding": [12, 10, 10, 8], "elements": [
				text(key="ui.battle.auto", visible="phase == 'playback' && auto", y=22),
				text(key="ui.battle.playing", visible="phase == 'playback' && !auto", y=22),
				text(key="ui.battle.hint.auto_stops", visible="phase == 'playback' && auto", args=["key_back"], max_width="cw", y=38, style="small_dim"),
				text(key="ui.battle.hint.hurry", visible="phase == 'playback'", args=["key_confirm"], max_width="cw", y="ch - 39", style="small_dim"),
				text(key="ui.battle.hint.skip", visible="phase == 'playback'", args=["key_fast_forward"], max_width="cw", y="ch - 26", style="small_dim"),
				text(key="ui.battle.hint.log", visible="phase == 'playback'", args=["key_text_log"], max_width="cw", y="ch - 13", style="small_dim"),
				text("outcome", visible="phase == 'ended'", y=22, style="title"),
				text(key="ui.battle.hint.continue", visible="phase == 'ended'", args=["key_confirm"], y=50, max_width="cw"),
			]},
			result,
		],
	}


def dump(path, layout):
	"""One window or param per line, so the files read like the built-in ones."""
	lines = ["{", '\t"version": 1,']
	if "params" in layout:
		lines.append('\t"params": ' + json.dumps(layout["params"], ensure_ascii=False) + ",")
	lines.append('\t"windows": [')
	windows = [json.dumps(w, ensure_ascii=False) for w in layout["windows"]]
	lines += ["\t\t" + w + ("," if i < len(windows) - 1 else "") for i, w in enumerate(windows)]
	lines += ["\t]", "}"]
	path.write_text("\n".join(lines) + "\n")


def main():
	export, fonts, out = (Path(a) for a in sys.argv[1:4])
	builtin = {p.stem: json.loads(p.read_text()) for p in (export / "ui").glob("*.json")}
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(fonts, out)
	manifest = json.loads((out / "assets.json").read_text())
	for font in manifest["bitmap_fonts"]["fonts"]:
		# Small and body text at one file pixel a screen pixel, titles at two; the battle's numbers at two or three.
		font["scale"] = 0.8 if font.get("role") == "battle_numbers" else 0.6
	manifest["ui_layout"] = {"enabled": True}
	(out / "assets.json").write_text(json.dumps(manifest, indent="\t", ensure_ascii=False) + "\n")
	(out / "ui").mkdir()
	(out / "ui/styles.json").write_text(json.dumps(styles(builtin["styles"]), indent="\t") + "\n")
	layouts = screens()
	missing = sorted(set(builtin) - {"styles"} - set(layouts))
	assert not missing, f"no layout for {missing}"
	for name, layout in sorted(layouts.items()):
		dump(out / "ui" / f"{name}.json", builtin[name] if layout is None else layout)


if __name__ == "__main__":
	main()
