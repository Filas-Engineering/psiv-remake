import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from mapkit import tiled  # noqa: E402

REPO = Path(__file__).resolve().parents[3]
AREAS = REPO / "game" / "data" / "areas"
MAPS = REPO / "game" / "data" / "maps"

# Every class and every optional field the area format has, on a 6 by 4 map.
SAMPLE_AREA = {
	"id": "sample",
	"kind": "dungeon",
	"encounter_mode": "random",
	"step_ms": 200,
	"music": "dungeon_sample",
	"map": ["######", "#..~.#", "#....#", "###..."],
	"markers": {"door": [1, 1], "pool_side": [4, 2]},
	"exits": [{"at": [5, 3], "to": "elsewhere", "marker": "back"}],
	"hooks": [
		{"id": "plate", "at": [2, 2], "trigger": "step", "event": "plate_event", "require_flag": "a.flag", "block_flag": "b.flag"},
		{"id": "sign", "at": [1, 2], "event": "sign_event"},
		{"id": "intro", "trigger": "load", "event": "intro_event", "block_flag": "c.flag"},
	],
	"shops": [{"id": "stall", "at": [4, 1], "stock": ["tonic"], "sell_percent": 30}, {"id": "mail_order", "stock": ["tonic", "clear_gel"]}],
	"rests": [{"id": "cot", "at": [3, 2], "cost": 4}, {"id": "dream", "cost": 0}],
	"chests": [{"id": "box", "at": [4, 3], "flag": "chest.box", "items": {"tonic": 2}, "equipment": {}, "money": 9}],
	"zones": [
		{
			"id": "all",
			"from": [1, 1],
			"to": [4, 2],
			"table": [{"encounter": "training", "weight": 3}],
			"visible": {"spawns": [[2, 1]], "cap": 1, "chase_ms": 100},
			"random": {"min_steps": 2, "max_steps": 4, "step_distance": 2},
		}
	],
}


def sample_tmj(tileset_ref="cells.tsj"):
	return tiled.area_to_tmj(copy.deepcopy(SAMPLE_AREA), tileset_ref)


def objects(tmj):
	return next(layer for layer in tmj["layers"] if layer["name"] == "objects")["objects"]


def terrain(tmj):
	return next(layer for layer in tmj["layers"] if layer["name"] == "terrain")


class TempCase(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.tmp = Path(self._tmp.name)
		(self.tmp / "cells.tsj").write_text(json.dumps(tiled.tileset()), encoding="utf-8")

	def tearDown(self):
		self._tmp.cleanup()

	def convert(self, tmj):
		return tiled.tmj_to_area(tmj, self.tmp)

	def rejects(self, tmj, *fragments):
		with self.assertRaises(tiled.MapError) as caught:
			self.convert(tmj)
		text = str(caught.exception)
		for fragment in fragments:
			self.assertIn(fragment, text)
		return text
