import base64
import zlib

from support import TempCase, objects, sample_tmj, terrain


def obj(tmj, name):
	return next(o for o in objects(tmj) if o["name"] == name)


def prop(o, name):
	return next(p for p in o["properties"] if p["name"] == name)


class MapRejectionTest(TempCase):
	def test_not_a_map(self):
		tmj = sample_tmj()
		tmj["type"] = "tileset"
		self.rejects(tmj, "this is a Tiled 'tileset' file, not a map")

	def test_orientation(self):
		for orientation in ("isometric", "staggered", "hexagonal"):
			tmj = sample_tmj()
			tmj["orientation"] = orientation
			with self.subTest(orientation=orientation):
				self.rejects(tmj, f"the map is {orientation}; only orthogonal maps are read")

	def test_infinite(self):
		tmj = sample_tmj()
		tmj["infinite"] = True
		self.rejects(tmj, "the map is infinite; uncheck Infinite")

	def test_size(self):
		tmj = sample_tmj()
		tmj["tilewidth"] = 0
		self.rejects(tmj, "the map's tilewidth must be a positive whole number")

	def test_chunks(self):
		tmj = sample_tmj()
		layer = terrain(tmj)
		layer["chunks"] = [{"x": 0, "y": 0, "width": 6, "height": 4, "data": layer.pop("data")}]
		self.rejects(tmj, "stored in chunks")

	def test_compression(self):
		for compression in ("zlib", "gzip", "zstd"):
			tmj = sample_tmj()
			layer = terrain(tmj)
			layer.update({"encoding": "base64", "compression": compression, "data": base64.b64encode(zlib.compress(b"\0" * 96)).decode()})
			with self.subTest(compression=compression):
				self.rejects(tmj, f"uses {compression} compression; set Tile Layer Format to CSV or Base64 (uncompressed)")

	def test_bad_base64_and_unknown_encoding(self):
		tmj = sample_tmj()
		terrain(tmj).update({"encoding": "base64", "data": "not base64!"})
		self.rejects(tmj, "the base64 data does not decode")
		tmj = sample_tmj()
		terrain(tmj)["encoding"] = "xml"
		self.rejects(tmj, "has encoding 'xml'")

	def test_csv_must_be_numbers_of_the_map_size(self):
		tmj = sample_tmj()
		terrain(tmj)["data"] = "1,2,3"
		self.rejects(tmj, "CSV data must be a list of tile numbers")
		tmj = sample_tmj()
		terrain(tmj)["data"].pop()
		self.rejects(tmj, "layer 'terrain' has 23 tiles, but the map is 6 by 4 (24 tiles)")

	def test_layers(self):
		tmj = sample_tmj()
		tmj["layers"] = [layer for layer in tmj["layers"] if layer["name"] != "terrain"]
		self.rejects(tmj, "no tile layer named 'terrain'")
		tmj = sample_tmj()
		terrain(tmj)["type"] = "objectgroup"
		self.rejects(tmj, "layer 'terrain' is a objectgroup; it must be a tile layer")
		tmj = sample_tmj()
		tmj["layers"].append(dict(terrain(tmj)))
		self.rejects(tmj, "more than one layer named 'terrain' or 'objects'")
		tmj = sample_tmj()
		next(layer for layer in tmj["layers"] if layer["name"] == "objects")["type"] = "tilelayer"
		self.rejects(tmj, "layer 'objects' is a tilelayer; it must be an object layer")
		tmj = sample_tmj()
		terrain(tmj)["width"] = 5
		self.rejects(tmj, "layer 'terrain' is not the size of the map")

	def test_tiles_without_a_kind(self):
		tmj = sample_tmj()
		terrain(tmj)["data"][7] = 0
		terrain(tmj)["data"][8] = 0
		self.rejects(tmj, "cell [1, 1] and 1 other cell has no tile")
		tmj = sample_tmj()
		terrain(tmj)["data"][7] = 40
		self.rejects(tmj, "cell [1, 1] has tile 39 of tileset 'cells.tsj', which has no 'kind' property")
		tmj = sample_tmj()
		tmj["tilesets"][0]["firstgid"] = 5
		self.rejects(tmj, "has tile 1, which no tileset holds")

	def test_unknown_kind(self):
		tmj = sample_tmj()
		tmj["tilesets"] = [{"firstgid": 1, "name": "odd", "tiles": [{"id": i, "properties": [{"name": "kind", "type": "string", "value": "lava"}]} for i in range(3)]}]
		self.rejects(tmj, "whose kind 'lava' is not one of floor, wall, water")

	def test_kind_must_be_a_string_property(self):
		tmj = sample_tmj()
		tmj["tilesets"] = [{"firstgid": 1, "name": "odd", "tiles": [{"id": 0, "properties": [{"name": "kind", "type": "int", "value": 1}]}]}]
		self.rejects(tmj, "tileset 0 tile 0: property 'kind' must be a string property")

	def test_external_tilesets(self):
		tmj = sample_tmj("cells.tsx")
		self.rejects(tmj, "tileset 'cells.tsx' is not a JSON tileset; save it as .tsj")
		tmj = sample_tmj("missing.tsj")
		self.rejects(tmj, "tileset 'missing.tsj': cannot read")
		(self.tmp / "broken.tsj").write_text("{", encoding="utf-8")
		self.rejects(sample_tmj("broken.tsj"), "tileset 'broken.tsj': not valid JSON")

	def test_old_property_format(self):
		tmj = sample_tmj()
		tmj["properties"] = {"id": "sample"}
		self.rejects(tmj, "properties are in the old Tiled format")

	def test_map_properties(self):
		tmj = sample_tmj()
		tmj["properties"] = [p for p in tmj["properties"] if p["name"] != "id"]
		self.rejects(tmj, "the map needs a string property 'id'")
		tmj = sample_tmj()
		tmj["properties"].append({"name": "weather", "type": "string", "value": "rain"})
		self.rejects(tmj, "the map: unknown property 'weather' (allowed: encounter_mode, id, kind, music, step_ms)")
		tmj = sample_tmj()
		next(p for p in tmj["properties"] if p["name"] == "step_ms").update({"type": "string", "value": "250"})
		self.rejects(tmj, "the map: property 'step_ms' must be an int property")


class ObjectRejectionTest(TempCase):
	def test_unknown_class(self):
		tmj = sample_tmj()
		obj(tmj, "door")["type"] = "npc"
		self.rejects(tmj, "object 1 ('door') has class 'npc'; use one of marker, exit, hook, shop, rest, chest, zone")

	def test_class_field_of_newer_tiled_is_read(self):
		tmj = sample_tmj()
		door = obj(tmj, "door")
		door["class"] = door.pop("type")
		self.assertIn("door", self.convert(tmj)["markers"])

	def test_shapes(self):
		for shape, value in (("ellipse", True), ("polygon", [{"x": 0, "y": 0}]), ("polyline", [{"x": 0, "y": 0}]), ("text", {"text": "hi"})):
			tmj = sample_tmj()
			obj(tmj, "door")[shape] = value
			with self.subTest(shape=shape):
				self.rejects(tmj, f"a marker is a {shape}; draw it as a point or a rectangle")

	def test_tile_objects_templates_and_rotation(self):
		tmj = sample_tmj()
		obj(tmj, "door")["gid"] = 1
		self.rejects(tmj, "a marker is a tile object")
		tmj = sample_tmj()
		obj(tmj, "door")["template"] = "door.tx"
		self.rejects(tmj, "uses a template; detach it")
		tmj = sample_tmj()
		obj(tmj, "door")["rotation"] = 90
		self.rejects(tmj, "a marker is rotated")

	def test_names_are_required_and_markers_unique(self):
		tmj = sample_tmj()
		obj(tmj, "box")["name"] = ""
		self.rejects(tmj, "a chest needs a name: it is the id")
		tmj = sample_tmj()
		obj(tmj, "pool_side")["name"] = "door"
		self.rejects(tmj, "marker 'door' is defined twice")

	def test_required_properties(self):
		tmj = sample_tmj()
		exit_obj = next(o for o in objects(tmj) if o["type"] == "exit")
		exit_obj["properties"] = [p for p in exit_obj["properties"] if p["name"] != "marker"]
		self.rejects(tmj, "an exit needs the property 'marker'")
		tmj = sample_tmj()
		zone = obj(tmj, "all")
		zone["properties"] = [p for p in zone["properties"] if p["name"] != "table"]
		self.rejects(tmj, "a zone needs the property 'table'")

	def test_unknown_and_mistyped_properties(self):
		tmj = sample_tmj()
		obj(tmj, "box")["properties"].append({"name": "gold", "type": "int", "value": 5})
		self.rejects(tmj, "object 11 ('box'), a chest: unknown property 'gold' (allowed: equipment, flag, items, money)")
		tmj = sample_tmj()
		prop(obj(tmj, "box"), "money").update({"type": "string", "value": "9"})
		self.rejects(tmj, "property 'money' must be an int property")
		tmj = sample_tmj()
		prop(obj(tmj, "mail_order"), "script_only").update({"type": "string", "value": "yes"})
		self.rejects(tmj, "property 'script_only' must be a bool property")
		tmj = sample_tmj()
		prop(obj(tmj, "box"), "flag")["value"] = ""
		self.rejects(tmj, "property 'flag' is empty")
		tmj = sample_tmj()
		prop(obj(tmj, "box"), "flag").update({"type": "file", "value": "chest.box"})
		self.rejects(tmj, "property 'flag' must be a string property")

	def test_json_text_properties(self):
		tmj = sample_tmj()
		prop(obj(tmj, "stall"), "stock")["value"] = '["tonic",'
		self.rejects(tmj, "a shop: property 'stock' is not valid JSON text")
		tmj = sample_tmj()
		prop(obj(tmj, "stall"), "stock")["value"] = '{"tonic": 1}'
		self.rejects(tmj, "property 'stock' must hold a JSON list")
		tmj = sample_tmj()
		prop(obj(tmj, "box"), "items")["value"] = '["tonic"]'
		self.rejects(tmj, "property 'items' must hold a JSON object")

	def test_zone_must_be_a_rectangle(self):
		tmj = sample_tmj()
		zone = obj(tmj, "all")
		zone.update({"point": True, "width": 0, "height": 0})
		self.rejects(tmj, "a zone must be a rectangle covering its cells")

	def test_every_problem_is_reported_at_once(self):
		tmj = sample_tmj()
		obj(tmj, "door")["type"] = "npc"
		obj(tmj, "box")["rotation"] = 45
		terrain(tmj)["data"][7] = 0
		text = self.rejects(tmj, "class 'npc'", "a chest is rotated", "cell [1, 1] has no tile")
		self.assertEqual(3, len(text.splitlines()))
