"""Command lines for tiled_to_area.py, area_to_tiled.py and make_tileset.py."""

import argparse
import json
import os
import sys
from pathlib import Path

from . import areajson, png, tiled

DEFAULT_TILESET = Path(__file__).resolve().parents[3] / "game" / "data" / "maps" / "cells.tsj"


def _write(path, text):
	Path(path).parent.mkdir(parents=True, exist_ok=True)
	Path(path).write_text(text, encoding="utf-8")


def tiled_to_area(argv=None):
	parser = argparse.ArgumentParser(prog="tiled_to_area.py", description="Convert a Tiled map (.tmj) to a game area file.")
	parser.add_argument("map", help="the .tmj map")
	parser.add_argument("-o", "--output", help="area file to write (default: standard output)")
	parser.add_argument("--check", metavar="AREA", help="write nothing; exit 1 unless AREA already holds exactly what the map converts to")
	args = parser.parse_args(argv)
	try:
		text = areajson.dumps(tiled.load_tmj(args.map))
	except (tiled.MapError, areajson.AreaError) as e:
		for problem in e.problems:
			print(f"{args.map}: {problem}", file=sys.stderr)
		return 1
	if args.check:
		try:
			current = Path(args.check).read_text(encoding="utf-8")
		except OSError:
			current = None
		if current != text:
			print(f"{args.check} is stale: it does not match {args.map}; run make -C game maps", file=sys.stderr)
			return 1
		return 0
	if args.output:
		_write(args.output, text)
	else:
		sys.stdout.write(text)
	return 0


def area_to_tiled(argv=None):
	parser = argparse.ArgumentParser(prog="area_to_tiled.py", description="Convert a game area file to a Tiled map (.tmj).")
	parser.add_argument("area", help="the area .json file")
	parser.add_argument("-o", "--output", required=True, help="the .tmj map to write")
	group = parser.add_mutually_exclusive_group()
	group.add_argument("--tileset", help=f"external .tsj tileset the map refers to (default: {DEFAULT_TILESET})")
	group.add_argument("--embed", action="store_true", help="embed the cell tileset in the map instead")
	parser.add_argument("--tile-size", type=int, default=16, help="tile width and height in pixels (default: 16)")
	args = parser.parse_args(argv)
	try:
		area = json.loads(Path(args.area).read_text(encoding="utf-8"))
	except OSError as e:
		print(f"cannot read {args.area}: {e.strerror}", file=sys.stderr)
		return 1
	except json.JSONDecodeError as e:
		print(f"{args.area} is not valid JSON: {e.msg} at line {e.lineno}", file=sys.stderr)
		return 1
	out_dir = Path(args.output).resolve().parent
	if args.embed:
		image = DEFAULT_TILESET.with_name("cells.png")
		ref = tiled.tileset(os.path.relpath(image, out_dir), args.tile_size)
	else:
		ref = os.path.relpath(Path(args.tileset or DEFAULT_TILESET).resolve(), out_dir)
	try:
		tmj = tiled.area_to_tmj(area, ref, args.tile_size)
	except areajson.AreaError as e:
		for problem in e.problems:
			print(f"{args.area}: {problem}", file=sys.stderr)
		return 1
	_write(args.output, tiled.dumps_tmj(tmj))
	return 0


def make_tileset(argv=None):
	parser = argparse.ArgumentParser(prog="make_tileset.py", description="Write the cell tileset (cells.tsj) and its image (cells.png).")
	parser.add_argument("directory", help="folder to write them to")
	parser.add_argument("--tile-size", type=int, default=16, help="tile width and height in pixels (default: 16)")
	args = parser.parse_args(argv)
	folder = Path(args.directory)
	folder.mkdir(parents=True, exist_ok=True)
	_write(folder / "cells.tsj", tiled.dumps_tmj(tiled.tileset("cells.png", args.tile_size)))
	(folder / "cells.png").write_bytes(png.cell_tiles(args.tile_size))
	return 0
