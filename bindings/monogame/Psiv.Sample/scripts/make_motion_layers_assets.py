#!/usr/bin/env python3
"""Writes the asset directory scripts/motion_layers.txt plays on the background data copy: a layer aligned with the
ground at any entry (`align` "ground"), a layer drawn over the people (`above`), a palette track by palette index on an
indexed sheet (`indexes`) and a random entry delay (`delay` {"min", "max"}) (docs/assets.md, "Motion"). Every color
below is a placeholder.

* characters/fighter.png (224x16, fourteen 16x16 frames, an indexed PNG): index 0 clear, 1 a #e0e0e0 hat on rows 0-4,
  2 and 3 both #c0a000, the coat's rows 5-9 and 10-15 (columns 3-12), so the coat is one color at two indexes.
* characters/yard_trader.png: the same frames as an RGBA PNG (for the broken copy's index track).
* Saltgrass Market keeps its tiles: motion/veil.png (32x32: the top left 16x16 #ff00ff, the rest clear) is a back layer
  named `veil` drawn over the people (`above`), repeating, at half the camera's pace (`scroll` 0.5), aligned with the
  ground where the party enters (`align` "ground"), so on arriving its pink cells are the even cells (both coordinates
  even) wherever the way in; a track with `targets: ["layer:veil"]` turns it #00ffff while cogway.loud is set. The
  fighter's upper coat (index 2) turns #ff0000 by `indexes`, the lower coat (index 3, the same color) stays. A track on
  the tiles turns the floor #00a000 from a random 200 to 800 ms after each entry (`delay` {"min": 200, "max": 800}).
* alt/assets_broken.json: a track by index whose sheets are not indexed, `indexes` with `colors`, an index out of range,
  `above` on a front layer, an unknown `align` and a random delay out of order, for `--check-assets`.

Usage: make_motion_layers_assets.py OUT
"""

import json
import shutil
import struct
import sys
import zlib
from pathlib import Path

WALK = {"north": [0, 1, 2], "east": [3, 4, 5], "south": [6, 7, 8], "west": [9, 10, 11]}
PALETTE = [(0, 0, 0), (0xe0, 0xe0, 0xe0), (0xc0, 0xa0, 0x00), (0xc0, 0xa0, 0x00)]


def chunk(kind, data):
	return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write(path, ihdr, extra, rows):
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + extra + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def png(path, width, height, pixel):
	rows = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))
	write(path, struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0), b"", rows)


def indexed(path, width, height, index):
	rows = b"".join(b"\x00" + bytes(index(x, y) for x in range(width)) for y in range(height))
	plte = chunk(b"PLTE", b"".join(bytes(c) for c in PALETTE))
	trns = chunk(b"tRNS", bytes([0, 255, 255, 255]))
	write(path, struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0), plte + trns, rows)


def coat_index(x, y):
	if not 3 <= x % 16 <= 12:
		return 0
	return 1 if y <= 4 else 2 if y <= 9 else 3


def save(path, data):
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(data, indent="\t") + "\n")


def manifest():
	return {
		"version": 1,
		"characters": [{"id": who, "image": f"characters/{who}.png", "frame": [16, 16], "walk": WALK} for who in ("fighter", "yard_trader")],
		"maps": {"areas": [
			{"area": "saltgrass",
				"palettes": [
					{"colors": {"pink": "#ff00ff"}, "steps": [{"pink": "#00ffff"}], "targets": ["layer:veil"], "flag": "cogway.loud"},
					{"indexes": {"upper": 2}, "steps": [{"upper": "#ff0000"}], "targets": ["actors"], "sheets": ["fighter"]},
					{"colors": {"floor": "#746c66"}, "steps": [{"floor": "#00a000"}], "targets": ["tiles"], "clock": "entry", "delay": {"min": 200, "max": 800}},
				],
				"layers": [{"image": "motion/veil.png", "name": "veil", "above": True, "scroll": [0.5, 0.5], "align": "ground"}]},
		]},
	}


def broken(good):
	bad = json.loads(json.dumps(good))
	area = bad["maps"]["areas"][0]
	area["palettes"][1]["sheets"] = ["yard_trader"]
	area["palettes"].append({"indexes": {"a": 300}, "steps": [{}], "targets": ["actors"], "sheets": ["fighter"]})
	area["palettes"].append({"indexes": {"a": 1}, "colors": {"b": "#000000"}, "steps": [{}], "targets": ["actors"], "sheets": ["fighter"]})
	area["palettes"][2]["delay"] = {"min": 900, "max": 100}
	area["layers"].append({"image": "motion/veil.png", "place": "front", "above": True, "align": "sideways"})
	return bad


def main():
	out = Path(sys.argv[1])
	shutil.rmtree(out, ignore_errors=True)
	indexed(out / "characters/fighter.png", 224, 16, coat_index)
	png(out / "characters/yard_trader.png", 224, 16, lambda x, y: (PALETTE[coat_index(x, y)] + (255,)) if coat_index(x, y) else (0, 0, 0, 0))
	png(out / "motion/veil.png", 32, 32, lambda x, y: (255, 0, 255, 255) if x < 16 and y < 16 else (0, 0, 0, 0))
	good = manifest()
	save(out / "assets.json", good)
	save(out / "alt/assets_broken.json", broken(good))


if __name__ == "__main__":
	main()
