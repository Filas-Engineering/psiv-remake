#!/usr/bin/env python3
"""Writes the asset directory scripts/flash_safety.txt plays: the placeholder export with a battle animation file whose
screen-wide color tracks are flashes only by their effect (docs/battle-animation.md, "Flashes by effect"):

* a basic attack (`shape:attack`) strobes the stage white four times with stepped `keys` (no `flash` track, no `flash`
  mode): four flashes in one track, past the limit of three, so it does not draw and the transcript says so;
* Spark (`ability:spark`) whites the stage out twice the same way: two flashes, drawn, and held to a fifth of the light
  under reduced flash;
* a defend (`shape:defend`) tints the stage slowly to 0.6 over 60 frames: no flash, the same with reduced flash;
* a heal (`ability:glint`) puts a dim backdrop tint marked `"flash": true`: counted once though it is not bright; the
  same script is selected for the Squall Fan, an accessory with a use (`item:squall_fan`), which the check accepts;
* Spark's script also poses the enemy beside its target on the right (`beside:right:target:0`) and the one adjacent on
  the left (`adjacent:left:target:0`, nobody next to place 0);
* every battle opens with a sequence transition: to amber in three steps and a hold, to black and a hold, back to amber,
  then the reveal.

Usage: make_flash_assets.py EXPORT OUT
"""

import json
import shutil
import sys
from pathlib import Path


def strobe(times):
	keys = []
	for k in range(times):
		keys += [[k * 6, {"add": "#ffffff"}], [k * 6 + 3, {}]]
	return {"id": "strobe", "type": "color", "on": "stage", "at": "effect", "mode": "keys", "ease": "step", "keys": keys, "length": times * 6}


ANIMATION = {
	"version": 1,
	"select": {"shape:attack": "flash.strobe", "ability:spark": "flash.double", "shape:defend": "flash.tint", "ability:glint": "flash.marked",
		"item:squall_fan": "flash.marked", "battle:transition_in": "flash.into"},
	"transitions": {"flash.sequence": {"kind": "sequence", "phases": [
		{"color": "#c0a040", "steps": 3, "step_frames": 2, "hold": 6},
		{"color": "#000000", "steps": 2, "step_frames": 3, "hold": 8},
		{"color": "#c0a040", "steps": 2, "step_frames": 2, "hold": 6}
	]}},
	"scripts": {
		"flash.strobe": {"extends": "turn.default", "phases": {"effect": 30}, "tracks": [strobe(4)]},
		"flash.double": {"extends": "turn.default", "phases": {"effect": 30}, "tracks": [strobe(2),
			{"id": "neighbor", "type": "pose", "on": "beside:right:target:0", "at": "effect", "pose": "hurt", "length": 20},
			{"id": "none_left", "type": "pose", "on": "adjacent:left:target:0", "at": "effect", "pose": "hurt", "length": 20}]},
		"flash.into": {"phases": {"cover": 44, "hold": 4, "reveal": 8}, "tracks": [{"type": "transition", "transition": "flash.sequence"}]},
		"flash.tint": {"extends": "turn.default", "phases": {"effect": 70}, "tracks": [
			{"id": "tint", "type": "color", "on": "stage", "at": "effect", "mode": "set", "ease": "linear", "to": {"tint": "#ffffff", "amount": 0.6}, "length": 60}
		]},
		"flash.marked": {"extends": "turn.default", "tracks": [
			{"id": "glow", "type": "color", "on": "backdrop", "at": "effect", "mode": "set", "to": {"multiply": "#e0e0e0"}, "length": 20, "flash": True}
		]},
	},
}


def main():
	export, out = (Path(a) for a in sys.argv[1:3])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(export, out)
	(out / "battle_animation.json").write_text(json.dumps(ANIMATION, indent="\t") + "\n")
	manifest = json.loads((out / "assets.json").read_text())
	manifest["battle_animation"] = "battle_animation.json"
	(out / "assets.json").write_text(json.dumps(manifest, indent="\t") + "\n")


if __name__ == "__main__":
	main()
