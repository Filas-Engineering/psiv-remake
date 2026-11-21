# Battle animation scripts for the MonoGame sample

Status: design. Nothing in this document is built yet. The sample (`bindings/monogame/Psiv.Sample`) plays battles from the battle presentation table ([`assets.md`](assets.md), "The battle presentation table"); this document specifies a data format that times every action frame by frame, and the work to build it.

The presentation table says how an action looks with a handful of fields: a figure animation, an effect, a cue, two step lengths, a shake and a flash. Battle animation scripts say it as a timeline: where each figure moves and which frames it shows, which sprite effects play where, how figures, the backdrop and the screen change color, which sounds play on which frame, and on which frame each result (a number, a bar, a status icon, a fall) reaches the stage. Scripts live in the asset directory next to the presentation table. A built-in set is embedded in the sample and reproduces today's battle draw call for call, so nothing needs authoring, and every headless run, transcript and frame digest stays as it is.

Scripts only decide when and how results the session already resolved are shown. They never change a result, and the `battlesync` check keeps comparing the shown battle with the session at the end of every round.

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

* The unit is the frame: 1/60 of a second at 1x battle speed. Every time in the file is a whole number of frames, or a string `"Nms"` with whole milliseconds (0 to 10000).
* The timeline keeps time in sub-frames, 1000 to a frame, so a millisecond is exactly 60 sub-frames. Each update adds `dtMs * speed * 3 / 5` sub-frames, where `speed` is the battle speed in percent (100, 150, 200, 300); this is a whole number for every whole-millisecond frame time, so the clock never rounds. The current script frame is the sub-frames divided by 1000, rounded down.
* Old millisecond timings convert exactly: the built-in `forward` step of 180 ms is 10800 sub-frames, 10.8 frames. The built-in set therefore keeps every step's length to the millisecond and every existing run's frame counts (the opening still playing after 50 frames at 1x, an ambush's opening over after 19 frames at 3x).
* The headless runner's frames are 16 ms: 960 sub-frames at 1x, 1440 at 1.5x, 1920 at 2x, 2880 at 3x. Script frame `f` is reached on runner frame `ceil(f * 25 / 24)` at 1x and `ceil(f * 25 / 48)` at 2x. The probes report script frames ([Probes, script verbs and the previewer](#probes-script-verbs-and-the-previewer)), so tests do not depend on this conversion.
* A progress ratio inside a step is computed from two integer sub-frame counts. Since today's progress is the same ratio of two millisecond counts, the float the drawing reads is identical, which the round trip depends on.

A time (TIME) is one of:

| Form | Means |
| --- | --- |
| `12` | frames |
| `"180ms"` | milliseconds |
| `"$timing.STEP"` | the presentation table's length of that step (`forward`, `banner`, `result`, ...) |
| `"$look.animate_ms"`, `"$look.effect_ms"` | the turn's or change's look from the presentation table |
| `"$param.NAME"` | a script parameter |
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

* a number: logical pixels (`10`, `-4`).
* `"Nf"`: forward units. One forward unit is the distance a figure steps forward today, by layout and side: front layout 24 for a member, 20 for an enemy, 12 for a vehicle; side layout 44 for a member and a vehicle, 24 for an enemy. A script written in forward units moves sensibly in both layouts.
* `"Nh"`, `"Nw"`: that fraction of the subject figure's drawn height or width (`"0.5h"`), so the distance follows a figure's `scale`, natural size and frame size.

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
| `changed` | in a change script: the enemy the change brings in or changes |
| `previous_form` | in a change script: the figure an enemy had before a transform or merge |
| `from`, `to` | in a merge: the enemy that leaves, the enemy it merges into |
| `self` | in a reaction script or figure state: the figure it plays on |

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

A point (POINT) is `SUBJECT:ANCHOR` (`"user:head"`, `"target:0:feet"`), `"enemies:center"` or `"party:center"` (the middle of every live figure of that side), `"row:front"` or `"row:back"` (the middle of that enemy rank, on its ground line; a single row is `front`), `"screen:X,Y"` in logical pixels or percentages of the logical frame (`"screen:50%,40%"`), or `"stage:center"` (the middle of the stage, above `stage_bottom`).

### Colors and color transforms

A color is a `styles.json` color name (`text`, `accent`, `bad`, `good`, `tp`, ... [`ui-layout.md`](ui-layout.md), "Colors") or `#rrggbb` / `#rrggbbaa`.

Rendering is not paletted, so palette-style effects are color transforms applied to whatever a sprite, a figure or the backdrop draws. A color transform (CT) is an object:

| Field | Default | What |
| --- | --- | --- |
| `multiply` | `#ffffff` | each channel times this, 0 to 1 (a color, or `[r, g, b]` in 0 to 1) |
| `add` | `#000000` | added to each channel where the picture is opaque, 0 to 255 (scaled by the pixel's alpha, so transparent pixels stay transparent) |
| `alpha` | 1 | the picture's opacity, 0 to 1 |
| `tint` and `amount` | none | shorthand: the picture moved `amount` (0 to 1) of the way toward `tint`, which is `multiply` 1 - amount and `add` tint times amount |

Brightening uses `add`; darkening uses `multiply`. A transform with an `add` term draws the picture, then its silhouette additively (below, [Implementation plan](#implementation-plan)).

### Easing

`linear`, `in` (quadratic), `out`, `in_out`, `smooth` (smoothstep, `t * t * (3 - 2t)`, today's `Ease`), `sine` (`sin(pi t)`: out and back, ending where it started), `step` (jumps at the end).

### Conditions

`if` takes one condition or a list (all must hold). Each is evaluated on the turn's already-resolved events and can only choose what is drawn:

`hit` (a target took damage), `miss`, `critical`, `weak`, `resisted`, `immune`, `defeat` (a target fell), `multi` (more than one target), `single`, `party_actor`, `enemy_actor`, `vehicle` (a vehicle battle), `combo`, `escaped`, `element:ID` (the damage's element), `layout:front`, `layout:side`, a boolean parameter (`"$look.flash"`, `"$param.NAME"`), each negated with a leading `!`.

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
| `phases` | each phase's length (TIME, 0 to 600 frames); a phase left out takes the parent's, else the built-in `turn.default`'s (`$timing.forward`, `$timing.banner`, `$look.animate_ms`, `$look.effect_ms`, `$timing.result`, `$timing.back`). A step script's `step` defaults to `$timing.STEP` of its step; a change script's `change` to `$look.effect_ms` |
| `marks` | name to an AT: names tracks can start from (`"impact+2"`). Phase names, `end` and mark names must not collide |
| `length` | a reaction or looping battle script's length (TIME, or `"$rest"`); a looping script repeats it |
| `tracks` | the timeline, below; a track with the `id` of an inherited track replaces it |
| `remove` | ids of inherited tracks to leave out |

A script lasts at most 600 frames (10 seconds at 1x) in all, phases summed; a battle-level transition at most 300.

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
| `ease` | default `smooth` |
| `length` | default `$phase` |

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
| `layer` | `effects` | `under_figures` (over the backdrop, under every figure), `over_figures` (over the figures, under a vehicle battle's overlay), `effects` (the layout's `effects` slot, under the enemies' labels and the numbers), `top` (over the numbers, under the windows the layout draws after `effects`) |
| `length` | the animation's | an instance is gone after it |

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
| `amplitude` | required | pixels, 0 to 32, or `"$look.shake"` |
| `decay` | `linear` | `linear` (to 0 over `length`), `none`, `out` |
| `axis` | `both` | `both`, `x`, `y` |
| `pattern` | `clock` | `clock`: today's jolt, `sin(clock * 0.09)` and `cos(clock * 0.13)` of the controller's clock; `script`: the same curves of script time, so a seek redraws it exactly |
| `length` | `$phase` | |
| `reduced` | `damp` | |

When several shakes play at once the stage shakes by the largest.

### `color`

A color transform on figures, the backdrop or the screen.

| Field | What |
| --- | --- |
| `on` | a subject, `backdrop` (both of the front layout's backdrop draws), `stage` (a fill over the stage) or `frame` (a fill over the whole frame) |
| `mode` | `set` (the transform for `length`), `flash` (from `to` back to none over `length`), `tint` (to `to` over `in`, held `hold`, back over `out`), `fade` (to `to` over `in`, then held to the script's end), `cycle` (through `colors`, `period` each, `smooth` blending between them), `keys` (`[[AT, CT], ...]`, eased between) |
| `to` | CT |
| `colors` | `[CT, ...]` |
| `period`, `in`, `hold`, `out` | TIME |
| `ease` | default `linear` |
| `reduced` | default `damp` on figures; `drop` for `flash` mode on `backdrop`, `stage` and `frame`; `cycle` holds its first color |

On `stage` and `frame` a transform is a fill: `add` fills additively, `tint` and `amount` fill with that color at that opacity; `multiply` is not available there (the check says so).

### `backdrop`

| Field | What |
| --- | --- |
| `effect` | `scroll` (`speed` `[x, y]` in pixels a frame; the backdrop wraps), `wave` (rows shifted sideways by `amplitude` pixels along a sine of `wavelength` pixels, moving one wavelength every `period`; drawn in bands of `band` rows, 2 to 32, default 4), `offset` (to `by` over `length`, eased), `swap` (crossfade over `length` to `image`, a PNG in the asset directory, or `key`, a backdrop key: `kind:field`, `area:kiln_lower`, `encounter:warden`, `terrain:water`, `generated`), `darken` (shorthand: `color` on the backdrop, `multiply` `#606060`), `tint` (shorthand: `color` on the backdrop with `to`) |
| `loop` | in a looping battle script, repeats with the script |
| `length` | default to the script's end |

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
| `frames` | the sheet entry's | frame indexes of the sheet, played in order |
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
| `directions` | none | frame lists by travel direction, `left`, `right`, `up`, `down`: the dominant axis of the travel picks one (no rotation is drawn) |
| `flip` | `auto` | `auto`: mirrored when traveling right and there are no `right` frames; `none`; `x` |
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
| `trail` | none | `count` copies following `gap` frames apart, each `alpha` times the one before |

An instance with `travel` lives `frames` plus its `hold_last`; with `spawn: targets` there is one projectile per target, each launched by the track's `stagger`.

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
| `motion` | a `move` track's fields (without `on` and `at`), or `{"kind": "sine", "axis": "x"|"y"|"forward", "amplitude": DIST, "period": TIME, "phase": TIME}`, or `{"kind": "shake", "amplitude": px, "frequency": N, "decay": "linear"}` |
| `color` | a CT, or a `color` track's fields (without `on` and `at`) |
| `kind` | defeat states only: below |

The states the battle plays by itself:

| State | When | Built-in (today's look) |
| --- | --- | --- |
| `idle` | always, looping on the controller's clock, each figure 137 ms apart | the figure's `idle` run; a flying enemy in the front layout hovers `size / 5` up, bobbing 5 pixels along `sin((clock + 211 * i) / 420 ms)` |
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

Defeat kinds, each over the state's `length` from the fall's release plus `delay` (TIME, default 0):

* `fade`: opacity to 0.
* `blink`: shown and hidden every `period` (TIME, default 4 frames, at least 4), then gone. With reduced flash it is a `fade`.
* `bands`: cut into `bands` (2 to 32) horizontal bands that vanish one at a time in an order hashed from the enemy's position.
* `slide`: moves `by` (DIST pair) while fading.
* `sink`: moves down `"1h"` and is clipped at its ground line, so it goes into the ground.
* `flash_fade`: `add` white over the first `flash` (TIME), then a fade.

The built-in `transform_in` reproduces what is drawn today: today's code asks for the new form "lit white" with a multiply-only tint, which draws no light. An author gets the light with `"color": {"mode": "flash", "to": {"add": "#ffffff"}}`.

Figure animations in `assets.json`: today a figure entry accepts only the six named animations. With this design an entry may also name any animation `[a-z0-9_]+` that a `pose` track, a figure state or a weapon variant plays. An animation no script and no state plays is a problem (`no battle animation script or figure state plays 'attack_axe'`), as a character pose no script animation plays is today. Back figures take the same names.

## Floating numbers

`numbers` gives the floating text of each released result its motion and color. The text itself stays the text tables' (`ui.battle.float.` keys) and the role stays `battle_numbers`.

Kinds: `damage`, `critical`, `weak`, `resisted`, `immune`, `miss`, `heal`, `tp`, `revive`, `status`, `status_resisted`, `status_ends`, `stat_up`, `stat_down`, `stat_normal`, `no_effect`, `felled`, `felling_resisted`.

| Field | Built-in | What |
| --- | --- | --- |
| `color` | per kind, today's (damage `text`, critical `accent`, weak `#ffa850`, resisted `#96c8ff`, heal `good`, tp `tp`, status `#cea0ff`, felled `#ff5050`, the rest `dim`) | |
| `size` | 28 for damage, weak, resisted, heal, felled; 36 critical; 24 tp; 20 the rest | text size |
| `motion` | `rise` | `rise` (up `rise` pixels over `rise_time`, `smooth`), `pop` (from `pop` times the size to the size over `pop_time`, then still), `bounce` (up `rise` and down `bounces` times, each lower), `float` (rises at `rise` pixels over the whole life), `none` |
| `rise`, `rise_time` | 30, `"50%"` | DIST, TIME (a `%` is of the life) |
| `pop`, `pop_time`, `bounces` | 1.5, 6, 2 | |
| `life` | `"$rest"` | TIME from the release: to the end of its step, today's |
| `fade` | `"30%"` | the last TIME of the life it fades over |
| `delay` | 0 | TIME after the release |
| `shadow` | `[2, 2, "#000000cc"]` | offset and color, or `false` |
| `stack` | 6 | pixels between numbers stacked on one figure, over the text size |
| `from` | `top` | where it starts: today 26 pixels above the figure's top, 36 below the top of a member seen from behind |

## Releasing results

A result step's events are released to the stage by the turn script's `release` tracks. Releasing an event:

* applies it to `BattleShow`: a bar starts running from its old value, a status icon grows in or blinks out, a fall starts the figure's `defeat` (or `fallen`) state, a revival its `rise`;
* starts its floating number;
* starts its reactions (`react:` scripts on the figure, below);
* plays its event cue (`hit`, `miss`, `critical`, `heal`, `status`, `enemy_down`), which today plays when the result step starts.

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
| 3 | `ability:ID` or `item:ID` | the ability or item (worn pieces cast through Item use their piece id, `item:storm_rod`) |
| 4 | `combo:RECIPE` | a combination by its recipe id (`combos.json`) |
| 5 | `weapon:CATEGORY` | a party member's basic attack, by its weapon's category (`blade`, `greatblade`, `rod`, `sling`) |
| 6 | `shape:KIND.EFFECT.ELEMENT` | the element level: kind, first effect and element, as the presentation table's fallbacks (`shape:technique.damage.shock`) |
| 7 | `shape:KIND.EFFECT` | the effect-kind level |
| 8 | `shape:KIND` | the kind (`attack`, `technique`, `skill`, `combo`, `item`, `defend`) |
| 9 | `default` | everything else |

Other keys, each with its own chain:

* Escape and cancel: `turn:escape`; `step:cancel.REASON` (`actor_unable`, `silenced`, `not_enough_tp`, `no_uses_left`, `no_item`, `no_target`), then `step:cancel`.
* Steps: `step:intro`, `step:opening.normal|party_first|enemy_first` then `step:opening`, `step:round`, `step:retarget`, `step:notice.KEY` (the notice's text key, `step:notice.game.level_up`) then `step:notice`, `step:roundend`, `step:outcome.victory|defeat|escaped|scripted_end` then `step:outcome`, `step:sync`. Any step key may end in `/encounter:ID` or `/area:ID`, tried first (`step:intro/encounter:warden`).
* Changes: `encounter:ENC/change:CHANGE.ENEMY`, `change:CHANGE.ENEMY`, `change:CHANGE`, then the change's fallbacks. CHANGE is `join`, `merge`, `transform`, `split` (a transform whose enemy then joins the battle more of itself, `split` in core), `split_piece` (each enemy a split adds; falls back to `join`), `leave` (an enemy gone with no partner; falls back to `merge`, today's look), `self_destruct` and `remove_beside` (the leave that follows those messages; fall back to `leave`). ENEMY is the enemy brought in, changed into or leaving.
* Reactions, per released event on its figure, every matching chain playing: `react:damage`, `react:critical`, `react:miss`, `react:heal`, `react:revive`, `react:status.ID` then `react:status`, `react:status_resisted`, `react:status_ends`, `react:stat`, `react:defeat`, `react:instant_defeat`, `react:felling_resisted`, `react:no_effect`, `react:tp`; each tried as `react:EVENT.ENEMY` first for an enemy figure.
* Battle level: `battle:transition_in`, `battle:backdrop`, `battle:transition_out`, each tried with `/encounter:ID`, `/area:ID`, `/terrain:floor|water|soft`, `/kind:town|field|dungeon`, `/boss` (an encounter `game.json`'s `bosses` names), `/opening:enemy_first|party_first`, `/vehicle` in that order, then bare.

Keys are checked against the data: encounter, enemy, ability, item, recipe, status and area ids, weapon categories, notice keys the game has. Ability before combination follows the requested lookup order; a recipe key only matters for a result ability with no `ability:` key (see [Open questions](#open-questions)).

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

`ease` (default `linear`) shapes every kind's progress. A rotating swirl distortion needs a shader and is not a kind ([Implementation plan](#implementation-plan)).

Under `cover` the first scene is drawn: going into battle, a picture of the last field frame (the controller captures it as the battle starts); coming out, the battle's last frame, live. Under `reveal` the next scene is drawn live.

### The moments

* `battle:transition_in`: plays when a battle starts, before the intro step, as a step of its own (`present: transition KIND`). Its `reveal` shows the stage with every figure in its `idle` state and no banner. The built-in script is 0 frames long, and a battle script 0 frames long adds no step, so the step list stays as today.
* `step:intro`: the intro step (the enemies' names, `battle_start`). Formation entry is its tracks: `move` with `kind: enter` on `enemies` and `party`, staggered, and `color` fades.
* `battle:backdrop`: a looping script (with `length`) that runs on the controller's clock from the intro to the result screen; its tracks are `backdrop` and `color` tracks on the backdrop with `loop`. Not scaled by the battle speed.
* `turn:escape` and `step:outcome.escaped`: how the escape plays (the members turning away and leaving with `exit` moves on `if: escaped`).
* `step:outcome.victory`: the outcome step; the place for the party's `victory` state.
* `step:notice.game.level_up`: each level-up notice, which already plays as its own step after the outcome (`AddBattleNotice`).
* `battle:transition_out`: plays when the result screen closes, before the field takes input again; not a step (the battle is over). The field gets no input while it plays. The built-in one is 0 frames long.

Per-encounter, per-area, per-terrain, per-kind and boss overrides are the `/encounter:ID` and other qualifiers of these keys. The encounter is this codebase's formation: the enemy group of up to eight places.

## Timing control

* Battle speed: scales the clock, so every phase, track, travel and number life is that much shorter. Idle states, status tints and `battle:backdrop` loops run on the controller's clock and keep their speed.
* Reduced flash: each track type has a policy, which `reduced_flash` can change per type and a track's `reduced` per track. Built-in: `flash` tracks drop; `color` tracks in `flash` mode on the backdrop, the stage or the frame drop, on figures damp; `cycle` holds its first color; `shake` damps to a quarter (today's); `flash` transitions become fades; `blink` defeats become fades; the strip's hit flash stays the layout's (framed red only). `damp` is 0.25, changeable as `reduced_flash.damp`.
* Flash limit, with or without reduced flash: at most three screen-wide flashes (`flash` tracks, `color` flashes on the backdrop, stage or frame whose peak `add` is above 64 or whose fill opacity is above 0.25, `flash` transitions) may start in any 60 frames of the controller's clock. A later one is dropped, with one log line per script (`animation: SPARK.BOLT: flash dropped, more than three in a second`). The built-in set flashes at most once a turn.
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
assets: battle_animation.json: scripts.spark.bolt.phases.effect: must be frames from 0 to 600, "Nms", or a $timing, $look or $param reference
assets: battle_animation.json: scripts.spark.bolt: lasts 812 frames; a script lasts at most 600
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

A problem leaves out the entry it is in (the script, the effect animation, the state, the style, the transition, the select key); keys that named a left-out script go on down their lookup chain. The summary line gains `, N battle animation scripts (M keys)`, or `, the built-in battle animations`.

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

* `preview anim KEY|SCRIPT [layout front|side] [enemy ID]`: plays it on dummies, as table entries play today (two members, three dummies of the data's first enemy or of `enemy`), from synthetic events shaped like the script's key (a transform for `change:transform.ENEMY`, a felling for `react:instant_defeat`, a combination of two members for `combo:`). A battle script plays over a captured field frame.
* `.` steps one frame forward (as now); `,` steps one frame back, which statelessness allows; `anim seek N` jumps to frame N; `anim step N` steps N frames, negative too; Page Down replays, End stops.
* The `anim` overlay (`overlay anim`, on by default while previewing a script) draws a frame ruler under the stage: the phases as labeled spans, marks, every track as a bar from its start to its end with its id, sound frames and release frames as ticks, and the current frame as a line; beside it the subjects' anchors as small crosses and each figure's offset.
* `anim why KEY` in the console prints the lookup chain for a key and `anim expand KEY` the expanded script.
* Probes: `preview` reads `anim KEY frame N`; `preview.KEY` reads the previewed stage's probes (`preview.released`).

## Implementation plan

### Phases

1. **Clock and data.** Integer sub-frame clock in `BattleTimeline` (no change in behavior, proved by unchanged transcripts and digests); `GameData` reads recipe ids from `combos.json`, weapon categories from `equipment.json` and the members' right-hand weapon; notice keys the game emits. About 300 lines.
2. **Schema, built-in set and evaluator.** `BattleAnimation.cs` (load, layer over the built-in set, check, problems), `battle/animation.json` embedded and its `Write` for the export, compiling a selected script with its `extends`, fragments, `$look`, `$timing` and per-layout values into a flat, sorted track list in sub-frames when the round's steps are built; `BattleAnimationEval.cs` evaluating tracks at a frame. `SampleController.BattleStage.cs` and `BattleFront.cs` draw from the evaluator instead of their formulas. Gate: every headless run's transcript and per-frame digest identical to the round before, all of them, as the layout round did. About 2000 lines.
3. **Surface.** `IDrawSurface` gains one call, `ImageEx(image, sourceRect, x, y, w, h, ColorTransform, Blend, flipX)`, and an additive `Fill`. `MonoGameSurface` draws the multiply as the tint, the add by drawing a lazily made white silhouette of the image additively, batches additive draws under one `BlendState.Additive` begin, flips with `SpriteEffects`; `RecordingSurface` folds every argument into the digest; `CanvasSurface` blends in software. Capture for transitions: `BeginCapture`/`EndCapture` returning an image of what was drawn between them (the window copies its logical render target; headless returns a handle whose hash is the captured draws' digest). About 500 lines.
4. **Results and figures.** Release tracks and per-event application in `BattleShow` (today it applies a whole step at its start), forced releases, event cues at release, reactions, figure states with weapon variants, defeat kinds, status tints, number styles, per-item `hit_flash` and `hit_fade` fields for the strip slots (the built-in layouts switch to them; identical when releases are at the step's start), the manifest's open figure animation names. About 900 lines.
5. **Battle level.** Transition step, field capture at the battle's start, transition kinds, transition out with the field's input held, `battle:backdrop` loops, `step:` keys with qualifiers, outcome and level-up scripts. About 600 lines.
6. **Tools and tests.** Probes, `animlog`, the previewer's list, seek and overlay, `anim why` and `anim expand`; `scripts/make_test_assets.py` writes an animation file with every worked example, and a broken one with every kind of problem in the broken copy (`check_assets_broken.expected` grows); a new headless run `scripts/battle_anim.txt` on the front data copy asserts each example's frames with `expect animlog` in both layouts, at 1x and 2x, hurried and fast-forwarded, with reduced flash; the round trip includes the export's animation file. Docs: [`assets.md`](assets.md) (the manifest entry, open figure animation names), `bindings/README.md` (probes and verbs), [`dev-tools.md`](dev-tools.md) (previewer), this file's status. About 900 lines of C# and scripts.

### Sample files that change

`BattleTimeline.cs`, `BattleShow.cs`, `BattlePresentation.cs` (exposes the resolved look and timings to scripts; its loader is unchanged), new `BattleAnimation.cs` and `BattleAnimationEval.cs`, new `battle/animation.json`, `Psiv.Sample.csproj` (embeds it), `SampleController.Battle.cs`, `SampleController.BattleStage.cs`, `SampleController.BattleFront.cs`, `SampleController.Audio.cs` (event cues at release), `SampleController.Dev.cs`, `SampleController.cs` (probes), `SampleGame.cs` (capture from the render target), `Art.Battle.cs` and `Art.Files.cs` (silhouettes, effect animation frames, swap backdrops), `AssetManifest.cs` (`battle_animation`, figure animation names), `Assets.cs` (load, files to watch), `PlaceholderExport.cs`, `Surface.cs`, `GameData.cs`, `UiFields.cs` and `ui/battle_front.json`, `ui/battle_side.json` (per-item hit fields), `Program.cs` (check summary, verbs), the scripts above. No change in the C API, the C# wrapper, core, events or game.

### Risks

* **Determinism.** Tracks must be stateless and evaluated from integer sub-frames; dictionary iteration, stagger orders and ties sorted ordinally; jitter and random tile orders from hashes, never `Random`. `MathF.Sin` and other float functions may differ across runtimes, so digests stay a same-machine comparison (as today) and transcripts, which carry no floats, stay the cross-platform contract. The `clock` shake pattern reads the controller's clock, so the previewer's backward step can only reproduce the `script` pattern exactly.
* **Reproducing today exactly.** The built-in set must compute every alpha, tint and position with the same float operations in the same order as today's code to keep digests byte-identical; the phase-2 gate catches a mismatch, and today's "lit white" that draws nothing is reproduced as nothing.
* **Performance.** Waves draw a backdrop in bands (up to 120 draws at 4-row bands), mosaics up to one draw per block (at least 8 pixels: 14400 at 1280x720), additive draws break the sprite batch, silhouettes double the memory of images drawn with `add`. Limits: 256 tracks a script, 8 nested calls, 64 sprite instances a track, and the evaluator caches compiled scripts per round.
* **Round-trip export.** The built-in set written and read back must compile to the same tracks: millisecond times are written as `"Nms"` strings, no float times are written, and the export's check runs over it.
* **Releases inside the step.** Moving `BattleShow` from per-step to per-event application touches the `battlesync` path; forced releases and the `released` probe make a lost event visible.
* **Hot reload mid-round.** Steps carry their compiled scripts; a reload never changes a step already queued.

### Out of scope

Rotation, scaling around a pivot and other affine transforms of sprites (only placement, scale, flip and color); shaders, and with them true palette cycling, swirl distortion and per-pixel dissolves (bands, tiles and color transforms stand in); particle systems with physics; changing any result, the session's random draws or the step list of a round; music and ambient changes from scripts; animating the layout's windows (they keep the layout's open and close transitions); per-cue gain, pitch or pan; scripts outside battle (field and events keep the presentation actions); any engine port: the format is the sample's, though an engine can read the same file.

## Open questions

* Lookup order: the requested order puts the ability before the combination recipe, so two recipes with the same result ability look different only while that ability has no `ability:` key. Swapping 3 and 4 would let recipe keys win.
* Weapon category for two hands: this design takes the right hand's weapon (`weapon` slot), then the left hand's; a two-weapon variant (`weapon:blade+blade`) is not defined.
* The level-up notice names the member by display name, not id (`Notice.Params`), so `step:notice.game.level_up` cannot anchor on the member who leveled without matching names; a parameter with the member's id, or matching by name, has to be chosen.
* "Formation" overrides: this design treats the encounter as the formation; keys by formation size or rank layout are not defined.
* Frame unit: 60 Hz frames against the runner's 16 ms frames (0.96 frame a runner frame at 1x). The alternative, one script frame per runner frame, would make 60 frames 960 ms.
* Releases before the `result` phase are not allowed, because the step's log lines and `present:` line come out at its start; moving log lines to the release frame would lift that rule but change transcripts' timing.
* Whether flashes should speed up with the battle speed; this design scales them and relies on the three-a-second limit.
* Notice keys and cancel reasons are listed from what the sample handles today; a key list the session publishes would make `step:` keys checkable without the sample's own table.
