#!/usr/bin/env python3
"""Writes the asset directory scripts/world_motion.txt plays on the background data copy: an area's palette tracks, frame
runs on its picture, layers behind and in front of it, and a tileset's palette track (docs/assets.md, "maps", "Motion").

* maps/wren_yard.png (14x8 cells at 16 pixels a cell): grass (#466e3c), a pond on cells 2-4 x 2-3 in deep blue
  (#3060c0) with foam pixels (#a0c8f0), and a stone bottom row; maps/wren_yard_above.png: leaves (#285a28) on cells
  10-12 x 1-2, clear elsewhere.
* Palette tracks on Wren Yard: the pond (global clock, 500 ms a step: as drawn; deep #3870d0 with foam taking the old
  deep color; deep #4080e0), the leaves (entry clock, 30 ticks a step: as drawn, then #5a8a28), and the grass recolored
  to #6e463c while cogway.loud is set (one step: a still recolor).
* Frame runs: motion/fountain.png (three 32x32 frames: gold #c8a040, red #c84040, teal #40c8a0) on cells 7-8 x 1-2 of the
  ground, 200, 200 and 400 ms, from entering the area; motion/lamp.png (two 16x16 frames: #e0e0e0, #202020) on the above
  layer at pixels 16,96 (cell 1,6), 12 ticks a frame, off while cogway.loud is set.
* Layers: motion/sky.png (64x32) behind the area, half the camera's motion, rows 0-15 (light, with clouds) drifting 8
  pixels a second and rows 16-31 (dark, with stripes) -16 at a quarter of the camera, waving 2 pixels over 16 lines;
  motion/rain.png (32x32, four white dots on clear) in front, falling 60 pixels a second with a one-pixel wave over its
  columns, while cogway.loud is set.
* tilesets/loft.png: Wren Loft's own tileset (floor #7a6a50, decor, wall top, wall face, water), its floor stepping to
  #8a5050 and back every 300 ms.
* alt/assets_still.json: the same manifest without any motion, and alt/assets_moving.json a copy of assets.json (the hot
  reload swaps one in, then the other).

Usage: make_world_motion_assets.py OUT
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


def ground(x, y):
	cx, cy = x // 16, y // 16
	if cy == 7:
		return rgb("#5a5046")
	if 2 <= cx <= 4 and 2 <= cy <= 3:
		return rgb("#a0c8f0") if (x + y) % 8 == 0 else rgb("#3060c0")
	return rgb("#466e3c")


def sky(x, y):
	if y < 16:
		return rgb("#f0f0f0") if x % 32 < 10 and 4 <= y <= 9 else rgb("#a0c0e0")
	return rgb("#405870") if x % 16 < 3 else rgb("#6080a0")


def loft(x, y):
	frame = x // 16
	colors = ["#7a6a50", "#7a6a50", "#505868", "#404050", "#3060c0"]
	if frame == 1 and 6 <= x % 16 <= 9 and 6 <= y <= 9:
		return rgb("#9a8a70")
	return rgb(colors[frame])


def main():
	out = Path(sys.argv[1])
	shutil.rmtree(out, ignore_errors=True)
	png(out / "maps/wren_yard.png", 224, 128, ground)
	png(out / "maps/wren_yard_above.png", 224, 128, lambda x, y: rgb("#285a28") if 10 <= x // 16 <= 12 and 1 <= y // 16 <= 2 else CLEAR)
	png(out / "motion/fountain.png", 96, 32, lambda x, y: rgb(["#c8a040", "#c84040", "#40c8a0"][x // 32]))
	png(out / "motion/lamp.png", 32, 16, lambda x, y: rgb(["#e0e0e0", "#202020"][x // 16]))
	png(out / "motion/sky.png", 64, 32, sky)
	png(out / "motion/rain.png", 32, 32, lambda x, y: rgb("#ffffff") if (x, y) in ((5, 7), (21, 3), (13, 22), (28, 27)) else CLEAR)
	png(out / "tilesets/loft.png", 80, 16, loft)
	tileset = {"area": "wren_loft", "image": "tilesets/loft.png", "floor": [0], "decor": [1], "decor_one_in": 1000, "wall_top": [2], "wall_face": [3], "water": [4]}
	still = {"version": 1, "tilesets": [tileset], "maps": {"areas": [{"area": "wren_yard", "image": "maps/wren_yard.png", "above": "maps/wren_yard_above.png"}]}}
	save(out / "alt/assets_still.json", still)
	moving = json.loads(json.dumps(still))
	moving["tilesets"][0]["palettes"] = [{"colors": {"floor": "#7a6a50"}, "steps": [{}, {"floor": "#8a5050"}], "ms": 300}]
	moving["maps"]["areas"][0].update({
		"palettes": [
			{"colors": {"deep": "#3060c0", "foam": "#a0c8f0"}, "steps": [{}, {"deep": "#3870d0", "foam": "#3060c0"}, {"deep": "#4080e0"}], "ms": 500},
			{"colors": {"leaf": "#285a28"}, "steps": [{}, {"leaf": "#5a8a28"}], "ticks": 30, "clock": "entry"},
			{"colors": {"grass": "#466e3c"}, "steps": [{"grass": "#6e463c"}], "flag": "cogway.loud"},
		],
		"animated": [
			{"cells": [7, 1, 2, 2], "image": "motion/fountain.png", "frames": [0, 1, 2], "ms": [200, 200, 400], "clock": "entry"},
			{"pixels": [16, 96, 16, 16], "image": "motion/lamp.png", "frames": [0, 1], "ticks": 12, "layer": "above", "unless_flag": "cogway.loud"},
		],
		"layers": [
			{"image": "motion/sky.png", "scroll": [0.5, 0.5], "bands": [{"rows": [0, 15], "speed": 8}, {"rows": [16, 31], "scroll": 0.25, "speed": -16}],
				"wave": {"axis": "lines", "amplitude": 2, "wavelength": 16, "speed": 8}},
			{"image": "motion/rain.png", "place": "front", "speed": [0, 60], "wave": {"axis": "columns", "amplitude": 1, "wavelength": 32}, "flag": "cogway.loud"},
		],
	})
	save(out / "assets.json", moving)
	save(out / "alt/assets_moving.json", moving)


if __name__ == "__main__":
	main()
