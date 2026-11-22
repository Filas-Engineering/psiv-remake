#!/usr/bin/env python3
"""Writes the asset directory scripts/motion_more.txt plays on the background data copy: palette tracks on a sprite and on
the generated tilesets, a layer placed with `at`, idle states that hop and pulse, and an encounter's key for the previewer
(docs/assets.md, "tilesets", "sprites and status_icons", "Motion"; docs/battle-animation.md, "Idle states").

* sprites/shadow.png (16x16): a solid #202020 band on rows 9-15, columns 2-13, clear elsewhere; its palette track steps
  the band to #c02020 and back every 20 ticks.
* Tilesets without a picture: the field kind's generated grass (#50984a) steps to #98504a and back every 400 ms; Wren
  Loft's generated dungeon floor (#3a3642) is a still #4e3a3c.
* Wren Yard keeps its tiles; motion/marker.png (16x16, #ff00ff) is a layer in front that does not repeat, placed at picture
  pixels 48,32 (cell 3,2), moving with the ground.
* battle_animation.json: Bog Wisps hop 6 pixels every 30 frames and the Gel Mite grows and shrinks by a quarter every 40
  frames, as their idle states; the Squall Kite's attack in the kites encounter plays mm.talon.

Usage: make_motion_more_assets.py OUT
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


def main():
	out = Path(sys.argv[1])
	shutil.rmtree(out, ignore_errors=True)
	png(out / "sprites/shadow.png", 16, 16, lambda x, y: rgb("#202020") if 2 <= x <= 13 and y >= 9 else CLEAR)
	png(out / "motion/marker.png", 16, 16, lambda x, y: rgb("#ff00ff"))
	save(out / "assets.json", {
		"version": 1,
		"tilesets": [
			{"kind": "field", "palettes": [{"colors": {"grass": "#50984a"}, "steps": [{}, {"grass": "#98504a"}], "ms": 400}]},
			{"area": "wren_loft", "palettes": [{"colors": {"floor": "#3a3642"}, "steps": [{"floor": "#4e3a3c"}]}]},
		],
		"sprites": {"shadow": {"image": "sprites/shadow.png", "palettes": [{"colors": {"band": "#202020"}, "steps": [{}, {"band": "#c02020"}], "ticks": 20}]}},
		"maps": {"areas": [{"area": "wren_yard", "layers": [{"image": "motion/marker.png", "place": "front", "repeat": "none", "at": [48, 32]}]}]},
		"battle_animation": "battle_animation.json",
	})
	save(out / "battle_animation.json", {
		"version": 1,
		"settings": {"rate": 60},
		"select": {"encounter:kites/enemy:squall_kite/attack": "mm.talon"},
		"placements": {"encounter:kites": [[300, 400], [600, 400], [900, 400]]},
		"figures": {
			"enemy:bog_wisp": {"states": {"idle": {"motion": {"kind": "bob", "axis": "y", "amplitude": "6px", "period": 30}}}},
			"enemy:gel_mite": {"states": {"idle": {"motion": {"kind": "pulse", "scale": 0.25, "period": 40}}}},
		},
		"scripts": {
			"mm.talon": {
				"phases": {"forward": 6, "banner": 14, "animate": 8, "effect": 12, "result": 24, "back": 8},
				"tracks": [
					{"id": "in", "type": "move", "on": "actor", "at": "forward", "kind": "forward"},
					{"id": "cut", "type": "sprite", "at": "effect", "effect": "slash", "spawn": "target:0", "length": 12},
					{"id": "out", "type": "move", "on": "actor", "at": "back", "kind": "home"},
				],
			},
		},
	})


if __name__ == "__main__":
	main()
