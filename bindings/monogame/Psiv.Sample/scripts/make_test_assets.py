#!/usr/bin/env python3
"""Builds the asset directories the sample's asset tests use, from the placeholder export (--export-placeholders).

  make_test_assets.py EXPORT OVERRIDE BROKEN

OVERRIDE: a small asset directory replacing a few ids with recolored copies of their exported pictures (the town
tileset, the fighter, the guide's portrait, the gel mite's battle figure, the field backdrop, the slash effect, the
sleep icon and an enabled UI skin), one cue and one track, and a battle presentation table; tilesets/town_alt.png is a
second recoloring for the hot reload test. Every other id keeps its generated placeholder.

BROKEN: a copy of the export with one problem of each kind the asset check reports.
"""

import json
import shutil
import struct
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


def main():
	export, override, broken = (Path(a) for a in sys.argv[1:4])
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

	shutil.rmtree(broken, ignore_errors=True)
	shutil.copytree(export, broken)
	(broken / "portraits/guide.png").unlink()
	write_png(broken / "tilesets/dungeon.png", 17, 17, [bytearray(17 * 4)] * 17)
	manifest["characters"].append(dict(fighter, id="nobody", image=manifest["characters"][0]["image"]))
	manifest["characters"][0]["animations"]["dance"] = "nod"
	manifest["characters"][1]["animations"]["nod"] = "twirl"
	manifest["figures"][0]["animations"]["juggle"] = {"frames": [0], "ms": 100}
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
	(broken / "battle_presentation.json").write_text(json.dumps(table, indent="\t") + "\n")


if __name__ == "__main__":
	main()
