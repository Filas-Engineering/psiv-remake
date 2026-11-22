#!/usr/bin/env python3
"""Writes the asset directory scripts/small_more.txt plays: a copy of the small example set (make_layout_assets.py
--small, 320x224) with the menu pages the newer slots draw, sized for it:

* menu: Items, Talk (`party_talk`, `immediate`: the talk starts as the tab opens), a System tab whose one child is the
  Save page (`immediate`: the child opens in place of the list) and an Extras tab with Options, Quests and Codex as
  children, listed by the menu_children params;
* menu_options: the party talk on the page (`party_talk` "page") in a `talk_box` slot with the talk params
  (`talk_size`, `talk_name_size`, `talk_pad`, `talk_row_h`, `talk_rows`) and the text styles `talk` and `talk_name`;
* menu_children: the children list at 320x224 (`list_w`, `row_h`, `rows`, `pad`);
* alt/menu_broken.json: `immediate` that is not a boolean and on a page that cannot act at once, which the Makefile
  checks on a copy.

Usage: make_small_more_assets.py EXPORT SMALL OUT

EXPORT is the placeholder export (its ui/ files are the built-in layouts), SMALL the small example set.
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
	export, small, out = (Path(a) for a in sys.argv[1:4])
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(small, out)
	ui = out / "ui"
	built_in = export / "ui"

	menu = load(ui / "menu.json")
	menu["entries"] = [
		{"page": "items"},
		{"page": "party_talk", "immediate": True},
		{"page": "system", "immediate": True, "children": [{"page": "save"}]},
		{"page": "extras", "children": [{"page": "options"}, {"page": "quests"}, {"page": "codex"}]},
	]
	save(ui / "menu.json", menu)
	save(out / "alt/menu_broken.json", dict(menu, entries=[
		{"page": "items", "immediate": True},
		{"page": "party_talk", "immediate": "yes"},
	]))

	styles = load(ui / "styles.json")
	styles["text_styles"]["talk"] = {"size": "size_small", "color": "text"}
	styles["text_styles"]["talk_name"] = {"size": "size_small", "color": "accent"}
	save(ui / "styles.json", styles)

	options = load(built_in / "menu_options.json")
	options["params"] = {"party_talk": "page", "talk_pad": 2, "talk_row_h": 11, "talk_rows": 6}
	options["windows"] = [
		{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
			{"type": "rows", "items": "options", "selected": 0, "x": 8, "y": 4, "w": "cw - 16", "size": "size_small", "row_height": "t(14)",
				"height": "t(13)"}]},
		{"name": "talk", "visible": "talking", "x": "area_x + 4", "y": "area_y + t(22)", "w": "area_w - 8", "h": "area_h - t(26)", "frame": "panel",
			"elements": [{"type": "slot", "slot": "talk_box", "x": 6, "y": 4, "w": "cw - 12", "h": "ch - 8"}]},
	]
	save(ui / "menu_options.json", options)

	children = load(built_in / "menu_children.json")
	children["params"] = {"list_w": 110, "row_h": 14, "rows": 5, "pad": 6, "text_size": 16}
	save(ui / "menu_children.json", children)


if __name__ == "__main__":
	main()
