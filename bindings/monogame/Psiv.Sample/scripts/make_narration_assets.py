#!/usr/bin/env python3
"""Writes the asset directory scripts/narration.txt plays: a copy of TEST (the test assets) with its
`alt/assets_more.json` as the manifest, for the still pictures `chart` and `tower`; the styles with a `narration` text
style; and a dialogue layout with the still pictures kept as a stack (`picture_stack`), drawn side by side at the top,
and a `narration` window without a frame under them, holding four lines, with its own reveal speed and line delay.

With --route (scripts/narration_route.txt): the same, plus a 16x16 page arrow picture as the UI skin's `page_arrow`
(drawn at the box's right by the `page_arrow` field once the line is all shown, with `shown_ms` at its left), and two dialogue
files to swap in: alt/dialogue_tagged.json (`narration_when: tagged`) and alt/dialogue_expr.json (an expression: a
`route.t` key or the tower on top of the picture stack).

Usage: make_narration_assets.py EXPORT TEST OUT [--route]
"""

import json
import shutil
import struct
import sys
import zlib
from pathlib import Path


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


def route(out, dialogue):
	# A down-pointing teal triangle on a clear ground.
	write_png(out / "ui" / "page_arrow.png", 16, 16, lambda x, y: (40, 200, 190, 255) if 2 + y // 2 <= x <= 13 - y // 2 and y < 12 else (0, 0, 0, 0))
	manifest = load(out / "assets.json")
	manifest["ui"]["page_arrow"] = "ui/page_arrow.png"
	save(out / "assets.json", manifest)
	box = next(w for w in dialogue["windows"] if w["name"] == "box")
	box["elements"].append({"type": "image", "name": "page_arrow", "visible": "all_shown", "image": "page_arrow", "x": "cw - 84", "y": "ch - 36", "w": 24, "h": 24})
	box["elements"].append({"type": "text", "name": "shown_ms", "visible": "all_shown", "value": "shown_ms", "x": 24, "y": "ch - 32",
		"style": "small_dim"})
	save(out / "ui" / "dialogue.json", dialogue)
	tagged = dict(dialogue, params=dict(dialogue["params"], narration_when="tagged"))
	save(out / "alt" / "dialogue_tagged.json", tagged)
	expr = dict(dialogue, params=dict(dialogue["params"], narration_when="starts_with(line_key, 'route.t') || picture_top == 'tower'"))
	save(out / "alt" / "dialogue_expr.json", expr)
	save(out / "alt" / "dialogue_all.json", dialogue)


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
	if "--route" in sys.argv[4:]:
		route(out, dialogue)


if __name__ == "__main__":
	main()
