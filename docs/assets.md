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

The export writes each generated picture into sheets (`maps/` (each area as written, drawn from its tiles, with the section turned off), `tilesets/`, `characters/`, `portraits/`, `figures/` (with `party_back_ID.png`, the back figures), `field_enemies/`, `vehicles/`, `backdrops/`, `effects/`, `sprites/`, `status_icons/`, `command_icons/` (no built-in layout draws them), `title/` (the title's traveler and creature), `ui/`, `fonts/` (a placeholder bitmap font per language, with the section turned off)), every sound the game can ask for as 16-bit mono WAV at 22050 Hz (`audio/music/`, `audio/ambient/`, `audio/cue/`), the built-in battle presentation table (`battle_presentation.json`) and `assets.json`. It reads every file back and compares it with what it generated, then checks the directory. Run with that directory, the sample draws every frame exactly as it does without one (see [Round trip](#round-trip)); replace one sheet at a time, keeping its layout, or rewrite its entry.

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
	"backdrop_maps": [],
	"effects": [],
	"sprites": {},
	"status_icons": {},
	"ui": {},
	"audio": [],
	"battle_presentation": "battle_presentation.json",
	"battle_animation": "battle_animation.json",
	"battle_figures": {},
	"maps": {},
	"objects": [],
	"field": {},
	"vehicle_overlays": [],
	"bitmap_fonts": {},
	"title_art": {},
	"command_icons": {},
	"pictures": {},
	"keepers": {},
	"ui_layout": {"enabled": true}
}
```

`ui_layout` turns on the layout files in `ui/` (`ui/styles.json` and one file per screen), which place every window, text, list and bar of the screens and set the logical resolution, scaling, frames, cursors and timings. Without the entry the `ui/*.json` files present load; with `"enabled": false` none do. They are documented in [`ui-layout.md`](ui-layout.md).

## How big pictures are drawn: scale, natural size and the foot row

The sample lays everything out on a grid of 16-pixel units: a field cell is 16 units drawn at 48 screen pixels (3 pixels a unit), a battle figure's slot is 16 units drawn at the slot's size (front layout: 144 for members, 176, 160, 136 or 112 for enemies by rank; side layout: 80 for members, 128 for enemies, 128 and 160 for vehicles). A file without `scale` is drawn as the generated pictures are: its pixels at the slot's factor (a 16x16 frame fills the slot, a 16x24 one rises half a cell above it), which for a figure is 9 screen pixels a file pixel for members in front and 11 for front-rank enemies, so a full-resolution sheet comes out enormous.

* `scale` (characters, figures, field enemies, vehicles, effects, tilesets): file pixels per unit, any number above 0 up to 64, whole or fractional. `"scale": 1` means one file pixel is one unit, the generated pictures' size; `"scale": 4` means a 64x64 frame takes the room of a 16x16 one. The screen pixels per file pixel are the slot's factor over the scale, snapped so every file pixel covers the same whole number of screen pixels: from 1 up, to the nearest whole number (halves up); below 1, to the nearest whole fraction 1/n. So a sheet keeps crisp pixels at the cost of drawing a little larger or smaller than its slot when the factor is not whole: in the field (3 pixels a unit) a scale of 3 draws a 48x48 frame at exactly 48 pixels, a scale of 2 draws a 32x32 frame at 64 (1.5 snaps to 2) and a scale of 4 a 64x64 frame at 64 (0.75 snaps to 1). Drawing is nearest-neighbor everywhere (`SamplerState.PointClamp`).
* Another cell size: the layouts' styles may set the field's cell (`"params": {"field_cell": 16}`, [`ui-layout.md`](ui-layout.md), "Params"); the field's factor is then that cell over 16 and the same snapping applies to it, so file pixels stay whole there too. At 16 (1 pixel a unit) a 16x16 frame draws at 16, a scale of 3 draws a 48x48 frame at 16 (1/3 exactly), a scale of 2 a 32x32 frame at 16 and a scale of 1.5 a 24x24 frame at 12 (2/3 snaps to 1/2). Pick a scale that divides the cell's factor or that the factor divides (at 32, 2 pixels a unit: scales 1, 2 and 4 stay exact, 3 draws a 48x48 frame at 24). A comic panel's cell is the field's times its camera's zoom, snapped the same way. Battle figure slots are the battle layouts' `party_size`, `vehicle_size` and `enemy_size`, and effects their `effect_scale`.
* Effects are drawn at 3 screen pixels a file pixel (4 on enemies in the front layout), so an effect sheet's `scale` gives 3 (or 4) over the scale, snapped the same way: a 64x64 sheet at `"scale": 2` draws at 128 in both cases (1.5 snaps to 2). Tiles lie edge to edge and fill their cell, so a tileset's `scale` must make a tile exactly one cell (below, [tilesets](#tilesets)).
* Natural size (`battle_figures.reference_height`, below): a figure entry without `scale` is drawn at a whole multiple of its own pixel size, the same for every slot and layout and independent of the window: the multiple is the logical screen's height (720 unless the layout's styles set another resolution, [`ui-layout.md`](ui-layout.md)) divided by the reference height, rounded down, at least 1 (at 720: 224 gives 3, 180 gives 4, 240 gives 3, 480 gives 1). The window then scales the whole frame as it does everything else.
* `natural` (figures): `true` or `false` opts one figure in or out of its natural size whatever the manifest-wide setting does: `false` draws it as a file without `scale` is drawn when there is no reference height (the slot's factor), so a sheet drawn for the slots can stand beside figures drawn at their natural size; `true` asks for it, which needs `battle_figures.reference_height` and does not go with `scale` (both are problems). Left out, a figure without `scale` follows `reference_height`.
* `foot` (characters, figures, field enemies, vehicles): the frame row, counted in file pixels from the frame's top (1 to the frame's height), that stands on the ground line; left out, the frame's bottom. Rows below it hang under the line, so figures of different sizes and paddings stand on the same ground. The figure's drawn rectangle is what the target pointer, the effects, the floating numbers and the retarget slide use, and the enemies' labels, bars and status icons sit under the ground line, so all of them follow a scaled figure.
* `shadow` (characters, figures): `false` leaves out the generated ground shadow under that figure or person; figures take `battle_figures.shadows` when they leave it out. In battle the shadow is as wide as the drawn figure.

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
* `scale` (optional): file pixels per unit, as for figures, with `tile` defaulting to 16 times it. A tile always fills exactly one cell, so the snapped factor (the field's cell over 16, over the scale; 3 over it at the default 48) times `tile` must be the field's cell (48 pixels unless the layouts' styles set `field_cell`): `{"kind": "town", "image": "tilesets/town_x3.png", "scale": 3, ...}` (48-pixel tiles at one screen pixel a file pixel), 24 at 1.5 or 96 at 6; a 32-pixel tile at scale 2 would draw 64 pixels a cell and is a problem. The check then guarantees every file pixel of the tileset covers the same whole number of screen pixels; without `scale` a tile of any size is stretched to its cell. In a panel whose camera zooms to another cell size the tiles fill the cell either way. At a 16-pixel cell a 48-pixel tile at scale 3, a 32-pixel one at 2 and a 16-pixel one at 1 fill it; `--check-assets` checks against the styles' cell (`a 48-pixel tile at scale 3 draws 24 screen pixels a cell, not the field's 20`).
* `animated` (optional): frame runs that stand in for a tile wherever the renderer picks it, `[{"tile": 0, "frames": [0, 29, 30], "ms": 300}]` (`ms` a number for every frame or a list). `tile` is one of the tileset's `floor`, `decor`, `wall_top` or `wall_face` frames (water animates by itself); every cell showing it plays the run in step, from the controller's clock. A kind's tileset animates every area of that kind without one of its own; an area's tileset animates that area only.
* `palettes` (optional): palette tracks over every tile picture the cells show (floors, decorations, walls, water, the animated tiles' frames), as an area's ([Motion](#motion)); the generated edge, bank, foam and shadow bands and the point objects keep their colors.
* An entry with `palettes` and no `image` names only `kind` or `area` and recolors the tiles that kind or area shows without it, the generated ones or its kind's file (an area's entry replaces its kind's tracks): `{"kind": "field", "palettes": [{"colors": {"grass": "#50984a"}, "steps": [{}, {"grass": "#98504a"}], "ms": 400}]}`. Any other field is a problem (`tilesets[3]: field 'floor' needs 'image': an entry without one recolors the tiles its kind shows with 'palettes' only`).

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
* `animations` maps script animation names (`anim fighter nod`; every name must be in `game.json`'s `presentation.animations`) to poses. A pose named after one of those animations plays for it with no entry here (the sheet above could leave out `"nod": "nod"`); an entry wins over the name. A pose neither named after an animation nor mapped is a problem.
* The generated motion of the animation (the hop of `jump` and `startle`, the startled mark, the strokes beside the head of `nod` and `shake`, the small bounces of names the sample does not know) plays over the frames unless the pose says `"motion": false`.
* An animation a sheet has no pose for plays on its standing frame with that motion only, and the transcript says so once per person and animation, when it first plays: `pose: character:medic has no pose 'shrug': the standing frame plays` (the check cannot know which animations the scenes ask of whom). The person's portrait, without a `portraits` entry, is its south standing frame.
* The state probe `sheetpose.ID` reads the pose the last frame drew the person in from its sheet, `-` for none.
* `scale`, `foot` and `shadow` as above ([How big pictures are drawn](#how-big-pictures-are-drawn-scale-natural-size-and-the-foot-row)): `{"id": "fighter", "image": "characters/fighter_x3.png", "frame": [48, 48], "scale": 3, ...}` draws 48x48 frames on the 48-pixel cell.

A sheet may draw fewer facings:

```json
{"id": "guide", "image": "characters/guide.png",
	"walk": {"south": [0, 1, 2], "east": [3, 4, 5]},
	"facing": {"mirror": {"west": "east"}, "fallback": "south", "start": "east"}}
```

* The facings `walk` gives are the ones the sheet has, each with three frames. Without `facing`, `walk` needs all four.
* `facing.mirror`: a facing the sheet lacks drawn as one it has, flipped left to right (`west` from `east`). A pose that leaves out a mirrored facing plays the mirrored one's frames flipped.
* `facing.fallback`: a facing the sheet has, drawn for every facing it lacks that `mirror` does not draw. Every facing must be given, mirrored or covered by the fallback.
* `facing.start` (script actors only): how the actor stands until a script turns or moves it. The game stands such an actor facing south and reports it as untouched (`ActorView`'s scripted flag); the sample draws it facing `start` instead, until the first `move` or `face`. Party members face where the party goes.
* The state probe `sheet.ID` reads how the sheet drew the facing the person was last drawn in, its source facing and its walk frames: `west: east mirrored 3 4 5`, `north: south 0 1 2`, or `generated` without a sheet. `expect actor ID X Y FACING` reads the facing drawn, `start` included.

### portraits

```json
"portraits": {"guide": "portraits/guide.png", "guide_smile": "portraits/guide_smile.png"}
```

By the line's portrait id (`game.json`'s `presentation.portraits`), else by the speaker's id, else the character's south standing frame, else the generated look. Any size, drawn at 80 by 80 in the dialogue box and in the status, name entry and battle panels.

An id may instead have expressions, which the dialogue's `{portrait:ID:EXPRESSION}` tag shows ([`ui-layout.md`](ui-layout.md), "Dialogue control tags"):

```json
"portraits": {"guide": {"image": "portraits/guide.png", "expressions": {"smile": "portraits/guide_smile.png", "frown": "portraits/guide_frown.png"}}}
```

* `image` (optional): the id's own portrait, as the flat form gives it. `expressions`: each name (letters, digits and `_`) to a file. An entry needs one of the two.
* `{portrait:guide:frown}` shows the expression. An expression the entry lacks shows what `{portrait:guide}` shows and logs one `tag problem:` line the first time the run meets that `ID:EXPRESSION`.
* Asset keys: `portrait:ID`, `portrait:ID:EXPRESSION`. The state probe `shownportrait` reads the key of the dialogue's portrait (`portrait:guide:smile`, `person:guide` for the person's frame, `off`).

### figures

Battle figures, by character or vehicle id (`"side": "party"`) or enemy id (`"side": "enemy"`). In a vehicle battle only the vehicle's figure stands on the field (the members ride inside; their panels stay); without a file a vehicle's figure is its west-facing field frames, bobbing.

```json
{"id": "fighter", "side": "party", "image": "figures/party_fighter.png", "frame": [16, 16],
	"idle": {"frames": [0], "ms": [400]},
	"animations": {"attack": {"frames": [1, 0], "ms": [140, 140]}, "cast": {"frames": [2], "ms": [280]}, "defeat": {"frames": [3], "ms": [400]}}}
```

`idle` loops. The named animations are `attack`, `cast`, `item` and `defend` (as the presentation table's `animation` names them, for enemies too), `hurt` (a hit) and `defeat` (a fallen party member, and an enemy's fallen pose while it fades out); each plays once and holds its last frame, and one left out plays `idle`. The battle adds its own motion on top, the same for files and generated pictures: stepping forward and back, a lunge with `attack`, a glow with `cast` and `item`, a shake and a red flash when hurt, a fade when an enemy falls. The party faces west, toward the enemies on the left.

A figure may have poses of its own beside those: any name of lower-case letters, digits and `_` starting with a letter, but `idle` (`"animations": {"attack": {...}, "flourish": {"frames": [2, 1, 2], "ms": [90, 90, 100]}}`). The presentation table's `animation` plays one by name ([The battle presentation table](#the-battle-presentation-table)); like the others it plays once and holds its last frame. The state probe `figureposes` reads each figure the last frame drew in a pose other than `idle`, by label (`Test Fighter flourish, Gel Mite B wobble`), or `none`.

`"view": "back"` (default `"side"`) makes an entry the party member or vehicle seen from behind, which the front battle layout draws (below, [The battle presentation table](#the-battle-presentation-table)); the side layout keeps using the `"side"` entry, so an id can have both. A back figure takes the same animations; the generated set is the person's look as head and shoulders, 16 by 16 drawn at 9x: `idle`, `attack` (the right arm raised), `cast` (both arms up), `item` (a hand up holding a pale bottle), `defend` (the head lowered, braced), `hurt` (the head knocked aside) and `defeat` (slumped, the head sunk between the shoulders), one frame each in that order (`attack` plays frames 1 then 0). A vehicle's generated back figure is its north field frames, bobbing. The front layout's motion: a member rises out of the line instead of stepping sideways (the member choosing a command already a little), an enemy comes toward the camera. A back figure's key is `figure:back:ID`.

Figures take `scale`, `foot` and `shadow` ([How big pictures are drawn](#how-big-pictures-are-drawn-scale-natural-size-and-the-foot-row)):

```json
{"id": "gel_mite", "side": "enemy", "image": "figures/gel_mite_x4.png", "frame": [64, 64], "scale": 4,
	"idle": {"frames": [0, 1], "ms": [400, 400]}, "animations": {"defeat": {"frames": [2], "ms": [400]}}}
{"id": "rust_hound", "side": "enemy", "image": "figures/rust_hound.png", "frame": [48, 60], "foot": 48, "shadow": false, "idle": {"frames": [0, 1], "ms": [400, 400]}}
{"id": "medic", "side": "party", "view": "back", "image": "figures/medic_back.png", "natural": false, "idle": {"frames": [0], "ms": [400]}}
{"id": "fuse_beetle", "side": "enemy", "image": "figures/fuse_beetle.png", "frame": [48, 60], "foot": 48, "natural": true, "idle": {"frames": [0, 1], "ms": [400, 400]}}
```

`palette` (optional) gives the picture's colors in order, so a battle animation's color transform can recolor them by index ([`battle-animation.md`](battle-animation.md), "Colors and color transforms"): a list of 1 to 256 `#rrggbb`, or a PNG strip in the asset directory whose top row holds them left to right (at most 256 pixels wide; reloaded with the rest). Without it, index entries of a palette do nothing on the figure. A list that is not colors, a missing strip or one wider than 256 pixels is a problem and leaves the entry out.

```json
{"id": "gel_mite", "side": "enemy", "image": "figures/enemy_gel_mite.png", "frame": [16, 16], "idle": {"frames": [0, 1], "ms": [400, 400]},
	"palette": ["#73d15e", "#1a1420", "#47813a", "#c0eab6"]}
```

With a battle animation file, an animation of a figure entry beyond the six named ones that no script's `pose` or `react` track, no figure state and no presentation table entry plays is a problem (`assets: assets.json: figures[5].animations.flourish: no battle animation script or figure state plays 'flourish'`).

### battle_figures

```json
"battle_figures": {"reference_height": 224, "shadows": true}
```

* `reference_height` (1 to 720, optional): the logical screen height the figures without a `scale` were drawn for; each is drawn at the natural multiple (above), unless its entry says `"natural": false`. Figures with a `scale`, and generated figures, are not affected.
* `shadows` (default true): whether figures cast the generated ground shadow, generated ones included, unless an entry's `shadow` says otherwise.

### field_enemies

Visible enemies in the field, by encounter id: `{"encounter": "training", "image": "field_enemies/training.png", "frames": [0, 1], "ms": 280}` (`ms` a number for every frame or a list).

A script actor whose id is an encounter id (`game.json`'s `actors` holding `"marsh"`, with a talk point named after it) is drawn as that encounter's field enemy, this entry's frames or the generated creature, unless `characters` gives the id a sheet. It moves, turns, shows and hides like any actor, and plays the actor animations below with the creature's own picture (a pose's lift applies, its person frames do not). `scripts/make_field_data.py` writes a data copy with one, which `scripts/field_actors.txt` plays.

### Actor animations

A script's `anim ACTOR NAME` plays on actors and party members in the field. The names the sample knows, each of which a scene may use once it is in `game.json`'s `presentation.animations`:

| Name | Plays | One play |
| --- | --- | --- |
| `nod`, `shake`, `bow`, `wave` | the person's head or arms (generated poses) | 640, 600, 900, 840 ms |
| `jump`, `hop` | one hop | 480 ms |
| `startle` | arms up, a hop and the `!` | 560 ms |
| `fade_in`, `fade_out` | the sprite and its shadow fading in or out | 400, 500 ms |
| `dissolve` | the sprite fading as it breaks into rising motes | 700 ms |
| `blink_out` | the sprite blinking, faster and faster, then gone | 640 ms |
| `tremble` | the sprite shaking side to side, easing off | 600 ms |
| any other | two small bounces | 520 ms |

A sheet's pose named after the animation, or mapped to it in `animations`, plays its frames instead (above, characters); the sprite's own looks (fades, motes, blinking, the shake) still apply. `fade_out`, `dissolve` and `blink_out` leave the sprite undrawn once played, until another `anim` for it starts or the event ends; follow one with `hide` to keep it gone (`anim marsh dissolve` then `hide marsh`), and show one with a fade by `show marsh nowait` then `anim marsh fade_in`. A `move ... speed N` walks the sprite a cell per area step time divided by N. The placeholder data names only `bow`, `jump`, `nod`, `shake`, `shrug`, `startle` and `wave`, so its scenes draw as before.

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

```json
{"terrain": "water", "image": "backdrops/water.png"}
```

```json
{"id": "reeds", "image": "backdrops/reeds.png"}
{"flag": "fells.ash_storm", "area": "reed_path", "image": "backdrops/ash_storm.png"}
```

Each entry names exactly one of `kind`, `area`, `encounter`, `terrain` and `flag`; a `flag` entry may add `area`, and an entry with an `id` may name none, serving the backdrop maps only. `id` (any entry): the name backdrop maps give it. `flag`: one of `game.json`'s flags; the entry serves while the flag is set, only in its `area` when it names one.

Backdrop maps choose a backdrop by the cell the party stood on when the battle began, per area:

```json
"backdrop_maps": [
	{"area": "reed_path", "image": "backdrop_maps/reed_path.png", "colors": {"#40a050": "reeds", "#d0b070": "dunes"}},
	{"area": "tidewater", "regions": {"square": {"rect": [1, 1, 5, 4], "backdrop": "reeds"}, "quay": {"rect": [8, 3, 6, 3], "backdrop": "dunes"}}}
]
```

* Exactly one of `image` and `regions`, one entry per area.
* `image`: a picture of one pixel a cell, exactly the area's width and height in cells (the reed path, 20x9). `colors` maps each opaque color (`#rrggbb`) to a backdrop's `id`; a transparent pixel gives no backdrop. A pixel neither transparent nor opaque, or of a color `colors` leaves out, is a problem.
* `regions`: named rectangles, `rect` `[x, y, width, height]` in cells, each to a backdrop's `id`. Regions lie inside the area and do not overlap; cells outside every region give no backdrop.
* An id that names no backdrop entry is a problem.

The lookup, first match wins: the encounter's; then the first `flag` entry, in the manifest's order, whose flag is set (and whose `area`, if any, is the battle's); then the area's backdrop map at the party's cell; then the terrain's (`floor`, `water` or `soft`: the cell the party stood on when the battle began, so a battle aboard a vehicle on water takes `water`); then the area's; then the area kind's; else the generated one. The state probe `backdrop` says which key was used: `encounter:warden`, `flag:fells.ash_storm@reed_path` (`flag:FLAG` without an area), `map:reed_path:reeds` (a picture's cell, by the backdrop's id), `region:tidewater:quay` (a region, by its name), `terrain:water`, `area:kiln_lower`, `kind:field` or `generated`. Asset keys: `backdrop:` and the entry's key (`backdrop:id:reeds` for an entry with only an id). The export writes no backdrop maps, flag or named backdrops: nothing generated has them. In the side layout drawn over the battlefield above the party panels, 1280 by 420 (the generated ones are 320 by 105, at 4x), any size stretched to it. In the front layout drawn at 1280 by 480 over the stage, and its part below 62% of its height (the generated ground, under the horizon's shadow) drawn again, taller, from there to the bottom of the screen, behind the members and the status strip.

### vehicle_overlays

```json
"vehicle_overlays": [{"vehicle": "reed_skimmer", "image": "overlays/skimmer_rim.png"}, {"image": "overlays/any_hull.png"}]
```

A picture over the stage in a vehicle battle (a cockpit, a hull's rim): the vehicle's own (an id of core's `party.json` vehicles), else the entry without `vehicle`, which serves every vehicle. It is stretched over the stage, 1280 pixels wide down to the status strip (front layout, 580 pixels) or the panels (side layout, 420 pixels), drawn over the backdrop and every figure and under the effects, numbers, labels' windows, the strip, the panels and the menus; transparent pixels show the stage.

### effects

Frame runs drawn over each target of an action, at 3x (4x on enemies in the front layout; with `scale`, 3 or 4 over the scale, snapped as figures are), centered on its figure (on the head of a member seen from behind, and on its status slot in a vehicle battle, where members have no figure; in the front layout an effect on several targets starts on each a moment after the one to its left, sweeping along them): `{"id": "spark_bolt", "image": "effects/spark_bolt.png", "frame": [32, 32], "frames": [0, 1, 2, 3], "ms": 60}`, or from a sheet drawn twice as large, `{"id": "slash", "image": "effects/slash_x2.png", "frame": [64, 64], "scale": 2, "frames": [0, 1, 2, 3], "ms": 60}` (128 pixels on screen, in both layouts). The presentation table names effects by id; an id here replaces the generated effect of that id or adds a new one. The generated effects are `slash`, `burst` and `nova` with the suffixes `_heat`, `_chill`, `_shock`, `_gale` and `_holy` (or none), and `ring_status`, `ring_stat`, `ring_cure`, `motes_heal`, `motes_revive`, `motes_item`, `shield`, `motes_join`, `ring_merge`, `nova_transform` and `mark_defeat` (the last four for the presentation table's `changes`). An effect takes `palette` as a figure does (above, [figures](#figures)): `{"id": "burst_shock", "image": "effects/burst_shock.png", "frame": [32, 32], "frames": [0, 1, 2, 3, 4, 5], "ms": 60, "palette": "effects/burst_shock_palette.png"}`.

### title_art

The title screen's own pictures:

```json
"title_art": {
	"background": "title/sky.png",
	"logo": "title/logo.png",
	"layers": [
		{"image": "title/clouds.png", "frame": [80, 20], "frames": [0, 1], "ms": 900, "at": [0, 60], "scale": 4, "scroll": [-30, 0]},
		{"image": "title/birds.png", "frame": [12, 8], "frames": [0, 1], "ms": 250, "at": [960, 150], "scale": 4}
	],
	"traveler": {"image": "title/traveler.png", "frames": [1, 0], "ms": 260},
	"creature": {"image": "title/creature.png", "frames": [0, 1], "ms": 400}
}
```

* `background`: stretched over the title layout's `scene` slot (the whole logical screen in the built-in files) in place of the generated field scene and its dimming, behind the title and the screens opened from it (Load, Options, the sound test).
* `layers`: drawn over the background (or the field scene) in order, each a frame run (`frame`, 16x16 by default; `frames`; `ms`, default 200, a number or a list) looping from the controller's clock. `at` (`[x, y]`, logical pixels, default `[0, 0]`) is the top left of its frame, counted from the `scene` slot's top left, drawn `scale` (1 to 16, default 1) screen pixels a file pixel. `scroll` (`[x, y]`, pixels a second, default `[0, 0]`) moves it; along each axis it moves on, it repeats edge to edge across the slot, so layers at different speeds make a parallax. Nothing is drawn outside the slot.
* `logo`: the title layout's `logo` field ([`ui-layout.md`](ui-layout.md), "Title"). The built-in title draws it at its own size, centered where the game's title is written, and leaves the title's text out; draw it about 48 pixels tall and at most about 380 wide for the built-in box, or place it in your own `ui/title.json`.
* `traveler` and `creature`: frame runs (`frame` 16x16 by default, `ms` default 260 and 400) for the layout's `hero_frame` and `creature_frame`, drawn at 64 by 64 in the built-in box.
* `reference_height` (1 to 4320, optional): the logical screen height the logo, traveler and creature were drawn for. With it they draw at their natural size, as battle figures do: a whole multiple of their pixels, the logical height over the reference height rounded down, at least 1 (at 720: 360 gives 2, 240 gives 3). The title layout's `logo_w`, `logo_h`, `hero_w`, `hero_h`, `creature_w`, `creature_h` and `art_scale` fields give the sizes, and its `scene` slot, images and windows place them; the built-in box stands the traveler and creature on the bottom of their 64-pixel squares. Without it the sizes are as before.
* Every part is optional; a part left out keeps the generated look. An entry with a problem is left out whole.
* Asset keys: `title:background`, `title:logo`, `title:layer:N` (from 0), `title:traveler`, `title:creature`. The state probe `titleart` lists the parts loaded (`background logo layers 2 traveler creature`) or `-`, and `titlesizes` the sizes the title gives its layout (`logo 480x80 traveler 32x32 creature 32x32 scale 2`).

```json
"title_art": {"reference_height": 360, "logo": "title/logo.png", "traveler": {"image": "title/traveler.png", "frames": [1, 0], "ms": 260}}
```
* The export writes the generated traveler and creature (`title/traveler.png`, `title/creature.png`) with the frame runs the title steps through, so the round trip stays exact; the background, logo and layers have no generated picture.

### sprites and status_icons

```json
"sprites": {"shadow": "sprites/shadow.png", "bubble": "sprites/bubble.png", "alert": "sprites/alert.png", "pointer": "sprites/pointer.png", "pier": "sprites/pier.png", "platform": "sprites/platform.png"},
"status_icons": {"sleep": "status_icons/sleep.png", "poison": "status_icons/poison.png"}
```

A sprite may instead be an object of its `image` and `palettes`, palette tracks as an area's ([Motion](#motion)) over its picture, on the controller's clock, with the field's flags and area entry: `"shadow": {"image": "sprites/shadow.png", "palettes": [{"colors": {"band": "#202020"}, "steps": [{}, {"band": "#c02020"}], "ticks": 20}]}`. The field sets the clock each frame it draws; the battle sets it each frame it draws, on its own clock with the session's flags, an entry track (`clock: entry`) counting from the battle's start. The state probe `spritesteps` reads each such sprite's track steps as the last frame left them (`shadow:2`), field or battle.

The shadow under everyone, the speech bubble over talk points, the `!` over chasing enemies and startled people, the pointer over targets, finished dialogue lines and what the real-time field input's interaction prompt points at ([`ui-layout.md`](ui-layout.md), Field), the pier a port stands on (greyed while the port is closed), a moving platform (`Platforms()`, drawn a cell large at its pixel place, `PixelX` and `PixelY` scaled from `GameInfo.CellPixels` to the field's cell, under whatever stands on it); and an icon per status id (from core's `statuses.json`), drawn in the battle's panels and under the enemies, growing in when a status lands and blinking out when it ends.

### command_icons

```json
"command_icons": {"attack": "command_icons/attack.png", "technique": "command_icons/technique.png", "defend": "command_icons/defend.png"}
```

A picture per command a member can choose for the round: `attack`, `technique`, `skill`, `combo`, `item`, `defend`, `escape`. The battle layouts' `command_icon` item field shows the chosen one ([`ui-layout.md`](ui-layout.md), "Battle"); a command left out takes the generated one (a square in the command's color with its mark), which no built-in layout draws. Any size; the layout gives the size. Asset key `command_icon:COMMAND`. The export writes the generated ones.

### pictures

```json
"pictures": {"chart": "pictures/chart.png", "tower": "pictures/tower.png"}
```

Still pictures by name (letters, digits and `_`), which the dialogue's `picture` tags and the scripts' `picture show` (game.json's `presentation.pictures`) show in the dialogue layout's picture window, as one stack ([`ui-layout.md`](ui-layout.md), "The `tags` table" and "Dialogue: cases and the picture stack"); `{picture:chart}` shows the first. A script's picture without an entry is logged once and not drawn. Any size; the built-in window draws it at its size in pixels (`picture_w`, `picture_h`). Asset key `picture:NAME`; the state probe `picture` reads the name shown. Nothing generated has one.

### keepers

```json
"keepers": {"tidewater_stall": "dockhand", "tidewater_inn": "lodgekeeper"}
```

A shop's or rest point's id (from the area files) to the portrait id shown as its keeper: a portrait of `game.json`'s `presentation.portraits`, an actor or a character, drawn as the dialogue draws it (the `portraits` entry, else the person's front). The shop and inn layouts' `keeper` field gives it ([`ui-layout.md`](ui-layout.md), "Shop", "Inn"); the built-in layouts do not draw it, and a point left out has none (-1). The session names no actor for a point, so the map is the assets'. An unknown point or portrait is a problem.

### maps

Whole-area pictures in place of tile drawing, by area id. Without the section every area with `maps/AREA.png` in the asset directory gets one by convention:

| File | What it is |
| --- | --- |
| `maps/AREA.png` | The ground: the whole map as written in the area file, drawn in place of the tiles |
| `maps/AREA_above.png` | Optional, drawn over everyone standing on the map (roofs, treetops, arches): transparent where people show |
| `maps/AREA@FLAG.png` | Optional, one per flag of the area's `changes`: the map as it looks with that change, of which only the change's cells are used |

```json
"maps": {"enabled": true, "cell": 16, "areas": [
	{"area": "ashfall_market", "image": "maps/ashfall_big.png", "above": "maps/ashfall_roofs.png", "cell": 32,
		"changes": {"fells.bridge_mended": "maps/ashfall_bridge.png"}}
]}
```

* Alignment: every picture is aligned cell for cell to the area's grid, its top left pixel on cell (0, 0). `cell` (1 to 256, default 16; per entry over the section's) is the picture's pixels a cell, so its size must be exactly the area's width and height in cells times `cell` (the reed path, 20x9 cells, at 16: 320x144); any other size is a problem. The field draws a cell at 48 screen pixels (the styles' `field_cell`), so 16 draws at 3x and 48 at 1x, nearest-neighbor; at a 16-pixel cell 16 draws at 1x and 32 at half.
* Layering, bottom to top (with the motion's layers and frame runs, see [Motion](#motion)): the ground picture; the cells drawn over it (below); exits' arrows and doorways, step runes, the runes on every cell of an active tile step hook (a hook with `tile`, whose cells the map marks with its character), and conveyor belts; then everyone sorted by row as before (objects and their pictures, chests, signs, shops, beds, piers, helms, vehicles, actors, visible enemies, the party); then the above picture; then speech bubbles, the `!` marks and pose marks; then the developer overlays and windows. The generated wall shadows, water foam, barrier frost and water frames are not drawn on a picture area (draw them into the picture). Belts and tile step hooks' runes are drawn on a picture area as on tiles, generated or from `field`'s overlay pictures; when the picture already shows them, turn them off for the area with `field`'s `conveyors` and `tile_hooks` ([field](#field)).
* Map changes by flag: the picture shows the map as written. Each frame, for each change of the area in data order whose flag is set, a cell of that change shows the change's picture (`changes` above, or `maps/AREA@FLAG.png`) cut out at that cell, a later change winning a cell, as in the game. Any other cell whose terrain is not as written (a change without a picture, a barrier a vehicle broke) is drawn from the area's tileset, as without a picture. Only the changed cells are cut from a change's picture; the rest of it is never drawn.
* `enabled: false` loads none of it and turns the file convention off (the export writes it so: a still picture cannot reproduce the water's frames, so the export's map pictures are a starting point, not a round trip). An explicit entry replaces the convention for its area. A picture in `maps/` that neither an entry nor the convention names is a problem (a misspelled area, most likely).
* A change's picture or an above picture without a ground picture is a problem; the ground is required.
* The above picture covers everything sorted by row wherever it is opaque, standing or mid-step: the party and its followers, a vehicle parked or ridden, script actors (one at its talk point's `actor_at` too), wanderers, visible enemies and the points' objects. Only the speech bubbles, the `!` marks and the pose marks go over it. The probe `abovecover` checks it pixel by pixel; `scripts/above_cover.txt` plays Wren Yard and Wren Hollow under a checkered above picture.
* Asset keys: `map:AREA`, `map:AREA:above`, `map:AREA@FLAG`. The state probe `fieldpicture` reads `AREA [above] patched N tiled N` (cells drawn from change pictures and from tiles in the last frame) or `-`.

### Motion

An area's entry in `maps.areas` may move its pictures: palette tracks recolor them (and, by `targets`, its tiles, layers and people), frame runs play on rectangles of them, layers scroll behind the area and in front of everything on it, and idle loops animate people and objects standing in place. All four are optional, and an entry with any of them may leave out `image`, `above` and `changes`: the area then keeps its tiles (and its `maps/AREA.png` is not used, as with any explicit entry). `maps.enabled: false` turns them off with the pictures.

```json
"maps": {"areas": [
	{"area": "wren_yard", "image": "maps/wren_yard.png", "above": "maps/wren_yard_above.png",
		"palettes": [
			{"colors": {"deep": "#3060c0", "foam": "#a0c8f0"}, "steps": [{}, {"deep": "#3870d0", "foam": "#3060c0"}, {"deep": "#4080e0"}], "ms": 500},
			{"colors": {"leaf": "#285a28"}, "steps": [{}, {"leaf": "#5a8a28"}], "ticks": 30, "clock": "entry"},
			{"colors": {"grass": "#466e3c"}, "steps": [{"grass": "#6e463c"}], "flag": "cogway.loud"}
		],
		"animated": [
			{"cells": [7, 1, 2, 2], "image": "motion/fountain.png", "frames": [0, 1, 2], "ms": [200, 200, 400], "clock": "entry"},
			{"pixels": [16, 96, 16, 16], "image": "motion/lamp.png", "frames": [0, 1], "ticks": 12, "layer": "above", "unless_flag": "cogway.loud"}
		],
		"layers": [
			{"image": "motion/sky.png", "scroll": [0.5, 0.5], "bands": [{"rows": [0, 15], "speed": 8}, {"rows": [16, 31], "scroll": 0.25, "speed": -16}],
				"wave": {"axis": "lines", "amplitude": 2, "wavelength": 16, "speed": 8}},
			{"image": "motion/rain.png", "place": "front", "speed": [0, 60], "wave": {"axis": "columns", "amplitude": 1, "wavelength": 32}, "flag": "cogway.loud"}
		]}
]}
```

Timing, the same for all four (and for the palette tracks of tilesets and sprites):

* `ms` or `ticks` (60 Hz frames; not both): a number for every step or frame, or a list with one for each (`ms` 1 to 60000, `ticks` 1 to 3600). A single step or frame needs neither.
* A random length: `{"min": N, "max": N}` (whole numbers in the field's unit, min at most max) in place of the number, for every step, or in place of one entry of the list: `"ms": [{"min": 100, "max": 900}, 200]`. Each time the step plays it lasts a length drawn in whole milliseconds (or ticks) from that range. The draw is not random from run to run: it comes from the session's seed (`--seed`, default 1), the piece (its area, list and index: `area:wren_yard.palettes[3]`; a tileset's or sprite's track by its place in the file) and the count of loops the piece has played, so the same seed shows the same frames on every run, headless or windowed, and a different seed other lengths. The piece keeps its place between frames and finds the next step from there; a clock that goes back (a new entry on the entry clock) starts it again from its first loop.
* `clock`: `global` (default), the controller's clock, so the area shows the same phase whenever it is visited; `entry`, which starts at 0 each time the party enters the area. Both run from the frame time the session gets, never the wall clock, so headless runs repeat exactly.
* `delay` (milliseconds, 0 to 600000, default 0; only with `clock: "entry"`): the piece is off that long after the party enters, then starts at its first step or frame: a palette track leaves its colors alone, a frame run or layer is not drawn, an idle loop leaves the standing frame. `delay` on the global clock is a problem.
* `flag` and `unless_flag` (optional, flags of `game.json`): each is one flag or a list of flags. The piece shows only while every flag of `flag` is set and no flag of `unless_flag` is (`"flag": ["cogway.loud", "kiln.upper_seen"]` needs both; `"unless_flag": ["kiln.draft_felt", "hollow.met"]` goes off when either is set). A palette track that is off leaves its colors alone; a frame run or layer that is off is not drawn. An empty list, a flag named twice in one list or a flag in both is a problem.

Palette tracks (`palettes`, a list; also on [tilesets](#tilesets)):

* `colors` names the source colors (`#rrggbb`, each different); `steps` is a list of at least one step, each an object giving some of those names a replacement color. A step that leaves a name out keeps that color as drawn; `{}` shows the picture unchanged. One step is a still recolor. The steps loop.
* Replacement is exact and done once: an opaque pixel whose color is a source takes that step's replacement, with no blending and no chaining (above, the foam takes the old deep blue in step 1, and that blue is not then replaced by the deep's step). Pixels that are not fully opaque keep their color. It is the battle animations' `palette` recolor.
* An area's tracks recolor its ground picture, its change pictures, its above picture and its frame runs' frames; a tileset's recolor its tile pictures. When several tracks that show name the same color, the first in the list wins.
* `targets` (an area's tracks only; default `["ground", "above", "runs"]`, today's): what the track recolors, any of `ground` (the ground picture and the change pictures), `above`, `runs` (the frame runs' frames), `tiles` (every tile the area's cells show, generated or from a tileset; an area's tracks naming `tiles` come before the tileset's own, so they win a color both name), `layers` (every layer of the area), `layer:NAME` (the layer whose `name` is NAME) and `actors` (the people drawn on the field: the party, its followers, script actors, wanderers, an actor wearing an enemy's figure; not their shadows, nor objects that show a person). Each target takes, in list order, the tracks that name it.
* `people` and `sheets` (lists of actor or party member ids; with `actors` in `targets`): the `actors` target recolors only a person whose id is in `people` or who wears a sheet in `sheets` (a script's `look` puts another's pictures on an actor, so `sheets` follows the pictures and `people` the person). Without either, every person.

```json
"palettes": [
	{"colors": {"pink": "#ff00ff"}, "steps": [{"pink": "#00ffff"}], "targets": ["layer:marker"]},
	{"colors": {"coat": "#c0a000"}, "steps": [{"coat": "#ff8000"}], "targets": ["actors"], "people": ["yard_lamplighter"]},
	{"colors": {"hat": "#e0e0e0"}, "steps": [{"hat": "#2020ff"}], "targets": ["actors"], "sheets": ["fighter"]},
	{"colors": {"floor": "#3a3642"}, "steps": [{"floor": "#00a000"}], "targets": ["tiles"]}
]
```

Frame runs (`animated`, a list):

* The rectangle is exactly one of `cells` (`[x, y, width, height]` in cells) or `pixels` (the same in the entry's picture pixels, `cell` a cell); it must lie inside the area.
* `image` is a sheet cut into frames the rectangle's size in picture pixels (a 2x2-cell run at 16 pixels a cell takes 32x32 frames); `frames` lists the frames played, with their `ms` or `ticks`.
* `layer`: `ground` (default) draws it over the ground picture (or the tiles) and the change pictures, under the exits, belts and everyone standing; `above` over the above picture.

Layers (`layers`, a list), each a whole picture drawn at the entry's pictures' scale (the field's cell over `cell` screen pixels a picture pixel):

* `place`: `back` (default) or `front`.
* `scroll` (`[x, y]`, -16 to 16, default `[1, 1]`): how far the layer moves for each pixel the camera moves. 1 keeps it on the ground, 0.5 moves it half as far (parallax), 0 holds it on the screen. At 1 its top left pixel is on cell (0, 0).
* `speed` (`[x, y]`, picture pixels a second, default `[0, 0]`): an automatic drift on the layer's clock.
* `at` (`[x, y]`, picture pixels, default `[0, 0]`): moves the layer, so a layer that does not repeat stands where it is placed (at `scroll` 1, `"at": [48, 32]` puts its top left pixel on cell (3, 2) at 16 pixels a cell).
* `repeat`: `both` (default), `x`, `y` or `none`: the axes along which the picture repeats across the view.
* `name` (optional; letters, digits and `_`, one per layer of the area): what a palette track's `layer:NAME` target names.
* `bands` (optional): ranges that scroll sideways at their own `scroll` and `speed` (numbers, default the layer's x values): a sky in strips drifting at different rates. A band gives exactly one of `rows` (`[first, last]`, rows of the picture) or `lines` (`[first, last]`, screen lines: logical pixels at the layout's resolution, counted down from the top of the view the field is drawn in, which is the whole logical screen, or a panel's own top in a panel; 0 to 4319); all bands of a layer use the same one, and bands do not overlap. A band of rows moves those picture rows wherever the layer stands; a band of lines moves whatever row of the picture is drawn on those lines (a row goes with the band its top line falls in), so a layer that scrolls up and down passes its rows through the bands:

```json
{"image": "motion/stripes.png", "place": "front", "scroll": [0, 0], "repeat": "x", "bands": [{"lines": [0, 11], "speed": 0}, {"lines": [12, 23], "speed": 16}]}
```
* `wave` (optional): a sine over the picture's `lines` (each row moved sideways, `axis` default) or `columns` (each moved up and down): `amplitude` in picture pixels (0 to 1024, default 2), `wavelength` in picture pixels (1 to 4096, default 32), `speed` in picture pixels a second the wave travels (default 0). Each row or column moves a whole number of picture pixels. A wave over columns does not go with `bands`.

Idle loops (`idle`, a list): a person or object standing in place plays frames of its own sheet in place of its standing frame.

* Exactly one of `actor` (an actor's or party member's id; the person needs a `characters` entry) or `object` (an `objects` entry of the area, by its `point` or its `at` cell as `[x, y]`).
* `frames`: indexes into the person's sheet (its `characters` entry's image) or the object's (its closed look's image; for an object that shows a person, that person's sheet), the same frames whatever the facing; timing, `clock`, `delay`, `flag` and `unless_flag` as above.
* A person plays it only while standing still with no pose (a walk, a script's `anim` or a library animation wins); an object only while it shows its closed look. The first loop that names the person or object and shows wins.

```json
"idle": [
	{"actor": "yard_trader", "frames": [12, 13], "ms": 300, "clock": "entry", "delay": 500},
	{"object": [11, 5], "frames": [1, 2], "ticks": 10, "flag": ["cogway.loud", "kiln.upper_seen"]}
]
```

Layer order, bottom to top: the `back` layers in list order; the ground picture or the tiles, with the change pictures; the `ground` frame runs; exits, runes and belts; everyone sorted by row; the above picture; the `above` frame runs; the `front` layers in list order; the speech bubbles and marks; the developer overlays and windows. A back layer shows where the ground is transparent and around an area smaller than the view.

`--check-assets` reports each bad color, a color named twice, a step naming no source, missing or doubled timing, a random length that is not `{"min", "max"}` in range, an unknown clock, flag or area, a `delay` off the entry clock, a bad flag list, an unknown target, a `layer:NAME` naming no layer, an unknown person in `people` or `sheets` or either without the `actors` target, a rectangle outside the area, a sheet that does not divide into the rectangle's frames, a frame the sheet lacks, a bad `layer`, `place`, `repeat`, `scroll`, layer name, band (both or mixed `rows` and `lines`, lines out of range) or wave, an idle loop with both or neither of `actor` and `object`, a person without a `characters` entry or an object that no entry of the area has; a problem in any of an entry's motion leaves all of it out. Asset keys: `animated:AREA:N` and `layer:AREA:N` (N from 0, in list order). The state probe `fieldmotion` reads, for the area shown, `palettes S,S tiles S sprites NAME:S,S runs F,F back N front N` (each track's step, the tileset's tracks', each sprite's tracks', each run's frame, `-` for one its flags or delay keep off, and the layers drawn), then `idle F,F` (each idle loop's frame, `-` when off) when the area has idle loops, each part only when there are any, or `-`; `scripts/world_motion.txt` plays the example above, `scripts/motion_more.txt` a sprite's and the generated tilesets' tracks and a layer placed with `at` (`scripts/make_motion_more_assets.py`), and `scripts/motion_options.txt` the targets, people and sheets, idle loops, a delay, a random step, flag lists and bands of lines (`scripts/make_motion_options_assets.py`).

### objects

A picture of its own for one interactable or cell, in place of the one picture its kind shares:

```json
"objects": [
	{"area": "ember_steps", "point": "steps_console", "image": "objects/console.png", "frame": [16, 32], "frames": [0, 1], "ms": 300, "size": [1, 2]},
	{"area": "ember_steps", "point": "steps_tinker", "character": "mystic", "facing": "east"},
	{"area": "reed_path", "point": "reed_cache", "image": "objects/long_chest.png", "frame": [32, 16], "frames": [0], "size": [2, 1], "anchor": [0, 0],
		"open": {"image": "objects/long_chest_open.png", "frame": [32, 32], "frames": [0], "size": [2, 2], "anchor": [0, 1]}},
	{"area": "fell_vault", "at": [8, 4], "image": "objects/door.png", "frame": [16, 32], "frames": [0], "size": [1, 2], "open_flag": "fells.vault_door_open",
		"open": {"frames": [1]}}
]
```

* Where: `area` and exactly one of `point` (the id of a hook, chest, shop, rest point, port or helm of that area; the point's own picture, the sign, chest, stall, bed, rune, pier or helm, is replaced) or `at` (`[x, y]`, a cell with no point: a door, a statue, a lamp; nothing is replaced). A talk point an actor stands on is a problem: the actor is drawn from its `characters` entry.
* What: exactly one of `character` (a party member's or actor's id: that person's standing frame, from its sheet or generated look, facing `facing`, `south` by default, with the ground shadow; for townspeople hooks that are not actors) or `image` (a sheet cut into `frame`, 16x16 by default) with `frames` and `ms` (default 400 a frame; a number or a list), looping from the controller's clock.
* Size and anchor: `size` is `[width, height]` in cells (1 to 16, default `[1, 1]`); the frame is stretched over those cells (draw it at `size` x 16 x a whole number pixels to keep it crisp). `anchor` is the cell of the object, `[x, y]` from its top left, that stands on the point's or `at` cell (default: the bottom row's middle, rounded left). The object sorts with everyone else by the anchor's row, so a person on a higher row is drawn behind it and one on the anchor's row or below in front.
* States: the fields above are the closed look. `open` (`image`, `frame`, `frames`, `ms`, `size`, `anchor`, each left out taken from the closed look; a new size without an anchor takes the default anchor) is shown once the point's chest is opened, or while `open_flag` (a flag of `game.json`) is set; `open` needs one of the two. A door that a flag's map change opens takes the same flag.
* A talk point keeps its speech bubble, on its cell. A closed port draws nothing either way.
* Asset keys: `object:AREA:POINT` and `object:AREA:X,Y` (pictures only). The state probe `objects` lists each object drawn in the last frame as `KEY closed|open WxH frame N` or `KEY CHARACTER FACING`.

### field

```json
"field": {"wall_shadows": true, "water_foam": true, "areas": {"saltgrass": {"wall_shadows": false, "water_foam": false}}}
```

Switches for the generated touches the tile drawing adds: `wall_shadows` (the dark bands on floor below and right of walls) and `water_foam` (the bank band and the light foam on water's edges), both on by default; `areas` sets them per area id over the section's. The wall's edge-colored borders and the barrier frost stay. The state probe `fieldlook` reads `shadows on|off foam on|off` for the area shown.

Conveyors and tile step hooks have no picture of their own in the area's tiles or its whole-area picture, so the field marks them:

```json
"field": {
	"conveyor_overlay": {"image": "overlays/belt.png", "frame": [16, 16], "facings": {"north": [0, 1], "east": [2, 3], "south": [4, 5], "west": [6, 7]}, "ms": 150},
	"tile_hook_overlay": {"image": "overlays/pad.png", "frames": [0, 1], "ms": 400},
	"areas": {"cogway": {"conveyors": false, "tile_hooks": false}}
}
```

* `conveyors` (default true): draw each conveyor cell; without `conveyor_overlay` as generated belt slats that run toward its direction while its flags hold and stand still otherwise. `conveyor_overlay` (optional) draws a picture instead: per direction a frame run, every facing at least one frame, looping at `ms` (default 120) a frame while the belt runs and showing its first frame while it stands. Each fills its cell, on the ground under everyone.
* `tile_hooks` (default true): draw the step rune (the tileset's `rune`) on every cell of a tile step hook while its flags hold, as a step hook at a point shows it; `tile_hook_overlay` (optional: `image`, `frame`, `frames`, `ms`, default 300 a frame, looping) draws its frames instead. The cells are those the area file's map writes with the hook's character.
* `false` for either leaves the marks to the art: a whole-area picture with the belts and pads painted in (the belts then do not move). Both take a per-area value in `areas`.
* `exit_markers` (optional): `{"always": OVERLAY, "from_floor": OVERLAY}`, each with `image`, `frame`, `frames` and `ms` (default 300) as `tile_hook_overlay`: the picture drawn on an open exit whose `FieldExit.Fires` is that way (game.json's `exit_fires` or the exit's own `fires`), in place of the generated arrow (on the map's edge) or doorway (inside it); a way left out keeps the generated marker. A closed exit is drawn as before (its tile, with a door over a tile that is not a wall). The state probe `exitmarks` lists each exit the last frame drew as `X Y closed` or `X Y always|from_floor marker|arrow|door`.

```json
"field": {"exit_markers": {"always": {"image": "overlays/exit_always.png", "frame": [16, 16], "frames": [0, 1], "ms": 250}}}
```

* Asset keys: `conveyor_overlay`, `tile_hook_overlay`, `exit_marker:always`, `exit_marker:from_floor`. The state probes `fieldmarks` (`belts generated|overlay|off pads generated|overlay|off` for the area shown), `conveyors` (`RUNNING/CELLS`) and `tilehooks` (`ACTIVE/CELLS`, the tile step hooks' cells whose flags hold).

### ui

```json
"ui": {"enabled": true,
	"frame": {"image": "ui/frame.png", "border": 8, "scale": 1},
	"battle_frame": {"image": "ui/battle_frame.png", "border": 8},
	"panel_frames": [{"image": "ui/panel.png", "border": 4}, {"camera": "lookout_east", "image": "ui/panel_east.png", "border": 4}],
	"cursor": "ui/cursor.png", "bar_track": "ui/bar_track.png", "bar_fill": "ui/bar_fill.png",
	"fonts": {"en": {"file": "fonts/body.ttf", "scale": 1.0}, "ja": {"file": "fonts/cjk.otf", "scale": 1.1}}}
```

* `frame`: every window (menus, dialogue, toasts, banners, the battle's panels) as a nine-slice: corners `border` pixels square drawn at `scale`, edges and middle stretched. It is drawn untinted, exactly as the picture is (only a fading window's opacity applies), with none of the generated window's band or shadow.
* `battle_frame` (optional): the battle's windows (the layouts' frame style `battle`, [`ui-layout.md`](ui-layout.md), "Frames and cursors"), the same way, so the battle can be skinned apart from the menus; without it they take `frame`. Asset key `ui:battle_frame`.
* `panel_frames`: the border around comic panels, one with no `camera` for all and one per camera id (`game.json`'s `presentation.cameras`) for its panels.
* `cursor`: the menu cursor; `bar_track` and `bar_fill`: HP and TP bars, the fill tinted with the bar's color and stretched to its value's width.
* `page_arrow` (optional): a picture the dialogue and battle layouts read as their `page_arrow` image field (the dialogue's next mark, the battle result's `result_more`; [`ui-layout.md`](ui-layout.md)); without it the field gives the generated pointer. Nothing draws it unless a layout does, and the export leaves it out. Asset key `ui:page_arrow`. `"page_arrow": "ui/page_arrow.png"`
* `fonts`: per language (`game.json`'s `languages`) a TrueType or OpenType file, with the system faces after it for glyphs it lacks, and a `scale` for every text size. A bitmap font for the language ([bitmap_fonts](#bitmap_fonts)) draws before it; this font then draws the glyphs the bitmap font lacks.
* `"enabled": false` keeps the generated look and loads nothing of the section (the export writes it that way, see [Round trip](#round-trip)).

### bitmap_fonts

Fonts drawn from a picture of glyphs, per language and optionally per role:

```json
"bitmap_fonts": {"enabled": true, "fonts": [
	{"language": "en", "image": "fonts/en.png", "metrics": "fonts/en.json"},
	{"language": "en", "role": "battle_numbers", "image": "fonts/numbers.png", "metrics": "fonts/numbers.json", "scale": 1.5},
	{"language": "ja", "image": "fonts/ja.png", "metrics": "fonts/ja.json"}
]}
```

* `language`: one of `game.json`'s `languages`. `role` (optional): `dialogue` (the dialogue box: the line, the speaker's plate and a choice), `hint` (the dialogue box's key hint; a language without a `hint` font draws it in the `menu` role's font), `battle_numbers` (the numbers and words that float over battle targets), `narration` (the dialogue layout's `narration` window; a language without a `narration` font draws it in the `dialogue` role's font) or `menu` (all other text). A layout's text style can name a role too ([`ui-layout.md`](ui-layout.md)). A font without `role` serves every role of its language that has no font of its own; two fonts for one language and role are a problem. A language without a font draws in the system font (or `ui.fonts`) as before.
* `image`: the glyph sheet, a PNG. Glyphs are drawn tinted with the text's color, so draw them white (shades of gray for anti-aliasing) on transparent; a colored sheet multiplies with the text's color.
* `metrics`: the metrics file, below.
* `scale` (above 0, at most 64, default 1): the screen pixels a file pixel are the text size times `scale` over the metrics' `size`, snapped as figures' scales are (from 1 up to the nearest whole number, halves up; below 1 to the nearest 1/n), so every file pixel covers the same whole number of screen pixels. The sample's text sizes are 16 (small), 20 (body), 28 (titles) and 28 to 36 (battle numbers), all times the profile's text size: a font with `size` 10 draws body text at 2, titles at 3 and, at 150%, body text at 3. Close sizes can snap to the same factor (small and body both at 2 there); give a role its own font or `scale` to set them apart.
* Characters the font has no glyph for are drawn in the fallback font, the language's `ui.fonts` file or the system font, at the text size, in place: measuring, wrapping and the text's reveal count them as that font does. Spaces take the metrics' `space`; line breaks are never glyphs.
* With a bitmap font the dialogue box steps its lines by the font's line height times the factor.
* The check reads the language's text tables (`text/CODE.json` and `text/CODE.strings`, placeholders such as `{0}` left out) and reports a font that lacks glyphs they use as one problem line listing them (the first sixty and how many more): for a font without `role` every line, for `dialogue` the string tables' lines and the `ui.dialogue.` keys, for `battle_numbers` the digits, `+`, the `ui.battle.float.` keys and the status names, for `hint` the `ui.dialogue.next` and `ui.dialogue.skip` keys and the `ui.key.` and `ui.button.` keys, for `menu` every line. Unlike other problems, the font still loads: those glyphs draw in the fallback font.
* `"enabled": false` loads none of it (the export writes it so, below).
* Asset keys: `bitmap_font:LANGUAGE`, `bitmap_font:LANGUAGE:ROLE`. The `bitmapfonts` probe lists `hint FILE` only when the hint's font is not the menu's. The state probes `bitmapfonts` (`menu FILE, dialogue FILE, battle_numbers FILE` for the text language, `-` where none), `wrapped` (the dialogue box's lines as last wrapped: `COUNT: WIDTH ...`) and `fontmissing` (the characters of the line on screen the dialogue's font draws in the fallback font, or `none`).

The metrics file is JSON:

```json
{
	"size": 10,
	"line_height": 10,
	"baseline": 8,
	"space": 4,
	"advance": 6,
	"cell": [10, 10],
	"rows": ["ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789.,!?"],
	"glyphs": {
		"I": {"advance": 4},
		"W": {"advance": 8},
		"g": {"rect": [0, 40, 6, 12], "offset": [0, 2], "advance": 6},
		"灰": {"rect": [10, 40, 10, 10], "advance": 10}
	},
	"kerning": {"AV": -1, "To": -1}
}
```

* `line_height` (required, 1 to 256): pixels from one line's top to the next's. `baseline` (0 to the line height, default the line height): the row, from the line's top, the letters stand on; the fallback font's text is placed so its baseline falls near it. `size` (1 to 256, default the line height): the text size at which the sheet draws at one screen pixel a file pixel.
* `space` (default `advance`, else a third of the line height): how far a space moves the pen. `advance` (optional): the default advance of every glyph; left out, a glyph's own width.
* A grid: `cell` (`[width, height]`) and `rows`, one string per row of cells from the top of the sheet, each character the glyph in that cell, left to right (a space in a row is an empty cell). Each grid glyph is its whole cell, drawn from the pen position at the line's top.
* `glyphs`: per character (one character of the Basic Multilingual Plane) `rect` (`[x, y, width, height]` in the sheet; required unless the character is in the grid, where it replaces the cell), `offset` (`[x, y]` from the pen position and the line's top, -256 to 256, default `[0, 0]`) and `advance` (0 to 1024). A grid character with only `advance` or `offset` keeps its cell.
* `kerning`: per pair of characters (both glyphs of the font) the pixels, -64 to 64, the second moves toward the first (negative) or away.
* Unknown fields, a rectangle past the sheet, a character twice in the rows and a font with no glyphs are problems.

Text without spaces (Japanese) wraps between characters where it reaches the edge, keeping closing punctuation (`、。」` and the like) on the line it closes; this is the same with the system font.

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
* The id must be one the game or the sample plays as that kind; the export lists them all. A cue or music id that the dialogue layout's tags table names (an entry's or case's `id`, `ids`, `values` or `default`, [`ui-layout.md`](ui-layout.md), "Dialogue: cases and the picture stack") counts as played, so its entry passes the check and the tag plays it.

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
* `actions`: by ability or item id (core's `abilities.json` and `items.json`; enemy techniques and skills are abilities too): `animation` (`attack`, `cast`, `item`, `defend`, `none`, or the name of a figure pose of the actor's own, below), `effect` (an effect id or `none`), `cue` (a sound played as the effect starts, any cue id), `animate_ms` and `effect_ms` (those two steps' lengths), `shake` (pixels, easing off over the effect) and `flash` (a white flash at the effect's start). An entry gives only the fields it changes; the rest come from its fallback.
* `fallbacks`: an id without an entry takes the most specific of `KIND.EFFECT.ELEMENT`, `KIND.EFFECT` and `KIND`. KIND is the ability's kind (`attack`, `technique`, `skill`, `combo`) or `item`, and `defend` for Defend; EFFECT is its first effect (`damage`, `heal`, `revive`, `status`, `cure`, `stat`, `other`); ELEMENT is the damage's element (`none`, `heat`, `chill`, `shock`, `gale`, `holy`; a basic attack takes its weapon's element from the hit). The built-in table has every key: attacks slash, damaging techniques and items burst in their element's color, combinations nova and flash, heals and revives rise in motes, statuses and stat changes ring, Defend raises a shield. Override any of them.
* `enemies`: by enemy id (core's `enemies.json`), how that enemy's actions look, over the action's own entry or fallback: `attack` (its basic attack), `defend`, or an ability id (`"enemies": {"gel_mite": {"attack": {"effect": "burst_chill", "cue": "miss"}}}`). The fields are an action's. A combination keeps its own look.
* `changes`: how the enemy formation's changes look, each a step of its own after the turn's results: `join` (a summon, or a replacement taking a fallen enemy's place: the newcomer fades in, rising into its formation place, and the stage spreads over that place when it is a new one), `merge` (an enemy that leaves into another slides into it and fades while that one changes form), `transform` (a next form: the old figure fades out as the new one fades in lit white, at full hit points with its own label, bar and statuses), and `instant_defeat` (the mark stamped on an enemy an instant defeat fells, for the whole result step, with "Felled!" over it). The fields are an action's; `effect` plays on the enemy brought or changed, `effect_ms` is the step's length (unused for `instant_defeat`, which lasts the result step), `animation` and `animate_ms` are unused. `CHANGE.ENEMY` overrides a change for the enemy it brings, changes into or marks (core's `enemies.json` ids), over the change's own entry. The built-in entries use the generated `motes_join`, `ring_merge`, `nova_transform` (with a 5-pixel shake and a flash) and `mark_defeat`.
* Poses: an `animation` that names no standard animation is a figure pose (any figure's `animations` name: `"focus_shot": {"animation": "flourish"}`). A figure that has the pose plays its frames with the motion of the animation the entry would have had without it (its fallback's: `attack` for Focus Shot); one that lacks it plays that animation, and the transcript says so once per figure and pose, when it first plays: `pose: figure:party:fighter has no pose 'flourish': its attack animation plays`. `--check-assets` reports a pose no figure of the manifest has (`actions.focus_shot: no figure has a pose 'flourish': the attack animation plays`), and in `enemies` one the enemy's own figure lacks (a generated figure has only the standard ones); the entry then plays its animation. The presentation step keeps the animation's name (`animate Test Fighter attack`).
* The battle speed scales every step; Confirm finishes the current step, holding fast-forward plays the round to its end. With the profile's reduced flash there is no flash and shakes go a quarter as far.

`battle_animation` names the battle animation scripts' file ([`battle-animation.md`](battle-animation.md)), which time each action, effect, figure state, release, intro and outro frame by frame over this table; without it the built-in set plays, which reads every field of the table, so the table keeps working with no change. The export writes the built-in set in full. `--check-assets` adds `N battle animation scripts (M keys)` to its summary, or `the built-in battle animations`; F5 reloads the file and the files it includes, applied from the next round.

## Hot reload

F5 in the window (and `--watch-assets`, a moment after files stop changing) loads `assets.json` and every file again (bitmap fonts' sheets and metrics files included), keeps the session and the screens where they are, and logs what changed (`assets: reloaded, changed: tilesets/town.png`) and any new problem. Pictures change at the next frame; a sound that changed plays from its new file the next time it starts; the battle presentation table applies to the next round, its layout at the next frame.

## Round trip

The exported directory reproduces the generated look exactly, its layout files (`ui/`, the built-in set, turned on) included. `make bindings` checks it: the smoke, actors, panels, battle and vehicles headless runs are played again with the export as `--assets`, and every frame's draws (each picture by a hash of its pixels, rectangles, text, clips) and the whole transcript must be identical to the generated run's. Every exported sound is read back and compared sample for sample with the generated one.

The generated command icons are written too; no built-in layout draws them, so the round trip draws the same frames either way. Two things are not part of it. The UI skin's frame, battle frame, panel border, cursor and bars are drawn with rectangles, and a window's top band and drop shadow depend on its size, which a nine-slice cannot reproduce at every size; the export writes pictures of them as a starting point with `"enabled": false`. Fonts are system files, so the export names no TrueType font; it writes a placeholder bitmap font per language (`fonts/CODE.png` and `fonts/CODE.json`: printable ASCII as 5 by 9 pixel letters, every other character the language's text tables use as a distinct pattern of strokes, wide ones in a 9 by 9 box) with `"enabled": false`, since the generated look is the system font. Turned on, it covers every glyph the tables use.

## Not replaceable yet

The motion marks of the generated poses (nod and shake strokes), the field's click-walk ring, fades, the battle's hit flash and the name plates, tabs and list highlights (drawn with rectangles). Tracks, loops and cues without a file keep their generated sound.
