# Assets for the MonoGame sample

The sample (`bindings/monogame/Psiv.Sample`) draws every picture and plays every sound from code: `Art` generates the pictures and `Synth` the sounds. An asset directory replaces any of them with files. Each entry is optional: an id with no file, or whose entry has a problem, keeps its generated placeholder, so the sample runs the same with no asset directory, with a few files, or with a full set. Files are read at run time (PNG through `Texture2D.FromStream`, WAV and Ogg Vorbis decoded in the sample), with no content pipeline step: drop files in, edit `assets.json`, run.

## Start from the export

```bash
cd bindings
dotnet run --project monogame/Psiv.Sample -- --export-placeholders my_assets   # every generated picture and sound, and a manifest
dotnet run --project monogame/Psiv.Sample -- --check-assets --assets my_assets  # every problem with file, entry and reason
dotnet run --project monogame/Psiv.Sample -- --assets my_assets                 # the window with them; F5 reloads
dotnet run --project monogame/Psiv.Sample -- --assets my_assets --watch-assets  # reloads by itself when a file changes
```

The export writes each generated picture into sheets (`tilesets/`, `characters/`, `portraits/`, `figures/` (with `party_back_ID.png`, the back figures), `field_enemies/`, `vehicles/`, `backdrops/`, `effects/`, `sprites/`, `status_icons/`, `ui/`), every sound the game can ask for as 16-bit mono WAV at 22050 Hz (`audio/music/`, `audio/ambient/`, `audio/cue/`), the built-in battle presentation table (`battle_presentation.json`) and `assets.json`. It reads every file back and compares it with what it generated, then checks the directory. Run with that directory, the sample draws every frame exactly as it does without one (see [Round trip](#round-trip)); replace one sheet at a time, keeping its layout, or rewrite its entry.

Without `--assets` the sample uses `assets/` beside the data directory (`game/assets` for the placeholder data) when there is one.

`--check-assets` is the asset counterpart of `game_cli --check`: it loads the manifest against the session's data without a graphics or audio device (PNG sizes come from the file header, sound lengths from the WAV or Ogg header), prints each problem as `assets: FILE: ENTRY: REASON`, then a count of what loaded, and exits 0 with `OK: no problems.` or 1 with `FAILED: N problems.` The window and headless runs report the same problems through the same log line and go on with the generated placeholders in their place.

## Files

* Pictures are PNG, 8 bits a channel, not interlaced (what every editor writes by default; anything else is a problem). Straight alpha in the file; the sample premultiplies it when it loads.
* A grid image (a sheet) is cut into equal frames of `frame` (`[width, height]`, 16 by 16 unless given; tilesets say `tile`). Frames are numbered from 0, left to right, then top to bottom. The image's size must divide into whole frames, and every frame an entry names must exist.
* The field draws a 16-pixel frame on a 48-pixel cell (3x). A frame of another size is drawn at the same scale, standing on the bottom middle of its cell, so a 16x24 person rises half a cell above it; a tile of any size fills its cell.
* Sounds are WAV (8 or 16-bit PCM, mono or stereo, any rate) or Ogg Vorbis. Loop points are in frames (one sample per channel).

## assets.json

Every field is checked: unknown fields, wrong types, unknown ids and files that do not fit their entry are problems.

```json
{
	"version": 1,
	"tilesets": [],
	"characters": [],
	"portraits": {},
	"figures": [],
	"field_enemies": [],
	"vehicles": [],
	"backdrops": [],
	"effects": [],
	"sprites": {},
	"status_icons": {},
	"ui": {},
	"audio": [],
	"battle_presentation": "battle_presentation.json"
}
```

### tilesets

One per area kind (`town`, `field`, `dungeon`) or per area id; an area uses its own tileset, else its kind's.

```json
{"kind": "town", "image": "tilesets/town.png", "tile": 16,
	"floor": [0, 1, 2, 3], "decor": [4, 5, 6, 7, 8, 9], "decor_one_in": 11,
	"wall_top": [10, 11], "wall_top_alt_one_in": 3,
	"wall_face": [12, 13], "wall_face_alt_one_in": 5,
	"water": [14, 15, 16, 17], "water_frame_ms": 220,
	"edge": "#282446", "bank": "#6e6862",
	"objects": {"door": 18, "chest_closed": 19, "chest_open": 20, "stall": 21, "bed": 22, "sign": 23, "rune": 24,
		"exit_north": 25, "exit_east": 26, "exit_south": 27, "exit_west": 28}}
```

How the renderer uses them, from a hash of each cell's coordinates so the picture never changes between frames:

* A floor cell is one of `floor`; one cell in `decor_one_in` is one of `decor` instead.
* A wall with a non-wall cell below shows a `wall_face`, otherwise a `wall_top`. The first picture of each list is the usual one; one cell in `wall_top_alt_one_in` (`wall_face_alt_one_in`) shows one of the others. Walls get `edge`-colored borders toward open cells (3 field pixels) and cast a shadow on the floor below and to the right.
* Water steps through `water` every `water_frame_ms`, neighbors a frame apart, with a `bank`-colored band and light foam on its edges.
* `objects` are the point objects: doorways (exits inside the map), chests closed and opened, shop stalls, rest beds, `sign` (a talk point that is not an actor), `rune` (an active step hook) and the four exit arrows on the map's edges. An object left out keeps its kind's picture (an area tileset's, its kind's tileset's).
* `edge` and `bank` are `#rrggbb` or `#rrggbbaa`; left out, the generated kind's colors.

### characters

The field sprite of a party member or actor, by its id.

```json
{"id": "fighter", "image": "characters/fighter.png", "frame": [16, 16],
	"walk": {"north": [0, 1, 2], "east": [3, 4, 5], "south": [6, 7, 8], "west": [9, 10, 11]},
	"poses": {
		"nod": {"frames": {"south": [6, 12, 13, 12], "north": [0, 14, 15, 14]}, "ms": [80, 80, 80, 80]},
		"bow": {"frames": {"south": [6, 16, 17, 16, 6]}, "ms": [46, 90, 629, 90, 45], "motion": false}
	},
	"animations": {"nod": "nod", "bow": "bow", "curtsy": "bow"}}
```

* `walk`: for each facing, the standing frame and the two walking frames.
* `poses`: named frame runs per facing, each frame with its milliseconds. A facing left out uses south's. One play lasts the sum of `ms`; the event waits for it, and a looping `anim` repeats it.
* `animations` maps script animation names (`anim fighter nod`; every name must be in `game.json`'s `presentation.animations`) to poses. A pose no animation plays is a problem.
* The generated motion of the animation (the hop of `jump` and `startle`, the startled mark, the strokes beside the head of `nod` and `shake`, the small bounces of names the sample does not know) plays over the frames unless the pose says `"motion": false`.
* An animation a sheet does not map plays on its standing frame with that motion only. The person's portrait, without a `portraits` entry, is its south standing frame.

### portraits

```json
"portraits": {"guide": "portraits/guide.png", "guide_smile": "portraits/guide_smile.png"}
```

By the line's portrait id (`game.json`'s `presentation.portraits`), else by the speaker's id, else the character's south standing frame, else the generated look. Any size, drawn at 80 by 80 in the dialogue box and in the status, name entry and battle panels.

### figures

Battle figures, by character or vehicle id (`"side": "party"`) or enemy id (`"side": "enemy"`). In a vehicle battle only the vehicle's figure stands on the field (the members ride inside; their panels stay); without a file a vehicle's figure is its west-facing field frames, bobbing.

```json
{"id": "fighter", "side": "party", "image": "figures/party_fighter.png", "frame": [16, 16],
	"idle": {"frames": [0], "ms": [400]},
	"animations": {"attack": {"frames": [1, 0], "ms": [140, 140]}, "cast": {"frames": [2], "ms": [280]}, "defeat": {"frames": [3], "ms": [400]}}}
```

`idle` loops. The named animations are `attack`, `cast`, `item` and `defend` (as the presentation table's `animation` names them), `hurt` (a hit) and `defeat` (a fallen party member); each plays once and holds its last frame, and one left out plays `idle`. The battle adds its own motion on top, the same for files and generated pictures: stepping forward and back, a lunge with `attack`, a glow with `cast` and `item`, a shake and a red flash when hurt, a fade when an enemy falls. The party faces west, toward the enemies on the left.

`"view": "back"` (default `"side"`) makes an entry the party member or vehicle seen from behind, which the front battle layout draws (below, [The battle presentation table](#the-battle-presentation-table)); the side layout keeps using the `"side"` entry, so an id can have both. A back figure takes the same animations; the generated set is the person's look as head and shoulders, 16 by 16 drawn at 9x: `idle`, `attack` (the right arm raised), `cast` (both arms up), `item` (a hand up holding a pale bottle), `defend` (the head lowered, braced), `hurt` (the head knocked aside) and `defeat` (slumped, the head sunk between the shoulders), one frame each in that order (`attack` plays frames 1 then 0). A vehicle's generated back figure is its north field frames, bobbing. The front layout's motion: a member rises out of the line instead of stepping sideways (the member choosing a command already a little), an enemy comes toward the camera. A back figure's key is `figure:back:ID`.

### field_enemies

Visible enemies in the field, by encounter id: `{"encounter": "training", "image": "field_enemies/training.png", "frames": [0, 1], "ms": 280}` (`ms` a number for every frame or a list).

### vehicles

A vehicle in the field, by the id of one of `game.json`'s `vehicles`: per facing a run of frames, looping at `ms` a frame (default 240).

```json
{"id": "reed_skimmer", "image": "vehicles/reed_skimmer.png", "frame": [16, 16],
	"facings": {"north": [0, 1], "east": [2, 3], "south": [4, 5], "west": [6, 7]}, "ms": 240}
```

Every facing needs at least one frame. The field draws the vehicle where it stands, or under the leader while the party rides it (the leader sits a few pixels up, in the hull), and the battle panel shows its south frame. The generated look is a hull colored from the vehicle's id that bobs a pixel, with spray on the first frame.

### backdrops

```json
{"kind": "field", "image": "backdrops/field.png"}
{"area": "kiln_lower", "image": "backdrops/kiln.png"}
{"encounter": "warden", "image": "backdrops/warden.png"}
```

The encounter's, else the area's, else the area kind's. In the side layout drawn over the battlefield above the party panels, 1280 by 420 (the generated ones are 320 by 105, at 4x), any size stretched to it. In the front layout drawn at 1280 by 480 over the stage, and its part below 62% of its height (the generated ground, under the horizon's shadow) drawn again, taller, from there to the bottom of the screen, behind the members and the status strip.

### effects

Frame runs drawn over each target of an action, at 3x (4x on enemies in the front layout), centered on its figure (on the head of a member seen from behind, and on its status slot in a vehicle battle, where members have no figure; in the front layout an effect on several targets starts on each a moment after the one to its left, sweeping along them): `{"id": "spark_bolt", "image": "effects/spark_bolt.png", "frame": [32, 32], "frames": [0, 1, 2, 3], "ms": 60}`. The presentation table names effects by id; an id here replaces the generated effect of that id or adds a new one. The generated effects are `slash`, `burst` and `nova` with the suffixes `_heat`, `_chill`, `_shock`, `_gale` and `_holy` (or none), and `ring_status`, `ring_stat`, `ring_cure`, `motes_heal`, `motes_revive`, `motes_item`, `shield`, `motes_join`, `ring_merge`, `nova_transform` and `mark_defeat` (the last four for the presentation table's `changes`).

### sprites and status_icons

```json
"sprites": {"shadow": "sprites/shadow.png", "bubble": "sprites/bubble.png", "alert": "sprites/alert.png", "pointer": "sprites/pointer.png", "pier": "sprites/pier.png"},
"status_icons": {"sleep": "status_icons/sleep.png", "poison": "status_icons/poison.png"}
```

The shadow under everyone, the speech bubble over talk points, the `!` over chasing enemies and startled people, the pointer over targets and finished dialogue lines, the pier a port stands on (greyed while the port is closed); and an icon per status id (from core's `statuses.json`), drawn in the battle's panels and under the enemies, growing in when a status lands and blinking out when it ends.

### ui

```json
"ui": {"enabled": true,
	"frame": {"image": "ui/frame.png", "border": 8, "scale": 1},
	"panel_frames": [{"image": "ui/panel.png", "border": 4}, {"camera": "lookout_east", "image": "ui/panel_east.png", "border": 4}],
	"cursor": "ui/cursor.png", "bar_track": "ui/bar_track.png", "bar_fill": "ui/bar_fill.png",
	"fonts": {"en": {"file": "fonts/body.ttf", "scale": 1.0}, "ja": {"file": "fonts/cjk.otf", "scale": 1.1}}}
```

* `frame`: every window (menus, dialogue, toasts, banners, the battle's panels) as a nine-slice: corners `border` pixels square drawn at `scale`, edges and middle stretched.
* `panel_frames`: the border around comic panels, one with no `camera` for all and one per camera id (`game.json`'s `presentation.cameras`) for its panels.
* `cursor`: the menu cursor; `bar_track` and `bar_fill`: HP and TP bars, the fill tinted with the bar's color and stretched to its value's width.
* `fonts`: per language (`game.json`'s `languages`) a TrueType or OpenType file, with the system faces after it for glyphs it lacks, and a `scale` for every text size.
* `"enabled": false` keeps the generated look and loads nothing of the section (the export writes it that way, see [Round trip](#round-trip)).

### audio

```json
{"kind": "music", "id": "town_tide_bells", "file": "audio/town.ogg", "loop_start": 88200, "loop_end": 1764000, "gain": 0.8}
{"kind": "ambient", "id": "marsh_wind", "file": "audio/wind.wav"}
{"kind": "cue", "id": "confirm", "file": "audio/confirm.wav", "gain": 0.5}
```

* `music`: `game.json`'s tracks (`presentation.audio` ids the scripts play with `music play` or the data names for areas, encounters, the title, victory and defeat) and the ids the sample derives where the data names none (`title`, `battle_town`, `battle_field`, `battle_dungeon`, `battle_scripted`, `<kind>_<area>`).
* `ambient`: the ids scripts play with `ambient play`.
* `cue`: the ids scripts play with `sound`, and the sample's own cues (`cursor`, `confirm`, `back`, `refuse`, `menu_open`, `menu_close`, `text`, `line`, `save`, `step`, `battle_start`, `hit`, `miss`, `critical`, `heal`, `status`, `enemy_down`, `victory`, `game_over`, `level_up`).
* Music and ambient loops play from the start to `loop_end` (default: the end), then loop from `loop_start` (default 0); cues play once and take no loop points. `gain` (0 to 4, default 1) multiplies the profile's volume for that sound.
* The id must be one the game or the sample plays as that kind; the export lists them all.

## The battle presentation table

`battle_presentation` names a JSON file. Without one the built-in table plays; the export writes it out in full.

```json
{
	"layout": "front",
	"layouts": {"encounter:warden": "side", "area:kiln_lower": "side"},
	"timings": {"intro": 900, "opening": 1000, "round": 480, "forward": 180, "retarget": 320, "banner": 420, "animate": 280,
		"effect": 360, "result": 560, "back": 160, "cancel": 640, "roundend": 440, "notice": 700, "outcome": 900},
	"fallbacks": {
		"technique.damage.shock": {"animation": "cast", "effect": "burst_shock"},
		"combo": {"animation": "cast", "effect": "nova", "animate_ms": 420, "effect_ms": 520, "shake": 6, "flash": true}
	},
	"actions": {
		"spark": {"effect": "spark_bolt", "cue": "hit", "effect_ms": 420},
		"tonic": {"animation": "item", "effect": "motes_heal"}
	},
	"changes": {
		"transform": {"effect": "nova_transform", "effect_ms": 960, "shake": 5, "flash": true},
		"transform.tide_colossus_core": {"cue": "critical"},
		"join": {"effect": "motes_join", "effect_ms": 720}
	}
}
```

* `layout`: how the stage is arranged, `front` (the default) or `side`. `front`: the backdrop fills the stage, the enemies stand in a row across the middle facing the camera (from five on in two ranks, the first ones in front and the rest smaller, further back in the gaps), the members are seen from behind along the bottom in marching order, partly behind a status strip of five equal slots (name, status icons, HP and TP numbers and bars, the command chosen or the state; an empty frame for a slot without a member), and the command window opens beside the member choosing. `side`: the enemies on the left, the party in a diagonal on the right, a panel per member and the command menu at the right. `layouts` overrides it per encounter (`encounter:ID`) or per area (`area:ID`), the encounter's first; the sample's `--battle-layout` flag and the `battlelayout` script verb override both. Steps, timings and log lines are the same in both layouts.
* `timings`: each step's milliseconds at 1x battle speed (see "Battle" in `bindings/README.md` for the steps).
* `actions`: by ability or item id (core's `abilities.json` and `items.json`; enemy techniques and skills are abilities too): `animation` (`attack`, `cast`, `item`, `defend` or `none`), `effect` (an effect id or `none`), `cue` (a sound played as the effect starts, any cue id), `animate_ms` and `effect_ms` (those two steps' lengths), `shake` (pixels, easing off over the effect) and `flash` (a white flash at the effect's start). An entry gives only the fields it changes; the rest come from its fallback.
* `fallbacks`: an id without an entry takes the most specific of `KIND.EFFECT.ELEMENT`, `KIND.EFFECT` and `KIND`. KIND is the ability's kind (`attack`, `technique`, `skill`, `combo`) or `item`, and `defend` for Defend; EFFECT is its first effect (`damage`, `heal`, `revive`, `status`, `cure`, `stat`, `other`); ELEMENT is the damage's element (`none`, `heat`, `chill`, `shock`, `gale`, `holy`; a basic attack takes its weapon's element from the hit). The built-in table has every key: attacks slash, damaging techniques and items burst in their element's color, combinations nova and flash, heals and revives rise in motes, statuses and stat changes ring, Defend raises a shield. Override any of them.
* `changes`: how the enemy formation's changes look, each a step of its own after the turn's results: `join` (a summon, or a replacement taking a fallen enemy's place: the newcomer fades in, rising into its formation place, and the stage spreads over that place when it is a new one), `merge` (an enemy that leaves into another slides into it and fades while that one changes form), `transform` (a next form: the old figure fades out as the new one fades in lit white, at full hit points with its own label, bar and statuses), and `instant_defeat` (the mark stamped on an enemy an instant defeat fells, for the whole result step, with "Felled!" over it). The fields are an action's; `effect` plays on the enemy brought or changed, `effect_ms` is the step's length (unused for `instant_defeat`, which lasts the result step), `animation` and `animate_ms` are unused. `CHANGE.ENEMY` overrides a change for the enemy it brings, changes into or marks (core's `enemies.json` ids), over the change's own entry. The built-in entries use the generated `motes_join`, `ring_merge`, `nova_transform` (with a 5-pixel shake and a flash) and `mark_defeat`.
* The battle speed scales every step; Confirm finishes the current step, holding fast-forward plays the round to its end. With the profile's reduced flash there is no flash and shakes go a quarter as far.

## Hot reload

F5 in the window (and `--watch-assets`, a moment after files stop changing) loads `assets.json` and every file again, keeps the session and the screens where they are, and logs what changed (`assets: reloaded, changed: tilesets/town.png`) and any new problem. Pictures change at the next frame; a sound that changed plays from its new file the next time it starts; the battle presentation table applies to the next round, its layout at the next frame.

## Round trip

The exported directory reproduces the generated look exactly. `make bindings` checks it: the smoke, actors, panels, battle and vehicles headless runs are played again with the export as `--assets`, and every frame's draws (each picture by a hash of its pixels, rectangles, text, clips) and the whole transcript must be identical to the generated run's. Every exported sound is read back and compared sample for sample with the generated one.

Two things are not part of it. The UI skin's frame, panel border, cursor and bars are drawn with rectangles, and a window's top band and drop shadow depend on its size, which a nine-slice cannot reproduce at every size; the export writes pictures of them as a starting point with `"enabled": false`. Fonts are system files, so the export names none.

## Not replaceable yet

The title screen's traveler and creature, the motion marks of the generated poses (nod and shake strokes), the field's click-walk ring, the dimming behind menus and panels, fades, the battle's hit flash and the name plates, tabs and list highlights (drawn with rectangles). Tracks, loops and cues without a file keep their generated sound.
