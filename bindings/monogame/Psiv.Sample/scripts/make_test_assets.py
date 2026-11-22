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
its natural size on its foot row), and the medic's back figure kept at its slot's size with `natural: false`. Then the
battle's own window frame (recolored rust brown), the fighter bowing by its pose's name alone, the medic's sheet without the
shrug the chart scene asks of it, a flourish pose on the fighter's back figure for its Focus Shot and a wobble on the gel
mites' for their attack; alt/battle_front_status.json and alt/battle_side_status.json are the built-in battle layouts
with a statuses table, for a run that swaps them in.
alt/assets_more.json is the same manifest with entries no other run should see: backdrops by cell (a picture for the
reed path, named regions for Tidewater Landing) and by a flag, the guide's sheet with two facings (west mirrored from
east, north from its fallback, facing east until a script turns it), the guide's portraits by expression, and title art
(a background, a logo, two layers, the traveler and the creature), still pictures for the dialogue's `picture` tags (a
chart and a bell tower), the shop's and inn's keepers at Tidewater Landing, and the commands' pictures recolored. Beside it in alt/:
assets_more_natural.json (the title art drawn at its natural size for a screen 360 high), title_natural.json (a title
layout placing it: the scene in a band, the logo, traveler and creature at their sizes), title_builtin.json (the built-in
title layout, to swap back) and dialogue_pictures.json (the built-in dialogue layout with a `bell` tag whose values map to
cues).

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
	looks(export, override, manifest)
	more(export, override)

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
	more_broken(broken)
	broken_layouts(broken)
	looks_broken(broken)
	items_broken(broken)
	menus_broken(broken)
	dialogue_items_broken(broken)
	motion_broken(broken)
	motion_more_broken(broken)
	choices_broken(broken)
	host_broken(broken)
	flows_broken(broken, export)
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


def looks(export, override, manifest):
	"""Adds the battle's own window frame, extra poses and the party slots' status looks to OVERRIDE (see the module's
	description)."""
	recolor(export / manifest["ui"]["battle_frame"]["image"], override / "ui/battle_frame.png", (2, 1, 0))
	medic = json.loads(json.dumps(next(c for c in manifest["characters"] if c["id"] == "medic")))
	recolor(export / medic["image"], override / "characters/medic.png", (1, 2, 0))
	medic["image"] = "characters/medic.png"
	del medic["poses"]["shrug"]
	del medic["animations"]["shrug"]
	# In both manifests, since the Cogway's run swaps in the second with the same battle table.
	for name in ("assets.json", "alt/assets_drawn.json"):
		m = json.loads((override / name).read_text())
		m["ui"]["battle_frame"] = {"image": "ui/battle_frame.png", "border": 8}
		# The fighter bows by its pose's name alone; the medic's sheet has no shrug, which the chart scene asks of it.
		fighter = next(c for c in m["characters"] if c["id"] == "fighter")
		del fighter["animations"]["bow"]
		m["characters"].append(medic)
		# Battle poses beyond the standard ones: the fighter's back figure flourishes its Focus Shot, the gel mites wobble as
		# they attack.
		back = next(f for f in m["figures"] if f["id"] == "fighter" and f.get("view") == "back")
		back["animations"]["flourish"] = {"frames": [2, 1, 2], "ms": [90, 90, 100]}
		mite = next(f for f in m["figures"] if f["id"] == "gel_mite")
		mite["animations"]["wobble"] = {"frames": [1, 0, 1], "ms": [90, 90, 100]}
		(override / name).write_text(json.dumps(m, indent="\t") + "\n")
	table = json.loads((override / "battle.json").read_text())
	table["actions"]["focus_shot"] = {"animation": "flourish"}
	table["enemies"]["gel_mite"] = {"attack": {"animation": "wobble"}}
	(override / "battle.json").write_text(json.dumps(table, indent="\t") + "\n")
	# The battle layouts with a statuses table, for a run that swaps them in: jumble tints the name pink over the others and
	# its icon comes first, sleep tints it blue and shows no icon, poison tints it green over sleep.
	statuses = {"jumble": {"color": "#f0a0f0", "priority": 3}, "poison": {"color": "good", "priority": 2}, "sleep": {"color": "tp", "icon": "none", "priority": 1}}
	for screen in ("battle_front", "battle_side"):
		layout = json.loads((export / f"ui/{screen}.json").read_text())
		layout["statuses"] = statuses
		(override / f"alt/{screen}_status.json").write_text(json.dumps(layout, indent="\t") + "\n")


def looks_broken(broken):
	"""One problem of each kind the battle frame, the poses and the status looks can have, in BROKEN."""
	m = json.loads((broken / "assets.json").read_text())
	m["ui"]["battle_frame"] = {"image": "ui/no_battle_frame.png", "border": 8}
	m["figures"][0]["animations"]["Big Jump"] = {"frames": [0], "ms": [100]}
	m["characters"][2]["poses"]["cartwheel"] = m["characters"][2]["poses"]["nod"]
	(broken / "assets.json").write_text(json.dumps(m, indent="\t") + "\n")
	table = json.loads((broken / "battle_presentation.json").read_text())
	table["enemies"]["rust_hound"] = {"attack": {"animation": "pounce"}}
	table["actions"]["tonic"] = {"animation": "Raise Bottle"}
	(broken / "battle_presentation.json").write_text(json.dumps(table, indent="\t") + "\n")
	ui = broken / "ui"
	side = json.loads((ui / "battle_side.json").read_text())
	side["statuses"] = {"nausea": {"color": "bad"}, "poison": {"color": "mud", "icon": "maybe", "priority": 500, "glow": True}}
	(ui / "battle_side.json").write_text(json.dumps(side, indent="\t") + "\n")
	inn = json.loads((ui / "inn.json").read_text())
	inn["statuses"] = {"poison": {"color": "bad"}}
	(ui / "inn.json").write_text(json.dumps(inn, indent="\t") + "\n")
	styles = json.loads((ui / "styles.json").read_text())
	styles["frames"]["battle"]["skin"] = "arena"
	styles["frames"]["ink"] = {"type": "outline", "skin": "battle"}
	(ui / "styles.json").write_text(json.dumps(styles, indent="\t") + "\n")


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


def more(export, override):
	"""Writes OVERRIDE's alt/assets_more.json (see the module's description)."""
	m = json.loads((override / "assets.json").read_text())
	blue, green = (2, 0, 1), (0, 2, 1)

	# Named backdrops for the maps, and one for the reed path while the ash storm's flag is set.
	synth(override / "backdrops/reeds.png", 320, 105,
		lambda x, y: (150, 200, 220, 255) if y < 50 else (40, 110 + (x * 7 % 30), 50, 255) if (x * 5 + y) % 9 < 6 else (90, 150, 70, 255))
	synth(override / "backdrops/dunes.png", 320, 105,
		lambda x, y: (230, 190, 120, 255) if y < 50 else (210, 170 - abs((x % 64) - 32), 100, 255))
	synth(override / "backdrops/ash_storm.png", 320, 105, lambda x, y: (70 + (x * 3 + y * 7) % 40, 64, 70, 255))
	m["backdrops"] += [
		{"id": "reeds", "image": "backdrops/reeds.png"},
		{"id": "dunes", "image": "backdrops/dunes.png"},
		{"flag": "fells.ash_storm", "area": "reed_path", "image": "backdrops/ash_storm.png"},
	]
	# The reed path, 20 by 9 cells: reeds on its west part, dunes on its east part, nothing between.
	synth(override / "backdrop_maps/reed_path.png", 20, 9,
		lambda x, y: (64, 160, 80, 255) if x < 9 else (208, 176, 112, 255) if x >= 13 else (0, 0, 0, 0))
	synth(override / "alt/reed_path_dunes.png", 20, 9, lambda x, y: (208, 176, 112, 255))
	m["backdrop_maps"] = [
		{"area": "reed_path", "image": "backdrop_maps/reed_path.png", "colors": {"#40a050": "reeds", "#d0b070": "dunes"}},
		{"area": "tidewater", "regions": {"square": {"rect": [1, 1, 5, 4], "backdrop": "reeds"}, "quay": {"rect": [8, 3, 6, 3], "backdrop": "dunes"}}},
	]

	# The guide's south and east frames, recolored, a yellow mark on the east frames' right; west is east flipped, north
	# falls back to south, and it faces east until a script turns it.
	width, height, rows = read_png(export / "characters/guide.png")
	def frame(i):
		cx, cy = i % (width // 16) * 16, i // (width // 16) * 16
		return [bytearray(rows[cy + y][cx * 4:(cx + 16) * 4]) for y in range(16)]
	frames = [frame(i) for i in (6, 7, 8, 3, 4, 5)]
	for f in frames[3:]:
		for y in (4, 5):
			f[y][13 * 4:15 * 4] = bytes((250, 220, 40, 255)) * 2
	write_png(override / "characters/guide_two.png", 96, 16, [b"".join(f[y] for f in frames) for y in range(16)])
	recolor(override / "characters/guide_two.png", override / "characters/guide_two.png", green)
	m["characters"].append({"id": "guide", "image": "characters/guide_two.png", "walk": {"south": [0, 1, 2], "east": [3, 4, 5]},
		"facing": {"mirror": {"west": "east"}, "fallback": "south", "start": "east"}})

	# The guide's portrait smiling and frowning; {portrait:guide:grin} names an expression it lacks.
	recolor(export / "portraits/guide.png", override / "portraits/guide_smile.png", green)
	recolor(export / "portraits/guide.png", override / "portraits/guide_frown.png", blue)
	m["portraits"]["guide"] = {"image": "portraits/guide.png", "expressions": {"smile": "portraits/guide_smile.png", "frown": "portraits/guide_frown.png"}}

	# Title art: a dusk sky with hills, a plaque for the logo, clouds drifting left and two birds flapping, and the
	# traveler and the creature recolored.
	def sky(x, y):
		hill = 130 + int(12 * ((x * 37 % 101) / 101.0)) - (x % 60 > 30) * 6
		if y >= hill:
			return (40, 70 + y % 7, 50, 255)
		return (40 + y, 50 + y // 2, 120 - y // 3, 255)
	synth(override / "title/sky.png", 320, 180, sky)
	def plaque(x, y):
		if x < 3 or y < 3 or x >= 237 or y >= 37:
			return (200, 160, 60, 255)
		return (240, 230, 200, 255) if 12 <= y < 28 and (x - 12) % 24 < 16 and 12 <= x < 228 else (60, 40, 30, 255)
	synth(override / "title/logo.png", 240, 40, plaque)
	synth(override / "title/clouds.png", 160, 20,
		lambda x, y: (250, 250, 255, 230) if ((x % 80) - 40 + (x // 80) * 6) ** 2 / 4 + (y - 12) ** 2 * 3 < 300 else (0, 0, 0, 0))
	synth(override / "title/birds.png", 24, 8,
		lambda x, y: (30, 30, 40, 255) if (y == 4 - abs((x % 12) - 6) // 2 if x // 12 == 0 else y == 2 + abs((x % 12) - 6) // 2) and x % 12 != 0 else (0, 0, 0, 0))
	recolor(export / "title/traveler.png", override / "title/traveler.png", green)
	recolor(export / "title/creature.png", override / "title/creature.png", blue)
	m["title_art"] = {
		"background": "title/sky.png",
		"logo": "title/logo.png",
		"layers": [
			{"image": "title/clouds.png", "frame": [80, 20], "frames": [0, 1], "ms": 900, "at": [0, 60], "scale": 4, "scroll": [-30, 0]},
			{"image": "title/birds.png", "frame": [12, 8], "frames": [0, 1], "ms": 250, "at": [960, 150], "scale": 4},
		],
		"traveler": {"image": "title/traveler.png", "frames": [1, 0], "ms": 260},
		"creature": {"image": "title/creature.png", "frames": [0, 1], "ms": 400},
	}

	# A chart of the cove and a bell tower for the dialogue's pictures, keepers for Tidewater's stall and inn, and the
	# commands' pictures recolored.
	synth(override / "pictures/chart.png", 240, 150, lambda x, y: (60, 40, 30, 255) if x < 4 or y < 4 or x >= 236 or y >= 146
		else (40, 90, 160, 255) if (x - 150) ** 2 + (y - 80) ** 2 * 2 < 2500 else (220, 200, 150, 255) if (x + y) % 16 else (170, 140, 90, 255))
	synth(override / "pictures/tower.png", 120, 160, lambda x, y: (200, 170, 70, 255) if (x - 60) ** 2 + (y - 40) ** 2 < 220
		else (120, 110, 100, 255) if 35 <= x < 85 and y >= 20 else (150, 190, 230, 255))
	m["pictures"] = {"chart": "pictures/chart.png", "tower": "pictures/tower.png"}
	m["keepers"] = {"tidewater_inn": "guide", "tidewater_stall": "dockhand"}
	m["command_icons"] = {}
	for command in ("attack", "defend", "item", "technique"):
		recolor(export / f"command_icons/{command}.png", override / f"command_icons/{command}.png", green)
		m["command_icons"][command] = f"command_icons/{command}.png"
	(override / "alt/assets_more.json").write_text(json.dumps(m, indent="\t") + "\n")
	m["title_art"]["reference_height"] = 360
	(override / "alt/assets_more_natural.json").write_text(json.dumps(m, indent="\t") + "\n")
	title = json.loads((export / "ui/title.json").read_text())
	(override / "alt/title_builtin.json").write_text(json.dumps(title, indent="\t") + "\n")
	scene, box = title["windows"][0], title["windows"][1]
	# The art's scene in a band across the top, the logo over it and the traveler and creature standing on its bottom.
	scene["y"], scene["h"] = 40, 260
	box["anchor"], box["y"] = "bottom", 40
	scene["elements"] += [
		{"type": "image", "image": "logo", "x": "(W - logo_w) / 2", "y": 24, "w": "logo_w", "h": "logo_h"},
		{"type": "image", "image": "hero_frame", "x": 200, "y": "260 - hero_h - 8", "w": "hero_w", "h": "hero_h"},
		{"type": "image", "image": "creature_frame", "x": "W - 200 - creature_w", "y": "260 - creature_h - 8", "w": "creature_w", "h": "creature_h"},
	]
	box["elements"] = [e for e in box["elements"] if e.get("image") not in ("logo", "hero_frame", "creature_frame")]
	(override / "alt/title_natural.json").write_text(json.dumps(title, indent="\t") + "\n")
	dialogue = json.loads((export / "ui/dialogue.json").read_text())
	dialogue["tags"]["bell"] = {"action": "cue", "values": {"1": "lookout_creak", "2": "cursor"}, "default": "confirm"}
	(override / "alt/dialogue_pictures.json").write_text(json.dumps(dialogue, indent="\t") + "\n")


def more_broken(broken):
	"""One problem of each kind the backdrop maps, flag backdrops, sheet facings, portrait expressions and title art can
	have, in BROKEN."""
	m = json.loads((broken / "assets.json").read_text())
	m["backdrops"] += [
		{"id": "reeds", "image": "backdrops/town.png"},
		{"flag": "no.such_flag", "image": "backdrops/town.png"},
		{"flag": "fells.ash_storm", "kind": "town", "image": "backdrops/town.png"},
		{"image": "backdrops/town.png"},
	]
	write_png(broken / "backdrop_maps/small.png", 16, 16, [bytearray((64, 160, 80, 255) * 16)] * 16)
	write_png(broken / "backdrop_maps/tidewater.png", 14, 9, [bytearray((64, 160, 80, 255) * 13 + (18, 52, 86, 255))] * 8 + [bytearray((9, 9, 9, 128) * 14)])
	m["backdrop_maps"] = [
		{"area": "atlantis", "regions": {}},
		{"area": "reed_path", "image": "backdrop_maps/small.png", "colors": {"#40a050": "reeds"}},
		{"area": "tidewater", "image": "backdrop_maps/tidewater.png", "colors": {"#40a050": "lagoon", "red": "reeds"}},
		{"area": "saltgrass", "regions": {"a": {"rect": [0, 0, 4, 4], "backdrop": "reeds"}, "b": {"rect": [2, 2, 4, 4], "backdrop": "reeds"},
			"c": {"rect": [0, 0, 999, 1], "backdrop": "reeds"}, "d": {"rect": [0, 8, 1, 1], "backdrop": "lagoon"}}},
		{"area": "reed_path", "image": "backdrop_maps/small.png", "regions": {}},
	]
	dockhand = next(c for c in m["characters"] if c["id"] == "dockhand")
	dockhand["walk"] = {k: v for k, v in dockhand["walk"].items() if k in ("east", "south")}
	dockhand["facing"] = {"mirror": {"west": "north", "east": "south"}, "fallback": "up"}
	next(c for c in m["characters"] if c["id"] == "medic")["facing"] = {"fallback": "south", "start": "east"}
	m["portraits"]["guide_smile"] = {"expressions": {"wink face": "portraits/guide.png", "sad": 3}}
	m["portraits"]["mystic_worried"] = {}
	m["portraits"]["scout_grin"] = {"image": "portraits/scout.png", "expressions": {"ok": "portraits/scout_ok.png"}}
	title = m["title_art"]
	title.update(background="title/missing.png", sparkle=True)
	title["layers"] = [{"image": "title/traveler.png", "frames": [9], "at": [0], "scroll": [99999, 0], "scale": 0}]
	title["reference_height"] = 0
	m["command_icons"] = {"attack": "command_icons/missing.png", "dance": "command_icons/defend.png"}
	m["pictures"] = {"bell tower": "title/traveler.png", "chart": 5}
	m["keepers"] = {"no_such_shop": "guide", "tidewater_inn": "nobody", "tidewater_stall": 3}
	(broken / "assets.json").write_text(json.dumps(m, indent="\t") + "\n")


def items_broken(broken):
	"""One problem of each kind the layout items' fields can have, in BROKEN's ui/ (docs/ui-layout.md, "Layout items")."""
	ui = broken / "ui"

	def change(name, edit):
		data = json.loads((ui / f"{name}.json").read_text())
		edit(data)
		(ui / f"{name}.json").write_text(json.dumps(data, indent="\t") + "\n")

	def front(d):
		d["params"]["command_axis"] = "diagonal"
		d["params"]["damage_numbers"] = "both"
		d["windows"].append({"name": "icons", "elements": [{"type": "repeater", "items": "menu_items", "dx": 40, "template": [
			{"type": "image", "image": "icon", "w": 32, "h": 32}, {"type": "text", "visible": "status_nausea", "value": "'?'"}]}]})
	change("battle_front", front)
	change("sound_test", lambda d: d.update({"start": True, "options": ["new_game"]}))
	change("menu_equip", lambda d: d.update({"places": [{"slot": "tail"}, {"slot": "head", "label": ""}, {"slot": "head"}, {"label": "ui.stat.defense"}]}))
	change("menu_status", lambda d: d.update({"profile": ["name", "shoe_size", "name"], "places": [{"slot": "head"}]}))
	change("field", lambda d: d.update({"params": {"arrive_toast": 0, "area_banners": 0, "followers_arrive": "beside"}}))
	change("dialogue", lambda d: d["tags"].update({"snd": {"cases": [{"value": "1", "from": 2, "action": "cue"}, {"from": 5, "to": 1, "action": "music"},
		{"from": 1, "to": 3, "action": "music_stop", "ids": ["title"]}, {"value": "4"}]}}))

	def styles(d):
		d["params"]["dev_scale"] = 5
		d["params"]["dev_draw"] = 2
	change("styles", styles)


def menus_broken(broken):
	"""One problem of each kind the menu items' fields can have, in BROKEN's ui/ (docs/ui-layout.md, "Menu entries")."""
	ui = broken / "ui"

	def change(name, edit):
		data = json.loads((ui / f"{name}.json").read_text())
		edit(data)
		(ui / f"{name}.json").write_text(json.dumps(data, indent="\t") + "\n")

	change("extras", lambda d: d.update({"entries": [{"page": "extras"}, {"page": "options"}, {"page": "options"}, {"label": "ui.title.options"},
		{"page": "dev", "colour": "red"}, {"page": "quests", "children": [{"page": "save"}]}]}))
	change("shop", lambda d: d.update({"entries": [{"page": "items"}]}))
	change("menu_system", lambda d: d.update({"rows": ["save", "teleport", "save"], "params": {"system_action": "always"}}))
	change("menu_abilities", lambda d: d.update({"params": {"unusable": "grey"}}))
	change("menu_options", lambda d: d.update({"params": {"party_talk": "speakers", "talk_hide_menu": "yes"}}))
	change("menu_status", lambda d: d.update({"params": {"tab_h": "tall"}}))
	change("battle_side", lambda d: d.update({"command_rows": {"top": ["fight", "dance"], "member": [], "side": 1, "vehicle": ["attack", "fly"]}}))


def dialogue_items_broken(broken):
	"""One problem of each kind the name pad's sizes, the shop's steps, the narration routing, the developer badge, the music fade and the
	page arrow can have, in BROKEN (docs/ui-layout.md)."""
	ui = broken / "ui"

	def change(name, edit):
		data = json.loads((ui / f"{name}.json").read_text())
		edit(data)
		(ui / f"{name}.json").write_text(json.dumps(data, indent="\t") + "\n")

	change("name_entry", lambda d: d.update({"params": {"pad_cell_w": "cell_size", "pad_spacing": 3}}))
	change("shop", lambda d: d.update({"params": {"steps": "flow"}}))
	change("dialogue", lambda d: d["params"].update({"narration_when": "starts_with(line_key)", "narration_wait": "later"}))

	def styles(d):
		d["params"]["dev_badge_x"] = -5
		d["params"]["dev_scale_bitmap"] = 2
		d["timings"]["music_fade_ms"] = -1
		d["timings"]["game_over_return_ms"] = -1
	change("styles", styles)
	m = json.loads((broken / "assets.json").read_text())
	m["ui"]["page_arrow"] = "ui/no_arrow.png"
	(broken / "assets.json").write_text(json.dumps(m, indent="\t") + "\n")


def motion_broken(broken):
	"""One problem of each kind an area's palette tracks, frame runs and layers and a tileset's palette tracks can have, in
	BROKEN (docs/assets.md, "Motion")."""
	m = json.loads((broken / "assets.json").read_text())
	tidewater = next(a for a in m["maps"]["areas"] if a["area"] == "tidewater")
	tidewater["palettes"] = [
		{"colors": {"sea": "#12345", "sand": "#d0b070", "dune": "#d0b070"}, "steps": [{"reef": "#000000"}], "ms": 100, "ticks": 6},
		{"colors": {"sea": "#123456"}, "steps": [{}, {}], "clock": "sometimes", "flag": "no.such_flag"},
	]
	tidewater["animated"] = [
		{"cells": [12, 8, 4, 4], "image": "tilesets/town.png", "frames": [0]},
		{"cells": [0, 0, 1, 1], "pixels": [0, 0, 16, 16], "image": "tilesets/town.png", "frames": [0]},
		{"cells": [0, 0, 1, 1], "image": "tilesets/town.png", "frames": [0, 999], "ms": [100], "layer": "middle"},
		{"pixels": [0, 0, 16, 16], "image": "motion/missing.png", "frames": [0]},
	]
	tidewater["layers"] = [
		{"image": "backdrops/town.png", "place": "under", "repeat": "always", "scroll": [1],
			"bands": [{"rows": [5, 2]}, {"rows": [0, 10]}, {"rows": [4, 12]}], "wave": {"axis": "columns", "amplitude": -1}},
		{"place": "front", "unless_flag": "no.such_flag"},
	]
	m["maps"]["areas"].append({"area": "atlantis", "layers": [{"image": "backdrops/town.png"}]})
	m["tilesets"][0]["palettes"] = [{"colors": {"floor": "#7a6a50"}, "steps": []}, {"steps": [{"floor": "#000000"}]}]
	(broken / "assets.json").write_text(json.dumps(m, indent="\t") + "\n")


def motion_more_broken(broken):
	"""A tileset entry without a picture that names tiles, a sprite's bad palette track and a sprite that is neither a file
	nor an object, and a layer's bad `at`, in BROKEN (docs/assets.md, "tilesets", "sprites and status_icons", "Motion")."""
	m = json.loads((broken / "assets.json").read_text())
	m["tilesets"].append({"kind": "dungeon", "floor": [0], "palettes": [{"colors": {"floor": "#3a3642"}, "steps": [{"floor": "#4e3a3c"}]}]})
	m["sprites"]["bubble"] = {"image": m["sprites"]["bubble"], "palettes": [{"colors": {"white": "#fff"}, "steps": [{}]}]}
	m["sprites"]["alert"] = 7
	tidewater = next(a for a in m["maps"]["areas"] if a["area"] == "tidewater")
	tidewater["layers"].append({"image": "backdrops/town.png", "repeat": "none", "at": [1]})
	(broken / "assets.json").write_text(json.dumps(m, indent="\t") + "\n")


def choices_broken(broken):
	"""A bad value of each layout choice the broken copy's files can take: the battle's Item list order, macro list
	switch, failed macro step and macro flow, the Items page's actions with nothing that draws them, and the Macros
	page's options switch (docs/ui-layout.md; the title's `continue` is checked on a copy in the layout_choices run, the
	broken copy's title file being unreadable)."""
	ui = broken / "ui"
	front = json.loads((ui / "battle_front.json").read_text())
	front["params"].update({"item_order": "worn_first", "macro_auto_first": "on", "macro_failed": "skip", "macro_flow": "keep"})
	(ui / "battle_front.json").write_text(json.dumps(front, indent="\t") + "\n")
	macros = json.loads((ui / "menu_macros.json").read_text())
	macros["params"] = {"options": "yes"}
	(ui / "menu_macros.json").write_text(json.dumps(macros, indent="\t") + "\n")
	items = json.loads((ui / "menu_items.json").read_text())
	items["params"] = {"actions": 1}
	(ui / "menu_items.json").write_text(json.dumps(items, indent="\t") + "\n")


def host_broken(broken):
	"""A bad value of each field the host-call adoption added: the Items page's Discard with nothing that draws its steps,
	the Load list's `select_newest` reading no field, and the field's exit markers with a missing picture, a bad frame time
	and an unknown way of firing (docs/ui-layout.md, docs/assets.md "field")."""
	ui = broken / "ui"
	items = json.loads((ui / "menu_items.json").read_text())
	items["params"]["discard"] = 1
	(ui / "menu_items.json").write_text(json.dumps(items, indent="\t") + "\n")
	load = json.loads((ui / "load_game.json").read_text())
	load["params"] = {"select_newest": "yes"}
	(ui / "load_game.json").write_text(json.dumps(load, indent="\t") + "\n")
	m = json.loads((broken / "assets.json").read_text())
	m.setdefault("field", {})["exit_markers"] = {"always": {"image": "overlays/no_exit.png", "frames": [0], "ms": 0}, "sometimes": {}}
	(broken / "assets.json").write_text(json.dumps(m, indent="\t") + "\n")


def flows_broken(broken, export):
	"""A bad value of each field the layout flows added: the Items page's row stacking and count formats, the shop's and
	the inn's flows, the travel's one-destination rule, Cancel row and message keys, the battle's first command delay and
	result sequence, a vehicle battle's file without a window its code places by, and the flow, intro and result
	sequence on screens that take none (docs/ui-layout.md, "Layout flows"; the title's intro and `after_defeat` and the
	menu's `navigation` are checked on copies in the flows run, the broken copy's title and menu files being unreadable
	or missing)."""
	ui = broken / "ui"

	def change(name, edit):
		data = json.loads((ui / f"{name}.json").read_text())
		edit(data)
		(ui / f"{name}.json").write_text(json.dumps(data, indent="\t") + "\n")

	change("menu_items", lambda d: d["params"].update({"stack_rows": "pile", "count_format": "x{1}", "count_format_one": 5}))
	change("shop", lambda d: d.update({"flow": ["welcome", "quantity", "nap"], "result_sequence": [{"phase": "result"}]}))
	change("inn", lambda d: d.update({"flow": ["choice", "welcome"]}))
	change("travel", lambda d: d.update({"params": {"one_destination": "jump", "cancel_row": "never", "leave_key": "Leaving now!", "arrive_key": ""}}))
	change("sound_test", lambda d: d.update({"intro": [{"name": "studio", "ms": 500}]}))

	def front(d):
		d["params"]["command_open_delay"] = "soon"
		d["result_sequence"] = [{"phase": "victory", "ms": -5}, {"phase": "party"}, {"phase": "victory"}, {"ms": 100}]
	change("battle_front", front)
	vehicle = json.loads((export / "ui/battle_front.json").read_text())
	vehicle["windows"] = [w for w in vehicle["windows"] if w["name"] != "keys"]
	vehicle["flow"] = ["list"]
	(ui / "battle_front_vehicle.json").write_text(json.dumps(vehicle, indent="\t") + "\n")


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
	styles["params"]["toast_size"] = 3
	styles["params"]["toast_h"] = "tall"
	styles["params"]["field_cell"] = 4
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
	dialogue = json.loads((ui / "dialogue.json").read_text())
	dialogue["tags"]["bell"] = {"action": "cue", "id": "confirm", "values": {"1": "", "2": 3}, "default": "cursor"}
	dialogue["tags"]["clear_all"] = {"action": "picture_clear", "default": "chart"}
	picture = next(w for w in dialogue["windows"] if w["name"] == "picture")
	picture["open"] = {"kind": "fade", "step": 8}
	picture["close"] = {"kind": "shrink", "step": -1}
	dialogue["params"] = {"box_h": "(70", "box_width": 300, "narration_cps": "speed_of_sound"}
	dialogue["windows"].append({"name": "narration", "elements": [{"type": "role", "role": "subtitles"}]})
	(ui / "dialogue.json").write_text(json.dumps(dialogue, indent="\t") + "\n")
	confirm = json.loads((ui / "confirm.json").read_text())
	confirm["params"] = {"backdrop_dim": 0.3}
	(ui / "confirm.json").write_text(json.dumps(confirm, indent="\t") + "\n")
	front = json.loads((ui / "battle_front.json").read_text())
	front["params"]["party_slot_5_x"] = 100
	(ui / "battle_front.json").write_text(json.dumps(front, indent="\t") + "\n")
	side = json.loads((ui / "battle_side.json").read_text())
	side["params"]["motion_percent"] = "speed * 2"
	(ui / "battle_side.json").write_text(json.dumps(side, indent="\t") + "\n")
	panels = json.loads((ui / "panels.json").read_text())
	panels["params"] = {"margin": 6, "zoom": 2}
	(ui / "panels.json").write_text(json.dumps(panels, indent="\t") + "\n")


if __name__ == "__main__":
	main()
