#!/usr/bin/env python3
"""Writes the asset directory the library adoption runs play (scripts/motion.txt, scripts/adopt.txt), from the sample's
built-in layouts in UI:

* sprites/platform.png: a moving platform as green planks, and alt/platform_red.png the same in red (swapped in to
  show the hot reload).
* ui/menu_status.json: the built-in status layout with a line of the member's detail fields under the header
  (`attack`, `base_strength`, `kind`, `total_experience`) and an experience bar (`experience_for_level`).
* ui/battle_front.json: the built-in front layout; alt/battle_front_rolled.json with `damage_numbers` "rolled".

Usage: make_adopt_assets.py UI OUT
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
	path.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n")


def planks(light, dark):
	"""Planks across a 16-pixel cell: a dark seam every fourth row and a dark border."""
	return lambda x, y: dark + (255,) if x in (0, 15) or y in (0, 15) or y % 4 == 3 else light + (255,)


def main():
	ui, out = Path(sys.argv[1]), Path(sys.argv[2])
	if out.exists():
		shutil.rmtree(out)
	out.mkdir(parents=True)
	png(out / "sprites" / "platform.png", 16, 16, planks((96, 168, 88), (40, 84, 36)))
	png(out / "alt" / "platform_red.png", 16, 16, planks((200, 80, 72), (110, 36, 32)))
	save(out / "assets.json", {"version": 1, "sprites": {"platform": "sprites/platform.png"}})
	status = json.loads((ui / "menu_status.json").read_text())
	header = next(w for w in status["windows"] if w["name"] == "header")
	header["elements"] += [
		{"type": "text", "value": "'ATK ' + attack + '  STR ' + base_strength + ' base  ' + kind + '  total ' + total_experience",
			"x": "cw - t(300)", "y": "t(32)", "max_width": "t(300)", "style": "small_dim"},
		{"type": "bar", "x": "cw - t(300)", "y": "t(32) + t(22)", "w": "t(300)", "h": 6,
			"value": "experience_for_level - experience_next", "max": "max(1, experience_for_level)", "color": "accent"},
	]
	save(out / "ui" / "menu_status.json", status)
	front = json.loads((ui / "battle_front.json").read_text())
	save(out / "ui" / "battle_front.json", front)
	front["params"] = dict(front.get("params", {}), damage_numbers="rolled")
	save(out / "alt" / "battle_front_rolled.json", front)


if __name__ == "__main__":
	main()
