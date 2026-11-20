import contextlib
import io
import json
import shutil

from support import AREAS, MAPS, TempCase

from mapkit import areajson, cli, tiled


def run(main, *argv):
	out, err = io.StringIO(), io.StringIO()
	with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
		code = main([str(a) for a in argv])
	return code, out.getvalue(), err.getvalue()


class RoundTripTest(TempCase):
	def test_every_area_survives_area_to_tmj_to_area(self):
		areas = sorted(AREAS.glob("*.json"))
		self.assertGreaterEqual(len(areas), 6)
		for path in areas:
			with self.subTest(area=path.name):
				original = json.loads(path.read_text(encoding="utf-8"))
				tmj_path = self.tmp / f"{path.stem}.tmj"
				# Through the files, as a person converting in Tiled would.
				tmj_path.write_text(tiled.dumps_tmj(tiled.area_to_tmj(original, "cells.tsj")), encoding="utf-8")
				back = tiled.load_tmj(tmj_path)
				self.assertTrue(areajson.same(original, back))
				self.assertEqual(areajson.dumps(original), areajson.dumps(back))

	def test_the_command_lines_round_trip_with_an_embedded_tileset(self):
		source = AREAS / "reed_path.json"
		tmj = self.tmp / "out" / "reed_path.tmj"
		self.assertEqual(0, run(cli.area_to_tiled, source, "-o", tmj, "--embed")[0])
		self.assertIn("image", json.loads(tmj.read_text(encoding="utf-8"))["tilesets"][0])
		code, out, _ = run(cli.tiled_to_area, tmj)
		self.assertEqual(0, code)
		self.assertTrue(areajson.same(json.loads(source.read_text(encoding="utf-8")), json.loads(out)))


class SourcesTest(TempCase):
	def test_checked_in_areas_match_their_tiled_sources(self):
		maps = sorted(MAPS.glob("*.tmj"))
		self.assertEqual(["kiln_lower", "kiln_upper", "saltgrass"], [m.stem for m in maps])
		for path in maps:
			with self.subTest(map=path.name):
				expected = areajson.dumps(tiled.load_tmj(path))
				self.assertEqual(expected, (AREAS / f"{path.stem}.json").read_text(encoding="utf-8"), "run make -C game maps")

	def test_the_tileset_has_every_kind_and_its_image(self):
		ts = json.loads((MAPS / "cells.tsj").read_text(encoding="utf-8"))
		self.assertEqual(tiled.tileset(), ts)
		self.assertTrue((MAPS / ts["image"]).read_bytes().startswith(b"\x89PNG"))

	def test_check_flag_reports_a_stale_area(self):
		shutil.copy(MAPS / "kiln_lower.tmj", self.tmp / "kiln_lower.tmj")
		shutil.copy(MAPS / "cells.tsj", self.tmp / "cells.tsj")
		area = self.tmp / "kiln_lower.json"
		shutil.copy(AREAS / "kiln_lower.json", area)
		self.assertEqual((0, "", ""), run(cli.tiled_to_area, self.tmp / "kiln_lower.tmj", "--check", area))
		area.write_text(area.read_text(encoding="utf-8").replace("\"step_ms\": 250", "\"step_ms\": 300"), encoding="utf-8")
		code, _, err = run(cli.tiled_to_area, self.tmp / "kiln_lower.tmj", "--check", area)
		self.assertEqual(1, code)
		self.assertIn("is stale", err)
		self.assertIn("make -C game maps", err)

	def test_errors_name_the_map_and_write_nothing(self):
		bad = self.tmp / "bad.tmj"
		bad.write_text(json.dumps({"type": "map", "orientation": "isometric", "width": 1, "height": 1, "tilewidth": 16, "tileheight": 16}), encoding="utf-8")
		out = self.tmp / "bad.json"
		code, _, err = run(cli.tiled_to_area, bad, "-o", out)
		self.assertEqual(1, code)
		self.assertIn(f"{bad}: the map is isometric", err)
		self.assertFalse(out.exists())
