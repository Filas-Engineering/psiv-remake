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
		self.assertEqual(["floor", "wall", "water", "soft", "barrier", "counter"], [t["properties"][0]["value"] for t in ts["tiles"]])

	def test_refuses_areas_the_format_does_not_allow(self):
		for change, fragment in (
			({"lighting": "dim"}, "unknown field 'lighting'"),
			({"map": ["#.", "#"]}, "same width"),
			({"map": ["#?"]}, "'.' for floor"),
			({"map": ["#?"]}, "'%' for barrier"),
			({"shops": [{"id": "s", "stock": [{"id": "tonic", "price": 3}]}]}, "shops[0].stock[0]: unknown field 'price'"),
			({"helms": [{"id": "wheel", "at": [1]}]}, "helms[0]: 'at' must be [x, y]"),
			({"gangway": [[0, 0], "plank"]}, "gangway[1] must be [x, y]"),
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


VESSEL_AREA = {
	"id": "barge",
	"kind": "vessel",
	"map": ["#:%.", "...."],
	"markers": {"deck": [3, 1]},
	"exits": [{"at": [0, 1], "to": "below", "marker": "hold", "fall": True}],
	"shops": [{"id": "galley", "at": [3, 0], "kind": "tool", "stock": ["tonic", {"block_flag": "b.flag", "id": "flask", "require_flag": "a.flag"}], "require_flag": "c.flag"}],
	"ports": [{"id": "berth", "at": [1, 1], "marker": "deck", "flag": "d.flag", "ship": "barge"}],
	"helms": [{"id": "wheel", "at": [2, 1]}],
	"gangway": [[0, 1], [1, 0]],
	"changes": [{"flag": "e.flag", "tile": ".", "cells": [[1, 0], [2, 0]]}, {"flag": "f.flag", "tile": "%", "cells": [[3, 1]]}],
}


class VesselAndChangesTest(TempCase):
	def test_soft_ground_and_barriers_use_the_appended_tiles(self):
		tmj = tiled.area_to_tmj(copy.deepcopy(VESSEL_AREA), "cells.tsj")
		self.assertEqual([2, 4, 5, 1], terrain(tmj)["data"][:4])
		self.assertEqual("#:%.", self.convert(tmj)["map"][0])

	def test_helms_gangway_and_changes_are_objects(self):
		objs = objects(tiled.area_to_tmj(copy.deepcopy(VESSEL_AREA), "cells.tsj"))
		self.assertEqual(["marker", "exit", "shop", "port", "helm", "gangway", "gangway", "change", "change"], [o["type"] for o in objs])
		helm, plank = objs[4], objs[6]
		self.assertEqual(("wheel", 40, 24), (helm["name"], helm["x"], helm["y"]))
		self.assertEqual((24, 8), (plank["x"], plank["y"]))
		change = objs[7]
		self.assertLess(change["x"], 0)
		props = {p["name"]: (p["type"], p["value"]) for p in change["properties"]}
		self.assertEqual({"flag": ("string", "e.flag"), "tile": ("string", "."), "cells": ("string", "[[1, 0], [2, 0]]")}, props)

	def test_every_new_field_survives_a_round_trip(self):
		back = self.convert(tiled.area_to_tmj(copy.deepcopy(VESSEL_AREA), "cells.tsj"))
		self.assertEqual(areajson.normalize(VESSEL_AREA), areajson.normalize(back))
		self.assertEqual(areajson.dumps(VESSEL_AREA), areajson.dumps(back))
		self.assertEqual({"id": "flask", "require_flag": "a.flag", "block_flag": "b.flag"}, back["shops"][0]["stock"][1])
		self.assertEqual(list(areajson.STOCK_FIELDS), list(areajson.normalize(VESSEL_AREA)["shops"][0]["stock"][1]))

	def test_gated_exits_and_ports_hooks_on_tiles_once_and_solid_and_conveyors_survive_a_round_trip(self):
		area = copy.deepcopy(VESSEL_AREA)
		area["exits"][0].update({"require_flag": "a.flag", "block_flag": "b.flag"})
		area["ports"][0]["block_flag"] = "b.flag"
		area["hooks"] = [
			{"id": "belt_bell", "trigger": "step", "event": "e", "tile": "."},
			{"id": "once", "trigger": "flag", "event": "e", "require_flag": "a.flag", "once": True},
			{"id": "sweeper", "at": [1, 0], "event": "e", "block_flag": "b.flag", "solid": "while_active"},
		]
		area["conveyors"] = [{"cells": [[1, 1], [2, 1]], "direction": "east", "step_ms": 200, "block_flag": "b.flag"}]
		back = self.convert(tiled.area_to_tmj(copy.deepcopy(area), "cells.tsj"))
		self.assertEqual(areajson.dumps(area), areajson.dumps(back))
		self.assertNotIn("at", back["hooks"][0])
		self.assertIn('\t"conveyors": [\n\t\t{"cells": [[1, 1], [2, 1]], "direction": "east", "step_ms": 200, "block_flag": "b.flag"}\n\t]', areajson.dumps(back))

	def test_a_dungeons_way_out_survives_a_round_trip_in_canonical_order(self):
		area = copy.deepcopy(VESSEL_AREA)
		area["kind"] = "dungeon"
		area["dungeon_exit"] = {"facing": "north", "marker": "gate", "area": "saltgrass"}
		back = self.convert(tiled.area_to_tmj(copy.deepcopy(area), "cells.tsj"))
		self.assertEqual(areajson.dumps(area), areajson.dumps(back))
		self.assertEqual(["area", "marker", "facing"], list(back["dungeon_exit"]))
		self.assertIn('\t"dungeon_exit": {"area": "saltgrass", "marker": "gate", "facing": "north"},\n\t"map": [', areajson.dumps(back))
		area["dungeon_exit"] = {"area": "saltgrass", "at": [1, 1], "door": "x"}
		with self.assertRaises(areajson.AreaError) as caught:
			areajson.normalize(area)
		self.assertIn("dungeon_exit: unknown field 'door'", caught.exception.problems)

	def test_counters_by_character_and_by_list_arrive_notice_and_a_fractional_step_survive_a_round_trip(self):
		area = copy.deepcopy(VESSEL_AREA)
		row = area["map"][0]
		area["map"][0] = "=" + row[1:]
		area["counters"] = [[2, 0]]
		area["arrive_notice"] = False
		area["step_ms"] = 133.5
		tmj = tiled.area_to_tmj(copy.deepcopy(area), "cells.tsj")
		# The counter is tile 5 of the cell tileset.
		self.assertEqual(6, terrain(tmj)["data"][0])
		back = self.convert(tmj)
		self.assertEqual(areajson.dumps(area), areajson.dumps(back))
		self.assertEqual("=", back["map"][0][0])
		self.assertEqual([[2, 0]], back["counters"])
		self.assertIs(False, back["arrive_notice"])
		self.assertEqual(133.5, back["step_ms"])
		self.assertEqual("counter", tiled.tileset()["tiles"][5]["properties"][0]["value"])

	def test_moving_platforms_and_field_status_survive_a_round_trip(self):
		area = copy.deepcopy(VESSEL_AREA)
		area["field_status"] = True
		area["name"] = [{"from": "saltgrass", "key": "area.alt"}, {"key": "area.x"}]
		area["platforms"] = [{"id": "raft", "from": [1, 1], "to": [3, 1], "step_ms": 200, "pause_ms": 400, "mode": "triggered"}]
		back = self.convert(tiled.area_to_tmj(copy.deepcopy(area), "cells.tsj"))
		self.assertEqual(areajson.dumps(area), areajson.dumps(back))
		self.assertIs(True, back["field_status"])
		self.assertEqual("raft", back["platforms"][0]["id"])

	def test_a_hooks_actor_cell_survives_a_round_trip(self):
		area = copy.deepcopy(VESSEL_AREA)
		area["hooks"] = [{"id": "keeper", "at": [1, 0], "event": "e", "actor_at": [2, 1]}]
		back = self.convert(tiled.area_to_tmj(copy.deepcopy(area), "cells.tsj"))
		self.assertEqual(areajson.dumps(area), areajson.dumps(back))
		self.assertEqual([2, 1], back["hooks"][0]["actor_at"])

	def test_a_closed_exits_tile_and_a_hooks_counter_and_cells_survive_a_round_trip(self):
		area = copy.deepcopy(VESSEL_AREA)
		area["exits"] = [{"at": [1, 1], "to": "elsewhere", "marker": "door", "require_flag": "door.open", "closed_tile": "normal", "sound": "door_creak"}]
		area["hooks"] = [{"id": "table", "at": [1, 0], "event": "e", "across": True, "cells": [[2, 0], [3, 0]]}]
		back = self.convert(tiled.area_to_tmj(copy.deepcopy(area), "cells.tsj"))
		self.assertEqual(areajson.dumps(area), areajson.dumps(back))
		self.assertIn('{"id": "table", "at": [1, 0], "event": "e", "across": true, "cells": [[2, 0], [3, 0]]}', areajson.dumps(back))
		self.assertEqual("normal", back["exits"][0]["closed_tile"])
		self.assertEqual("door_creak", back["exits"][0]["sound"])

	def test_a_touch_hook_a_keeper_and_a_voice_survive_a_round_trip(self):
		area = copy.deepcopy(VESSEL_AREA)
		area["hooks"] = [{"id": "goat", "trigger": "touch", "event": "e", "actor": "goat"}]
		area["shops"] = [{"id": "stall", "at": [1, 0], "stock": ["tonic"], "keeper": "trader", "voice": "voice.trader"}]
		area["rests"] = [{"id": "cot", "cost": 5, "keeper": "warden", "per_member": True, "voice": "voice.warden"}]
		back = self.convert(tiled.area_to_tmj(copy.deepcopy(area), "cells.tsj"))
		self.assertEqual(areajson.dumps(area), areajson.dumps(back))
		self.assertNotIn("at", back["hooks"][0])
		self.assertEqual("goat", back["hooks"][0]["actor"])
		self.assertEqual(["trader", "warden"], [back["shops"][0]["keeper"], back["rests"][0]["keeper"]])
		self.assertEqual(["voice.trader", "voice.warden", True], [back["shops"][0]["voice"], back["rests"][0]["voice"], back["rests"][0]["per_member"]])

	def test_refuses_a_change_tile_that_is_not_a_map_character_and_a_change_without_cells(self):
		tmj = tiled.area_to_tmj(copy.deepcopy(VESSEL_AREA), "cells.tsj")
		changes = [o for o in objects(tmj) if o["type"] == "change"]
		next(p for p in changes[0]["properties"] if p["name"] == "tile")["value"] = "?"
		changes[1]["properties"] = [p for p in changes[1]["properties"] if p["name"] != "cells"]
		self.rejects(tmj, "a change: property 'tile' must be one map character: ., #, ~, :, %", "a change needs the property 'cells'")

	def test_a_helm_needs_a_name(self):
		tmj = tiled.area_to_tmj(copy.deepcopy(VESSEL_AREA), "cells.tsj")
		next(o for o in objects(tmj) if o["type"] == "helm")["name"] = ""
		self.rejects(tmj, "a helm needs a name: it is the id")


class AreaTextTest(TempCase):
	def test_canonical_text_orders_fields_and_drops_empty_lists(self):
		area = {"markers": {"b": [1, 0], "a": [0, 0]}, "map": ["..", ".."], "id": "x", "hooks": [], "exits": [{"marker": "m", "to": "y", "at": [0, 1]}]}
		self.assertEqual(
			'{\n\t"id": "x",\n\t"map": [\n\t\t"..",\n\t\t".."\n\t],\n\t"markers": {\n\t\t"b": [1, 0],\n\t\t"a": [0, 0]\n\t},\n'
			'\t"exits": [\n\t\t{"at": [0, 1], "to": "y", "marker": "m"}\n\t]\n}\n',
			areajson.dumps(area),
		)

	def test_canonical_text_puts_each_gangway_cell_and_change_on_its_own_line(self):
		area = {"id": "x", "kind": "vessel", "map": [".."], "markers": {}, "gangway": [[0, 0], [1, 0]], "changes": [{"cells": [[1, 0]], "tile": "#", "flag": "f"}]}
		self.assertEqual(
			'{\n\t"id": "x",\n\t"kind": "vessel",\n\t"map": [\n\t\t".."\n\t],\n\t"markers": {},\n'
			'\t"gangway": [\n\t\t[0, 0],\n\t\t[1, 0]\n\t],\n\t"changes": [\n\t\t{"flag": "f", "tile": "#", "cells": [[1, 0]]}\n\t]\n}\n',
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
