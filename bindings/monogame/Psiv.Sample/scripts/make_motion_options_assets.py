#!/usr/bin/env python3
"""Writes the asset directory scripts/motion_options.txt plays on the background data copy: what an area's palette
tracks recolor (`targets`, `people`, `sheets`), idle loops on a person and an object, an entry delay, steps of random
length, flag lists and layer bands in screen lines (docs/assets.md, "Motion"). Every color below is a placeholder.

* characters/fighter.png, characters/yard_lamplighter.png, characters/yard_trader.png (224x16, fourteen 16x16 frames):
  frames 0 to 11 are the walk frames, a #e0e0e0 hat on rows 0-4 and a #c0a000 coat on rows 5-15 (columns 3-12); frame
  12 is solid #00c0c0 and frame 13 solid #c000c0 on the same columns.
* Wren Yard: maps/wren_yard.png, a #406080 ground (224x128, 16 pixels a cell). Its palette tracks: the first turns
  #ff00ff #00ffff on the layer named `marker` only; the second turns the coat #ff8000 on the lamplighter only
  (`people`); the third turns the hat #2020ff on whoever wears the fighter's sheet (`sheets`); the fourth steps the
  ground #406080 to #608040 and back, its first step lasting a random 100 to 900 ms each time, its second 200 ms, on
  the entry clock. Two front layers of motion/marker.png (16x16, #ff00ff) stand on cells 3,2 (named `marker`) and 5,2.
  The trader idles on frames 12 and 13, 300 ms each, from 500 ms after the party enters (`delay`); the lamp object on
  cell 11,5 (objects/lamp.png, three 16x16 frames #101010, #f0f000, #00f0f0) idles on frames 1 and 2, 10 ticks each,
  while cogway.loud and kiln.upper_seen are both set; motion/dot.png (16x16, #ffffff) plays on cell 7,5 unless
  kiln.draft_felt or hollow.met is set.
* Wren Loft keeps its generated tiles: a track with `targets: ["tiles"]` turns the floor (#3a3642) #00a000; a second
  track on the same color, with the default targets, leaves the tiles alone.
* Wren Hollow: motion/stripes.png (16x8: rows 0-3 #ff0000, rows 4-7 #0000ff, column 0 #ffffff) is a front layer held
  on the screen (`scroll` 0), repeating across, on screen lines 0 to 23; its bands are screen lines, 0-11 still and
  12-23 drifting 16 picture pixels a second; a track with `targets: ["layers"]` turns its red #ff8080 while
  cogway.open is set.
* alt/assets_broken.json: the same manifest with a problem of each new kind, for `--check-assets`.

Usage: make_motion_options_assets.py OUT
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


def save(path, data):
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(data, indent="\t") + "\n")


def rgb(text, alpha=255):
	return (int(text[1:3], 16), int(text[3:5], 16), int(text[5:7], 16), alpha)


CLEAR = (0, 0, 0, 0)
WALK = {"north": [0, 1, 2], "east": [3, 4, 5], "south": [6, 7, 8], "west": [9, 10, 11]}


def person(x, y):
	frame, fx = divmod(x, 16)
	if not 3 <= fx <= 12:
		return CLEAR
	if frame == 12:
		return rgb("#00c0c0")
	if frame == 13:
		return rgb("#c000c0")
	return rgb("#e0e0e0") if y <= 4 else rgb("#c0a000")


def manifest():
	return {
		"version": 1,
		"characters": [{"id": who, "image": f"characters/{who}.png", "frame": [16, 16], "walk": WALK} for who in ("fighter", "yard_lamplighter", "yard_trader")],
		"objects": [{"area": "wren_yard", "at": [11, 5], "image": "objects/lamp.png", "frames": [0]}],
		"maps": {"areas": [
			{"area": "wren_yard", "image": "maps/wren_yard.png",
				"palettes": [
					{"colors": {"pink": "#ff00ff"}, "steps": [{"pink": "#00ffff"}], "targets": ["layer:marker"]},
					{"colors": {"coat": "#c0a000"}, "steps": [{"coat": "#ff8000"}], "targets": ["actors"], "people": ["yard_lamplighter"]},
					{"colors": {"hat": "#e0e0e0"}, "steps": [{"hat": "#2020ff"}], "targets": ["actors"], "sheets": ["fighter"]},
					{"colors": {"ground": "#406080"}, "steps": [{}, {"ground": "#608040"}], "ms": [{"min": 100, "max": 900}, 200], "clock": "entry"},
				],
				"layers": [
					{"image": "motion/marker.png", "name": "marker", "place": "front", "repeat": "none", "at": [48, 32]},
					{"image": "motion/marker.png", "place": "front", "repeat": "none", "at": [80, 32]},
				],
				"animated": [{"cells": [7, 5, 1, 1], "image": "motion/dot.png", "frames": [0], "unless_flag": ["kiln.draft_felt", "hollow.met"]}],
				"idle": [
					{"actor": "yard_trader", "frames": [12, 13], "ms": 300, "clock": "entry", "delay": 500},
					{"object": [11, 5], "frames": [1, 2], "ticks": 10, "flag": ["cogway.loud", "kiln.upper_seen"]},
				]},
			{"area": "wren_loft", "palettes": [
				{"colors": {"floor": "#3a3642"}, "steps": [{"floor": "#00a000"}], "targets": ["tiles"]},
				{"colors": {"floor": "#3a3642"}, "steps": [{"floor": "#a00000"}]},
			]},
			{"area": "wren_hollow",
				"palettes": [{"colors": {"red": "#ff0000"}, "steps": [{"red": "#ff8080"}], "targets": ["layers"], "flag": "cogway.open"}],
				"layers": [{"image": "motion/stripes.png", "place": "front", "scroll": [0, 0], "repeat": "x", "clock": "entry",
					"bands": [{"lines": [0, 11], "speed": 0}, {"lines": [12, 23], "speed": 16}]}]},
		]},
	}


def broken(good):
	"""One problem of each new kind, each in an area entry of its own so each is reported."""
	bad = json.loads(json.dumps(good))
	areas = bad["maps"]["areas"]
	yard = areas[0]
	yard["palettes"][0]["targets"] = ["layer:nowhere", "roofs"]
	yard["palettes"][1]["people"] = ["nobody"]
	yard["palettes"][2]["targets"] = ["ground"]
	yard["palettes"][3]["ms"] = [{"min": 900, "max": 100}, 200]
	yard["layers"][0]["name"] = "bad name"
	yard["animated"][0]["unless_flag"] = ["kiln.draft_felt", "no.such_flag"]
	yard["idle"] = [
		{"actor": "yard_goat", "frames": [0]},
		{"actor": "yard_trader", "object": [11, 5], "frames": [0]},
		{"actor": "yard_trader", "frames": [99], "ms": 300, "delay": 500},
		{"object": [2, 2], "frames": [1]},
		{"object": [11, 5], "frames": [7], "flag": ["cogway.loud", "cogway.loud"]},
	]
	areas[2]["layers"][0]["bands"] = [{"lines": [0, 11]}, {"rows": [4, 7]}, {"lines": [10, 5000]}]
	return bad


def main():
	out = Path(sys.argv[1])
	shutil.rmtree(out, ignore_errors=True)
	for who in ("fighter", "yard_lamplighter", "yard_trader"):
		png(out / f"characters/{who}.png", 224, 16, person)
	png(out / "maps/wren_yard.png", 224, 128, lambda x, y: rgb("#406080"))
	png(out / "motion/marker.png", 16, 16, lambda x, y: rgb("#ff00ff"))
	png(out / "motion/dot.png", 16, 16, lambda x, y: rgb("#ffffff"))
	png(out / "motion/stripes.png", 16, 8, lambda x, y: rgb("#ffffff") if x == 0 else rgb("#ff0000") if y < 4 else rgb("#0000ff"))
	png(out / "objects/lamp.png", 48, 16, lambda x, y: rgb(("#101010", "#f0f000", "#00f0f0")[x // 16]))
	good = manifest()
	save(out / "assets.json", good)
	save(out / "alt/assets_broken.json", broken(good))


if __name__ == "__main__":
	main()
