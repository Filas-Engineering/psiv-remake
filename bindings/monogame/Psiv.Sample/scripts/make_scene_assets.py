#!/usr/bin/env python3
"""Writes the two asset directories the scene presentation runs play, from the sample's built-in layouts in UI. SCENE
(scripts/scene.txt, vehicle_commands.txt, game_over.txt and arrive_behind.txt):

* pictures/: the four story pictures the scene data's scripts show (harbor_dawn, storm, beacon, farewell) and one only
  the dialogue's `picture` tag shows (tag_card), each a plain generated pattern.
* ui/battle_side.json and ui/battle_front.json: the built-in battle layouts with `command_rows.vehicle` listing every
  vehicle row (attack, skills, macro, auto, escape); alt/battle_side_no_confirm.json and
  alt/battle_front_no_confirm.json also with `game_over_confirm` 0 and `party_figures` off on the result step
  (the backdrop and the enemies' remains stay).
* alt/styles_game_over.json: the built-in styles with the timing `game_over_return_ms` 1500.
* alt/field_behind.json: the built-in field layout with `followers_arrive` "behind".
* alt/shop_steps.json: the built-in shop layout with `steps` 1 (a quantity before the confirm).

ABOVE (scripts/above_cover.txt, on the background data copy):

* maps/wren_yard.png and maps/wren_yard_above.png: Wren Yard (14x8 cells, at 16 pixels a cell) as a flat ground and an
  above picture opaque magenta on every cell whose column and row add up to an even number, clear on the others;
  maps/wren_hollow.png and maps/wren_hollow_above.png (10x7) the same, over the tallykeeper behind its counter.

Usage: make_scene_assets.py UI SCENE ABOVE
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


PICTURES = {
	# A sky going from orange to pale over a blue band of sea.
	"harbor_dawn": lambda x, y: (255, 140 + y, 60 + y, 255) if y < 60 else (30, 70 + (x + y) % 20, 140, 255),
	# Dark grey with light diagonal streaks.
	"storm": lambda x, y: (220, 220, 230, 255) if (x + 2 * y) % 23 < 3 else (60, 62, 74, 255),
	# Navy with a yellow disc in the middle.
	"beacon": lambda x, y: (250, 220, 60, 255) if (x - 80) ** 2 + (y - 48) ** 2 < 900 else (20, 30, 80, 255),
	# Purple and lilac stripes.
	"farewell": lambda x, y: (120, 60, 150, 255) if y // 12 % 2 == 0 else (190, 150, 220, 255),
	# A green card with a white border.
	"tag_card": lambda x, y: (240, 240, 240, 255) if x < 4 or y < 4 or x > 115 or y > 75 else (40, 140, 90, 255),
}


def main():
	ui, out, above = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
	for d in (out, above):
		if d.exists():
			shutil.rmtree(d)
		d.mkdir(parents=True)
	for name, pixel in PICTURES.items():
		w, h = (120, 80) if name == "tag_card" else (160, 96)
		png(out / "pictures" / f"{name}.png", w, h, pixel)
	png(above / "maps" / "wren_hollow.png", 160, 112, lambda x, y: (120, 100, 80, 255))
	png(above / "maps" / "wren_hollow_above.png", 160, 112, lambda x, y: (255, 0, 255, 255) if (x // 16 + y // 16) % 2 == 0 else (0, 0, 0, 0))
	png(above / "maps" / "wren_yard.png", 224, 128, lambda x, y: (70, 110, 60, 255) if y < 112 else (90, 80, 70, 255))
	save(above / "assets.json", {"version": 1})
	png(above / "maps" / "wren_yard_above.png", 224, 128, lambda x, y: (255, 0, 255, 255) if (x // 16 + y // 16) % 2 == 0 else (0, 0, 0, 0))
	save(out / "assets.json", {"version": 1, "pictures": {name: f"pictures/{name}.png" for name in PICTURES}})
	for screen in ("battle_side", "battle_front"):
		layout = json.loads((ui / f"{screen}.json").read_text())
		layout["command_rows"] = {"vehicle": ["attack", "skills", "macro", "auto", "escape"]}
		save(out / "ui" / f"{screen}.json", layout)
		layout["params"] = dict(layout.get("params", {}), game_over_confirm=0, party_figures="result_step ? 0 : 1")
		save(out / "alt" / f"{screen}_no_confirm.json", layout)
	styles = json.loads((ui / "styles.json").read_text())
	styles["timings"]["game_over_return_ms"] = 1500
	save(out / "alt" / "styles_game_over.json", styles)
	field = json.loads((ui / "field.json").read_text())
	field["params"] = dict(field.get("params", {}), followers_arrive="behind")
	save(out / "alt" / "field_behind.json", field)
	shop = json.loads((ui / "shop.json").read_text())
	shop["params"] = dict(shop.get("params", {}), steps=1)
	save(out / "alt" / "shop_steps.json", shop)


if __name__ == "__main__":
	main()
