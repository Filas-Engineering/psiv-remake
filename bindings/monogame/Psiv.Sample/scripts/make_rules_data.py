#!/usr/bin/env python3
"""Copies the interaction data copy (game/build/make_actor_data ... interact: Wren Hollow, Wren Loft, Wren Cellar and the
interaction rules on) for scripts/rules_*.txt and turns on what the sample shows from it: `presentation.animations`
entries as objects (`tally_nod` with its own seconds), `presentation.music.vehicles` (the skimmer's battles, the marsh's own track taken off) and
`presentation.music.areas` (Wren Cellar's track by its door's flag), `arrive_notice` walked, `step_timing` exact with a
fractional step time in Wren Cellar, and a counter drawn as '=' in front of the cellar's clerk.

With --assets ASSETS OUT_ASSETS it also copies an asset directory (build/test_assets) and gives it an `objects` entry over
both cells of the cellar's long table (an interact hook with `cells`) and a title layout that lists Erase.

With --keepers KEEPERS_OUT KEEPERS_ASSETS it also copies both for scripts/rules_keepers.txt: the party starting as the
fighter and the mystic, the fighter's party.json `profile` rows, Tidewater's inn per member with a voice and its stall with
a voice (their lines in both text tables), and inn and shop layouts that draw the keeper's line and the inn's price per
member.

Usage: make_rules_data.py INTERACT_DATA_ROOT OUT [--assets ASSETS OUT_ASSETS] [--keepers KEEPERS_OUT KEEPERS_ASSETS]

INTERACT_DATA_ROOT and OUT hold game/data and core/data side by side, where game.json's core_data looks.
"""

import json
import shutil
import sys
from pathlib import Path


def data(source, out):
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(source, out)
	game = out / "game" / "data"
	game_file = game / "game.json"
	config = json.loads(game_file.read_text())
	config["arrive_notice"] = "walked"
	config["step_timing"] = "exact"
	presentation = config["presentation"]
	presentation["animations"] = [{"id": a, "seconds": 0.7} if a == "tally_nod" else a for a in presentation["animations"]]
	music = presentation.setdefault("music", {})
	music["vehicles"] = {"reed_skimmer": "battle_brass_warden"}
	# The skimmer's table is the marsh, whose own track would win over the vehicle's.
	music.get("encounters", {}).pop("marsh", None)
	music["areas"] = {"wren_cellar": [{"flag": "cellar.door_open", "track": "title_harbor_dawn"}, "field_marsh_reeds"]}
	game_file.write_text(json.dumps(config, indent="\t", ensure_ascii=False) + "\n")
	area_file = game / "areas" / "wren_cellar.json"
	area = json.loads(area_file.read_text())
	area["step_ms"] = 133.5
	row = area["map"][2]
	area["map"][2] = row[:2] + "=" + row[3:]
	area_file.write_text(json.dumps(area, indent="\t", ensure_ascii=False) + "\n")


def assets(source, out):
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(source, out)
	manifest_file = out / "assets.json"
	manifest = json.loads(manifest_file.read_text())
	manifest.setdefault("objects", []).append(
		{"area": "wren_cellar", "point": "cellar_table", "image": "objects/board.png", "frame": [32, 16], "frames": [0], "size": [2, 1], "anchor": [0, 0]})
	manifest_file.write_text(json.dumps(manifest, indent="\t", ensure_ascii=False) + "\n")
	# The built-in title layout with its `erase` param set.
	title_file = out / "ui" / "title.json"
	title = json.loads((Path(__file__).resolve().parent.parent / "ui" / "title.json").read_text())
	title.setdefault("params", {})["erase"] = 1
	title_file.write_text(json.dumps(title, indent="\t", ensure_ascii=False) + "\n")


def edit_json(path, change):
	value = json.loads(path.read_text())
	change(value)
	path.write_text(json.dumps(value, indent="\t", ensure_ascii=False) + "\n")


def keepers(source, out, source_assets, out_assets):
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(source, out)
	game = out / "game" / "data"
	edit_json(game / "game.json", lambda c: c["start"].update(lineup=["fighter", "mystic"]))

	def tidewater(area):
		area["rests"][0].update(per_member=True, voice="voice.lodge")
		area["shops"][0]["voice"] = "voice.dockhand"
	edit_json(game / "areas" / "tidewater.json", tidewater)

	def fighter(party):
		next(c for c in party["characters"] if c["id"] == "fighter")["profile"] = [
			{"key": "profile.age", "value": 19}, {"key": "profile.home", "value": "profile.fighter.home"}]
	edit_json(out / "core" / "data" / "party.json", fighter)
	lines = {
		"en": {"profile.age": "Age", "profile.home": "Home", "profile.fighter.home": "Tidewater", "voice.lodge.confirm": "A bed each, {0} in all?",
			"voice.dockhand.welcome": "Fresh off the boats!", "voice.dockhand.thanks": "Come again!"},
		"ja": {"profile.age": "年齢", "profile.home": "出身", "profile.fighter.home": "潮見の船着き場", "voice.lodge.confirm": "ひとり一台、全部で{0}だよ？",
			"voice.dockhand.welcome": "船から下ろしたてだよ！", "voice.dockhand.thanks": "またどうぞ！"},
	}
	for language, added in lines.items():
		edit_json(game / "text" / f"{language}.json", lambda t: t["text"].update(added))

	if out_assets.exists():
		shutil.rmtree(out_assets)
	shutil.copytree(source_assets, out_assets)
	ui = Path(__file__).resolve().parent.parent / "ui"
	inn = json.loads((ui / "inn.json").read_text())
	inn["windows"][0]["elements"] += [
		{"type": "text", "value": "voice_line", "x": 146, "y": "t(52)", "style": "small"},
		{"type": "text", "visible": "per_member", "value": "cost_each + ' x ' + (cost / cost_each)", "w": "cw - 24", "align": "right", "y": "t(30)", "style": "small_dim"}]
	(out_assets / "ui" / "inn.json").write_text(json.dumps(inn, indent="\t", ensure_ascii=False) + "\n")
	shop = json.loads((ui / "shop.json").read_text())
	shop["windows"][1]["elements"][0]["elements"].append({"type": "text", "value": "voice_line", "x": 24, "y": 58, "style": "small_dim"})
	(out_assets / "ui" / "shop.json").write_text(json.dumps(shop, indent="\t", ensure_ascii=False) + "\n")


def main():
	args = sys.argv[1:]
	data(Path(args[0]), Path(args[1]))
	if len(args) >= 5 and args[2] == "--assets":
		assets(Path(args[3]), Path(args[4]))
	if len(args) == 8 and args[5] == "--keepers":
		keepers(Path(args[1]), Path(args[6]), Path(args[4]), Path(args[7]))


if __name__ == "__main__":
	main()
