import base64
import copy
import json
import struct

from support import SAMPLE_AREA, TempCase, objects, sample_tmj, terrain

from mapkit import areajson, tiled


class AreaToTiledTest(TempCase):
	def test_map_properties_terrain_and_object_classes(self):
		tmj = sample_tmj()
		props = {p["name"]: (p["type"], p["value"]) for p in tmj["properties"]}
		self.assertEqual({"id": ("string", "sample"), "kind": ("string", "dungeon"), "encounter_mode": ("string", "random"), "step_ms": ("int", 200), "music": ("string", "dungeon_sample")}, props)
		self.assertEqual((6, 4, 16, 16), (tmj["width"], tmj["height"], tmj["tilewidth"], tmj["tileheight"]))
		self.assertEqual([{"firstgid": 1, "source": "cells.tsj"}], tmj["tilesets"])
		# Tile ids 0, 1, 2 are floor, wall, water; gids start at 1.
		self.assertEqual([2, 2, 2, 2, 2, 2, 2, 1, 1, 3, 1, 2], terrain(tmj)["data"][:12])
		classes = [o["type"] for o in objects(tmj)]
		self.assertEqual(["marker", "marker", "exit", "hook", "hook", "hook", "shop", "shop", "rest", "rest", "chest", "zone"], classes)

	def test_points_sit_on_cell_centers_and_zones_cover_their_cells(self):
		objs = objects(sample_tmj())
		door = objs[0]
		self.assertEqual(("door", True, 24, 24), (door["name"], door["point"], door["x"], door["y"]))
		zone = objs[-1]
		self.assertEqual((16, 16, 64, 32), (zone["x"], zone["y"], zone["width"], zone["height"]))
		self.assertNotIn("point", zone)

	def test_cell_free_objects_stand_left_of_the_map_with_their_flag(self):
		objs = {o["name"]: o for o in objects(sample_tmj())}
		self.assertLess(objs["intro"]["x"], 0)
		self.assertLess(objs["mail_order"]["x"], 0)
		mail = {p["name"]: p for p in objs["mail_order"]["properties"]}
		self.assertEqual({"name": "script_only", "type": "bool", "value": True}, mail["script_only"])
		self.assertEqual("string", mail["stock"]["type"])
		self.assertEqual(["tonic", "clear_gel"], json.loads(mail["stock"]["value"]))
		self.assertNotIn("script_only", {p["name"] for p in objs["stall"]["properties"]})

	def test_embedded_tileset_carries_the_kinds(self):
		tmj = tiled.area_to_tmj(copy.deepcopy(SAMPLE_AREA), tiled.tileset("cells.png"))
		ts = tmj["tilesets"][0]
		self.assertEqual(1, ts["firstgid"])
		self.assertEqual("cells.png", ts["image"])
		self.assertEqual(["floor", "wall", "water"], [t["properties"][0]["value"] for t in ts["tiles"]])

	def test_refuses_areas_the_format_does_not_allow(self):
		for change, fragment in (
			({"lighting": "dim"}, "unknown field 'lighting'"),
			({"map": ["#.", "#"]}, "same width"),
			({"map": ["#?"]}, "'.' for floor"),
			({"exits": [{"at": [0, 0], "to": "x", "marker": "y", "door": "oak"}]}, "exits[0]: unknown field 'door'"),
			({"chests": [{"id": "c", "at": "here", "flag": "f"}]}, "chests[0]: 'at' must be [x, y]"),
		):
			area = copy.deepcopy(SAMPLE_AREA)
			area.update(change)
			with self.subTest(fragment=fragment), self.assertRaises(areajson.AreaError) as caught:
				tiled.area_to_tmj(area, "cells.tsj")
			self.assertIn(fragment, str(caught.exception))


class TiledToAreaTest(TempCase):
	def test_sample_converts_back_exactly(self):
		self.assertEqual(areajson.normalize(SAMPLE_AREA), areajson.normalize(self.convert(sample_tmj())))

	def test_embedded_and_external_tilesets_read_the_same(self):
		external = self.convert(sample_tmj())
		embedded = self.convert(tiled.area_to_tmj(copy.deepcopy(SAMPLE_AREA), tiled.tileset()))
		self.assertEqual(external, embedded)

	def test_uncompressed_base64_data(self):
		tmj = sample_tmj()
		layer = terrain(tmj)
		layer["data"] = base64.b64encode(struct.pack(f"<{len(layer['data'])}I", *layer["data"])).decode()
		layer["encoding"] = "base64"
		self.assertEqual(SAMPLE_AREA["map"], self.convert(tmj)["map"])

	def test_flip_bits_and_a_second_tileset(self):
		tmj = sample_tmj()
		# A decoration tileset after the cell tileset: its tiles carry their own kind property.
		tmj["tilesets"].append({"firstgid": 10, "name": "decor", "tiles": [{"id": 0, "properties": [{"name": "kind", "type": "string", "value": "water"}]}]})
		data = terrain(tmj)["data"]
		data[7] = 1 | 0x80000000
		data[8] = 10
		self.assertEqual("#.~~.#", self.convert(tmj)["map"][1])

	def test_rectangles_count_by_their_center_and_names_come_from_objects(self):
		tmj = sample_tmj()
		door = objects(tmj)[0]
		del door["point"]
		door.update({"x": 16, "y": 16, "width": 16, "height": 16})
		self.assertEqual([1, 1], self.convert(tmj)["markers"]["door"])

	def test_zone_rectangles_snap_outward_to_whole_cells(self):
		tmj = sample_tmj()
		zone = objects(tmj)[-1]
		zone.update({"x": 18, "y": 20, "width": 40, "height": 20})
		area = self.convert(tmj)
		self.assertEqual(([1, 1], [3, 2]), (area["zones"][0]["from"], area["zones"][0]["to"]))

	def test_other_layers_are_ignored(self):
		tmj = sample_tmj()
		tmj["layers"].insert(0, {"id": 9, "name": "decoration", "type": "imagelayer", "image": "fog.png"})
		self.assertTrue(areajson.same(SAMPLE_AREA, self.convert(tmj)))

	def test_explicit_interact_trigger_is_kept(self):
		tmj = sample_tmj()
		sign = next(o for o in objects(tmj) if o["name"] == "sign")
		sign["properties"].insert(0, {"name": "trigger", "type": "string", "value": "interact"})
		hook = next(h for h in self.convert(tmj)["hooks"] if h["id"] == "sign")
		self.assertEqual({"id": "sign", "at": [1, 2], "trigger": "interact", "event": "sign_event"}, hook)


class AreaTextTest(TempCase):
	def test_canonical_text_orders_fields_and_drops_empty_lists(self):
		area = {"markers": {"b": [1, 0], "a": [0, 0]}, "map": ["..", ".."], "id": "x", "hooks": [], "exits": [{"marker": "m", "to": "y", "at": [0, 1]}]}
		self.assertEqual(
			'{\n\t"id": "x",\n\t"map": [\n\t\t"..",\n\t\t".."\n\t],\n\t"markers": {\n\t\t"b": [1, 0],\n\t\t"a": [0, 0]\n\t},\n'
			'\t"exits": [\n\t\t{"at": [0, 1], "to": "y", "marker": "m"}\n\t]\n}\n',
			areajson.dumps(area),
		)

	def test_same_ignores_key_order_but_not_list_order(self):
		a = {"id": "x", "map": ["."], "markers": {"p": [0, 0], "q": [0, 0]}, "exits": [{"at": [0, 0], "to": "a", "marker": "p"}, {"at": [0, 0], "to": "b", "marker": "q"}]}
		b = json.loads(json.dumps(a))
		b["markers"] = {"q": [0, 0], "p": [0, 0]}
		b["exits"][0] = {"marker": "p", "to": "a", "at": [0, 0]}
		self.assertTrue(areajson.same(a, b))
		b["exits"].reverse()
		self.assertFalse(areajson.same(a, b))
		self.assertTrue(areajson.same(a, dict(a, zones=[])))
