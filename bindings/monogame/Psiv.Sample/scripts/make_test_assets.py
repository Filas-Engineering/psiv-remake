#!/usr/bin/env python3
"""Builds the asset directories the sample's asset tests use, from the placeholder export (--export-placeholders).

  make_test_assets.py EXPORT OVERRIDE BROKEN FONTS [LAYOUT]

OVERRIDE: a small asset directory replacing a few ids with recolored copies of their exported pictures (the town
tileset, the fighter, the guide's portrait, the gel mite's battle figure, the field backdrop, the slash effect, the
sleep icon and an enabled UI skin), one cue and one track, and a battle presentation table; tilesets/town_alt.png is a
second recoloring for the hot reload test. Every other id keeps its generated placeholder. Then the newer entries, each
from a recolored, enlarged or synthesized picture: whole-area pictures for the reed path (by the maps/ file convention,
with an above picture) and for Ashfall Market (an explicit entry at 32 pixels a cell, with a picture for the mended
bridge); the fighter's sheet and three battle figures drawn larger with a matching scale; objects with pictures of
their own at Ember Steps, the reed path's chest (closed and opened at another size) and the Fell Vault's locked door;
Saltgrass Market's own tileset with an animated tile and its wall shadows and water foam turned off; a backdrop for
battles on water, an overlay for the reed skimmer's battles and an enemy's own attack look; the Cogway's whole-area
picture with pictures for its conveyor belts and its tile step hooks' cells (alt/assets_drawn.json is the same manifest
with both turned off and a picture that has them painted in, for a run that swaps it in), the slash effect drawn larger with a scale, Saltgrass Market's
tileset at three times the size with a scale, the brine jelly and the fuse beetle's figures (one with a scale, one at
its natural size on its foot row), and the medic's back figure kept at its slot's size with `natural: false`.

BROKEN: a copy of the export with one problem of each kind the asset check reports.

LAYOUT (optional): FONTS with the example layout set, written by make_layout_assets.py.

FONTS: an asset directory with bitmap fonts only: the export's English and Japanese fonts turned on, and a recolored copy
of the English one at one and a half times the size for the battle numbers; alt/ja_partial.json is the Japanese metrics
without the kanji, for a run that swaps it in and sees them fall back to the system font.
"""

import json
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path


def read_png(path):
	data = Path(path).read_bytes()
	assert data[:8] == b"\x89PNG\r\n\x1a\n", path
	pos, idat, width, height = 8, b"", 0, 0
	while pos < len(data):
		length, kind = struct.unpack(">I4s", data[pos:pos + 8])
		body = data[pos + 8:pos + 8 + length]
		if kind == b"IHDR":
			width, height, depth, color = struct.unpack(">IIBB", body[:10])
			assert (depth, color) == (8, 6), f"{path}: the export writes 8-bit RGBA"
		elif kind == b"IDAT":
			idat += body
		pos += 12 + length
	raw = zlib.decompress(idat)
	stride = width * 4
	rows, prev = [], bytearray(stride)
	for y in range(height):
		kind = raw[y * (stride + 1)]
		line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
		for i in range(stride):
			a = line[i - 4] if i >= 4 else 0
			b = prev[i]
			c = prev[i - 4] if i >= 4 else 0
			if kind == 1:
				line[i] = (line[i] + a) & 255
			elif kind == 2:
				line[i] = (line[i] + b) & 255
			elif kind == 3:
				line[i] = (line[i] + (a + b) // 2) & 255
			elif kind == 4:
				p = a + b - c
				pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
				line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
		rows.append(line)
		prev = line
	return width, height, rows


def write_png(path, width, height, rows):
	def chunk(kind, body):
		return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
	raw = b"".join(b"\x00" + bytes(r) for r in rows)
	Path(path).parent.mkdir(parents=True, exist_ok=True)
	Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
		+ chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def recolor(src, dst, order):
	"""Writes src with its color channels taken in `order` (a permutation of 0, 1, 2), alpha kept."""
	width, height, rows = read_png(src)
	out = []
	for row in rows:
		line = bytearray(row)
		for x in range(width):
			px = row[x * 4:x * 4 + 3]
			for i in range(3):
				line[x * 4 + i] = px[order[i]]
		out.append(line)
	write_png(dst, width, height, out)


def upscale(src, dst, k, extra=None):
	"""Writes src enlarged k times (each pixel a k by k block), with `extra(width, height, rows)` appending rows first."""
	width, height, rows = read_png(src)
	if extra:
		width, height, rows = extra(width, height, rows)
	out = []
	for row in rows:
		line = bytearray()
		for x in range(width):
			line += bytes(row[x * 4:x * 4 + 4]) * k
		out += [bytearray(line) for _ in range(k)]
	write_png(dst, width * k, height * k, out)


def synth(dst, width, height, pixel):
	"""Writes a picture whose every pixel is pixel(x, y), an (r, g, b, a) tuple."""
	write_png(dst, width, height, [bytearray(b for x in range(width) for b in pixel(x, y)) for y in range(height)])


def squashed(width, height, rows):
	"""Adds a frame after the sheet's first: the first frame squashed into its bottom half, darker (a fallen pose)."""
	frame = [bytearray(width * 4) for _ in range(height)]
	for y in range(height // 2):
		src = rows[y * 2]
		frame[height // 2 + y] = bytearray(v // 2 if i % 4 != 3 else v for i, v in enumerate(src[:width * 4]))
	return width * 2, height, [bytearray(r) + bytearray(height and frame[y]) for y, r in enumerate(rows)]


def main():
	export, override, broken, fonts = (Path(a) for a in sys.argv[1:5])
	manifest = json.loads((export / "assets.json").read_text())

	def entry(listing, key, value):
		return dict(next(e for e in manifest[listing] if e.get(key) == value))

	shutil.rmtree(override, ignore_errors=True)
	override.mkdir(parents=True)
	blue, red = (2, 0, 1), (1, 2, 0)
	town = entry("tilesets", "kind", "town")
	recolor(export / town["image"], override / "tilesets/town.png", blue)
	recolor(export / town["image"], override / "tilesets/town_alt.png", red)
	fighter = entry("characters", "id", "fighter")
	recolor(export / fighter["image"], override / "characters/fighter.png", blue)
	recolor(export / manifest["portraits"]["guide"], override / "portraits/guide.png", red)
	mite = entry("figures", "id", "gel_mite")
	recolor(export / mite["image"], override / "figures/gel_mite.png", red)
	field = entry("backdrops", "kind", "field")
	recolor(export / field["image"], override / "backdrops/field.png", red)
	slash = entry("effects", "id", "slash")
	recolor(export / slash["image"], override / "effects/slash.png", blue)
	recolor(export / manifest["status_icons"]["sleep"], override / "icons/sleep.png", blue)
	recolor(export / manifest["ui"]["frame"]["image"], override / "ui/frame.png", red)
	(override / "audio").mkdir()
	shutil.copy(export / "audio/cue/confirm.wav", override / "audio/confirm.wav")
	shutil.copy(export / "audio/music/town_tide_bells.wav", override / "audio/town.wav")
	track = next(a for a in manifest["audio"] if a["id"] == "town_tide_bells")
	(override / "battle.json").write_text(json.dumps({
		"timings": {"banner": 600},
		"actions": {"strike": {"effect": "burst_heat", "flash": True, "shake": 8, "cue": "critical"}},
		"fallbacks": {"technique.damage.shock": {"effect": "nova_shock"}},
	}, indent="\t") + "\n")
	fighter["image"] = "characters/fighter.png"
	town["image"] = "tilesets/town.png"
	mite["image"] = "figures/gel_mite.png"
	field["image"] = "backdrops/field.png"
	slash["image"] = "effects/slash.png"
	(override / "assets.json").write_text(json.dumps({
		"version": 1,
		"tilesets": [town],
		"characters": [fighter],
		"portraits": {"guide": "portraits/guide.png"},
		"figures": [mite],
		"backdrops": [field],
		"effects": [slash],
		"status_icons": {"sleep": "icons/sleep.png"},
		"ui": {"enabled": True, "frame": {"image": "ui/frame.png", "border": 8}},
		"audio": [
			{"kind": "cue", "id": "confirm", "file": "audio/confirm.wav", "gain": 0.5},
			{"kind": "music", "id": "town_tide_bells", "file": "audio/town.wav", "loop_start": track["loop_end"] // 4, "loop_end": track["loop_end"]},
		],
		"battle_presentation": "battle.json",
	}, indent="\t") + "\n")
	newer(export, override, manifest)

	shutil.rmtree(broken, ignore_errors=True)
	shutil.copytree(export, broken)
	(broken / "portraits/guide.png").unlink()
	write_png(broken / "tilesets/dungeon.png", 17, 17, [bytearray(17 * 4)] * 17)
	manifest["characters"].append(dict(fighter, id="nobody", image=manifest["characters"][0]["image"]))
	manifest["characters"][0]["animations"]["dance"] = "nod"
	manifest["characters"][1]["animations"]["nod"] = "twirl"
	manifest["figures"][0]["animations"]["juggle"] = {"frames": [0], "ms": 100}
	next(f for f in manifest["figures"] if f["side"] == "enemy")["view"] = "back"
	next(a for a in manifest["audio"] if a["kind"] == "music")["loop_end"] = 99999999
	next(a for a in manifest["audio"] if a["kind"] == "ambient")["loop_start"] = 2
	next(a for a in manifest["audio"] if a["kind"] == "ambient")["loop_end"] = 1
	manifest["audio"].append({"kind": "music", "id": "not_a_track", "file": "audio/music/title.wav"})
	manifest["backdrops"].append({"area": "atlantis", "image": "backdrops/town.png"})
	manifest["sprites"]["moon"] = "sprites/shadow.png"
	skimmer = dict(manifest["vehicles"][0], id="hovercart")
	skimmer["facings"] = {k: v for k, v in skimmer["facings"].items() if k != "west"}
	manifest["vehicles"].append(skimmer)
	(broken / "assets.json").write_text(json.dumps(manifest, indent="\t") + "\n")
	table = json.loads((broken / "battle_presentation.json").read_text())
	table["actions"]["strike"] = {"effect": "fireworks", "animation": "moonwalk"}
	table["actions"]["unknown_move"] = {}
	table["layout"] = "diagonal"
	table["layouts"] = {"encounter:nowhere": "side"}
	table["changes"]["join.nobody"] = {}
	table["changes"]["transform"] = {"effect": "smoke"}
	table["enemies"] = {"nobody": {}, "gel_mite": {"juggle": {}}}
	(broken / "battle_presentation.json").write_text(json.dumps(table, indent="\t") + "\n")
	newer_broken(broken)
	broken_layouts(broken)
	bitmap_fonts(export, fonts)
	if len(sys.argv) > 5:
		subprocess.run([sys.executable, "-I", str(Path(__file__).with_name("make_layout_assets.py")), str(export), str(fonts), sys.argv[5]], check=True)


def bitmap_fonts(export, fonts):
	"""Writes FONTS (see the module's description)."""
	shutil.rmtree(fonts, ignore_errors=True)
	(fonts / "fonts").mkdir(parents=True)
	(fonts / "alt").mkdir()
	for name in ("en.png", "en.json", "ja.png", "ja.json"):
		shutil.copy(export / "fonts" / name, fonts / "fonts" / name)
	width, height, rows = read_png(export / "fonts/en.png")
	gold = [bytearray(v if i % 4 == 3 else (255, 200, 80)[i % 4] * row[i - i % 4 + 3] // 255 for i, v in enumerate(row)) for row in rows]
	write_png(fonts / "fonts/numbers.png", width, height, gold)
	metrics = json.loads((export / "fonts/ja.json").read_text())
	metrics["rows"] = ["".join(" " if 0x4E00 <= ord(c) <= 0x9FFF else c for c in row) for row in metrics["rows"]]
	metrics["glyphs"] = {c: g for c, g in metrics["glyphs"].items() if not 0x4E00 <= ord(c) <= 0x9FFF}
	(fonts / "alt/ja_partial.json").write_text(json.dumps(metrics, indent="\t", ensure_ascii=False) + "\n")
	(fonts / "assets.json").write_text(json.dumps({
		"version": 1,
		"bitmap_fonts": {"fonts": [
			{"language": "en", "image": "fonts/en.png", "metrics": "fonts/en.json"},
			{"language": "en", "role": "battle_numbers", "image": "fonts/numbers.png", "metrics": "fonts/en.json", "scale": 1.5},
			{"language": "ja", "image": "fonts/ja.png", "metrics": "fonts/ja.json"},
		]},
	}, indent="\t") + "\n")


def newer(export, override, manifest):
	"""Adds the newer entries to OVERRIDE's manifest (see the module's description)."""
	m = json.loads((override / "assets.json").read_text())
	blue, red, green = (2, 0, 1), (1, 2, 0), (0, 2, 1)

	# The reed path by the file convention: its map recolored, and an above picture of hanging reeds along the top two
	# rows and over the lookout's corner, half see-through, which the party walks under.
	recolor(export / "maps/reed_path.png", override / "maps/reed_path.png", red)
	recolor(export / "maps/reed_path.png", override / "alt/reed_path.png", green)
	def reeds(x, y):
		hanging = y < 20 + (x * 7 % 13) and x % 6 < 3
		corner = x < 64 and 16 <= y < 48 and (x + y) % 4 < 2
		return (40, 120, 60, 200) if hanging else (30, 90, 40, 150) if corner else (0, 0, 0, 0)
	synth(override / "maps/reed_path_above.png", 320, 144, reeds)
	# Ashfall Market at 32 pixels a cell, and a picture of the mended bridge: planks across the canal.
	upscale(export / "maps/ashfall_market.png", override / "maps/ashfall_big.png", 2)
	def bridge(x, y):
		plank = (x // 4) % 2 == 0
		return (170, 120, 60, 255) if plank else (120, 80, 40, 255)
	synth(override / "maps/ashfall_bridge.png", 16 * 32, 9 * 32, bridge)
	m["maps"] = {"cell": 16, "areas": [{"area": "ashfall_market", "image": "maps/ashfall_big.png", "cell": 32,
		"changes": {"fells.bridge_mended": "maps/ashfall_bridge.png"}}]}

	# The fighter's sheet twice as large, and battle figures larger still, each with the scale that draws it as before.
	fighter = next(c for c in m["characters"] if c["id"] == "fighter")
	upscale(override / fighter["image"], override / "characters/fighter_x3.png", 3)
	fighter.update(image="characters/fighter_x3.png", frame=[48, 48], scale=3)
	mite = next(f for f in m["figures"] if f["id"] == "gel_mite")
	upscale(override / mite["image"], override / "figures/gel_mite_x4.png", 4, squashed)
	mite.update(image="figures/gel_mite_x4.png", frame=[64, 64], scale=4)
	mite["animations"] = {"defeat": {"frames": [2], "ms": [400]}}
	def figure(fid, view, k, order):
		e = dict(next(f for f in manifest["figures"] if f["id"] == fid and f.get("view", "side") == view))
		recolor(export / e["image"], override / f"figures/{fid}_{view}.png", order)
		upscale(override / f"figures/{fid}_{view}.png", override / f"figures/{fid}_{view}_x{k}.png", k)
		e.update(image=f"figures/{fid}_{view}_x{k}.png", frame=[16 * k, 16 * k], scale=k)
		m["figures"].append(e)
	figure("fighter", "back", 3, blue)
	figure("mystic", "side", 2, green)
	# The bog wisp at its natural size: three times as large with twelve empty rows under its feet and a fallen pose, no
	# scale, drawn at the multiple battle_figures' reference height gives, standing on its foot row, without the ground
	# shadow.
	hound = dict(next(f for f in manifest["figures"] if f["id"] == "bog_wisp"))
	def padded(width, height, rows):
		return width, height + 4, [bytearray(r) for r in rows] + [bytearray(width * 4) for _ in range(4)]
	upscale(export / hound["image"], override / "figures/bog_wisp_natural.png", 3, lambda w, h, r: padded(*squashed(w, h, r)))
	hound.update(image="figures/bog_wisp_natural.png", frame=[48, 60], foot=48, shadow=False, animations={"defeat": {"frames": [2], "ms": [400]}})
	m["figures"].append(hound)
	m["battle_figures"] = {"reference_height": 224}

	# Ember Steps: the console blinking (two frames, one cell wide and two tall), the notice board two cells wide, the
	# tinker as a person facing east, the spring two cells square.
	def console(x, y):
		frame, ly = x // 16, y
		if ly < 6:
			return (0, 0, 0, 0)
		lit = (90, 230, 250, 255) if frame == 0 else (30, 90, 120, 255)
		return lit if 9 <= ly < 15 and 3 <= x % 16 < 13 else (70, 74, 90, 255)
	synth(override / "objects/console.png", 32, 32, console)
	synth(override / "objects/board.png", 32, 16, lambda x, y: (150, 104, 60, 255) if y > 2 and x % 31 else (90, 60, 30, 255))
	synth(override / "objects/spring.png", 32, 32, lambda x, y: (70, 140, 220, 255) if (x - 16) ** 2 + (y - 18) ** 2 < 150 else (0, 0, 0, 0))
	# The reed path's chest: a long chest closed, and opened a picture two cells tall with its lid up.
	synth(override / "objects/long_chest.png", 32, 16, lambda x, y: (200, 150, 40, 255) if 4 <= y and 1 <= x < 31 else (0, 0, 0, 0))
	synth(override / "objects/long_chest_open.png", 32, 32,
		lambda x, y: (120, 80, 30, 255) if y < 14 and 1 <= x < 31 else (240, 220, 120, 255) if 20 <= y and 1 <= x < 31 else (0, 0, 0, 0))
	# The Fell Vault's locked door: one cell wide and two tall, shut and then open (dark inside a frame).
	def door(x, y):
		frame, lx = x // 16, x % 16
		if lx < 2 or lx >= 14 or y < 2:
			return (110, 100, 120, 255)
		return (150, 60, 50, 255) if frame == 0 else (20, 16, 24, 255)
	synth(override / "objects/door.png", 32, 32, door)
	m["objects"] = [
		{"area": "ember_steps", "point": "steps_console", "image": "objects/console.png", "frame": [16, 32], "frames": [0, 1], "ms": 300, "size": [1, 2]},
		{"area": "ember_steps", "point": "steps_board", "image": "objects/board.png", "frame": [32, 16], "frames": [0], "size": [2, 1], "anchor": [0, 0]},
		{"area": "ember_steps", "point": "steps_tinker", "character": "mystic", "facing": "east"},
		{"area": "ember_steps", "point": "steps_spring", "image": "objects/spring.png", "frame": [32, 32], "frames": [0], "size": [2, 2]},
		{"area": "reed_path", "point": "reed_cache", "image": "objects/long_chest.png", "frame": [32, 16], "frames": [0], "size": [2, 1], "anchor": [0, 0],
			"open": {"image": "objects/long_chest_open.png", "frame": [32, 32], "frames": [0], "size": [2, 2], "anchor": [0, 1]}},
		{"area": "fell_vault", "at": [8, 4], "image": "objects/door.png", "frame": [16, 32], "frames": [0], "size": [1, 2], "open_flag": "fells.vault_door_open",
			"open": {"frames": [1]}},
	]

	# Saltgrass Market's own tileset, recolored, its first floor frame stepping through the water's frames; no wall
	# shadows or water foam there.
	town = next(t for t in manifest["tilesets"] if t["kind"] == "town")
	recolor(export / town["image"], override / "tilesets/saltgrass.png", green)
	salt = {k: v for k, v in town.items() if k != "kind"}
	salt.update(area="saltgrass", image="tilesets/saltgrass.png", animated=[{"tile": 0, "frames": [0] + town["water"], "ms": 300}])
	m["tilesets"].append(salt)
	m["field"] = {"areas": {"saltgrass": {"wall_shadows": False, "water_foam": False}}}

	# Battles on water: a backdrop of waves; the reed skimmer's battles seen through its hull's rim; the bog wisp's own
	# attack look.
	synth(override / "backdrops/water.png", 320, 105,
		lambda x, y: (60, 110, 200, 255) if y < 60 else (30, 70 + (x + y * 3) % 40, 150, 255))
	def rim(x, y):
		edge = min(x, y, 319 - x, 179 - y)
		return (60, 50, 40, 255) if edge < 10 else (120, 100, 70, 255) if edge < 14 and (x + y) % 12 < 2 else (0, 0, 0, 0)
	synth(override / "overlays/skimmer_rim.png", 320, 180, rim)
	m["backdrops"].append({"terrain": "water", "image": "backdrops/water.png"})
	m["vehicle_overlays"] = [{"vehicle": "reed_skimmer", "image": "overlays/skimmer_rim.png"}]

	# The Cogway by the file convention, recolored, with belts of moving bars per direction (two frames each) and a pad
	# that pulses on the cells of its tile step hooks (on the data copy that has one).
	recolor(export / "maps/cogway.png", override / "maps/cogway.png", green)
	def belt(x, y):
		frame, lx, ly = x // 16, x % 16, y % 16
		direction, phase = frame // 2, frame % 2
		along = lx if direction in (1, 3) else ly
		step = (along + (4 if phase else 0) * (1 if direction in (1, 2) else -1)) % 8
		if lx < 1 or lx > 14 or ly < 1 or ly > 14:
			return (40, 40, 50, 255)
		return (230, 190, 60, 255) if step < 3 else (90, 80, 70, 255)
	synth(override / "overlays/belt.png", 128, 16, belt)
	synth(override / "overlays/pad.png", 32, 16,
		lambda x, y: (120, 230, 255, 255) if 3 <= x % 16 < 13 and 3 <= y < 13 and (x // 16 == 0 or (x % 16 + y) % 3) else (0, 0, 0, 0))
	m["field"]["conveyor_overlay"] = {"image": "overlays/belt.png", "facings": {"north": [0, 1], "east": [2, 3], "south": [4, 5], "west": [6, 7]}, "ms": 150}
	m["field"]["tile_hook_overlay"] = {"image": "overlays/pad.png", "frames": [0, 1], "ms": 400}
	drawn = json.loads(json.dumps(m))
	drawn["field"]["areas"]["cogway"] = {"conveyors": False, "tile_hooks": False}
	# The same picture with the belt and the pads painted in, which the swapped manifest uses for the Cogway.
	width, height, rows = read_png(override / "maps/cogway.png")
	for y in range(height):
		for x in range(width):
			belt_cell = 32 <= x < 96 and 49 <= y < 63
			pad_cell = 112 <= x < 160 and 83 <= y < 93 and 3 <= x % 16 < 13
			if belt_cell or pad_cell:
				rows[y][x * 4:x * 4 + 4] = bytes((200, 170, 60, 255) if belt_cell and x % 8 < 3 else (80, 70, 60, 255) if belt_cell else (90, 170, 200, 255))
	write_png(override / "alt/cogway_drawn.png", width, height, rows)
	drawn["maps"]["areas"].append({"area": "cogway", "image": "alt/cogway_drawn.png"})

	# The slash twice as large with a scale of 2; Saltgrass Market's tileset three times as large with a scale of 3.
	slash = next(e for e in m["effects"] if e["id"] == "slash")
	upscale(override / slash["image"], override / "effects/slash_x2.png", 2)
	slash.update(image="effects/slash_x2.png", frame=[64, 64], scale=2)
	salt = next(t for t in m["tilesets"] if t.get("area") == "saltgrass")
	upscale(override / salt["image"], override / "tilesets/saltgrass_x3.png", 3)
	salt.update(image="tilesets/saltgrass_x3.png", scale=3)
	salt.pop("tile", None)

	# The Cogway's enemies in formations with free places: the brine jelly twice as large with a scale of 2, the fuse
	# beetle three times as large with four empty rows under its feet at its natural size; the medic's back figure from a
	# file kept at its slot's size although battle_figures gives a reference height.
	jelly = dict(next(f for f in manifest["figures"] if f["id"] == "brine_jelly"))
	recolor(export / jelly["image"], override / "figures/brine_jelly.png", blue)
	upscale(override / "figures/brine_jelly.png", override / "figures/brine_jelly_x2.png", 2)
	jelly.update(image="figures/brine_jelly_x2.png", frame=[32, 32], scale=2)
	beetle = dict(next(f for f in manifest["figures"] if f["id"] == "fuse_beetle"))
	upscale(export / beetle["image"], override / "figures/fuse_beetle_natural.png", 3, padded)
	beetle.update(image="figures/fuse_beetle_natural.png", frame=[48, 60], foot=48, natural=True)
	medic = dict(next(f for f in manifest["figures"] if f["id"] == "medic" and f.get("view") == "back"))
	recolor(export / medic["image"], override / "figures/medic_back.png", red)
	medic.update(image="figures/medic_back.png", natural=False)
	for figures in (m["figures"], drawn["figures"]):
		figures += [jelly, beetle, medic]
	for target in (m, drawn):
		target["effects"] = m["effects"]
		target["tilesets"] = m["tilesets"]
	(override / "alt").mkdir(exist_ok=True)
	(override / "alt/assets_drawn.json").write_text(json.dumps(drawn, indent="\t") + "\n")
	(override / "assets.json").write_text(json.dumps(m, indent="\t") + "\n")
	table = json.loads((override / "battle.json").read_text())
	table["enemies"] = {"bog_wisp": {"attack": {"effect": "burst_chill", "cue": "miss"}}}
	(override / "battle.json").write_text(json.dumps(table, indent="\t") + "\n")


def newer_broken(broken):
	"""One problem of each kind the newer entries can have, in BROKEN."""
	m = json.loads((broken / "assets.json").read_text())
	m["maps"]["enabled"] = True
	write_png(broken / m["maps"]["areas"][0]["image"], 16, 16, [bytearray(16 * 4)] * 16)
	m["maps"]["areas"][1]["changes"] = {"no.such_flag": "maps/bell_cove.png"}
	write_png(broken / "maps/atlantis.png", 16, 16, [bytearray(16 * 4)] * 16)
	m["figures"][1]["scale"] = 0
	m["figures"][2]["foot"] = 99
	m["battle_figures"] = {"reference_height": 0, "shadows": True}
	m["tilesets"][0]["animated"] = [{"tile": 14, "frames": [14, 15], "ms": 200}]
	m["objects"] = [
		{"area": "tidewater", "point": "nowhere", "character": "fighter"},
		{"area": "tidewater", "point": "guide", "character": "fighter"},
		{"area": "tidewater", "point": "board", "image": "sprites/shadow.png", "frames": [0], "open": {"frames": [0]}},
	]
	m["field"]["areas"] = {"atlantis": {"water_foam": False}}
	m["backdrops"].append({"terrain": "lava", "image": "backdrops/town.png"})
	m["vehicle_overlays"] = [{"vehicle": "hovercart", "image": "backdrops/town.png"}]
	m["effects"][1]["scale"] = 0
	m["tilesets"][1]["scale"] = 2
	m["figures"][3]["natural"] = True
	m["figures"][4].update(natural=True, scale=2)
	m["field"]["conveyor_overlay"] = {"image": "sprites/shadow.png", "facings": {"north": [0]}}
	m["field"]["tile_hook_overlay"] = {"image": "sprites/shadow.png", "frames": [3]}
	m["field"]["areas"]["tidewater"] = {"conveyors": "yes", "tile_hooks": 1}
	fonts = m["bitmap_fonts"]
	fonts["enabled"] = True
	fonts["fonts"] += [
		{"language": "en", "role": "subtitles", "image": "fonts/en.png", "metrics": "fonts/en.json", "scale": 0},
		{"language": "en", "image": "fonts/en.png", "metrics": "fonts/en.json"},
		{"language": "ja", "role": "dialogue", "image": "fonts/ja.png", "metrics": "fonts/ja_partial.json"},
		{"language": "ja", "role": "menu", "image": "fonts/ja.png", "metrics": "fonts/ja_bad.json"},
	]
	metrics = json.loads((broken / "fonts/ja.json").read_text())
	metrics["rows"] = [row.replace("灰", " ").replace("降", " ") for row in metrics["rows"]]
	metrics["glyphs"] = {c: g for c, g in metrics["glyphs"].items() if c not in "灰降"}
	(broken / "fonts/ja_partial.json").write_text(json.dumps(metrics, indent="\t", ensure_ascii=False) + "\n")
	(broken / "fonts/ja_bad.json").write_text(json.dumps({"line_height": 10, "cell": [10, 10], "rows": ["AB"], "colour": "red",
		"glyphs": {"Q": {"rect": [0, 0, 999, 10]}}, "kerning": {"AZ": -1}}) + "\n")
	(broken / "assets.json").write_text(json.dumps(m, indent="\t") + "\n")


def broken_layouts(broken):
	"""Layout files that fail each way, in BROKEN's ui/: each screen with a problem falls back to the built-in one, and
	the hostile but valid pause layout must draw without stopping the sample."""
	ui = broken / "ui"
	(ui / "title.json").write_text('{"version": 1, "windows": [')
	(ui / "menu.json").unlink()
	(ui / "nonsense.json").write_text('{"version": 1, "windows": []}\n')
	inn = json.loads((ui / "inn.json").read_text())
	box = inn["windows"][0]
	box["colour"] = "red"
	box["elements"] += [
		{"type": "sparkle", "x": 0},
		{"type": "text", "value": "no_such_field + 1", "x": "(3 +", "y": "t(1, 2)"},
		{"type": "fill", "w": 10, "h": 10, "color": "#12345"},
		{"type": "slot", "slot": "nowhere", "w": 1, "h": 1},
	]
	box["relative_to"] = "missing"
	(ui / "inn.json").write_text(json.dumps(inn, indent="\t") + "\n")
	styles = json.loads((ui / "styles.json").read_text())
	styles["resolution"]["width"] = 50
	styles["colors"]["mud"] = "brown"
	(ui / "styles.json").write_text(json.dumps(styles, indent="\t") + "\n")
	pause = json.loads((ui / "pause.json").read_text())
	pause["windows"] += [
		{"name": "loop_a", "relative_to": "loop_b", "x": "loop_b.x / 0", "w": -40, "h": "0 - 99999", "frame": "panel",
			"elements": [{"type": "text", "value": "'x' + 1 / 0", "max_width": -5, "wrap": True, "lines": -1},
				{"type": "list", "items": "rows", "state": "rows", "w": -1, "h": -1},
				{"type": "repeater", "count": 100000000, "layout": "slots", "slots": [[0, 0]], "template": [{"type": "bar", "w": -9, "h": 3, "value": 5, "max": 0}]}]},
		{"name": "loop_b", "relative_to": "loop_a", "x": 2147483647, "y": "-2147483647 * 4", "w": 99999, "h": 99999, "frame": "panel"},
	]
	(ui / "pause.json").write_text(json.dumps(pause, indent="\t") + "\n")


if __name__ == "__main__":
	main()
