#!/usr/bin/env python3
"""Writes the asset directory scripts/narration.txt plays: a copy of TEST (the test assets) with its
`alt/assets_more.json` as the manifest, for the still pictures `chart` and `tower`; the styles with a `narration` text
style; and a dialogue layout with the still pictures kept as a stack (`picture_stack`), drawn side by side at the top,
and a `narration` window without a frame under them, holding four lines, with its own reveal speed and line delay.

Usage: make_narration_assets.py EXPORT TEST OUT
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
	export, test, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(test, out)
	shutil.copy(out / "alt" / "assets_more.json", out / "assets.json")
	styles = load(export / "ui" / "styles.json")
	styles["text_styles"]["narration"] = {"size": "size_body", "color": "#f0e6c8"}
	save(out / "ui" / "styles.json", styles)
	dialogue = load(export / "ui" / "dialogue.json")
	dialogue["params"] = dict(dialogue.get("params", {}), picture_stack=1, narration_lines=4, narration_cps=40, narration_delay_ms=200)
	dialogue["windows"] = [w for w in dialogue["windows"] if w["name"] != "picture"] + [
		{"name": "pictures", "visible": "len(pictures) > 0", "anchor": "top", "y": 40, "w": 600, "h": 260, "elements": [
			{"type": "repeater", "items": "pictures", "layout": "row", "dx": 236, "template": [
				{"type": "box", "w": "picture_w + 16", "h": "picture_h + 16"},
				{"type": "image", "image": "picture", "x": 8, "y": 8, "w": "picture_w", "h": "picture_h"},
			]},
		]},
		{"name": "narration", "visible": "narration_visible", "x": 120, "y": 200, "w": "W - 240", "h": "4 * t(32)", "z": 10, "elements": [
			{"type": "fill", "x": -12, "y": -6, "w": "cw + 24", "h": "len(narration_rows) * t(32) + 8", "color": "black", "alpha": 0.45},
			{"type": "role", "role": "narration"},
			{"type": "repeater", "items": "narration_rows", "dy": "t(32)", "template": [
				{"type": "text", "value": "text", "style": "narration"},
			]},
			{"type": "role", "role": "menu"},
			{"type": "text", "visible": "narration_all_shown", "value": "hint", "y": "4 * t(32) + 8", "w": "cw", "align": "right", "style": "hint"},
		]},
	]
	save(out / "ui" / "dialogue.json", dialogue)


if __name__ == "__main__":
	main()
