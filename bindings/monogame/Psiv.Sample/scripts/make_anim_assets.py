"""The example battle animation set (docs/battle-animation.md), as a copy of the exported placeholders.

make_anim_assets.py EXPORT OUT writes OUT: a copy of EXPORT with battle_animation.json (and animation/extras.json, which
it includes) holding original placeholder animations that use every track type and every field the format has: a basic
attack per weapon class and hand count, a projectile technique with a path, an all-enemies technique with a backdrop
effect, staggered releases and per-layer shake, a combination with its own script, a heal with a window number, an
enemy attack that draws a member under the enemy, an instant-defeat mark, a boss form change that recolors the old form
and swaps the backdrop, a stepped fade and split reveal into battle, a victory, a level-up and a tile transition out,
and a status's idle and tick scripts; and alt/battle_animation_ntsc.json, the same scripts at 59.92 Hz with log lines on
their release frames. It also writes an fx script for the dialogue's `fx` tag.

make_anim_assets.py EXPORT OUT BASE copies BASE instead of EXPORT (another layout set) and adds the same files.

make_anim_assets.py --broken DIR adds broken animation files to the broken copy DIR, one problem of each kind.
"""

import json
import shutil
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_test_assets import read_png, write_png  # noqa: E402


def colors_of(path, count):
	"""The `count` most common opaque colors of a picture, as #rrggbb."""
	width, height, rows = read_png(path)
	seen = {}
	for row in rows:
		for x in range(width):
			r, g, b, a = row[x * 4:x * 4 + 4]
			if a == 255:
				seen[(r, g, b)] = seen.get((r, g, b), 0) + 1
	top = sorted(seen.items(), key=lambda kv: (-kv[1], kv[0]))[:count]
	return ["#%02x%02x%02x" % c for c, _ in top]


def backdrop(path):
	"""A 320x105 backdrop: a deep teal sky in bands over a dark floor with a lighter grid."""
	rows = []
	for y in range(105):
		row = bytearray()
		for x in range(320):
			if y < 62:
				band = y // 8
				r, g, b = 10 + band * 4, 40 + band * 9, 60 + band * 10
			else:
				line = (x % 20 == 0) or (y % 10 == 0)
				r, g, b = (60, 90, 110) if line else (20, 28, 40)
			row += bytes((r, g, b, 255))
		rows.append(row)
	write_png(path, 320, 105, rows)


def scripts(palette):
	glow = {"type": "state", "on": "actor", "at": 0, "state": "cast", "length": 24}
	return {
		# Basic attacks, by weapon class and hand count.
		"ex.blade.one": {
			"phases": {"forward": 8, "banner": 18, "animate": 10, "effect": 6, "result": 30, "back": 8},
			"tracks": [
				{"id": "in", "type": "move", "on": "actor", "at": "forward", "kind": "forward", "distance": "1f", "ease": "out"},
				{"id": "swing", "type": "state", "on": "actor", "at": "animate", "state": "attack"},
				{"id": "lunge", "type": "move", "on": "actor", "at": "animate", "kind": "to", "to": "target:0:center", "reach": 0.15, "length": 10, "ease": "in"},
				{"id": "whoosh", "type": "sound", "at": "animate+4", "cue": "ex_swing"},
				{"id": "cut", "type": "sprite", "at": "effect", "effect": "slash", "spawn": "target:0", "durations": 2, "path": [[0, -10, -10], [6, 10, 10]], "length": 20},
				{"id": "hit", "type": "release", "at": "result"},
				{"id": "jolt", "type": "shake", "at": "result", "if": "hit", "amplitude": 2, "length": 8},
				{"id": "out", "type": "move", "on": "actor", "at": "back", "kind": "home"},
			],
		},
		"ex.blade.both": {
			"extends": "ex.blade.one",
			"phases": {"effect": 9},
			"tracks": [
				{"id": "cut2", "type": "sprite", "at": "effect+3", "effect": "slash", "spawn": "target:0", "durations": 2, "flip": "x", "offset": [6, 0], "length": 20},
				{"id": "plate", "type": "name_window", "at": "banner", "length": 70, "fade": 4},
			],
		},
		"ex.rod": {
			"phases": {"forward": 8, "banner": 18, "animate": 14, "effect": 8, "result": 28, "back": 8},
			"tracks": [
				{"id": "in", "type": "move", "on": "actor", "at": "forward", "kind": "forward"},
				{"id": "blink", "type": "pose", "on": "actor", "at": "animate", "pose": "hidden", "length": 4},
				{"id": "there", "type": "pose", "on": "actor", "at": "animate+4", "pose": "attack", "place_of": "target:0", "offset": {"front": [0, 60], "side": [60, 0]}, "length": 18},
				{"id": "ring", "type": "sprite", "at": "effect", "effect": "ring_stat", "spawn": "target:0", "anchor": "head", "length": 16},
				{"id": "out", "type": "move", "on": "actor", "at": "back", "kind": "home"},
			],
		},
		"ex.heavy": {
			"phases": {"forward": 10, "banner": 18, "animate": 20, "effect": 6, "result": 30, "back": 10},
			"tracks": [
				{"id": "in", "type": "move", "on": "actor", "at": "forward", "kind": "forward", "distance": "1.5f"},
				{"id": "leap", "type": "move", "on": "actor", "at": "animate", "kind": "arc", "to": "target:0:center", "reach": 0.5, "height": "0.4h", "length": 20, "ease": "in_out"},
				{"id": "pose", "type": "state", "on": "actor", "at": "animate+12", "state": "attack", "length": 14},
				{"id": "crash", "type": "sprite", "at": "effect", "effect": "burst", "spawn": "target:0", "anchor": "feet", "scale": "x1.5", "length": 18},
				{"id": "quake", "type": "shake", "at": "result", "amplitude": 5, "decay": "out", "length": 16},
				{"id": "out", "type": "move", "on": "actor", "at": "back", "kind": "home"},
			],
		},
		# A projectile with a path.
		"ex.spark": {
			"phases": {"forward": 8, "banner": 20, "animate": 18, "effect": 16, "result": 32, "back": 8},
			"marks": {"launch": "effect", "impact": "result"},
			"tracks": [
				{"id": "in", "type": "move", "on": "actor", "at": "forward", "kind": "forward"},
				{"id": "cast", "type": "state", "on": "actor", "at": "animate", "state": "cast", "length": 34},
				{"id": "charge", "type": "color", "on": "actor", "at": "animate", "mode": "tint", "to": {"tint": "#fff080", "amount": 0.5}, "in": 8, "hold": 10, "out": 8},
				{"id": "hum", "type": "sound", "at": "animate", "cue": "ex_charge"},
				{"id": "orb", "type": "sprite", "at": "launch", "effect": "ex.orb", "spawn": "target:0", "length": 30},
				{"id": "zap", "type": "sound", "at": "launch", "cue": "ex_zap"},
				{"id": "lit", "type": "color", "on": "target:0", "at": "impact", "mode": "flash", "to": {"add": "#fff080"}, "length": 8},
				{"id": "out", "type": "move", "on": "actor", "at": "back", "kind": "home"},
			],
		},
		# All enemies: the backdrop chills and ripples, a burst on each enemy six frames apart, each released with its burst.
		"ex.frost": {
			"phases": {"forward": 8, "banner": 20, "animate": 16, "effect": 9, "result": 40, "back": 8},
			"tracks": [
				{"id": "in", "type": "move", "on": "actor", "at": "forward", "kind": "forward"},
				{"id": "cast", "type": "state", "on": "actor", "at": "animate", "state": "cast", "length": 25},
				{"id": "chill", "type": "color", "on": "backdrop", "at": "animate", "mode": "tint", "to": {"tint": "#6080c0", "amount": 0.5}, "in": 12, "hold": 50, "out": 10},
				{"id": "ripple", "type": "backdrop", "at": "effect", "effect": "wave", "amplitude": 3, "wavelength": 48, "period": 40, "length": 49},
				{"id": "bursts", "type": "sprite", "at": "effect", "effect": "burst_chill", "spawn": "targets", "order": "left_to_right", "stagger": 6, "durations": 3, "blend": "additive",
					"length": 40},
				{"id": "cracks", "type": "sound", "at": "effect", "cue": "ex_crack", "per": "each", "order": "left_to_right", "stagger": 6},
				{"id": "hits", "type": "release", "at": "result", "unit": "target", "order": "left_to_right", "stagger": 6},
				{"id": "flinch", "type": "react", "state": "hurt", "effect": "ring_cure", "length": 18},
				{"id": "tremor", "type": "shake", "at": "result", "amplitude": 3, "length": 24,
					"offsets": {"backdrop": [[3, 0], [0, 0], [-3, 0], [0, 0]], "figures": [[0, 2], [0, -2]]}},
				{"id": "out", "type": "move", "on": "actor", "at": "back", "kind": "home"},
			],
		},
		"ex.frost.three": {
			"extends": "ex.frost",
			"tracks": [
				{"id": "columns", "type": "sprite", "at": "effect", "effect": "ring_status", "spawn": "columns", "column_y": 220, "length": 30},
				{"id": "over", "type": "sprite", "at": "effect+6", "effect": "nova_chill", "spawn": "all_targets", "scale": "x2", "blend": "additive", "length": 30},
			],
		},
		# A combination: each caster glows in turn, two projectiles converge, a flash and a nova where they meet.
		"ex.twin": {
			"phases": {"forward": 10, "banner": 30, "animate": 24, "effect": 18, "result": 40, "back": 10},
			"tracks": [
				{"id": "in", "type": "move", "on": "actor", "at": "forward", "kind": "forward", "stagger": 4},
				{"id": "glow0", "type": "call", "fragment": "ex.glow", "at": "animate", "bind": {"actor": "actor:0"}, "params": {"color": "#c0f0ff"}},
				{"id": "glow1", "type": "call", "fragment": "ex.glow", "at": "animate+12", "bind": {"actor": "actor:1"}, "params": {"color": "#fff080"}},
				{"id": "mote", "type": "sprite", "at": "effect", "effect": "ex.mote", "spawn": "target:0", "length": 22},
				{"id": "orb", "type": "sprite", "at": "effect+4", "effect": "ex.arc", "spawn": "target:0", "length": 18},
				{"id": "flash", "type": "flash", "at": "result", "amount": 0.5, "length": 10},
				{"id": "nova", "type": "sprite", "at": "result", "effect": "nova_shock", "spawn": "target:0", "scale": "x1.5", "blend": "additive", "length": 24},
				{"id": "quake", "type": "shake", "at": "result", "amplitude": 6, "length": 30},
				{"id": "out", "type": "move", "on": "actor", "at": "back", "kind": "home", "stagger": 4},
			],
		},
		# A heal: motes rise through the target while it warms green; its number opens in a window.
		"ex.mend": {
			"phases": {"forward": 8, "banner": 18, "animate": 16, "effect": 20, "result": 36, "back": 8},
			"tracks": [
				{"id": "in", "type": "move", "on": "actor", "at": "forward", "kind": "forward"},
				{"id": "cast", "type": "state", "on": "actor", "at": "animate", "state": "cast", "length": 36},
				{"id": "motes", "type": "sprite", "at": "effect", "effect": "ex.motes", "spawn": "targets", "offsets": [[-10, 0], [8, 4]], "length": 40},
				{"id": "warm", "type": "color", "on": "targets", "at": "effect+8", "mode": "tint", "to": {"tint": "good", "amount": 0.4}, "in": 6, "hold": 14, "out": 10},
				{"id": "out", "type": "move", "on": "actor", "at": "back", "kind": "home"},
			],
		},
		# An enemy's attack that pulls the member under the enemy for the bite.
		"ex.hound": {
			"phases": {"forward": 6, "banner": 14, "animate": 14, "effect": 4, "result": 30, "back": 12},
			"tracks": [
				{"id": "in", "type": "move", "on": "actor", "at": "forward", "kind": "forward"},
				{"id": "hop", "type": "move", "on": "actor", "at": "animate", "kind": "hop", "height": "0.3h", "length": 8},
				{"id": "pull", "type": "pose", "on": "target:0", "at": "animate+6", "pose": "hurt", "place_of": "user", "offset": {"front": [0, 24], "side": [24, 0]}, "length": 34},
				{"id": "bite", "type": "sprite", "at": "effect", "effect": "slash_heat", "spawn": "target:0", "anchor": "head", "length": 20},
				{"id": "snap", "type": "sound", "at": "effect", "cue": "ex_snap"},
				{"id": "out", "type": "move", "on": "actor", "at": "back", "kind": "home"},
			],
		},
		# An instant defeat: a dark ring, then the mark while the figure cuts away in bands.
		"ex.unmake": {
			"extends": "turn.default",
			"phases": {"effect": 24},
			"tracks": [
				{"id": "effect", "type": "sprite", "at": "effect", "effect": "ring_status", "spawn": "targets", "color": {"multiply": "#403050"}, "durations": 4, "length": "$phase"},
				{"id": "cue", "type": "sound", "at": "effect", "cue": "ex_void"},
			],
		},
		"ex.felled": {
			"length": "$rest",
			"tracks": [
				{"id": "mark", "type": "sprite", "effect": "mark_defeat", "spawn": "self", "anchor": "center", "scale": "large", "loops": -1, "length": "$rest", "layer": "over_labels"},
				{"id": "pulse", "type": "color", "on": "self", "mode": "cycle", "colors": [{}, {"add": "#ff3030"}], "period": 8, "length": 32},
				{"id": "stamp", "type": "sound", "cue": "ex_stamp"},
			],
		},
		# The boss's next form: the old form recolors and shudders, the backdrop darkens and swaps for the battle's rest,
		# and on frame 48 the core replaces it.
		"ex.core": {
			"phases": {"change": 96},
			"tracks": [
				{"id": "quake", "type": "sound", "at": 0, "cue": "ex_quake"},
				{"id": "shudder", "type": "state", "on": "previous_form", "at": 0, "state": "hurt", "length": 40},
				{"id": "recolor", "type": "color", "on": "changed", "at": 0, "mode": "keys", "ease": "step", "length": 48,
					"keys": [[0, {}], [12, {"palette": palette}], [24, {"palette": palette, "add": "#402020"}], [36, {"palette": palette}]]},
				{"id": "rumble", "type": "shake", "at": 0, "amplitude": 4, "decay": "none", "length": 48},
				{"id": "dim", "type": "color", "on": "backdrop", "at": 12, "mode": "fade", "to": {"multiply": "#505060"}, "in": 30, "length": 30},
				{"id": "swap", "type": "backdrop", "at": 42, "effect": "swap", "image": "backdrops/ex_core.png", "length": 20, "persist": True},
				{"id": "form", "type": "release", "at": 48},
				{"id": "flash", "type": "flash", "at": 48, "amount": 0.7, "length": 12},
				{"id": "in", "type": "state", "on": "changed", "at": 48, "state": "transform_in", "length": 30},
				{"id": "lit", "type": "color", "on": "changed", "at": 48, "mode": "flash", "to": {"add": "#ffffff"}, "length": 30},
				{"id": "boom", "type": "sound", "at": 48, "cue": "critical"},
				{"id": "after", "type": "shake", "at": 48, "amplitude": 8, "length": 36},
			],
		},
		# Into battle: a stepped fade over the field, then the backdrop and the figures slide in from opposite sides.
		"ex.intro": {"phases": {"cover": 12, "hold": 4, "reveal": 20}, "tracks": [{"id": "split", "type": "transition", "transition": "ex.split"}]},
		"ex.victory": {
			"phases": {"step": 72},
			"tracks": [
				{"id": "cheer", "type": "state", "on": "party", "at": 0, "state": "victory", "length": 72, "stagger": 4},
				{"id": "sparkle", "type": "sprite", "at": 6, "effect": "motes_revive", "spawn": "party", "anchor": "auto", "stagger": 4, "blend": "additive", "length": 40},
				{"id": "clear", "type": "backdrop", "at": 0, "effect": "restore", "length": 20},
			],
		},
		"ex.levelup": {
			"phases": {"step": 48},
			"tracks": [
				{"id": "ring", "type": "sprite", "at": 0, "effect": "ring_stat", "spawn": "screen:50%,35%", "scale": 6, "blend": "additive", "length": 30},
				{"id": "glow", "type": "color", "on": "stage", "at": 0, "mode": "tint", "to": {"tint": "accent", "amount": 0.15}, "in": 6, "hold": 12, "out": 12},
			],
		},
		"ex.outro": {"phases": {"cover": 16, "hold": 4, "reveal": 20}, "tracks": [{"id": "tiles", "type": "transition", "transition": "ex.tiles"}]},
		# While a combatant is silenced it shivers in a pale tint; each round's banner rings over it.
		"ex.silence.idle": {
			"length": 40,
			"tracks": [
				{"id": "pale", "type": "color", "on": "self", "mode": "cycle", "colors": [{"tint": "#9080ff", "amount": 0.15}, {"tint": "#9080ff", "amount": 0.4}], "period": 20, "smooth": True,
					"length": 40},
				{"id": "shiver", "type": "move", "on": "self", "kind": "sine", "axis": "x", "amplitude": 2, "period": 10, "length": 40},
			],
		},
		"ex.silence.tick": {
			"length": "$rest",
			"tracks": [
				{"id": "ring", "type": "sprite", "effect": "ring_status", "spawn": "self", "length": 24},
				{"id": "tick", "type": "sound", "cue": "ex_tick"},
			],
		},
		# The dialogue's `fx` tag: screen-level tracks only.
		"ex.fx.red": {"phases": {"step": 30}, "tracks": [{"id": "red", "type": "flash", "color": "#ff2020", "amount": 0.6, "length": 20, "area": "frame"}]},
		"ex.fx.dusk": {"phases": {"step": 60}, "tracks": [
			{"id": "dusk", "type": "color", "on": "frame", "mode": "tint", "to": {"tint": "#203060", "amount": 0.45}, "in": 15, "hold": 30, "out": 15},
			{"id": "hum", "type": "sound", "cue": "ex_charge"},
		]},
	}


def extras():
	return {
		"fragments": {"ex.glow": {"params": {"color": "#ffffff"}, "tracks": [
			{"type": "state", "on": "actor", "at": 0, "state": "cast", "length": 24},
			{"type": "color", "on": "actor", "at": 0, "mode": "tint", "to": {"tint": "$param.color", "amount": 0.6}, "in": 6, "hold": 10, "out": 8},
			{"type": "sound", "at": 0, "cue": "ex_charge"},
		]}},
		"effects": {
			"ex.orb": {"sheet": "burst_shock", "frames": [0, 1, 2, -1, 2, 1], "durations": 2, "loops": -1, "blend": "additive", "scale": "x1",
				"travel": {"from": {"point": "user:head", "offset": [0, -12]}, "frames": 16, "path": "arc", "height": "0.6h", "ease": "in"}, "impact": "ex.pop"},
			"ex.pop": {"sheet": "burst_shock", "frames": [0, 1, 2, 3, 4, 5], "durations": [2, 2, 3, 3, 4, 6], "blend": "additive", "scale": "x1.25"},
			"ex.mote": {"sheet": "motes_item", "durations": 2, "loops": -1, "blend": "additive",
				"travel": {"from": "actor:0:head", "frames": 18, "path": "wave", "waves": 2, "height": 24}},
			"ex.arc": {"sheet": "burst_shock", "durations": 3, "loops": -1, "blend": "additive", "travel": {"from": "actor:1:head", "frames": 14, "path": "arc", "height": "0.5h"}},
			"ex.motes": {"sheet": "motes_heal", "durations": 4, "loops": -1, "anchor": "feet", "blend": "additive", "hold_last": 6, "color": {"from": {}, "to": {"alpha": 0.4}},
				"travel": {"from": "self:feet", "to": "self:top", "frames": 30, "ease": "out"}},
		},
		"transitions": {
			"ex.split": {"kind": "split", "color": "#000000", "steps": 4, "step_frames": 3, "speed": 24, "frames": 20},
			"ex.tiles": {"kind": "tiles", "color": "#101018", "tile": 80, "order": "spiral_in"},
		},
		"figures": {
			"enemy": {"states": {"defeat:instant": {"kind": "bands", "bands": 10, "length": 30, "delay": 8}}},
			"enemy:gel_mite": {"states": {"defeat": {"kind": "frames", "pose": "defeat", "length": "$rest"}}},
			"party": {"states": {"victory": {"pose": "cast", "loop": True, "motion": {"kind": "hop", "height": 10, "length": 20}}}},
		},
		"numbers": {
			"heal": {"motion": "window", "row": 72, "open": 6, "pad": 6},
			"felled": {"motion": "bounce", "rise": 20, "bounces": 2},
		},
	}


def write(path, data):
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(json.dumps(data, indent="\t") + "\n")


def main():
	if sys.argv[1] == "--broken":
		broken(Path(sys.argv[2]))
		return
	export, out = Path(sys.argv[1]), Path(sys.argv[2])
	base = Path(sys.argv[3]) if len(sys.argv) > 3 else export
	shutil.rmtree(out, ignore_errors=True)
	shutil.copytree(base, out)
	colossus = colors_of(export / "figures/enemy_tide_colossus.png", 2)
	palette = [[colossus[0], "#d04030"], [colossus[1], "#f0a040"]]
	backdrop(out / "backdrops/ex_core.png")
	select = {
		"weapon:blade/one": "ex.blade.one", "weapon:blade/both": "ex.blade.both", "weapon:rod": "ex.rod", "weapon:heavy/both": "ex.heavy",
		"ability:spark": "ex.spark", "ability:frostbite": "ex.frost", "ability:frostbite/count:3": "ex.frost.three", "combo:twin_arc": "ex.twin",
		"ability:mend": "ex.mend", "enemy:rust_hound/attack": "ex.hound", "ability:unmake": "ex.unmake", "react:instant_defeat": "ex.felled",
		"change:transform.tide_colossus_core": "ex.core", "battle:transition_in": "ex.intro", "step:outcome.victory": "ex.victory",
		"step:notice.game.level_up": "ex.levelup", "battle:transition_out": "ex.outro", "status:silence/idle": "ex.silence.idle",
		"status:silence/tick": "ex.silence.tick",
	}
	main_file = {
		"version": 1,
		"include": ["animation/extras.json"],
		"settings": {"rate": 60, "log_timing": "step"},
		"weapon_classes": {"category:greatblade": "heavy"},
		"placements": {"encounter:lone_ooze": {"front": [[640, 360]], "side": [[300, 420]]}},
		"reduced_flash": {"damp": 0.25},
		"select": select,
		"scripts": scripts(palette),
	}
	write(out / "battle_animation.json", main_file)
	write(out / "animation/extras.json", extras())
	ntsc = dict(main_file, settings={"rate": 59.92, "log_timing": "release"})
	write(out / "alt/battle_animation_ntsc.json", ntsc)
	manifest = json.loads((out / "assets.json").read_text())
	manifest["battle_animation"] = "battle_animation.json"
	write(out / "assets.json", manifest)
	# The dialogue layout scripts/dialogue_fx.txt swaps in: screen effects and the music held and resumed.
	if (out / "ui/dialogue.json").exists():
		dialogue = json.loads((out / "ui/dialogue.json").read_text())
		dialogue.setdefault("tags", {}).update({
			"flash": {"action": "fx", "values": {"red": "ex.fx.red", "dusk": "ex.fx.dusk"}},
			"hush": {"action": "music_pause"},
			"resume": {"action": "music_resume"},
		})
		write(out / "alt/dialogue_fx.json", dialogue)
		dialogue["tags"]["glow"] = {"action": "fx", "values": {"1": "turn.default", "2": "no.such_fx"}}
		write(out / "alt/dialogue_fx_broken.json", dialogue)


def broken(out):
	"""Broken animation files beside the broken copy's other broken files: one of each kind of problem."""
	write(out / "battle_animation.json", {
		"version": 2,
		"include": ["animation/missing.json", "animation/broken_extras.json"],
		"scirpts": {},
		"settings": {"rate": 500, "log_timing": "sometimes"},
		"select": {
			"ability:sparkk": "turn.default", "weapon:axe": "turn.default", "combo:twin_arcs": "turn.default", "ability:spark": "spark.bolts",
			"step:notice.Game Lvl": "step.banner", "status:poison/wobble": "turn.default",
		},
		"scripts": {
			"spark.bolt": {
				"phases": {"forward": 8, "banner": 20, "animate": 18, "effect": 700, "result": 32, "back": 8},
				"tracks": [
					{"type": "move", "on": "actor", "at": "forward"},
					{"type": "sprite", "at": "impakt", "effect": "sprak.orb"},
					{"type": "release", "at": 30},
					{"type": "flash", "if": "crit"},
					{"type": "color", "on": "stage", "to": {"multiply": "#808080"}},
					{"type": "wobble"},
				],
			},
			"loop.a": {"extends": "loop.b", "tracks": []},
			"loop.b": {"extends": "loop.a", "tracks": []},
			"Bad Id": {"tracks": []},
		},
		"effects": {"spark.orb": {"sheet": "no_such_sheet", "frames": [0, -2]}},
		"figures": {"enemy": {"states": {"defeat": {"kind": "explode"}}}, "monster": {"states": {}}},
		"numbers": {"critical": {"color": "gold"}, "huge": {}},
		"transitions": {"blocks": {"kind": "mosaic", "block": 4}, "dots": {"kind": "tiles", "tile": 2}},
		"weapon_classes": {"category:axe": "chop"},
		"placements": {"count:9": [[1, 2]]},
	})
	write(out / "animation/broken_extras.json", {"fragments": {"cast_glow": {"tracks": [{"type": "call", "fragment": "cast_glow"}]}}, "version": 1})
	manifest = json.loads((out / "assets.json").read_text())
	manifest["battle_animation"] = "battle_animation.json"
	write(out / "assets.json", manifest)


if __name__ == "__main__":
	main()
