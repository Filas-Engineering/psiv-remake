#!/usr/bin/env python3
"""Copies the scene data copy (`make_actor_data ... scene`, with Wren Gallery) for scripts/scene.txt and adds Gallery
Hall, a 32x20 hall bigger than the screen, so the scene camera visibly moves, reached by a developer warp, with a door
down to Wren Gallery whose exit plays the `gallery_door` sound. game.json gets `party_talk_ambient` false.

* The plinth's event shows the story picture harbor_dawn, says a line whose `{picture:tag_card}` tag puts the dialogue's
  own picture on top, moves the camera to the far corner over a second, sends it back over half a second, then shows
  the beacon and ends: harbor_dawn and the beacon stay up over the field.
* The lantern's event plays a track with its own 300 ms fade, takes the pictures down and moves the camera to the hall's
  top right corner over 2 s without waiting, so it eases there after the event, on the field.
* The bell's event resets the camera at once (no seconds) and shows the farewell picture.
* Tidewater's stall gets the voice `voice.hall_stall`, whose lines take the keeper's parameters ({0} the shop's kind
  word, {1} the item, {2} the count, {3} the total, {4} the price of one) and have their own buying and selling
  results (`confirm_buy`, `thanks_buy`, `thanks_sell`; selling confirms with the plain `confirm`).

Usage: make_scene_data.py SCENE_DATA OUT

SCENE_DATA and OUT each hold game/data and core/data side by side.
"""

import json
import shutil
import sys
from pathlib import Path

MAP = ["################################"] + ["#" + "." * 30 + "#"] * 18 + ["################.###############"]

AREA = {
	"id": "gallery_hall",
	"kind": "town",
	"encounter_mode": "random",
	"step_ms": 250,
	"region": "coast",
	"map": MAP,
	"markers": {"hall_door": [16, 18], "plinth_front": [4, 3], "lantern_front": [27, 3], "bell_front": [16, 3], "hall_far": [29, 17]},
	"exits": [{"at": [16, 19], "to": "wren_gallery", "marker": "gallery_door", "sound": "gallery_door"}],
	"hooks": [
		{"id": "hall_plinth", "at": [4, 2], "event": "hall_plinth"},
		{"id": "hall_lantern", "at": [27, 2], "event": "hall_lantern"},
		{"id": "hall_bell", "at": [16, 2], "event": "hall_bell"},
	],
}

EVENTS = """
# Test-only (scripts/make_scene_data.py): story pictures and the scene camera in a hall bigger than the screen.
event hall_plinth skippable
	picture show harbor_dawn
	narrate @hall.plinth.card
	camera to hall_far 1
	narrate @hall.plinth.far
	camera reset 0.5
	picture show beacon
	narrate @hall.plinth.kept
end

event hall_lantern skippable
	music play field_marsh_reeds fade 300
	picture clear
	camera to 30 1 2 nowait
end

event hall_bell skippable
	camera reset
	picture show farewell
end

# The way in, which the run takes by the developer console instead.
event hall_way skippable
	warp gallery_hall hall_door
end
"""

VOICE = {
	"en": {
		"voice.hall_stall.welcome": "Welcome to the {0} stall.",
		"voice.hall_stall.confirm_buy": "{2} {1} at {4} each, {3} in all?",
		"voice.hall_stall.confirm": "Part with {2} {1} for {3}?",
		"voice.hall_stall.thanks_buy": "{2} {1}, {3} paid. Thank you!",
		"voice.hall_stall.thanks_sell": "{3} for {2} {1}. A fair trade.",
	},
	"ja": {
		"voice.hall_stall.welcome": "{0}の みせへ ようこそ。",
		"voice.hall_stall.confirm_buy": "{1}を {2}こ、ひとつ {4}、ぜんぶで {3}で いい?",
		"voice.hall_stall.confirm": "{1}を {2}こ、{3}で うる?",
		"voice.hall_stall.thanks_buy": "{1}を {2}こ、{3} いただきました。",
		"voice.hall_stall.thanks_sell": "{1} {2}こで {3}。いい とりひきだ。",
	},
}

STRINGS = {
	"en": {
		"hall.plinth.card": "{picture:tag_card}A card is pinned over the harbor at dawn.",
		"hall.plinth.far": "Far down the hall, the corner lies in shadow.",
		"hall.plinth.kept": "The beacon goes up beside the dawn.",
	},
	"ja": {
		"hall.plinth.card": "{picture:tag_card}よあけの みなとの うえに かーどが ある。",
		"hall.plinth.far": "ろうかの おくの すみは かげの なか。",
		"hall.plinth.kept": "のろしの えが よあけの となりに かかる。",
	},
}

AREA_NAMES = {"en": "Gallery Hall", "ja": "がろうの ひろま"}


def main():
	scene, out = Path(sys.argv[1]), Path(sys.argv[2])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(scene, out)
	data = out / "game" / "data"
	(data / "areas" / "gallery_hall.json").write_text(json.dumps(AREA, indent="\t") + "\n")
	(data / "events" / "hall.ev").write_text(EVENTS.lstrip("\n"))
	game_file = data / "game.json"
	game = json.loads(game_file.read_text())
	game["areas"].append("areas/gallery_hall.json")
	game["events"].append("events/hall.ev")
	game["party_talk_ambient"] = False
	game_file.write_text(json.dumps(game, indent="\t", ensure_ascii=False) + "\n")
	area_file = data / "areas" / "tidewater.json"
	area = json.loads(area_file.read_text())
	area["shops"][0]["voice"] = "voice.hall_stall"
	area_file.write_text(json.dumps(area, indent="\t", ensure_ascii=False) + "\n")
	for language in game["languages"]:
		code = language["code"]
		with open(data / language["strings"], "a", encoding="utf-8") as strings:
			strings.write("".join(f"{key} = {text}\n" for key, text in STRINGS[code].items()))
		text_file = data / language["text"]
		text = json.loads(text_file.read_text())
		text["text"]["area.gallery_hall"] = AREA_NAMES[code]
		text["text"].update(VOICE[code])
		text_file.write_text(json.dumps(text, indent="\t", ensure_ascii=False) + "\n")


if __name__ == "__main__":
	main()
