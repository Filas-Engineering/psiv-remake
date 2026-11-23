# Battle animation scripts for the MonoGame sample

Status: built. The sample (`bindings/monogame/Psiv.Sample`) plays every battle through these scripts: the built-in set (`battle/animation.json`, embedded) reproduces the battle presentation table's look draw call for draw call, and an asset directory's `battle_animation.json` is layered over it. This document describes what is built; [Changes from the first draft](#changes-from-the-first-draft) lists every field that differs from the design this document first was, and [Not built](#not-built) what of that design is not.

The presentation table says how an action looks with a handful of fields: a figure animation, an effect, a cue, two step lengths, a shake and a flash. Battle animation scripts say it as a timeline: where each figure moves and which frames it shows, which sprite effects play where, how figures, the backdrop and the screen change color, which sounds play on which frame, and on which frame each result (a number, a bar, a status icon, a fall) reaches the stage. Scripts live in the asset directory next to the presentation table. A built-in set is embedded in the sample and reproduces today's battle draw call for call, so nothing needs authoring, and every headless run, transcript and frame digest stays as it is.

Scripts only decide when and how results the session already resolved are shown. They never change a result, and the `battlesync` check keeps comparing the shown battle with the session at the end of every round.

## Decisions on the first draft's open questions

* Lookup order: a combination's own script (`combo:RECIPE`, then `ability:` of the combination's ability) wins over the scripts of its participants' abilities, which a combination turn never reads. Within an ability, `ability:ID/character:CHAR` and then `ability:ID/weapon:CLASS` win over `ability:ID`.
* Basic attacks and two weapons: `weapon:CLASS/HANDS/character:CHAR`, `weapon:CLASS/character:CHAR`, `weapon:CLASS/HANDS`, `weapon:CLASS`, one script per variant. CLASS is the weapon animation class: `weapon_classes` maps `equipment:ID` or `category:NAME` to a class name, the piece's entry first; a weapon no entry maps has its category as its class. HANDS is `both` when both hands hold weapons or the weapon takes two hands (`"hands": 2` in `equipment.json`), else `one`. The right hand's weapon gives the class, else the left hand's.
* Formation overrides: every turn key is tried with `/encounter:ID`, then `/count:N` (the enemies standing, 1 to 5, 5 meaning five or more), then bare. `placements` moves the enemies' places by encounter or by count (below).
* Frame unit: a script frame is exactly 1/60 s at the default rate; `settings.rate` (for example 59.92) sets another. Script time accumulates in whole clock units and is never rounded per render frame; the headless runner's 16 ms frames stay deterministic.
* Log lines: `settings.log_timing` is `step` (the built-in set's: every line as its step starts, so transcripts are as before) or `release` (each result's lines on the frame its result is released; the step's own lines as it starts).
* The level-up notice still names the member by display name; scripts for `step:notice.game.level_up` place their effects by screen points.

## What exists and what changes

Today:

* `ResolveRound` returns the round's typed events. `BattleTimeline.Build` groups them into staged steps: the intro, opening and round banners, then per turn `forward`, any `retarget`, `banner`, `animate`, `effect`, `result`, one step per formation change (`join`, `merge`, `transform`) and `back`, then `roundend`, notices, `cancel`, `sync` and the `outcome`. Each step's length comes from the table (`timings`, and `animate_ms` and `effect_ms` from the action's look).
* The timeline plays steps on the controller's clock, scaled by the battle speed. When a step starts, its events change `BattleShow` (bars start running, statuses land, figures fall), its log lines go to the log and the transcript, its cues play and a `present:` line names it.
* `SampleController.BattleStage.cs` draws the stage from the current step's kind and progress, with the motions, tints, effects, numbers, shakes and flashes written as formulas in code.

With scripts:

* The step list stays exactly as it is: the same steps in the same order with the same descriptions, log lines and `present:` lines. `expect present` reads the same.
* A script decides each step's length, everything drawn while it plays, the frame within the result step at which each result event is released to the stage, and which sounds play on which frames.
* The drawing formulas move out of `SampleController.BattleStage.cs` into the built-in script set, where they are data.

A script cannot add, drop, reorder or alter an event, change a number or a target, add a step to a round (only the battle-level transitions of [Battle intro and outro](#battle-intro-and-outro), with a nonzero length, add a step, outside the rounds), touch a window (the name plate, the strip, the menus and the log stay the layout's, [`ui-layout.md`](ui-layout.md)), or change music.

## Files

```
ASSETS/assets.json                  "battle_animation": "battle_animation.json"
ASSETS/battle_animation.json        selection, scripts, fragments, effect animations, figure states, numbers, transitions
ASSETS/animation/*.json             optional: files battle_animation.json includes
ASSETS/battle_presentation.json     the presentation table, unchanged; scripts read it as $look and $timing
Psiv.Sample/battle/animation.json   the built-in set, embedded in the sample
```

* `battle_animation` in `assets.json` names the file, as `battle_presentation` does. Without the entry the built-in set plays. A missing file is a problem and the built-in set plays.
* The file is layered over the built-in set entry by entry: a script, fragment, effect animation or transition id the file defines replaces the built-in one of that id whole; `select` keys, figure states and number styles replace the built-in ones key by key. So a file can hold one script, or replace `turn.default` and change every turn that has nothing more specific.
* `include` (optional): a list of further files in the asset directory, read in order, each with the same top-level fields except `version` and `include`. An id or key defined twice across the files is a problem; the first definition stays. Included files are watched and reloaded with the rest.
* Every field is checked: unknown fields, wrong types, unknown ids and values out of range are problems ([Check, export and hot reload](#check-export-and-hot-reload)). An entry with a problem is left out, and what it would have replaced keeps the built-in definition.

## Time

* The unit is the frame: 1/60 of a second at 1x battle speed, or 1/R with `settings.rate` R (1 to 240 Hz, fractions allowed: 59.92). Every time in the file is a whole number of frames, or a string `"Nms"` with whole milliseconds.
* The clock counts units of a millionth of a frame. A millisecond is R thousandths of a hertz units (60000 at 60 Hz, 59920 at 59.92 Hz), so frame and millisecond lengths are both whole. Each update adds `dtMs * speed * R / 100` units, where `speed` is the battle speed in percent (100, 150, 200, 300); the remainder of the division is carried to the next update, so the clock never rounds. The current script frame is the units divided by a million, rounded down.
* Old millisecond timings convert exactly: the built-in `forward` step of 180 ms is 10.8 frames. The built-in set keeps every step's length to the millisecond and every existing run's frame counts.
* The headless runner's frames are 16 ms: 0.96 script frames at 1x and 60 Hz, 0.95872 at 59.92 Hz (fifty runner frames are 48 script frames at 60 Hz and 47 at 59.92 Hz). The probes report script frames, so tests do not depend on this conversion.
* A progress ratio inside a step is computed from two integer unit counts as the float of the reduced fraction, so the float the drawing reads is identical to the old millisecond ratio, which the round trip depends on.
* There is no limit on a script's length; a script or phase longer than 36000 frames (ten minutes at 60 Hz) is a problem (a sanity limit).

A time (TIME) is one of:

| Form | Means |
| --- | --- |
| `12` | frames |
| `"180ms"` | milliseconds |
| `"$timing.STEP"` | the presentation table's length of that step (`forward`, `banner`, `result`, ...) |
| `"$look.animate_ms"`, `"$look.effect_ms"` | the turn's or change's look from the presentation table |
| `"$param.NAME"` | a script parameter |
| `"$step"` | the length the step has without a script (the table's, or 0 for a cancelled action of a fallen actor) |
| `"$phase"` | the whole length of the phase the track starts in |
| `"$rest"` | from the track's start to the end of the phase (or, in a reaction, the step) it starts in |
| `"40%"` | that share of the phase the track starts in (of the number's life in `numbers`) |

A start (AT) is a TIME counted from the script's start, or a phase or mark name with an optional offset: `"effect"`, `"effect+6"`, `"result-4"`, `"impact+2"`, `"end"`, `"end-10"`. The offset is a TIME without `$` references. An AT that falls outside the script is a problem.

## battle_animation.json

```json
{
	"version": 1,
	"include": [],
	"select": {},
	"scripts": {},
	"fragments": {},
	"effects": {},
	"figures": {},
	"numbers": {},
	"transitions": {},
	"reduced_flash": {}
}
```

| Field | What |
| --- | --- |
| `version` | required, 1 |
| `settings` | `rate` (frames a second, default 60), `log_timing` (`step` or `release`, default `step`), `flash_limit` and `event_cues_off` ([Timing control](#timing-control), [Releasing results](#releasing-results)), `solo` ([The member alone](#the-member-alone)) |
| `weapon_classes` | `equipment:ID` or `category:NAME` to a weapon animation class name |
| `placements` | `count:N` (1 to 8) or `encounter:ID` to the enemies' places, `[[x, foot], ...]` in logical pixels per formation place, or `[x, foot, "front"|"back"]` for a place in a rank (per-layout values allowed); the encounter's entry first, then the exact count, then `count:5` for five or more; a layout's `enemy_slot_N_x` still wins ([Placements](#placements)) |
| `include` | further files, above |
| `select` | key to script id: which script plays for a turn, a step, a formation change, a reaction or a battle-level moment ([Selection and chaining](#selection-and-chaining)) |
| `scripts` | id to script ([Scripts](#scripts)) |
| `fragments` | id to a reusable track list that scripts call ([Selection and chaining](#selection-and-chaining)) |
| `effects` | id to an effect animation: a sprite sheet effect with per-frame durations, anchor, scale, blend, color and travel ([Effect animations](#effect-animations)) |
| `figures` | scope to figure states: idle, step forward, attack variants, cast, hurt, fallen, victory, defeat kinds, status tints ([Figure states](#figure-states)) |
| `numbers` | result kind to the floating number's motion and color ([Floating numbers](#floating-numbers)) |
| `transitions` | id to a transition into or out of battle ([Battle intro and outro](#battle-intro-and-outro)) |
| `reduced_flash` | what the profile's reduced flash does to each track type, over the built-in policy ([Timing control](#timing-control)) |

Ids are lowercase letters, digits, `_` and `.`, 1 to 64 characters.

## Common values

### Distances

A distance (DIST) is one of:

* a number, or `"Nm"`: pixels scaled by the battle layout's `motion_percent` (`10`, `-4`, `"10m"`): at 50% a 10 moves 5 logical pixels. This is the unit of every distance before `px` existed, so old files move as they did.
* `"Npx"`: logical pixels, never scaled (`"40px"`): a 40 moves 40 logical pixels at any `motion_percent`.
* `"Nf"`: forward units. One forward unit is the distance a figure steps forward today, by layout and side: front layout 24 for a member, 20 for an enemy, 12 for a vehicle; side layout 44 for a member and a vehicle, 24 for an enemy. A script written in forward units moves sensibly in both layouts.
* `"Nh"`, `"Nw"`: that fraction of the subject figure's drawn height or width (`"0.5h"`), so the distance follows a figure's `scale`, natural size and frame size.

Which unit the built-in set and the stage use: the built-in scripts' lunges (10 front, 16 side), the hurt shake (6), the join's rise (36), the flier's bob (5), forward units and the screen shake's amplitude are scaled by `motion_percent`; a `shake` track's per-frame `offsets`, a backdrop's `offset`, `scroll` and `wave`, and every floating number field (`rise`, `stack`, `row`, `pad`, `shadow`) are logical pixels and never scaled. `shake.amplitude` and a `move`'s `kind: shake` `amplitude` take a number (scaled) or `"Npx"`; the number fields take a number or `"Npx"`, both unscaled.

```json
{"type": "move", "on": "actor", "at": "forward", "kind": "offset", "by": ["40px", 0]},
{"type": "move", "on": "party_others", "at": "forward", "kind": "offset", "by": [0, "40m"]},
{"type": "shake", "at": "result", "amplitude": "6px", "length": 10}
```

The forward axis points toward the other side: in the side layout along x (members toward the left, enemies toward the right); in the front layout along y (members up, out of the line; enemies down, toward the camera). An offset `[dx, dy]` is in screen axes unless the track says `"space": "forward"`, where x runs along the forward axis and y across it.

### Subjects

| Subject | Who |
| --- | --- |
| `actor` | every actor of the turn: the user, or every participant of a combination |
| `actor:N` | participant N of a combination in the event's order (0 to 3); `actor:0` is the user of any turn |
| `user` | `actor:0` |
| `targets` | each target of the turn, as the timeline lists them (a Defend's target is its actor) |
| `target:N` | target N (0 to 7) |
| `hit_targets`, `fallen_targets` | the targets that took damage, the targets that fell this turn |
| `party`, `enemies`, `all` | every figure of that side, or both |
| `party_others`, `enemies_others` | every figure of that side except the turn's actors (the members watching an attack, a combination's onlookers) |
| `allies` | every figure of the first actor's side except the actors |
| `changed` | in a change script: the enemy the change brings in or changes |
| `previous_form` | in a change script: the figure an enemy had before a transform or merge |
| `from`, `to` | in a merge: the enemy that leaves, the enemy it merges into |
| `self` | in a reaction script or figure state: the figure it plays on; in a projectile's `travel`, the instance's own figure |
| `place:N` | the enemy standing at formation place N (0 to 7) |
| `slot:N` | the party member in marching slot N (0 to 4) |
| `beside:SIDE:SUBJECT` | the figure of the same side nearest the subject's first figure on that side (`left`: a lower formation place, `right`: a higher one; marching slots for the party), passing over empty places: `beside:right:target:0` |
| `adjacent:SIDE:SUBJECT` | as `beside`, but only the place right next to it: nobody when that place is empty |

A subject that names nobody in this turn (a `target:2` of a single-target action, an `actor:1` outside a combination) makes its tracks draw nothing. The check reports `actor:N` with N above 0 in a script that no `combo:` key or combination shape selects.

### Anchors and points

An anchor is a point on a subject's drawn rectangle, the rectangle `Art.Stand` returns after `scale`, natural size and `foot` ([`assets.md`](assets.md), "How big pictures are drawn"):

| Anchor | Point |
| --- | --- |
| `center` | the rectangle's center |
| `feet` | the middle of the ground line (the foot row, not the frame's bottom) |
| `head` | a member seen from behind: 3/8 of the height down from the top (today's effect point); any other figure: 1/4 down |
| `top`, `left`, `right` | the middle of that edge |
| `auto` | `head` for a member seen from behind, `center` for everyone else (today's rule) |

A member with no figure (members in a vehicle battle, or a figure hidden by the layout) has its strip slot's `anchor` rectangle instead ([`ui-layout.md`](ui-layout.md), "Battle").

A point (POINT) is `SUBJECT:ANCHOR` (`"user:head"`, `"target:0:feet"`), `"enemies:center"` or `"party:center"` (the middle of every live figure of that side), `"row:front"` or `"row:back"` (the middle of that enemy rank, on its ground line; a single row is `front`), `"screen:X,Y"` in logical pixels or percentages of the logical frame (`"screen:50%,40%"`), or `"stage:center"` (the middle of the stage, above `stage_bottom`). The anchor `place` is a figure's place (its x and its foot row) without any offset. A point may also be an object `{"point": POINT, "offset": [dx, dy]}`, with DIST offsets.

### Colors and color transforms

A color is a `styles.json` color name (`text`, `accent`, `bad`, `good`, `tp`, ... [`ui-layout.md`](ui-layout.md), "Colors") or `#rrggbb` / `#rrggbbaa`.

Rendering is not paletted, so palette-style effects are color transforms applied to whatever a sprite, a figure or the backdrop draws. A color transform (CT) is an object:

| Field | Default | What |
| --- | --- | --- |
| `multiply` | `#ffffff` | each channel times this, 0 to 1 (a color, or `[r, g, b]` in 0 to 1) |
| `add` | `#000000` | added to each channel where the picture is opaque, 0 to 255 (scaled by the pixel's alpha, so transparent pixels stay transparent) |
| `alpha` | 1 | the picture's opacity, 0 to 1 |
| `tint` and `amount` | none | the multiply moved `amount` (0 to 1) of the way from white toward `tint` (a multiply-only tint, which is what the table's glow and hurt colors draw) |
| `palette` | none | `[[SOURCE, "#rrggbb"], ...]` (up to 64): each opaque pixel of exactly a source color is drawn in its replacement; with `keys` and `"ease": "step"` a palette changes per key. A source is a `"#rrggbb"` color, or an index (0 to 255) into the palette the picture's manifest entry declares ([`assets.md`](assets.md), "figures"): on figures and sprites only, and left out where the picture declares none or fewer colors |

Brightening uses `add`; darkening uses `multiply`. A transform with an `add` term draws the picture, then its white silhouette tinted with the added color additively.

```json
{"type": "color", "on": "targets", "at": "effect", "mode": "set", "to": {"palette": [[0, "#40c0ff"], [2, "#2060a0"]]}, "length": 40},
{"type": "sprite", "at": "effect", "effect": "burst_shock", "spawn": "targets", "color": {"palette": [[0, "#ff40ff"], [1, "#a020a0"]]}}
```

The first recolors each target's palette entries 0 and 2, whatever colors its picture has there; the second recolors the burst through the strip its effect entry names. Color-matched pairs (`["#73d15e", "#40c0ff"]`) work as before and mix with index pairs.

### Easing

`linear`, `in` (quadratic), `out`, `in_out`, `smooth` (smoothstep, `t * t * (3 - 2t)`, today's `Ease`), `sine` (`sin(pi t)`: out and back, ending where it started), `step` (jumps at the end).

### Conditions

`if` takes one condition or a list (all must hold). Each is evaluated on the turn's already-resolved events and can only choose what is drawn:

`hit` (a target took damage), `miss`, `critical`, `weak`, `resisted`, `immune`, `defeat` (a target fell), `multi` (more than one target), `single`, `party_actor`, `enemy_actor`, `vehicle` (a vehicle battle), `combo`, `escaped`, `element:ID` (the damage's element), `layout:front`, `layout:side`, `result_step` (the result screen is up), `transition_playing` (a battle-in or battle-out transition script runs), a boolean parameter (`"$look.flash"`, `"$param.NAME"`), each negated with a leading `!`.

### Per-layout values

Any value field (not `type`, `id` or `on`) may be `{"front": VALUE, "side": VALUE}`; the stage's current layout picks one. A track with `"layout": "front"` or `"side"` plays only in that layout.

## Scripts

### Kinds

| Kind | Selected by | Phases | Plays during |
| --- | --- | --- | --- |
| turn | the action of a turn (`ability:`, `enemy:`, `weapon:`, `shape:`, ...; `turn:escape`) | `forward`, `banner`, `animate`, `effect`, `result`, `back` | the turn's steps, one phase per step |
| step | a non-turn step (`step:intro`, `step:round`, `step:outcome.victory`, ...) | `step` | that step |
| change | a formation change (`change:join`, `change:transform.ENEMY`, ...) | `change` | the change's step |
| reaction | a released result on a figure (`react:damage`, `react:instant_defeat`, ...) | none; a `length` | from the release, on that figure |
| battle | a battle-level moment (`battle:transition_in`, `battle:backdrop`, `battle:transition_out`) | `cover`, `hold`, `reveal`; or none, looping | outside the rounds |

### Fields

```json
"spark.bolt": {
	"extends": "turn.default",
	"params": {"glow": "#fff080"},
	"phases": {"forward": 8, "banner": 20, "animate": 18, "effect": 16, "result": 32, "back": 8},
	"marks": {"launch": "effect", "impact": "result"},
	"remove": ["shake"],
	"tracks": []
}
```

| Field | What |
| --- | --- |
| `extends` | another script of the same kind: its phases, marks, params and tracks are inherited, then this script's fields apply over them. At most 8 deep; a cycle is a problem |
| `params` | name to a string, number or boolean, read as `$param.NAME` in this script, scripts extending it and fragments it calls |
| `phases` | each phase's length (TIME); a phase left out takes the parent's, else the built-in `turn.default`'s (`$timing.forward`, `$timing.banner`, `$look.animate_ms`, `$look.effect_ms`, `$timing.result`, `$timing.back`). A step script's `step` defaults to `$timing.STEP` of its step; a change script's `change` to `$look.effect_ms` |
| `marks` | name to an AT: names tracks can start from (`"impact+2"`). Phase names, `end` and mark names must not collide |
| `length` | a reaction or looping battle script's length (TIME, or `"$rest"`); a looping script repeats it |
| `tracks` | the timeline, below; a track with the `id` of an inherited track replaces it |
| `remove` | ids of inherited tracks to leave out |

A script has no length limit; one longer than 36000 frames is a problem.

### Turn phases and steps

* Each phase is the step of the same name: the step's length is the phase's length, and the step starts when the phase starts. Tracks are not clipped by phases: a sprite started in `effect` keeps playing into `result` until its own length ends. Every track is clipped at the script's end.
* Retarget steps are not part of the script. They play between `forward` and `banner` with their own length (`$timing.retarget`, or the `step:retarget` script); the turn's script clock holds while they play, so script frames never count them. Formation change steps play between `result` and `back` the same way, each with its change script, and the turn's clock holds.
* The battle leaves out phases as it leaves out steps today: `effect` when the turn has no results, no formation change and is not a Defend; `result` when it has no results. Tracks that start in a left-out phase do not play; later phases close up. A cancelled turn is one `cancel` step (a step script; length 0 when the actor is down, as today). An escape turn's script (`turn:escape`) has `forward`, `banner`, `result` and `back`, and `back` is left out when the party got away.
* The effect step's `present:` description names the effect of the first `sprite` track that starts in the `effect` phase, else the look's effect; the animate step's names the first `state` or `pose` track's state or pose in `animate`, else the look's animation. With the built-in set both read the look, as today.

## Tracks

Fields every track takes:

| Field | Default | What |
| --- | --- | --- |
| `type` | required | below |
| `id` | none | unique in the script; `extends` and `remove` match it; probes show it |
| `at` | 0 | AT: when it starts |
| `length` | per type | TIME |
| `if` | none | conditions |
| `layout` | both | `front` or `side` |
| `stagger` | 0 | on a subject of several figures, each later one starts this TIME after the one before; `"sweep"` is today's front-layout rule, `min(90 ms, effect_ms / (2 * targets))` |
| `order` | `event` | the order a stagger walks: `event`, `left_to_right`, `right_to_left`, `near_first` (closest to the user first), `far_first` |
| `reduced` | per type | what reduced flash does with it: `keep`, `drop`, `damp` (amplitudes and amounts times `reduced_flash.damp`, 0.25), or a replacement track object, which inherits `at` |
| `flash` | false | true: the track counts as a screen-wide flash toward the flash limit whatever it draws; a marked sprite is damped under reduced flash unless it has its own `reduced` ([Flashes by effect](#flashes-by-effect)) |

Tracks are stateless: what a track draws at script frame `t` depends only on `t`, the script's inputs (the turn's events, the look, the params) and the stage's geometry. That is what lets the battle jump to any frame (hurry, fast-forward, the previewer's seek) and still draw the same thing it would have drawn by playing there.

### `pose`

Shows a figure's frames.

| Field | What |
| --- | --- |
| `on` | subject |
| `pose` | a figure animation name: `idle`, the six built-in names (`attack`, `cast`, `item`, `defend`, `hurt`, `defeat`) or any name a figure entry in `assets.json` defines (below, [Figure states](#figure-states)). A figure without it shows `idle` |
| `frame` | instead of `pose`: one frame index of the figure's sheet |
| `loop` | false: plays once and holds its last frame; true: repeats over `length` |
| `length` | default the run's length, else `$phase` |
| `pose: "hidden"` | the figure is not drawn while the track plays, unless a `pose` track on the same figure that starts later is playing: that one lifts it |
| `pose: "shown"` | lifts an earlier `hidden` and changes nothing else |
| `place_of`, `offset` | the figure is drawn at another figure's place (a subject), moved by `offset` (`[dx, dy]` of DIST, per-layout values allowed) |

### `state`

Plays a figure state from `figures` (a pose, a motion and a color together).

| Field | What |
| --- | --- |
| `on` | subject |
| `state` | a state name or `"$look.animation"`; `attack` tries `attack:CATEGORY` first for a member whose right hand holds a weapon of that category (`attack:blade`) |
| `length` | default the state's own, else `$phase` |

### `move`

Moves a figure off its place. A figure's offset at frame `t` is defined by its move tracks in start order: each goes from where the one before left the figure (at its own start) to its own end point over its `length`, so moves chain without state.

| Field | What |
| --- | --- |
| `on` | subject |
| `kind` | `forward` (to `distance` along the forward axis from the figure's place), `home` (back to its place), `to` (toward `to`, `reach` of the way, stopping `stop_short` before it), `arc` (as `to`, rising `height` at the middle), `hop` (up `height` and down, ending where it started), `offset` (to the figure's place plus `by`), `enter` (from its place plus `by` to its place: formation entry), `exit` (from its place to its place plus `by`) |
| `distance`, `height`, `stop_short` | DIST; `forward` defaults to `"1f"` |
| `to` | POINT |
| `reach` | 0 to 1, default 1 |
| `by` | `[dx, dy]` of DIST |
| `space` | `screen` (default) or `forward` |
| `ease` | default `smooth`; with `sine` a move ends where it started |
| `length` | default `$phase` |
| `speed` | the move completes in 1/`speed` of its length and holds (the table's join rises at 1.6, the merge slides at 1.4) |
| `axis` | `both` (default), `x` or `y`: a `to` move on one axis only |
| `kind: sine`, `kind: shake`, `kind: bob`, `kind: pulse` | not chained: added to the figure's offset (`pulse`: its size) while they play; `sine` swings `amplitude` (DIST) on `axis` (`x`, `y`, `forward`) every `period` from `phase`; `shake` is `sin(t * frequency) * amplitude * motion * (1 - t)` (`decay: none` drops the last factor); `bob` hops `amplitude` away from the place and back every `period` from `phase` (`amplitude * abs(sin(pi * cycle))`, up on `y`); `pulse` grows and shrinks the picture by `scale` (0 to 1, default 0.1) of its size every `period` from `phase`, about the middle of its bottom edge |

A figure with no picture on the stage (a member in a vehicle battle) does not move. The shadow stays on the ground under a `hop` or `arc`.

### `sprite`

Plays an effect animation ([Effect animations](#effect-animations)): every field of an effect animation can be given here and wins over the animation's.

| Field | Default | What |
| --- | --- | --- |
| `effect` | required | an effect animation id, an effect id of `assets.json`, a generated effect id, or `"$look.effect"`; `none` draws nothing |
| `spawn` | `targets` | where one instance plays: a subject (one per figure: `each` of `targets`, `party`, ...), `all_targets` (one at the targets' middle), or a POINT |
| `anchor` | the animation's, else `auto` | on each spawned figure |
| `offsets` | none | `[[dx, dy], ...]`, the k-th instance taking entry k (the last repeating) |
| `jitter` | none | `[dx, dy]`: each instance moved up to that far, by a hash of the script id, the track and the instance index (the same every time) |
| `travel` | none | a projectile ([Effect animations](#effect-animations)) |
| `layer` | `effects` | `under_figures` (over the backdrop, under every figure), `over_figures` (over the figures and a vehicle battle's overlay), `effects` (the layout's `effects` slot, under the enemies' labels and the numbers), `over_labels` (over the enemies' labels and the target pointer, under the numbers: the instant defeat's mark), `top` (over the numbers, under the windows the layout draws after `effects`) |
| `length` | the animation's | an instance is gone after it; an instance staggered later ends with the track |
| `path` | none | `[[frame, dx, dy], ...]`: the instance's offset from its anchor, linear between keys (frames from the instance's start) |
| `fade_out` | none | `"N%"`: the instance fades out over the last N% of its length |
| `spawn: "columns"`, `column_y` | | one instance per target at its x and at the logical row `column_y` (default the stage's middle): a target-column effect |
| `scale: "large"` | | the layout's `effect_scale_large` |
| `reduced` | `keep` | with reduced flash: `drop` leaves the sprite out (it is not instantiated, so `animtracks` does not list it), `damp` draws it at `reduced_flash.damp` of its opacity and added light, or a replacement track. `reduced_flash.sprite` sets the default; whiteouts drawn as sprites obey reduced flash this way |

### `flash`

A full-screen flash: shorthand for a `color` track on the stage.

| Field | Default | What |
| --- | --- | --- |
| `color` | `#ffffff` | |
| `amount` | 0.55 | opacity at its start, falling linearly to 0 over `length` |
| `length` | `"25%"` | |
| `area` | `stage` | `stage` (from the top to the layout's `stage_bottom`, today's) or `frame` (the whole logical frame, over every window) |
| `reduced` | `drop` | |

### `shake`

| Field | Default | What |
| --- | --- | --- |
| `amplitude` | required | 0 to 32: a number (scaled by `motion_percent`), `"Npx"` (not scaled), or `"$look.shake"` |
| `decay` | `linear` | `linear` (to 0 over `length`), `none`, `out` |
| `axis` | `both` | `both`, `x`, `y` |
| `pattern` | `clock` | `clock`: today's jolt, `sin(clock * 0.09)` and `cos(clock * 0.13)` of the controller's clock in milliseconds; `script`: the same curves of the script's time in milliseconds, so a seek redraws it exactly |
| `length` | `$phase` | |
| `reduced` | `damp` | |
| `offsets` | none | per-frame `[dx, dy]` lists for `backdrop`, `figures` and `screen` (both), each frame taking the next entry, repeating |

When several shakes play at once the stage shakes by the largest, with the largest's pattern. The probe `battlejolt` reads the jolt the last frame drew (`-2,1`).

```json
{"type": "shake", "at": "result", "amplitude": 6, "decay": "none", "pattern": "script", "length": 20}
```

### `color`

A color transform on figures, the backdrop or the screen.

| Field | What |
| --- | --- |
| `on` | a subject, `backdrop` (both of the front layout's backdrop draws), `stage` (a fill over the stage) or `frame` (a fill over the whole frame) |
| `mode` | `set` (the transform for `length`), `flash` (from `to` back to none over `length`), `tint` (to `to` over `in`, held `hold`, back over `out`), `fade` (to `to` over `in`, then held to the script's end), `cycle` (through `colors`, `period` each, `smooth` blending between them), `keys` (`[[AT, CT], ...]`, eased between) |
| `to` | CT |
| `colors` | `[CT, ...]` |
| `period`, `in`, `hold`, `out` | TIME |
| `ease` | default `linear`; on `set` an ease scales the transform's amount over the length (the table's glow is `set` with `sine`); on `keys`, `step` jumps from key to key |
| `length` | default: `tint` its `in` + `hold` + `out`, `fade` to the script's end, otherwise `$phase` |
| `reduced` | default `damp` on figures; `drop` for `flash` mode on `backdrop`, `stage` and `frame`; `cycle` holds its first color |

On `backdrop` the whole transform applies: `multiply`, `alpha` (the backdrop over the stage's clear color), `add` (added light where the picture is opaque) and `palette`; with reduced flash a `cycle` holds its first color there too.

```json
{"type": "color", "on": "backdrop", "at": "effect", "mode": "set", "to": {"add": "#603000", "alpha": 0.85}, "length": 50}
```

On `stage` and `frame` a transform is a fill: `add` fills additively, `tint` and `amount` fill with that color at that opacity; `multiply` is not available there (the check says so).

### `backdrop`

| Field | What |
| --- | --- |
| `effect` | `restore` (crossfade back to the battle's own backdrop over `length`), `scroll` (`speed` `[x, y]` in pixels a frame; the backdrop wraps), `wave` (rows shifted sideways by `amplitude` pixels along a sine of `wavelength` pixels, moving one wavelength every `period`; drawn in bands of `band` rows, 2 to 32, default 4), `offset` (to `by` over `length`, eased), `swap` (crossfade over `length` to `image`, a PNG in the asset directory, or `key`, a backdrop key: `kind:field`, `area:kiln_lower`, `encounter:warden`, `terrain:water`, `generated`), `darken` (shorthand: `color` on the backdrop, `multiply` `#606060`), `tint` (shorthand: `color` on the backdrop with `to`) |
| `persist` | with `swap`: the swapped picture stays as the backdrop for the rest of the battle (until a `restore`); without it the swapped picture stays after its crossfade to its script's end, or until a later `restore` in the script crossfades back |
| `loop` | in a looping battle script, repeats with the script |
| `length` | default to the script's end; of a `swap` or `restore`, its crossfade |

Backdrop tracks apply in start order, so a restore crossfades back from the swap before it whatever order the script lists them in. An attack that darkens the stage as it swings and brings the battle's backdrop back as the attacker returns:

```json
{"id": "dark", "type": "backdrop", "at": "animate", "effect": "swap", "image": "backdrops/lo_swap.png", "length": 6},
{"id": "light", "type": "backdrop", "at": "back", "effect": "restore", "length": 8}
```

The probe `backdropfx` reads `swap` from the swap's start, `swap, restore` while the restore crossfades, and `none` after.

### `sound`

| Field | Default | What |
| --- | --- | --- |
| `cue` | required | any cue id (`Synth` makes a sound for every id; an `assets.json` cue replaces it), or `"$look.cue"` (nothing when empty) |
| `per` | `once` | `once`, `each` (once per figure of `spawn`, with the track's `stagger` and `order`), `release` (once per release unit, on its frame) |
| `spawn` | `targets` | for `per: each` |

A sound plays when the script reaches its frame by playing. A hurry or a fast-forward that jumps over its frame drops it.

### `release`

When results reach the stage ([Releasing results](#releasing-results)).

| Field | Default | What |
| --- | --- | --- |
| `at` | `result` | AT, or a list of ATs: unit k at entry k, the last entry plus `stagger` for each unit past the list |
| `targets` | `targets` | which targets' events this track releases |
| `unit` | `target` | `all`, `target` or `hit` |
| `order`, `stagger` | `event`, 0 | as for every track |

### `call`

Plays a fragment ([Selection and chaining](#selection-and-chaining)) from `at`, its frame 0 there.

| Field | What |
| --- | --- |
| `fragment` | a fragment id |
| `bind` | subject names inside the fragment to subjects here: `{"actor": "actor:1", "targets": "target:0"}` |
| `params` | parameter values for the fragment |
| `length` | clips the fragment |

### `transition`

Battle-level scripts only: draws a transition from `transitions` over its script's `cover`, `hold` and `reveal` phases ([Battle intro and outro](#battle-intro-and-outro)).

### `name_window`

The action name's plate on the turn's timeline. A turn script with any `name_window` track shows the plate only while one plays, opening and closing over `fade` (TIME); a script without one leaves the plate to the layout, as before.

### `react`

A reaction on each released target from its release frame: `pose`, `state` and `effect` (with any sprite field) start on every target of `targets` (default `targets`) a release of this step lets out, for `length` (default to the step's end).

## Effect animations

`effects` defines sprite effects with frame-exact timing on top of the sheets `assets.json` cuts. A sheet effect id (`assets.json`'s `effects`, or a generated one: `slash`, `burst_shock`, `nova`, `motes_heal`, `mark_defeat`, ...) can be played directly; it then plays its sheet's frames and milliseconds, anchored `auto`, at the layout's factor, as today.

```json
"effects": {
	"spark.orb": {"sheet": "spark_orb", "frames": [0, 1, 2, 3], "durations": 3, "loops": -1, "blend": "additive",
		"anchor": "center", "scale": "x1.5",
		"travel": {"from": "user:head", "to": "target:0:center", "frames": 16, "path": "arc", "height": "0.6h", "ease": "in",
			"trail": {"count": 3, "gap": 2, "alpha": 0.5}},
		"impact": "spark.pop"},
	"spark.pop": {"sheet": "burst_shock", "durations": [2, 2, 3, 3, 4, 6], "blend": "additive", "scale": "x1.25",
		"color": {"add": "#403000"}}
}
```

| Field | Default | What |
| --- | --- | --- |
| `sheet` | required | an effect id of `assets.json` or a generated effect |
| `frames` | the sheet entry's | frame indexes of the sheet, played in order; -1 draws nothing for that frame |
| `durations` | the sheet entry's milliseconds | TIME for every frame, or a list, one per frame |
| `loops` | 0 | extra plays; -1 repeats until the instance's length ends |
| `pingpong` | false | plays forward then back |
| `hold_last` | 0 | TIME the last frame stays |
| `anchor` | `auto` | where it sits on a spawned figure |
| `origin` | `[0.5, 0.5]` (`[0.5, 1]` on `feet`) | the point of the frame, in fractions of its size, put on the anchor |
| `offset` | `[0, 0]` | DIST pair from the anchor |
| `scale` | `layout` | `layout`: today's factor, 3 screen pixels a file pixel, 4 on enemies in the front layout and on formation changes; a number: that factor; `"xN"`: the layout's factor times N. Either way divided by the sheet's own `scale` and snapped as figures are ([`assets.md`](assets.md)), so every file pixel covers the same whole number of screen pixels |
| `blend` | `alpha` | `alpha` or `additive` |
| `color` | none | a CT, or `{"from": CT, "to": CT, "ease": E}` over the instance's life, or `{"keys": [[TIME, CT], ...]}` |
| `directions` | none | frame lists by travel direction, `left`, `right`, `up`, `down`: the dominant axis of the whole travel (from its `from` to its `to`) picks one, in place of `frames` (no rotation is drawn) |
| `flip` | `none` | `auto`: mirrored when traveling right and there are no `right` frames; `none`; `x` |
| `travel` | none | below |
| `impact` | none | an effect animation (or sheet id) spawned where and when the travel arrives |

`travel` makes the instance a projectile:

| Field | Default | What |
| --- | --- | --- |
| `from`, `to` | `user:auto`, `target:auto` (the instance's own target) | POINT |
| `frames` | required | TIME from launch to arrival |
| `path` | `line` | `line`, `arc` (rising `height`), `wave` (`waves` sine swings of `height` across the line) |
| `height` | `"0.5h"` | DIST |
| `ease` | `linear` | |
| `trail` | none | `count` (default 3) copies of the instance's current frame where it was `gap` (TIME, default 2 frames, at least 1), 2 `gap`, ... ago, each `alpha` (default 0.5) times as opaque as the one before; drawn in flight only, under the instance |

An instance with `travel` lives `frames` plus its `hold_last`; with `spawn: targets` there is one projectile per target, each launched by the track's `stagger`.

```json
"lo.glob": {"sheet": "burst_heat", "frames": [0, 1, 2], "durations": 3, "loops": -1, "blend": "additive", "flip": "auto",
	"directions": {"left": [3, 4, 5], "up": [0, 1, 2]},
	"travel": {"from": "user:center", "frames": 18, "path": "arc", "height": 30, "trail": {"count": 3, "gap": 2, "alpha": 0.5}}}
```

Thrown by an enemy at a member, in the front layout (the member down and to the left) it shows its `left` frames; in the side layout it flies right, has no `right` frames and is mirrored. The probe `animsprites` reads each sprite instance the last frame drew as `TRACK frame N`, with ` flip` and ` trail` (`glob frame 1 flip trail, glob frame 1 flip`).

## Figure states

`figures` gives each figure's states as data. A state is looked up in the figure's own scope (`party:ID`, `enemy:ID`, `vehicle:ID`), then its side's (`party`, `enemy`, `vehicle`), then the built-in set. A variant name (`attack:blade`, `defeat:instant`) is tried before its base name (`attack`, `defeat`).

```json
"figures": {
	"party": {"states": {
		"attack:blade": {"pose": "attack_blade", "length": 16, "motion": {"kind": "to", "to": "target:0:center", "reach": 0.2, "ease": "sine"}},
		"victory": {"pose": "victory", "loop": true, "motion": {"kind": "hop", "height": 6, "length": 20}}
	}},
	"enemy": {"states": {
		"defeat": {"kind": "fade", "pose": "defeat", "length": "$rest"},
		"defeat:instant": {"kind": "bands", "bands": 8, "length": 30, "color": {"to": {"multiply": "#202028"}}},
		"status:sleep": {"color": {"mode": "cycle", "colors": [{"tint": "#8090ff", "amount": 0.1}, {"tint": "#8090ff", "amount": 0.3}], "period": 45, "smooth": true}}
	}},
	"enemy:bog_wisp": {"states": {"idle": {"motion": {"kind": "sine", "axis": "y", "amplitude": 4, "period": 90}}}}
}
```

A state has:

| Field | What |
| --- | --- |
| `pose` | a figure animation name (falls back to `idle` when the figure lacks it) |
| `loop` | the pose loops |
| `length` | TIME; default the pose's length |
| `motion` | a `move` track's fields (without `on` and `at`), or `{"kind": "sine", "axis": "x"|"y"|"forward", "amplitude": DIST, "period": TIME, "phase": TIME}`, `{"kind": "bob", ...}` with the same fields, `{"kind": "pulse", "scale": 0.25, "period": TIME, "phase": TIME}`, or `{"kind": "shake", "amplitude": px, "frequency": N, "decay": "linear"}` |
| `color` | a CT, or a `color` track's fields (without `on` and `at`) |
| `kind` | defeat states only: below |
| `opacity` | `{from, to, speed, ease}`: the figure's opacity over the state (the join fades in from 0 at speed 1.6) |
| `previous` and `ease` | `crossfade`: the form before a change fades out as this one fades in, eased (`transform_in`) |

The states the battle plays by itself:

| State | When | Built-in (today's look) |
| --- | --- | --- |
| `idle` | always, looping on the controller's clock, each figure 137 ms apart; `idle:flying` first for a flying enemy in the front layout | none: the figure's `idle` run; a flying enemy in the front layout hovers `size / 5` up, bobbing 5 pixels along `sin((clock + 211 * i) / 420 ms)` ([Idle states](#idle-states)) |
| `choosing` | a member choosing a command | front layout: 12 pixels forward; side: nothing |
| `attack`, `cast`, `item`, `defend` | through `state` tracks (`$look.animation`) | `attack`: pose `attack` and a lunge of 10 pixels (front) or 16 (side) along `sine` over the phase; `cast` and `item`: their pose and a glow toward the effect's element color, amount `0.5 * sin(pi t)`; `defend`: its pose |
| `hurt` | the `react:damage` reaction | pose `hurt`, a sideways shake `6 * sin(40 t) * (1 - t)` and a tint toward `#ff786e` at 0.7 for the first 40% |
| `fallen` | a member that is down | pose `defeat`, `multiply` `#9696aa` |
| `rise` | a combatant revived | opacity from 0 to 1 over its step |
| `defeat` | an enemy falling, from its fall's release | kind `fade` with pose `defeat` over `$rest` |
| `join` | an enemy joining | opacity `smooth(min(1, 1.6 t))`, rising from 36 pixels below with `smooth` |
| `merge_away` | the enemy that leaves in a merge | slides to the partner's x over `smooth(min(1, 1.4 t))` |
| `transform_in` | the new form of a transform or merge | the old figure fading out over `smooth`, the new one fading in |
| `victory` | the `step:outcome.victory` script, when it plays it | pose `victory` (else `idle`) |
| `status:ID` | while the figure shows that status | none |

### Idle states

An `idle` state (and `idle:flying`, tried first for a flying enemy in the front layout) replaces the stage's own idle for the figures of its scope: its `pose` loops in place of the figure's `idle` run, its `motion` (`sine`, `bob`, `pulse` or `shake`) and `color` (a CT, or a `color` track's fields; a `cycle` cycles) play on the controller's clock read in script units at `settings.rate`, each figure `stagger` (TIME, default `"137ms"`) after the figure before it, and `lift` (DIST) is the hover over its place, its shadow staying on the ground (0 when left out: a flier with an idle state no longer takes the built-in hover). Battle speed does not scale it; it reloads on the next frame; a figure that is down plays none.

```json
"figures": {
	"enemy:gel_mite": {"states": {"idle": {"stagger": 10, "motion": {"kind": "sine", "axis": "y", "amplitude": "4px", "period": 40},
		"color": {"mode": "cycle", "colors": [{}, {"tint": "#80ff80", "amount": 0.4}], "period": 30, "smooth": true}}}},
	"enemy": {"states": {"idle:flying": {"lift": "0.4h", "motion": {"kind": "sine", "axis": "y", "amplitude": "8px", "period": 60}}}}
}
```

The mites sway 4 pixels and tint green in turn, 10 frames apart; every flier hovers 0.4 of its height up, bobbing 8 pixels a second (`flying` lists them; `figuresizes` gives the hover as the bottom's distance above the ground line).

A defeat state may carry `outlast` (TIME): its kind then plays over that length from the fall's release, on through the steps after the result step (the back phase, later turns, the round's end), on the timeline's clock (which battle speed scales and a fast-forward jumps over). The enemy's label, bar and target are gone at the release as before, so `battlesync` and the target lists do not change; another enemy that joins at its formation place ends it.

```json
"figures": {"enemy": {"states": {"defeat": {"kind": "fade", "outlast": 90}}}}
```

Defeat kinds, each over the state's `length` (or `outlast`) from the fall's release plus `delay` (TIME, default 0):

* `fade`: opacity to 0.
* `blink`: shown and hidden every `period` (TIME, default 4 frames, at least 4), then gone. With reduced flash it is a `fade`.
* `bands`: cut into `bands` (2 to 32) horizontal bands that vanish one at a time in an order hashed from the enemy's position.
* `slide`: moves `by` (DIST pair) while fading.
* `sink`: moves down `"1h"` and is clipped at its ground line, so it goes into the ground.
* `flash_fade`: `add` white over the first `flash` (TIME), then a fade.
* `frames`: the pose's frames only, no fade; the figure is gone at the state's end.

The built-in `transform_in` reproduces what is drawn today: today's code asks for the new form "lit white" with a multiply-only tint, which draws no light. An author gets the light with `"color": {"mode": "flash", "to": {"add": "#ffffff"}}`.

Figure animations in `assets.json`: an entry may name any animation `[a-z0-9_]+` beside the six named ones, which a `pose` track, a `react` track's `pose`, a figure state (a weapon variant's too) or the presentation table plays. With an animation file, an animation none of them plays is a problem (`assets: assets.json: figures[5].animations.flourish: no battle animation script or figure state plays 'flourish'`), as a character pose no script animation plays is. Back figures take the same names.

## The flashing level

The player's flashing level (the options' Flashing row, [`ui-layout.md`](ui-layout.md), "Flashing") decides what the safeguards do: `reduced` (the profile's reduced flash: the `reduced` replacements and damping, and the flash limit), `standard` (the flash limit only, no replacements) and `original` (no limit and no replacements: scripts play as authored). The script verb `flashing reduced|standard|original` sets it in a test, answering the photosensitivity question.

## Flashes by effect

The flash limit and reduced flash judge a color track on the backdrop, the stage or the frame by what it does to the screen, not by its type or mode, so a whiteout written as stepped `set` or `keys` colors, an `add`, an `alpha` or a palette swap counts as a `flash` track does. Default on, for every set.

* A track's light at a frame is how far it moves the whole area's brightness from untouched, 0 to 1, with luminance `0.2126 R + 0.7152 G + 0.0722 B` over 255: on the backdrop the luminance of its added light, plus `1 - luminance(multiply) x alpha` (darkening), plus the largest luminance difference between a palette pair's source and replacement; on the stage and the frame (fills) the luminance of its added light plus `1 - luminance(multiply)` (a tint at its amount).
* The light is sampled every frame of the track's life (its first 3600 frames), starting from 0 before the track. A flash is a rise of at least 0.25 within 6 frames (0.1 s at 60 Hz); after one, the next counts only once the light has fallen 0.25 below the peak, so a strobe counts every step up and a slow tint (0.6 over 60 frames, 0.06 in any 6) counts none.
* Each flash counts toward `settings.flash_limit`; a track whose flashes would take the count in the window past the limit does not draw at all (one transcript line per script, as before). A track marked `"flash": true` counts at least once.
* Under reduced flash a track that flashes and has no `reduced` of its own draws at most 0.2 of light: its transform is eased toward untouched by `0.2 / peak` (a palette swap is left out). `flash` tracks and `mode: "flash"` colors keep their built-in `drop`.
* Stage and frame fills follow `keys` (the added light of the key at each moment; before, a keys track on the stage drew nothing).

```json
{"type": "color", "on": "stage", "at": "effect", "mode": "keys", "ease": "step", "length": 24,
	"keys": [[0, {"add": "#ffffff"}], [3, {}], [6, {"add": "#ffffff"}], [9, {}], [12, {"add": "#ffffff"}], [15, {}], [18, {"add": "#ffffff"}], [21, {}]]}
```

That strobe makes four flashes in 24 frames and never draws under the default limit; two of its steps would draw, and at a fifth of the light with reduced flash. The probe `screenlight` reads the most light the stage's fills gave the last frame (`1.00`, `0.20`). `scripts/make_flash_assets.py` and `scripts/flash_safety.txt` test it.

## Floating numbers

`numbers` gives the floating text of each released result its motion and color. The text itself stays the text tables' (`ui.battle.float.` keys) and the role stays `battle_numbers`.

Kinds: `damage`, `critical`, `weak`, `resisted`, `immune`, `miss`, `heal`, `tp`, `revive`, `status`, `status_resisted`, `status_ends`, `stat_up`, `stat_down`, `stat_normal`, `no_effect`, `felled`, `felling_resisted`.

| Field | Built-in | What |
| --- | --- | --- |
| `color` | per kind, today's (damage `text`, critical `accent`, weak `#ffa850`, resisted `#96c8ff`, heal `good`, tp `tp`, status `#cea0ff`, felled `#ff5050`, the rest `dim`) | |
| `size` | 28 for damage, weak, resisted, heal, felled; 36 critical; 24 tp; 20 the rest | text size |
| `motion` | `rise` | `rise` (up `rise` pixels over `rise_time`, `smooth`), `pop` (from `pop` times the size to the size over `pop_time`, then still), `bounce` (up `rise` and down `bounces` times, each lower), `float` (rises at `rise` pixels over the whole life), `none` |
| `rise`, `rise_time` | 30, `"50%"` | logical pixels (a number or `"Npx"`), TIME (a `%` is of the life) |
| `pop`, `pop_time`, `bounces` | 1.5, 6, 2 | |
| `life` | `"$rest"` | TIME from the release: to the end of its step, today's |
| `fade` | `"30%"` | the last TIME of the life it fades over |
| `delay` | 0 | TIME after the release |
| `shadow` | `[2, 2, "#000000cc"]` | offset and color, or `false` |
| `stack` | 6 | logical pixels between numbers stacked on one figure, over the text size (a number or `"Npx"`) |
| `from` | `top` | where it starts: today 26 pixels above the figure's top, 36 below the top of a member seen from behind |
| `motion: "window"`, `row`, `open`, `pad` | | a framed box at logical row `row` over the target's column, opening sideways over `open` (TIME, default 6 frames), the text once it is open, no rise, no fade; `pad` pixels round the text |
| `row` | 40 | with `window`: a row, or `{"party": ROW, "enemy": ROW}`, the row for the target's side |
| `pad` | 6 | with `window`: pixels round the text, or `[x, y]` |
| `frame` | none | with `window`: a frame style of `styles.json` (`battle`, the battle windows' frame, which takes the UI skin's `battle_frame`); without it the box is the panel color with an edge |
| `close` | none | with `window`: the box closes sideways over the last `close` (TIME; a `%` is of the life) of its `life` (default to the end of its step), its text gone as it starts, and is gone at the life's end |

A kind without an entry draws exactly as the table did (its size from the layout's `number_size` params, rising `number_rise` over half its life and fading over the last 30%).

```json
"numbers": {
	"damage": {"motion": "window", "row": {"party": 360, "enemy": 120}, "pad": [14, 4], "frame": "battle", "open": 4, "close": 8},
	"heal": {"motion": "window", "row": {"party": 330, "enemy": 120}, "pad": 6, "frame": "battle", "close": "25%", "color": "good"}
}
```

Damage on an enemy opens in a battle-framed box at row 120 (on a member at 360), 14 pixels wide of the text and 4 above and below, and closes over its last 8 frames; the `uiframes` probe counts the frame among the layouts' (`battle generated 5`).

## The member alone

`solo` draws only the member choosing or acting, the other members left out of the stage (their places still anchor effects and numbers, and their slots and panels stay), in both layouts. Its value is `true` (every kind of step), `false`, or a list of kinds:

| Kind | Draws only |
| --- | --- |
| `command` | the member choosing a command |
| `action` | in a member's turn, its acting members (every participant of a combination) during `forward`, `banner`, `animate`, `effect`, `back` and the retarget and change steps the turn holds for |
| `result` | the same during `result` |

`settings.solo` sets it for the battle; a turn script's `solo` (`action` and `result` only, inherited through `extends`; the nearest that has one decides) overrides it for its turn. An enemy's turn draws every member. The `figures` probe lists only the members drawn.

```json
"settings": {"solo": ["command"]},
"scripts": {"lo.blade": {"solo": ["action"], "phases": {"forward": 8, "banner": 18, "animate": 12, "effect": 8, "result": 30, "back": 12}, "tracks": []}}
```

## Placements

`placements` puts the enemies where the file says, by encounter, then by the exact enemy count (`count:1` to `count:8`), then `count:5` for five or more. A place's third value puts it in a rank: `"back"` takes the layout's `enemy_size` for the back rank (`enemy_back`), `"front"` the front's; the foot row also orders them back to front. Six enemies in two ranks, in each layout:

```json
"placements": {"count:6": {
	"front": [[340, 430], [640, 430], [940, 430], [490, 330, "back"], [790, 330, "back"], [1090, 330, "back"]],
	"side": [[180, 360], [360, 360], [540, 360], [270, 270, "back"], [450, 270, "back"], [630, 270, "back"]]
}}
```

The probe `enemyplaces` reads each enemy standing, back to front, as `LABEL X FOOT SIZE`.

## Releasing results

A result step's events are released to the stage by the turn script's `release` tracks. Releasing an event:

* applies it to `BattleShow`: a bar starts running from its old value, a status icon grows in or blinks out, a fall starts the figure's `defeat` (or `fallen`) state, a revival its `rise`;
* starts its floating number;
* starts its reactions (`react:` scripts on the figure, below);
* plays its event cue (`hit`, `miss`, `critical`, `heal`, `status`, `enemy_down`), which today plays when the result step starts, unless it is turned off: a script's `event_cues_off` (inherited through `extends`; the nearest script that has one decides), else `settings.event_cues_off`, is `true` for every event cue or a list of the ones turned off, so a script's own sounds are not doubled. The default is none off. When to expect each: a unit plays the cue of each of its events once (a critical hit plays `critical` and `hit` together, being two events); several units released on the same frame of a step (one action's targets) play a cue once, `enemy_down` aside, which plays for every enemy that falls; a `sound` track with `per: release` plays per unit on top of the event cue unless `event_cues_off` turns that cue off; fast-forward plays none; a developer's `win` or `lose` falls in a sync step, which plays none.

```json
"settings": {"event_cues_off": ["enemy_down"]},
"scripts": {"blade.loud": {"extends": "turn.default", "event_cues_off": true, "tracks": [{"type": "sound", "at": "result", "cue": "big_hit"}]}}
```

Rules:

* The events of a result step are its results in event order. Each belongs to one target (an `ItemHadNoEffect` to its actor).
* Units: `all` (every event), `target` (all of one target's events), `hit` (each damage, healing, revival, status, stat or felling event with the events tied to it: a `Hit` or `Critical` just before it, a `CombatantDefeated` or a defeat's `StatusRemoved` just after it on the same target). An ability with two damage effects (Hailstorm) has two hits on each target.
* Release tracks are taken in script order; each releases the events of its `targets` that no earlier release track took, unit by unit in its `order`, unit k at `at + k * stagger` (or its `at` list's entry k).
* One target's units always keep event order: a unit is never released before an earlier unit of the same target, whatever `order` says; its frame is raised to the earlier one's.
* Events no release track takes are released at the start of the `result` phase. The built-in turn scripts have no release track, so everything is released as the step starts, as today.
* The earliest a release can be is the start of the `result` phase, where the step's log lines and `present:` line come out; the check reports an earlier one and moves it there. To show a hit on the frame a projectile lands, put the `result` phase (or an `impact` mark at its start) on that frame.
* Every event is released by the end of its step. An event still waiting on the step's last frame (a release frame past the step's end) is released then, "forced"; the `released` probe counts forced events, and the check reports release frames it can see fall after the `result` phase.
* Formation changes: a change step's events are released as the step starts, or at its change script's `release` track (a joining enemy appears on that frame, a new form replaces the figure, label and bar on that frame).

## Selection and chaining

### Lookup

`select` maps keys to script ids. For each turn the battle tries these keys in order and plays the first script found; the built-in set has `default`, so a script is always found.

| Order | Key | For |
| --- | --- | --- |
| 1 | `encounter:ENC/enemy:FORM/ACTION` | an enemy's action in one encounter; FORM is the enemy's current id (a next form is its own id), ACTION `attack`, `defend` or an ability id |
| 2 | `enemy:FORM/ACTION` | that enemy's action anywhere |
| 3 | `combo:RECIPE` | a combination by its recipe id (`combos.json`; the recipe whose result and participants' abilities match) |
| 4 | `ability:ID/character:CHAR`, `ability:ID/weapon:CLASS`, `ability:ID/vehicle:VEHICLE`, `ability:ID` or `item:ID` | the ability or item (a combination's own ability; a basic attack's ability, `ability:strike`; anything cast through the Item command is `item:`, so a worn piece cast that way is `item:PIECE`: the check accepts any equipment piece with a `use`, of any slot (the Squall Fan, an accessory), and any weapon; `weapon_classes` accepts `equipment:ID` of any piece and `category:NAME` of any equipment category); `/vehicle:VEHICLE` when the vehicle in a vehicle battle acts |
| 5 | `weapon:CLASS/HANDS/character:CHAR`, `weapon:CLASS/character:CHAR`, `weapon:CLASS/HANDS`, `weapon:CLASS` | a party member's basic attack, by its weapon animation class and hand count (`one`, `both`) |
| 6 | `shape:KIND.EFFECT.ELEMENT` | the element level: kind, first effect and element, as the presentation table's fallbacks (`shape:technique.damage.shock`) |
| 7 | `shape:KIND.EFFECT` | the effect-kind level |
| 8 | `shape:KIND/character:CHAR` (a member's defend), `shape:KIND/vehicle:VEHICLE` (a vehicle's action), then `shape:KIND` | the kind (`attack`, `technique`, `skill`, `combo`, `item`, `defend`); the actor's key is tried before the element and effect levels |
| 9 | `default` | everything else |

Other keys, each with its own chain:

* Escape and cancel: `turn:escape`; `step:cancel.REASON` (`actor_unable`, `silenced`, `not_enough_tp`, `no_uses_left`, `no_item`, `no_target`), then `step:cancel`.
* Steps: `step:intro`, `step:opening.normal|party_first|enemy_first` then `step:opening`, `step:round`, `step:retarget`, `step:notice.KEY` (the notice's text key, `step:notice.game.level_up`) then `step:notice`, `step:roundend`, `step:outcome.victory|defeat|escaped|scripted_end` then `step:outcome`, `step:sync`. Any step key may end in `/encounter:ID` or `/area:ID`, tried first (`step:intro/encounter:warden`).
* Changes: `encounter:ENC/change:CHANGE.ENEMY`, `change:CHANGE.ENEMY`, `change:CHANGE`, then the change's fallbacks. CHANGE is `join`, `merge`, `transform`, `split` (a transform whose enemy then joins the battle more of itself, `split` in core), `split_piece` (each enemy a split adds; falls back to `join`), `leave` (an enemy gone with no partner; falls back to `merge`, today's look), `self_destruct` and `remove_beside` (the leave that follows those messages; fall back to `leave`). ENEMY is the enemy brought in, changed into or leaving.
* Reactions, per released event on its figure, every matching chain playing: `react:damage`, `react:critical`, `react:miss`, `react:heal`, `react:revive`, `react:status.ID` then `react:status`, `react:status_resisted`, `react:status_ends`, `react:stat`, `react:defeat`, `react:instant_defeat`, `react:felling_resisted`, `react:no_effect`, `react:tp`; each tried as `react:EVENT.ENEMY` first for an enemy figure.
* Battle level: `battle:transition_in`, `battle:backdrop`, `battle:transition_out`, each tried with `/encounter:ID`, `/area:ID`, `/terrain:floor|water|soft`, `/kind:town|field|dungeon`, `/boss` (an encounter `game.json`'s `bosses` names), `/opening:enemy_first|party_first`, `/vehicle` in that order, then bare.

Every key of the turn's lookup is tried with `/encounter:ID`, then `/count:N`, then bare.

Any key of any kind (turn, step, change, reaction, battle, status) may end in `/layout:front` or `/layout:side`: when the set has such a key, every lookup tries each key with the stage's layout before the key without it (and the `animkey` trace lists those tries), so one file plays a script its own way in one layout: `"ability:spark/layout:side": "lo.spark.side"`. Keys are checked against the data: encounter, enemy, ability, item, recipe, status, character and area ids, weapon classes (categories and `weapon_classes` names); notice keys are checked for their form only.

* Statuses: `status:ID/idle`, a looping script (with `length`) played on every figure that shows the status, on the controller's clock (asleep, unable to act); `status:ID/tick`, a reaction played on every figure that shows the status as each round's banner step starts (the session reports no damage-over-time events, so the tick is per round).
* `step:recover` (a fallen machine standing up after the battle) and `change:leave` (an enemy gone with no partner, then `change:merge`) are keys of their own.

### Looks and scripts together

The presentation table keeps resolving a look for every turn and change exactly as it does now (the enemy's entry, the action's, then the fallbacks). Selecting a script is a separate lookup. Every script the turn plays reads that look as `$look.*`: `$look.animation`, `$look.effect`, `$look.cue`, `$look.animate_ms`, `$look.effect_ms`, `$look.shake`, `$look.flash`.

### Chaining

* `extends`: a script built on another (above). `turn.default` is the usual parent.
* Fragments: shared track lists. A fragment is `{"params": {...}, "tracks": [...]}`; its ATs are frames from the call (no phase names: a fragment does not know the caller's phases), its subjects are renamed by the call's `bind`. Fragments call fragments up to 8 deep; a cycle is a problem. A script expanded with its fragments has at most 256 tracks.
* Per-boss-form sequences: a next form is its own enemy id, so `enemy:tide_colossus_core/attack` and `change:transform.tide_colossus_core` give each form and the change between them their own scripts.
* Mid-battle changes: each change step plays its change script while the turn's clock holds; a reaction script plays on a figure from a release.

## Battle intro and outro

### Transitions

`transitions` is a library of transitions, each a kind and its parameters. A `transition` track in a battle script draws one over the whole logical frame: during the script's `cover` phase from the scene to the cover, during `hold` the cover, during `reveal` from the cover to the next scene.

| Kind | Fields | Picture |
| --- | --- | --- |
| `flash` | `color`, `count` (1 to 3), `on`, `off` (TIME) | flashes of the color, then the cover; with reduced flash a `fade` |
| `fade` | `color` | the scene fades to the color, and from it |
| `wipe` | `color`, `from` (`left`, `right`, `top`, `bottom`, `top_left`, ...), `edge` (pixels of soft edge, drawn as 8 bands) | the cover sweeps across |
| `iris` | `color`, `shape` (`circle`, `diamond`, `square`), `center` (POINT; `party:center`, `screen:50%,50%`) | the cover closes on the point, then opens from it (drawn as one span per row) |
| `mosaic` | `block` (8 to 64 pixels), then `color` for the hold | the scene breaks into growing blocks, then the next scene sharpens from blocks |
| `tiles` | `color`, `tile` (8 to 160 pixels), `order` (`rows`, `columns`, `checker`, `diagonal`, `random`, `spiral`, `spiral_in`), `grow` (TIME each tile takes to fill its cell) | cells of the cover appear in that order, and go in the reverse |
| `spiral` | `color`, `tile`, `turn` (`cw`, `ccw`), `from` (`center`, `edge`) | `tiles` along a spiral from the center or the edge |
| `sequence` | `phases`: 1 to 8 of `{"color", "steps" (1 to 32, default 1), "step_frames" (default 4 frames), "hold"}` | under `cover` the screen goes to each phase's color in turn from the one before (the scene first), in `steps` steps `step_frames` apart, then holds it `hold`; under `hold` the last color; under `reveal` the last color steps back to the next scene at the last phase's rate. Each step that raises the light by a flash's step (0.25, [Flashes by effect](#flashes-by-effect)) counts toward the flash limit; under reduced flash or past the limit the sequence plays as a fade to its last color |
| `split` | `color`, `steps`, `step_frames`, `speed` (pixels a frame), `frames`, `axis` (`horizontal`, default, or `vertical`) | a (stepped) fade over the cover; in `reveal` the backdrop slides in from the left and the figures, their labels and bars from the right, `speed` pixels a frame for `frames`; with `vertical` the backdrop drops in from above and the figures and labels rise from below |

`fade` and `split` take `steps` (2 to 32) and `step_frames` (TIME): the cover moves one level every `step_frames`, `steps` levels to the color and back; `cover_step_frames` and `reveal_step_frames` give each half its own rate (each falls back to `step_frames`).

`mosaic` takes `block` (8 to 64, default 32) and `steps` (default 4): under `cover` every picture of the scene is drawn in blocks growing to `block` logical pixels in `steps` levels, under `hold` the `color` covers the frame, under `reveal` the next scene sharpens from `block` the same way. Nothing captures the frame, so each picture (a tile, a figure, a window's frame) breaks into blocks of its own and text stays sharp. The probe `transition` adds the block (`mosaic cover 13/16 block 32`).

```json
"transitions": {"lo.mosaic": {"kind": "mosaic", "block": 32, "steps": 4, "color": "#000000"}},
"select": {"battle:transition_in/encounter:horde": "lo.mosaic"},
"scripts": {"lo.mosaic": {"phases": {"cover": 16, "hold": 4, "reveal": 16}, "tracks": [{"type": "transition", "transition": "lo.mosaic"}]}}
```

```json
"transitions": {"drop": {"kind": "split", "axis": "vertical", "steps": 4, "cover_step_frames": 2, "reveal_step_frames": 5, "speed": 12, "frames": 20}}
```

```json
"transitions": {"amber_dark": {"kind": "sequence", "phases": [
	{"color": "#c0a040", "steps": 3, "step_frames": 2, "hold": 6},
	{"color": "#000000", "steps": 2, "step_frames": 3, "hold": 8},
	{"color": "#c0a040", "steps": 2, "step_frames": 2, "hold": 6}
]}}
```

The probe `transition` adds the sequence's phase and whether it steps or holds (`sequence cover 20/44 phase 1 hold`).

`ease` (default `linear`) shapes every kind's progress. A rotating swirl distortion needs a shader and is not a kind ([Implementation plan](#implementation-plan)).

Under `cover` the first scene is drawn live: going into battle, the field (drawn as the field screen draws it); coming out, the battle's result screen. Under `reveal` the next scene is drawn live. Nothing is captured.

### The moments

* `battle:transition_in`: plays when a battle starts, before the intro step, as a step of its own (`present: transition KIND`). Its `reveal` shows the stage with every figure in its `idle` state and no banner. The built-in script is 0 frames long, and a battle script 0 frames long adds no step, so the step list stays as today.
* `step:intro`: the intro step (the enemies' names, `battle_start`). Formation entry is its tracks: `move` with `kind: enter` on `enemies` and `party`, staggered, and `color` fades.
* `battle:backdrop`: a looping script (with `length`) that runs on the controller's clock from the intro to the result screen; its tracks are `backdrop` and `color` tracks on the backdrop with `loop`. Not scaled by the battle speed.
* `turn:escape` and `step:outcome.escaped`: how the escape plays (the members turning away and leaving with `exit` moves on `if: escaped`).
* `step:outcome.victory`: the outcome step; the place for the party's `victory` state.
* `step:notice.game.level_up`: each level-up notice, which already plays as its own step after the outcome (`AddBattleNotice`).
* `battle:transition_out`: plays when the result screen closes, before the field takes input again; not a step (the battle is over). The field gets no input while it plays. The built-in one is 0 frames long.

The battle layout files add two timings around these moments: `command_open_delay`, a wait before the first command window after the intro, and `result_sequence`, the result screen as steps (the outcome alone, the result window, the level-ups) with their own lengths, which takes the level-up notices out of the steps after the outcome ([`ui-layout.md`](ui-layout.md), "Battle windows' timing and order"); `level_page_lines` pages that level-ups step ("More layout flows").

Per-encounter, per-area, per-terrain, per-kind and boss overrides are the `/encounter:ID` and other qualifiers of these keys. The encounter is this codebase's formation: the enemy group of up to eight places.

## Timing control

* Battle speed: scales the clock, so every phase, track, travel and number life is that much shorter. Idle states, status tints and `battle:backdrop` loops run on the controller's clock and keep their speed.
* Reduced flash: each track type has a policy, which `reduced_flash` can change per type and a track's `reduced` per track. A track's `reduced` may be a replacement track object: with reduced flash on when the round's scripts are built, the replacement plays in its place, taking the original's `at` unless it has its own (`{"type": "flash", "at": "effect", "reduced": {"id": "calm", "type": "color", "on": "stage", "mode": "tint", "to": {"tint": "#4060a0", "amount": 0.2}, "in": 2, "hold": 4, "out": 2}}`). Built-in: `flash` tracks drop; `color` tracks in `flash` mode on the backdrop, the stage or the frame drop, on figures damp; `cycle` holds its first color; `shake` damps to a quarter (today's); `flash` transitions become fades; `blink` defeats become fades; the strip's hit flash stays the layout's (framed red only). `damp` is 0.25, changeable as `reduced_flash.damp`.
* Flash limit, with or without reduced flash: at most three screen-wide flashes (`flash` tracks, `color` flashes on the backdrop, stage or frame whose peak `add` is above 64 or whose fill opacity is above 0.25, any backdrop, stage or frame color track that flashes by its effect, counting each of its flashes, tracks marked `"flash": true`, and `flash` transitions, which then play as fades) may start in any 60 frames of the controller's clock (1000 ms); a track whose flashes would pass the limit does not draw. A later one is dropped, with one transcript line per script (`animation: more.blade: flash dropped, more than 3 in 60 frames`). `settings.flash_limit` changes it (`{"count": 2, "frames": 90}`) or turns it off (`false`). A flash counts on the first frame it draws, so a fast-forward that draws nothing counts nothing. The built-in set flashes at most once a turn, so no existing run drops one.
* Hurry (Confirm): finishes the current step. The script jumps to the start of the next phase (the step's end). Every event of the step not yet released is released at once, with its number, reaction and bar already over (their life ended with the step); the event cues of the units it released play once each by cue id. `sound` tracks whose frames were jumped over do not play. Moves, sprites and colors are drawn as they are at the new frame, which statelessness guarantees.
* Fast-forward (held): plays the rest of the round at once, as today: every remaining step starts, each script jumps to its end, every event is released, no cue plays, nothing of the skipped steps is drawn.
* Skipping in general: whatever the jump, every event of every step it passes is released, in order, so `BattleShow` reaches the same state as a played round.
* Scripts never change results. They read the resolved events, never write them. Selection, conditions, release order and stagger are computed from the events and the stage, never from a random draw of the session, and never feed back into it. Jitter and random tile orders hash script and track ids.
* `battlesync`: unchanged. When a round's playback ends, the shown numbers, statuses and falls must equal the session's. The `released` probe adds what the scripts did: events released, and how many were forced.

## Geometry

* Figure scale, natural size and foot row: every anchor reads the rectangle `Art.Stand` returns, so a scaled figure, one at its natural size and one with a `foot` row get effects at their own head, center and feet; `feet` is the ground line. `h` and `w` distances follow the drawn size; forward units and pixels do not.
* Effects keep today's factor (3, or 4 on enemies in the front layout and on formation changes) over the sheet's `scale`, snapped; a script's `scale` changes the factor before the snap.
* Front and side layouts: places, sizes and ranks come from the layout files' params as today ([`ui-layout.md`](ui-layout.md), "Params"). The forward axis, forward units and per-layout values let one script fit both; `layout` restricts a track to one. In a vehicle battle members have no figure: their anchors are their strip slots, moves on them do nothing, and the vehicle moves with its own forward unit.
* Logical resolution: every coordinate is a logical pixel of the frame `styles.json` sets. Pixel values keep their size at another resolution, as the code's constants do today; `f`, `h` and `w` distances, anchors and `screen:` percentages adapt. Transitions cover the whole logical frame.

## The presentation table

The table keeps working with no change and no translation step:

| Table entry | Read by |
| --- | --- |
| `layout`, `layouts` | the stage, unchanged |
| `timings.STEP` | the built-in scripts' phase and step lengths, as `$timing.STEP` |
| `actions`, `fallbacks`, `enemies` | the turn's look, resolved as today, read as `$look.*` |
| `changes.CHANGE[.ENEMY]` | change scripts and the `react:instant_defeat` reaction, as `$look.*` |

The compatibility rule: a table with no animation file plays exactly as before, because every built-in script reads every field of the look. A table entry still changes a turn whose script reads `$look` (every built-in script, and any authored one that uses it). An authored script that names its own effect, cue or lengths ignores the table's for those.

The built-in set (`Psiv.Sample/battle/animation.json`) holds `turn.default`, `turn.escape`, `step.banner` (intro, opening, round, notice, outcome), `step.cancel`, `step.roundend`, `step.retarget`, `step.sync`, `change.join`, `change.merge`, `change.transform`, `change.leave`, `react.hurt` (`react:damage`), `react.critical`, `react.instant_defeat`, `battle.none` (the transitions and backdrop, 0 frames), the figure states above and the number styles above. `turn.default`:

```json
"turn.default": {
	"phases": {"forward": "$timing.forward", "banner": "$timing.banner", "animate": "$look.animate_ms", "effect": "$look.effect_ms",
		"result": "$timing.result", "back": "$timing.back"},
	"tracks": [
		{"id": "step_in", "type": "move", "on": "actor", "at": "forward", "kind": "forward", "distance": "1f", "ease": "smooth"},
		{"id": "action", "type": "state", "on": "actor", "at": "animate", "state": "$look.animation"},
		{"id": "effect", "type": "sprite", "at": "effect", "effect": "$look.effect", "spawn": "targets", "anchor": "auto",
			"stagger": {"front": "sweep", "side": 0}, "order": {"front": "left_to_right", "side": "event"}, "length": "$phase"},
		{"id": "cue", "type": "sound", "at": "effect", "cue": "$look.cue"},
		{"id": "flash", "type": "flash", "at": "effect", "if": "$look.flash"},
		{"id": "shake", "type": "shake", "at": "effect", "amplitude": "$look.shake"},
		{"id": "step_out", "type": "move", "on": "actor", "at": "back", "kind": "home", "ease": "smooth"}
	]
},
"react.critical": {"length": "$rest", "tracks": [{"type": "shake", "amplitude": 6, "length": "$rest"}]},
"react.instant_defeat": {"length": "$rest", "tracks": [
	{"type": "sprite", "effect": "$look.effect", "spawn": "self", "anchor": "center", "scale": 4, "loops": -1, "length": "$rest",
		"color": {"keys": [["85%", {}], ["100%", {"alpha": 0}]]}}
]}
```

`dev anim expand KEY` prints the script a key selects with its `$look`, `$timing` and fragments filled in, as JSON to paste into the file: the translation aid for moving an entry from the table to a script.

## Check, export and hot reload

### `--check-assets`

The animation file is checked with the rest, against the session's data and the manifest, without a graphics device. Each problem prints as `assets: FILE: ENTRY: REASON`; the entry is the JSON path. Examples:

```
assets: battle_animation.json: -: unknown field 'scirpts'
assets: battle_animation.json: version: must be 1
assets: battle_animation.json: include[0]: missing file
assets: battle_animation.json: select.ability:sparkk: unknown ability or item 'sparkk': not in the game data
assets: battle_animation.json: select.weapon:axe: unknown weapon category 'axe': blade, greatblade, rod, sling
assets: battle_animation.json: select.combo:twin_arcs: unknown combination recipe 'twin_arcs': not in combos.json
assets: battle_animation.json: select.ability:spark: unknown script 'spark.bolts'
assets: battle_animation.json: select.step:notice.game.lvl: unknown notice key 'game.lvl'
assets: battle_animation.json: scripts.spark.bolt.phases.effect: must be frames from 0 to 36000, "Nms", or a $timing, $look, $step or $param reference
assets: battle_animation.json: scripts.spark.bolt: lasts 36812 frames; a script lasts at most 36000
assets: battle_animation.json: scripts.spark.bolt.tracks[2].at: unknown phase or mark 'impakt': forward, banner, animate, effect, result, back, end, launch, impact
assets: battle_animation.json: scripts.spark.bolt.tracks[3].effect: unknown effect 'sprak.orb': not an effect animation, an effect in assets.json, a generated effect or $look.effect
assets: battle_animation.json: scripts.spark.bolt.tracks[4]: release at frame 30 is before the result phase (frame 46); it is released at frame 46
assets: battle_animation.json: scripts.spark.bolt.tracks[4]: release at frame 120 is after the result phase ends (frame 94); its events would be forced then
assets: battle_animation.json: scripts.spark.bolt.tracks[5].if: unknown condition 'crit': hit, miss, critical, weak, ...
assets: battle_animation.json: scripts.spark.bolt.tracks[6].on: 'actor:1' in a script no combination selects
assets: battle_animation.json: scripts.spark.bolt.tracks[7]: 'multiply' cannot be used on 'stage': use tint and amount
assets: battle_animation.json: scripts.spark.bolt.extends: 'turn.heavy' extends 'spark.bolt': a cycle
assets: battle_animation.json: fragments.cast_glow: calls nest deeper than 8
assets: battle_animation.json: effects.spark.orb.frames[4]: frame 9 is not in sheet 'spark_orb' (8 frames)
assets: battle_animation.json: figures.enemy.states.defeat.kind: unknown defeat kind 'explode': fade, blink, bands, slide, sink, flash_fade
assets: battle_animation.json: numbers.critical.color: unknown color 'gold': a styles.json color name or #rrggbb
assets: battle_animation.json: transitions.blocks.block: must be 8 to 64 pixels
assets: assets.json: figures[3].animations.attack_axe: no battle animation script or figure state plays 'attack_axe'
```

`scripts/check_assets_broken.expected` holds the problems the broken copy's animation files give (`make_anim_assets.py --broken`). A problem leaves out the entry it is in (the script, the effect animation, the state, the style, the transition, the select key); keys that named a left-out script go on down their lookup chain. The summary line gains `, N battle animation scripts (M keys)`, or `, the built-in battle animations`.

### `--export-placeholders`

Writes `battle_animation.json` with the complete built-in set (every select key, script, fragment, figure state, number style and transition) and adds `"battle_animation": "battle_animation.json"` to `assets.json`; the presentation table is written as today. The round trip plays the smoke, actors, panels, battle and vehicles runs with the export and must draw every frame and print every line as without it. Times the built-in set holds in milliseconds are written as `"Nms"` strings so they read back exactly.

### Hot reload

F5, `assets reload` and `--watch-assets` read the animation file and its includes again with the rest of the directory and log what changed.

| What | Applies |
| --- | --- |
| scripts, select, fragments, effect animations, numbers, reactions | the next round (a round's steps carry the scripts they were built with) |
| idle, choosing and status figure states | the next frame |
| `battle:backdrop` | the next battle |
| transitions | the next transition |
| the previewer | the next replay (Page Down) |

## Worked examples

The examples use the placeholder data's ids (characters `fighter`, `medic`, `mystic`; enemies `gel_mite`, `rust_hound`, `tide_colossus`, `tide_colossus_core`; encounters `training`, `warden`, `colossus`; abilities `spark`, `frostbite` (Frost Pane), `glint`, `mend`, `unmake`; recipe `twin_arc`) and invented sheet and cue names, which an `assets.json` entry would supply (`Synth` sounds any cue id).

### A basic weapon attack

The fighter's blade attack: a step in, a three-frame swing on the blade pose, a short lunge, the slash on the target and the hit on frame 42. The figure entry adds the pose:

```json
{"id": "fighter", "side": "party", "image": "figures/party_fighter.png", "frame": [16, 16], "idle": {"frames": [0], "ms": [400]},
	"animations": {"attack_blade": {"frames": [4, 5, 6], "ms": [50, 50, 167]}}}
```

```json
"select": {"weapon:blade": "attack.blade"},
"figures": {"party": {"states": {"attack:blade": {"pose": "attack_blade", "length": 16}}}},
"scripts": {
	"attack.blade": {
		"phases": {"forward": 8, "banner": 18, "animate": 10, "effect": 6, "result": 30, "back": 8},
		"tracks": [
			{"type": "move", "on": "actor", "at": "forward", "kind": "forward", "distance": "1f", "ease": "out"},
			{"type": "state", "on": "actor", "at": "animate", "state": "attack"},
			{"type": "move", "on": "actor", "at": "animate", "kind": "to", "to": "target:0:center", "reach": 0.15, "length": 10, "ease": "in"},
			{"type": "sound", "at": "animate+4", "cue": "blade_swing"},
			{"type": "sprite", "at": "effect", "effect": "slash", "spawn": "target:0", "anchor": "auto"},
			{"type": "release", "at": "result"},
			{"type": "shake", "at": "result", "if": "hit", "amplitude": 2, "length": 8},
			{"type": "move", "on": "actor", "at": "result+6", "kind": "forward", "distance": "1f", "length": 8},
			{"type": "move", "on": "actor", "at": "back", "kind": "home"}
		]
	}
}
```

Frames: 80 in all, the hit at 42 (8 + 18 + 10 + 6). The member's weapon category decides the key; a rod or sling attack takes its own key or falls to `shape:attack.damage.ELEMENT` and `turn.default`.

### A single-target technique with a projectile

Spark: the mystic casts for 18 frames, an orb arcs to the target in 16 and bursts on arrival, frame 62.

```json
"select": {"ability:spark": "spark.bolt"},
"effects": {
	"spark.orb": {"sheet": "spark_orb", "durations": 3, "loops": -1, "blend": "additive", "scale": "x1.5",
		"travel": {"from": "user:head", "frames": 16, "path": "arc", "height": "0.6h", "ease": "in", "trail": {"count": 3, "gap": 2, "alpha": 0.5}},
		"impact": "spark.pop"},
	"spark.pop": {"sheet": "burst_shock", "durations": [2, 2, 3, 3, 4, 6], "blend": "additive", "scale": "x1.25"}
},
"scripts": {
	"spark.bolt": {
		"phases": {"forward": 8, "banner": 20, "animate": 18, "effect": 16, "result": 32, "back": 8},
		"tracks": [
			{"type": "move", "on": "actor", "at": "forward", "kind": "forward"},
			{"type": "state", "on": "actor", "at": "animate", "state": "cast", "length": 34},
			{"type": "color", "on": "actor", "at": "animate", "mode": "tint", "to": {"tint": "#fff080", "amount": 0.5}, "in": 8, "hold": 10, "out": 8},
			{"type": "sound", "at": "animate", "cue": "charge_hum"},
			{"type": "sprite", "at": "effect", "effect": "spark.orb", "spawn": "target:0"},
			{"type": "sound", "at": "effect", "cue": "spark_zap"},
			{"type": "color", "on": "target:0", "at": "result", "mode": "flash", "to": {"add": "#fff080"}, "length": 8},
			{"type": "move", "on": "actor", "at": "back", "kind": "home"}
		]
	}
}
```

The implicit release is at the `result` phase's start, frame 62 (8 + 20 + 18 + 16), the frame the orb arrives (`effect` + 16). The table's look for Spark is ignored: this script names its own effect and cue.

### An all-enemies technique with a backdrop effect and staggered hits

Frost Pane: the backdrop chills and ripples, a burst lands on each enemy left to right six frames apart, and each enemy's number appears with its burst's brightest frame.

```json
"select": {"ability:frostbite": "frost.pane"},
"scripts": {
	"frost.pane": {
		"phases": {"forward": 8, "banner": 20, "animate": 16, "effect": 9, "result": 40, "back": 8},
		"tracks": [
			{"type": "move", "on": "actor", "at": "forward", "kind": "forward"},
			{"type": "state", "on": "actor", "at": "animate", "state": "cast", "length": 25},
			{"id": "chill", "type": "color", "on": "backdrop", "at": "animate", "mode": "tint", "to": {"tint": "#6080c0", "amount": 0.35},
				"in": 12, "hold": 50, "out": 10},
			{"type": "backdrop", "at": "effect", "effect": "wave", "amplitude": 3, "wavelength": 48, "period": 40, "length": 49},
			{"type": "sprite", "at": "effect", "effect": "burst_chill", "spawn": "targets", "order": "left_to_right", "stagger": 6,
				"durations": 3, "blend": "additive"},
			{"type": "sound", "at": "effect", "cue": "frost_crack", "per": "each", "order": "left_to_right", "stagger": 6},
			{"type": "release", "at": "result", "unit": "target", "order": "left_to_right", "stagger": 6},
			{"type": "shake", "at": "result", "amplitude": 3, "length": 24},
			{"type": "move", "on": "actor", "at": "back", "kind": "home"}
		]
	}
}
```

The burst's third frame starts 6 frames into it; with the `result` phase 9 frames after the bursts start (frame 53), each enemy's release falls on its own burst's peak: frames 53, 59 and 65 for three enemies. The last number lives 28 frames, to the step's end at 93. Six enemies in two ranks get the same order across both ranks, by x.

### A two-part combination

Twin Arc (the medic's Glint and the mystic's Spark): each caster glows in turn, two projectiles converge, a flash and a nova at the meeting point.

```json
"select": {"combo:twin_arc": "twin.arc"},
"fragments": {
	"cast_glow": {"tracks": [
		{"type": "state", "on": "actor", "at": 0, "state": "cast", "length": 24},
		{"type": "color", "on": "actor", "at": 0, "mode": "tint", "to": {"tint": "$param.color", "amount": 0.6}, "in": 6, "hold": 10, "out": 8},
		{"type": "sound", "at": 0, "cue": "charge_hum"}
	]}
},
"effects": {
	"glint.mote": {"sheet": "glint_mote", "durations": 2, "loops": -1, "blend": "additive",
		"travel": {"from": "user:head", "frames": 18, "path": "wave", "waves": 2, "height": 24}},
	"arc.orb": {"sheet": "spark_orb", "durations": 3, "loops": -1, "blend": "additive",
		"travel": {"from": "actor:1:head", "frames": 14, "path": "arc", "height": "0.5h"}}
},
"scripts": {
	"twin.arc": {
		"phases": {"forward": 10, "banner": 30, "animate": 24, "effect": 18, "result": 40, "back": 10},
		"tracks": [
			{"type": "move", "on": "actor", "at": "forward", "kind": "forward", "stagger": 4},
			{"type": "call", "fragment": "cast_glow", "at": "animate", "bind": {"actor": "actor:0"}, "params": {"color": "#c0f0ff"}},
			{"type": "call", "fragment": "cast_glow", "at": "animate+12", "bind": {"actor": "actor:1"}, "params": {"color": "#fff080"}},
			{"type": "sprite", "at": "effect", "effect": "glint.mote", "spawn": "target:0"},
			{"type": "sprite", "at": "effect+4", "effect": "arc.orb", "spawn": "target:0"},
			{"type": "flash", "at": "result", "amount": 0.5, "length": 10},
			{"type": "sprite", "at": "result", "effect": "nova_shock", "spawn": "target:0", "scale": "x1.5", "blend": "additive"},
			{"type": "shake", "at": "result", "amplitude": 6, "length": 30},
			{"type": "move", "on": "actor", "at": "back", "kind": "home", "stagger": 4}
		]
	}
}
```

Both projectiles arrive at frame 82 (10 + 30 + 24 + 18), the release. With reduced flash the flash is left out and the shake goes 1.5 pixels.

### A heal

Mend on a member: motes rise through the target while it warms green; the healing number pops.

```json
"select": {"ability:mend": "mend.rise"},
"effects": {
	"mend.motes": {"sheet": "motes_heal", "durations": 4, "loops": -1, "anchor": "feet", "blend": "additive",
		"travel": {"from": "target:auto:feet", "to": "target:auto:top", "frames": 30, "ease": "out"}, "hold_last": 6}
},
"numbers": {"heal": {"motion": "pop", "pop": 1.6, "pop_time": 8}},
"scripts": {
	"mend.rise": {
		"phases": {"forward": 8, "banner": 18, "animate": 16, "effect": 20, "result": 36, "back": 8},
		"tracks": [
			{"type": "move", "on": "actor", "at": "forward", "kind": "forward"},
			{"type": "state", "on": "actor", "at": "animate", "state": "cast", "length": 36},
			{"type": "sprite", "at": "effect", "effect": "mend.motes", "spawn": "targets", "offsets": [[-10, 0], [8, 4], [0, -6]]},
			{"type": "color", "on": "targets", "at": "effect+8", "mode": "tint", "to": {"tint": "good", "amount": 0.4}, "in": 6, "hold": 14, "out": 10},
			{"type": "move", "on": "actor", "at": "back", "kind": "home"}
		]
	}
}
```

In the front layout the target is a member seen from behind: its `feet` and `top` come from its back figure, and the motes rise over its slot; in a vehicle battle they rise over its strip slot's `anchor` rectangle.

### An enemy action

The Rust Hound's basic attack: a hop, a lunge most of the way to the target, bite marks on the head. A harder variant in the training encounter adds a second bite.

```json
"select": {
	"enemy:rust_hound/attack": "hound.bite",
	"encounter:training/enemy:rust_hound/attack": "hound.bite.double"
},
"scripts": {
	"hound.bite": {
		"phases": {"forward": 6, "banner": 14, "animate": 14, "effect": 4, "result": 30, "back": 12},
		"tracks": [
			{"type": "move", "on": "actor", "at": "forward", "kind": "forward"},
			{"type": "move", "on": "actor", "at": "animate", "kind": "hop", "height": "0.3h", "length": 8},
			{"type": "move", "on": "actor", "at": "animate+8", "kind": "to", "to": "target:0:head", "reach": 0.6, "length": 10, "ease": "in"},
			{"type": "sprite", "at": "effect", "effect": "bite_marks", "spawn": "target:0", "anchor": "head"},
			{"type": "sound", "at": "effect", "cue": "snap"},
			{"type": "move", "on": "actor", "at": "back", "kind": "home"}
		]
	},
	"hound.bite.double": {
		"extends": "hound.bite",
		"phases": {"result": 40},
		"tracks": [
			{"type": "sprite", "at": "result+10", "effect": "bite_marks", "spawn": "target:0", "anchor": "head", "offset": [6, 4]},
			{"type": "sound", "at": "result+10", "cue": "snap"}
		]
	}
}
```

In the front layout the hound comes toward the camera (forward is down) and down toward the member's slot; in the side layout it runs right across the stage. The second bite is only a picture: the session's events have one hit, released at the `result` phase's start.

### An instant-defeat mark

Unmake on the lone Twin Ooze: a dark ring, then the mark stamped and blinking while the figure cuts away in bands.

```json
"select": {
	"ability:unmake": "unmake.ring",
	"react:instant_defeat": "felled.mark"
},
"figures": {"enemy": {"states": {"defeat:instant": {"kind": "bands", "bands": 10, "length": 30, "delay": 8}}}},
"numbers": {"felled": {"motion": "bounce", "rise": 20, "bounces": 2}},
"scripts": {
	"unmake.ring": {
		"extends": "turn.default",
		"phases": {"effect": 24},
		"tracks": [
			{"id": "effect", "type": "sprite", "at": "effect", "effect": "ring_status", "spawn": "targets", "color": {"multiply": "#403050"},
				"durations": 4},
			{"id": "cue", "type": "sound", "at": "effect", "cue": "void_hum"}
		]
	},
	"felled.mark": {
		"length": "$rest",
		"tracks": [
			{"type": "sprite", "effect": "mark_defeat", "spawn": "self", "anchor": "center", "scale": 4, "loops": -1, "length": "$rest"},
			{"type": "color", "on": "self", "mode": "cycle", "colors": [{}, {"add": "#ff3030"}], "period": 8, "length": 32},
			{"type": "sound", "cue": "stamp"}
		]
	}
}
```

The reaction starts at the felling event's release, on the felled enemy; `defeat:instant` replaces the plain `defeat` for an enemy an instant defeat felled. With reduced flash the cycle holds its first color. A resisted or immune felling plays `react:felling_resisted` instead and floats its word in `felling_resisted`'s style.

### A boss form change

The Tide Colossus takes its next form: it shudders and flashes, the backdrop darkens and swaps, and on frame 48 the core replaces it.

```json
"select": {"change:transform.tide_colossus_core": "colossus.core"},
"scripts": {
	"colossus.core": {
		"phases": {"change": 96},
		"tracks": [
			{"type": "sound", "at": 0, "cue": "quake"},
			{"type": "state", "on": "previous_form", "at": 0, "state": "hurt", "length": 40},
			{"type": "color", "on": "previous_form", "at": 0, "mode": "cycle", "colors": [{}, {"add": "#ffffff"}], "period": 6, "length": 42},
			{"type": "shake", "at": 0, "amplitude": 4, "decay": "none", "length": 48},
			{"type": "color", "on": "backdrop", "at": 12, "mode": "fade", "to": {"multiply": "#505060"}, "in": 30},
			{"type": "backdrop", "at": 42, "effect": "swap", "image": "backdrops/colossus_core.png", "length": 20},
			{"type": "release", "at": 48},
			{"type": "flash", "at": 48, "amount": 0.7, "length": 12},
			{"type": "state", "on": "changed", "at": 48, "state": "transform_in", "length": 30},
			{"type": "color", "on": "changed", "at": 48, "mode": "flash", "to": {"add": "#ffffff"}, "length": 30},
			{"type": "sound", "at": 48, "cue": "critical"},
			{"type": "shake", "at": 48, "amplitude": 8, "length": 36}
		]
	}
}
```

Until frame 48 the stage shows the old form, its label and its bar; the release applies `EnemyTransformed`, and the new form's label, full bar and statuses appear with it. The turn that caused it holds its clock for these 96 frames, then plays its `back` phase. `enemy:tide_colossus_core/attack` gives the core's attacks their own script.

### A battle intro

The warden's battle: an iris closes on the party over the field, opens on the stage, the enemies slide in from the left a few frames apart and the backdrop ripples for the whole battle. Every other battle gets spiral tiles.

```json
"transitions": {
	"party_iris": {"kind": "iris", "shape": "circle", "center": "party:center", "color": "#000000", "ease": "in_out"},
	"spiral_tiles": {"kind": "spiral", "tile": 40, "turn": "cw", "from": "edge", "color": "#101018"}
},
"select": {
	"battle:transition_in": "intro.spiral",
	"battle:transition_in/encounter:warden": "intro.iris",
	"step:intro/encounter:warden": "intro.warden",
	"battle:backdrop/encounter:warden": "warden.water"
},
"scripts": {
	"intro.spiral": {"phases": {"cover": 30, "hold": 4, "reveal": 24}, "tracks": [{"type": "transition", "transition": "spiral_tiles"}]},
	"intro.iris": {"phases": {"cover": 24, "hold": 6, "reveal": 20}, "tracks": [{"type": "transition", "transition": "party_iris"}]},
	"intro.warden": {
		"phases": {"step": 54},
		"tracks": [
			{"type": "move", "on": "enemies", "at": 0, "kind": "enter", "by": [-320, 0], "length": 24, "ease": "out", "stagger": 6, "order": "right_to_left"},
			{"type": "color", "on": "enemies", "at": 0, "mode": "keys", "keys": [[0, {"alpha": 0}], [12, {}]], "stagger": 6, "order": "right_to_left"},
			{"type": "move", "on": "party", "at": 12, "kind": "enter", "by": {"front": [0, 60], "side": [80, 0]}, "length": 18, "stagger": 3},
			{"type": "sound", "at": 0, "cue": "battle_start"}
		]
	},
	"warden.water": {
		"length": 240,
		"tracks": [
			{"type": "backdrop", "effect": "wave", "amplitude": 2, "wavelength": 64, "period": 120, "loop": true},
			{"type": "backdrop", "effect": "scroll", "speed": [0.25, 0], "loop": true}
		]
	}
}
```

The warden is in `game.json`'s `bosses`, so `battle:transition_in/boss` would also reach it; `/encounter:` is tried first. The transition adds one step, `transition iris`, before the intro; the intro step's own sound replaces nothing (the intro step's `battle_start` cue is its own event cue, played once).

### A victory and level-up outro

Victory: the members hop in turn and sparkle; each level-up notice plays a ring and a chime; the field comes back through a fade.

```json
"transitions": {"fade_black": {"kind": "fade", "color": "#000000"}},
"select": {
	"step:outcome.victory": "outro.victory",
	"step:notice.game.level_up": "outro.level_up",
	"battle:transition_out": "outro.fade"
},
"scripts": {
	"outro.victory": {
		"phases": {"step": 72},
		"tracks": [
			{"type": "state", "on": "party", "at": 0, "state": "victory", "length": 72, "stagger": 4},
			{"type": "sprite", "at": 6, "effect": "motes_revive", "spawn": "party", "anchor": "auto", "stagger": 4, "blend": "additive"}
		]
	},
	"outro.level_up": {
		"phases": {"step": 48},
		"tracks": [
			{"type": "sprite", "at": 0, "effect": "ring_stat", "spawn": "screen:50%,35%", "scale": 6, "blend": "additive"},
			{"type": "color", "on": "stage", "at": 0, "mode": "tint", "to": {"tint": "accent", "amount": 0.15}, "in": 6, "hold": 12, "out": 12}
		]
	},
	"outro.fade": {"phases": {"cover": 16, "hold": 4, "reveal": 20}, "tracks": [{"type": "transition", "transition": "fade_black"}]}
}
```

The victory step was 54 frames (`$timing.outcome`, 900 ms); this one is 72, so the result screen comes 18 frames later. The level-up notice's own `level_up` cue still plays as its step starts.

## Frame budget

A turn script's length is the sum of its phases; the hit lands on the `result` phase's start (or a mark there). To hit a target timing:

1. Pick the total and the impact frame. A quick basic attack: about 70 frames with impact near 40; a technique: 90 to 110 with impact near 60; a combination: up to 140.
2. Work back from the impact: `forward + banner + animate + effect = impact`. Keep `banner` at 18 or more, so the name plate can be read.
3. Put anything that travels in `animate` and `effect` so it arrives on the impact: launch frame plus travel frames equals the impact frame.
4. `result` is the time numbers and bars have to play after the last release: the last release frame plus about 24 frames.
5. `back` is 8 to 12 frames.

| Phase | Frames | Starts at |
| --- | --- | --- |
| forward | 8 | 0 |
| banner | 14 | 8 |
| animate | 10 | 22 |
| effect | 8 | 32 |
| result | 24 | 40 (impact) |
| back | 8 | 64 |
| total | 72 | |

A projectile launched at `animate+4` (frame 26) with `travel.frames` 14 arrives on 40. At 1x 72 frames are 1.2 seconds and 75 headless runner frames; at 2x 0.6 seconds and 38 runner frames. Steps outside the script add to the turn: each retarget its `step:retarget` length (320 ms, 19.2 frames, by default), each formation change its change script. The built-in turn at 1x is 117.6 frames (180 + 420 + 280 + 360 + 560 + 160 ms).

The `anim` probe and the previewer show the frame; `expect animlog` asserts it (below).

## Probes, script verbs and the previewer

### State probes (`expect state KEY VALUE`)

* `anim`: the turn, step, change or battle script playing, the key that chose it, the frame and its phase: `spark.bolt ability:spark frame 34/102 animate`, or `-`.
* `animkey`: the lookup of the current turn, each key tried with `-` or the script it found: `encounter:training/enemy:gel_mite/attack - | enemy:gel_mite/attack - | ability:bite - | shape:attack.damage.none - | shape:attack.damage - | shape:attack - | default turn.default`.
* `animframe`: the script frame (works with `waitstate MAX animframe N`).
* `animtracks`: each active track as `ID TYPE FRAME/LENGTH` (tracks without an id by index), joined by `, `.
* `released`: the current step's events, `RELEASED/TOTAL`, then `forced N` when any were forced, then each release as `LABEL KIND @FRAME`: `2/3 Gel Mite A -24 @53, Gel Mite B -22 @59`.
* `figure.LABEL`: a figure as last drawn: `pose attack_blade frame 5 offset 0,-24 color tint #ff786e 0.70 alpha 1.00`.
* `transition`: `iris cover 12/24`, or `-`.
* `backdropfx`: the backdrop's active effects: `wave 2, scroll`, or `none`.

### Verbs

* `animlog mark`, then `expect animlog ENTRY | ENTRY ...` and `expect animloghas TEXT`: the animation events since the mark, by script frame: `start spark.bolt (ability:spark) @0`, `phase effect @46`, `cue spark_zap @46`, `release Gel Mite A -24 @62`, `forced Gel Mite B @94`, `hurry @20>28`, `skip @20>102`, `end @102`, `transition iris reveal @24`. Like `present mark` and `expect present`, an `expect animlog` compares the whole list.
* `expect present` and every existing probe read as before.

A test of the Spark example: `animlog mark`, a round, then `expect animloghas cue spark_zap @46` and `expect animloghas release Gel Mite A -24 @62`, and `waitstate 200 animframe 62` followed by `expect statehas released 1/1`.

### The previewer

With `--dev`, the previewer's picker gains a third list, every select key and script, beside events and the table's entries ([`dev-tools.md`](dev-tools.md), "Previewer").

* `preview anim KEY|SCRIPT [layout front|side] [enemy ID] [count N] [targets N]`: plays it on dummies, as table entries play today (two members, `count` dummies, default three, of the data's first enemy or of `enemy`; `targets` of them hit by a damaging action), from synthetic events shaped like the script's key (a transform for `change:transform.ENEMY`, a felling for `react:instant_defeat`, a combination of two members for `combo:`, the first dummy, of that enemy, acting on the members for `enemy:ID/ACTION`, the first member using the item, on a member when it heals, for `item:ID`; a key of an encounter, `encounter:ENC/...` or one ending in `/encounter:ENC`, in that encounter, so its placements and layout apply, its dummies of the key's enemy, else of an enemy the encounter lists). An enemy dummy's basic attack is its data's attack ability, which the battle log names (`Dummy A uses Talon on Test Fighter.`). A battle script plays over a captured field frame.
* `.` steps one frame forward (as now); `,` steps one frame back, which statelessness allows; `anim seek N` jumps to frame N; `anim step N` steps N frames, negative too; Page Down replays, End stops.
* The `anim` overlay (`overlay anim [on|off]` toggles it; on by default while previewing a script, and kept as set through seeks) draws a frame ruler under the stage: the phases as labeled spans, marks, every track as a bar from its start to its end with its id, sound frames and release frames as ticks, and the current frame as a line; beside it the subjects' anchors as small crosses and each figure's offset.
* `anim why KEY` in the console prints the lookup chain for a key and `anim expand KEY` the expanded script.
* Probes: `preview` reads `anim KEY frame N`; `preview.KEY` reads the previewed stage's probes (`preview.released`).

## The dialogue's `fx` tag

The dialogue's tags table ([`ui-layout.md`](ui-layout.md), "Control tags") has an `fx` action: the tag's value (through `values` and `default`, as for every action that takes an id) names a script of the animation set, played over whatever the screen draws, outside battle, on the controller's clock. An fx script is a step script (one `step` phase, or none: 60 frames) whose tracks are screen-level only: `flash`, `color` on `stage` or `frame` (both fill the whole frame outside battle), `shake` (which moves the field's view as a script shake does) and `sound`. `--check-assets` reports an fx tag of the dialogue layout that names no script, or a script with other phases or tracks (`assets: ui/dialogue.json: tags.glow: no fx script 'no.such_fx' in the battle animation set`); a name the tag's value gives that is not such a script logs one `tag problem:` line. The actions `music_pause` and `music_resume` hold the music track where it is and let it go on. The built-in tags table maps none of them.

## Implementation

| File | What |
| --- | --- |
| `battle/animation.json` | the built-in set, embedded (`EmbeddedResource`) and written by the export |
| `BattleAnimation.cs`, `BattleAnimation.Check.cs` | loading, layering over the built-in set, the check, the export, `FxProblem` |
| `BattleAnimationRun.cs` | a script instantiated for one use: `extends`, fragments with `bind` and `params`, phases, marks and every track's start and length in clock units |
| `BattleAnimationEval.cs` | easing, color transforms, effect animation frames, distances |
| `SampleController.BattleAnim.cs` | selection with the lookup trace, release units, reactions, sounds, the playback's update (play, finish, hurry, fast-forward), the animation log and probes |
| `SampleController.BattleAnimDraw.cs`, `SampleController.BattleAnimSprites.cs` | each figure's look from its tracks and states (chained moves, poses, colors, opacity, defeat kinds, status states), the shake, fills, sprites with travel, paths and impacts, numbers, the scripted backdrop |
| `SampleController.BattleAnimLevel.cs` | the transitions in and out, the split reveal, `name_window`, status idle and tick scripts |
| `SampleController.BattleAnimDev.cs`, `SampleController.Fx.cs` | the previewer, `anim why`, `anim expand`, `anim seek`, `anim step`, the frame ruler; the dialogue's fx |
| `BattleTimeline.cs`, `BattleShow.cs` | the integer clock, steps carrying their scripts and release units; `BattleShow` applies a release's events at their frame |
| `Surface.cs` | `ImageEx` (a flip, and a tint with alpha 0 drawing additively over premultiplied pixels) and `Derive` (a picture made once from another's pixels: silhouettes and palette recolors) |

The stage (`SampleController.BattleStage.cs`) draws every figure, sprite, shake and flash from the scripts; the formulas it had are the built-in set's data. The proof: every headless run's transcript and per-frame digest is byte-identical to the round before this one with no animation file, and the round trip plays the export's `battle_animation.json` with the same digests.

Tests: `scripts/make_anim_assets.py` writes the example set (original placeholder animations: every track type and field on the placeholder abilities and figures, an fx red flash and dusk tint, and the 59.92 Hz variant with release log timing); `scripts/battle_anim.txt` asserts its frames from the animation log and probes on the front data copy (intro, blade, heal, hound, silence tick, staff, boss form, Frost Pane's releases on 53, 59 and 65, a hurry, a fast-forward, the side layout, the previewer with the combination and the two-hand variants, `anim expand`, the 59.92 Hz reload mid-battle with log lines on their release frames); `scripts/battle_anim_layouts.txt` plays it over the 640x480 and 320x224 layout sets; `scripts/dialogue_fx.txt` plays the fx and music tags. `make_anim_assets.py --leftovers` writes the set of the last additions on the export (number windows, `solo`, palettes by index, idle states, a backdrop restored inside an attack, six enemies placed in two ranks, a projectile's trail and directions, a script shake, a mosaic, a layout key), and `scripts/battle_anim_leftovers.txt` asserts it on the front data copy in both layouts, with the previewer's enemy actors, item key, counts, ruler toggle and the windows after a seek, and the fliers' lift read again mid-battle; `scripts/battle_anim_side.txt` plays the example set in the side layout (intro, blade, heal window, hound, tick, staff, boss form, Frost Pane's releases, instant defeat, victory and tiles out) and the two-weapon variants and Twin Arc live through `equip` and `combo` in both layouts. `make_anim_assets.py --more` writes the set of the later additions over a 50% `motion_percent` front layout, and `scripts/battle_anim_more.txt` asserts it on the front data copy: `"40px"` against scaled distances (`figure.LABEL` offsets 40 and 20), `party_others`, a hidden pose lifted (the `figures` probe), the backdrop's added light (`backdropfx`), the flash limit (the run's transcript holds the dropped line), a 6-pixel `"6px"` shake (`battleshake`), release cues off (`expect noaudio cue hit`), a defeat outlasting its step into the next turn, reduced flash dropping a sprite and replacing a flash (`animtracks`), the defend's and the vehicle's actor keys, `item:storm_rod`, and a vertical split into a vehicle battle.

### Probes and verbs as built

* `anim`: `SCRIPT KEY frame N/TOTAL PHASE` (`ex.spark ability:spark frame 34/102 animate`), `-` outside playback. `animkey`: the step's lookup, `KEY SCRIPT|-` joined by ` | `. `animframe`, `animtracks` (`ID TYPE FRAME/LENGTH`, reactions as `KEY:ID TYPE`), `released` (`RELEASED/TOTAL [forced N] LABEL RESULT @FRAME, ...`, counting main results and formation changes: `Tide Colossus Core becomes Tide Colossus Core @48`), `figure.LABEL` with `_` for the label's spaces (`pose hurt frame 5 offset 0,-24 color #ff786e alpha 1.00 defeat`), `transition` (`split reveal 4/20`), `backdropfx` (`tint, wave`).
* `battlejolt` (the jolt the last frame drew, `X,Y`), `enemyplaces` (each enemy standing, back to front, `LABEL X FOOT SIZE`), `animsprites` (each sprite instance drawn, `TRACK frame N [flip] [trail]`); `figure.LABEL` ends in `palette N` while N palette entries recolor it.
* The headless verbs `equip MEMBER PIECE [SLOT]` (a piece worn through the session's equip call, the field menu opened around it; `left_hand` for a second weapon) and `combo RECIPE` (while the members choose: each of the recipe's abilities set as the command of a member who knows it, on the first enemy standing or on the member for an ally's ability, the others defending, and the round resolved) play the weapon variants and a combination's script live.
* `animlog mark`, `expect animlog ENTRY | ...`, `expect animloghas TEXT`: `start ID (KEY) @0`, `phase NAME @F`, `cue ID @F` (a reaction's cue at its own frame), `release LABEL RESULT @F`, `forced LABEL RESULT @F`, `tick STATUS LABEL @F`, `hurry @A>B`, `skip @A>B`, `seek @F`, `preview ID (KEY)`, `end @F`.
* Console (`--dev`): `preview anim KEY|SCRIPT [layout front|side] [enemy ID] [count N] [targets N]`, `anim seek N`, `anim step N` (negative too; after a seek the battle layout's windows show as playing there would have, none still opening or closing), `anim why KEY`, `anim expand KEY` (also logged as `anim: JSON`), `overlay anim [on|off]`. In the previewer `.` steps a frame, `,` steps one back (the preview plays again quietly up to the frame), Page Down replays, End stops; the `anim` overlay draws the frame ruler (phases, the first seven tracks, release ticks, the frame). `preview` reads `anim KEY frame N`. A combination key previews on the first two members; a change key adds the change after the results; a reaction script plays on the dummies' damage.

## Changes from the first draft

Renamed or changed meaning:

* `tint` and `amount` in a color transform: a multiply-only tint (white toward the color), not multiply plus add.
* The clock: units of a millionth of a frame at `settings.rate`, not sub-frames of 1000 to a 60 Hz frame.
* Lookup order: `combo:RECIPE` before `ability:ID` (draft: ability first). `weapon:CATEGORY` became `weapon:CLASS` with a class table and hand counts.
* `phases` (0 to 600 frames) and the 600- and 300-frame script limits: no limit; 36000 frames is a sanity limit with a problem line.
* A `sine` eased move ends where it started (the draft left the end value to the ease).
* The `released` probe counts main results and formation changes, not every event; `figure.LABEL` takes `_` for spaces.
* `layer: over_figures` draws over a vehicle battle's overlay (draft: under it).
* Transitions draw the field live under `cover` (draft: a captured picture of the last field frame).
* Reactions end with their step, whatever their `length`.
* An effect animation's `flip` defaults to `none` (draft: `auto`), so a projectile flying right is drawn as before; `auto` mirrors it.
* A `swap` without `persist` keeps its picture after its crossfade to its script's end (it went back at the crossfade's end).

Added:

* Top level: `settings` (`rate`, `log_timing`), `weapon_classes`, `placements`.
* Times: `$step`. Subjects: `place:N`, `slot:N`; the anchor `place`; point objects with `offset`; `self` in `travel`.
* Track types: `name_window` (`fade`), `react` (`targets`, `pose`, `state`, `effect` and sprite fields).
* `pose`: `"hidden"`, `place_of`, `offset`. `move`: `speed`, `axis`, kinds `sine` and `shake` (`amplitude`, `period`, `phase`, `frequency`, `decay`). `sprite`: `path`, `fade_out`, `column_y`, `spawn: "columns"`, `scale: "large"`, layer `over_labels`; frame -1. `shake`: `offsets`. `color`: `palette` in transforms, `ease` on `set`, `step` keys, default lengths of `tint` and `fade`. `backdrop`: `restore`, `persist`, keys `id:NAME`.
* Figure states: `opacity`, `previous: "crossfade"`, `ease`; defeat kind `frames`. Numbers: `motion: "window"` with `row`, `open`, `pad`. Transitions: kind `split` (`speed`, `frames`), `steps` and `step_frames` on `fade` and `split`.
* Keys: `ability:ID/character:CHAR`, `ability:ID/weapon:CLASS`, `weapon:CLASS/one|both[/character:CHAR]`, `/count:N` and `/encounter:ID` on turn keys, `status:ID/idle`, `status:ID/tick`, `step:recover`, `change:leave`.
* Later: the `"Npx"` and `"Nm"` distance units; subjects `party_others`, `enemies_others`, `allies`; `pose: "shown"` and a later pose lifting `hidden`; the whole color transform on the backdrop; `reduced` on sprites and replacement track objects; the flash limit with `settings.flash_limit`; `event_cues_off` in `settings` and scripts; a defeat state's `outlast`; `item:PIECE`; `/vehicle:ID` on ability and shape keys and `/character:ID` on the shape keys of a member's defend; `split`'s `axis`, and `cover_step_frames` and `reveal_step_frames` on `fade` and `split`.
* The dialogue tags' `fx`, `music_pause` and `music_resume` actions.
* Probes `animkey` as a trace, `figure.LABEL`, `transition`, `backdropfx`; log entries `tick`, `seek`, `preview`; console `anim seek`, `anim step`.
* Last: number windows' `row` per side, `pad` as `[x, y]`, `frame` and `close`; `solo` in `settings` and turn scripts; palette sources by index into a figure's or effect's declared `palette`; `idle` and `idle:flying` states with `lift` and `stagger`; placements `count:6` to `count:8` and ranks; the `/layout:` qualifier on every key; the previewer's `count`, `targets`, enemy actors, item users, `overlay anim` and windows settled after a seek; probes `battlejolt`, `enemyplaces`, `animsprites`; headless verbs `equip` and `combo`.

Removed (not built, below): the capture calls, the per-item `hit_flash` and `hit_fade` fields.

## Not built

* Capturing frames (the mosaic breaks each picture on its own) and a rotating swirl.
* An `idle` state's `move` kinds other than `sine` and `shake` (a hop that repeats, say).
* Checking notice keys against a list the session publishes.
* The previewer's anchor crosses and offsets on the ruler, and previewing a battle-level transition over the field.
* Per-item `hit_flash` and `hit_fade` fields for the strip (the built-in layouts keep the step's; a member's `hit` is true once its damage is released).
* Release frames before the `result` phase are moved to its start, as designed; nothing moves the step's log lines but `log_timing: release`.

## Open questions

* Whether flashes should speed up with the battle speed; scripts scale them with everything else.
* A key list of notices and cancel reasons published by the session would make `step:` keys checkable without the sample's own lists.
