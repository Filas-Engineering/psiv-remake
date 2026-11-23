# UI layouts for the MonoGame sample

Every screen of the sample (`bindings/monogame/Psiv.Sample`) places its windows, text, numbers, bars, icons and lists from layout files instead of constants in code. Input and behavior stay in code: a layout only places and styles what a screen exposes. The built-in layouts are the sample's own look and are embedded in it (`Psiv.Sample/ui/*.json`); with them every frame is drawn call for call as before the layouts existed, which the headless runs' transcripts and frame digests check. An asset directory's `ui/` files replace them file by file, so another arrangement needs no code change.

The developer tools' own screens (console, picker, previewer, badges, overlays) stay in code ([`dev-tools.md`](dev-tools.md)).

## Files

```
ASSETS/ui/styles.json        resolution, scaling, colors, sizes, text styles, frames, cursors, timings
ASSETS/ui/SCREEN.json        one per screen (the screen keys below)
ASSETS/assets.json           "ui_layout": {"enabled": true}
```

* Without a `ui_layout` entry in `assets.json`, the `ui/*.json` files present load (the file convention); the screens without a file keep the built-in layout, silently.
* With `"ui_layout": {"enabled": true}`, every file is expected: a missing one is a problem and the built-in one stands in. `--export-placeholders` writes this entry with the complete built-in set (below).
* With `"enabled": false`, none load.
* A file that has a problem (not JSON, an unknown field, a wrong type, an expression that does not parse or reads an unknown name, an unknown element type, style, frame, cursor, slot or window) is reported and the built-in file of that screen (or the built-in styles) stands in for the whole screen. A file named after no screen is a problem.
* A layout that still trips the code while drawing (it should not: expressions are total, division by zero gives 0, cycles between windows place the window at its parent's origin) is logged once as `ui: FILE: REASON; the built-in layout of this screen stands in` and dropped for the built-in one.

Every file has `"version": 1`.

Screen keys: `title`, `sound_test`, `credits`, `load_game`, `options`, `controls`, `confirm`, `field`, `dialogue`, `name_entry`, `pause`, `travel`, `shop`, `inn`, `save_prompt`, `panels`, `menu`, `menu_status`, `menu_items`, `menu_equip`, `menu_order`, `menu_macros`, `menu_combos`, `menu_quests`, `menu_codex`, `menu_log`, `menu_options`, `menu_abilities`, `menu_system`, `menu_children`, `extras`, `save_slots`, `quest_log`, `text_log`, `battle_front`, `battle_side`, and the optional `battle_front_vehicle` and `battle_side_vehicle` ("Vehicle battles", below). The two battle files are the two stage layouts; the battle presentation table's `layout: front|side` (and its per-encounter and per-area overrides, `--battle-layout` and the `battlelayout` script verb) chooses which one draws.

## Export, check and reload

* `--export-placeholders DIR` writes `DIR/ui/` with every built-in file, byte for byte, and `"ui_layout": {"enabled": true}`. Played with that directory, the round-trip runs draw every frame as without it (`make bindings` compares the digests of the smoke, actors, panels, battle and vehicles runs).
* `--check-assets` reads the layouts with the rest and prints each problem as `assets: ui/FILE.json: ENTRY: REASON`; its summary line ends with `, N layout files (WxH)` when files load, or `, layouts turned off`.
* F5 and the `assets reload` script verb read the layout files again with the rest of the directory and apply them from the next frame, the resolution included (the window's render target follows). `--watch-assets` sees layout files change too.

## styles.json

```json
{
	"version": 1,
	"resolution": {"width": 1280, "height": 720},
	"scaling": {"mode": "fit", "safe_area": [0, 0, 0, 0], "filter": "auto"},
	"colors": {"text": "#ecece2", "accent": "#fcd660"},
	"sizes": {"small": 16, "body": 20, "title": 28, "line": 26},
	"text_styles": {"small_dim": {"size": "size_small", "color": "dim"}, "hint": {"size": "size_small", "color": "dim", "role": "hint"}},
	"frames": {"panel": {"type": "generated"}, "battle": {"type": "generated", "skin": "battle"}, "none": {"type": "none"}, "parchment": {"type": "nine_slice", "image": "ui/parchment.png", "border": 6, "scale": 2}},
	"cursors": {"pointer": {"type": "generated", "size": 12, "color": "accent"}},
	"timings": {"text_speed": {"slow": 25, "normal": 50, "fast": 100}, "punctuation_pauses": {".": 120}, "toast_ms": 4000},
	"params": {"toast_h": 32, "toast_gap": 6, "toast_pad": 14, "toast_text_y": 7}
}
```

### Coordinates and scaling

* `resolution`: the logical frame every screen draws, 160x120 to 7680x4320 (default 1280x720). Every coordinate in a layout is a whole logical pixel; expressions use whole-number arithmetic (pixel snapping). Headless runs draw at it, the window renders into a target of that size and puts it in the window.
* `scaling.mode`: `fit` (the default and the sample's old behavior: the largest whole number that fits when the window has room, else shrunk to fit), `integer` (only whole numbers, at least 1: a window smaller than the frame crops it, centered; every logical pixel covers the same number of window pixels), `stretch` (any factor that keeps the frame's shape).
* `scaling.safe_area`: `[left, top, right, bottom]`, each 0 to 25 percent of the window, kept clear: the frame fits inside the rest, centered there.
* `scaling.filter`: `point`, `linear`, or `auto` (point at 1x and above, linear when shrunk).
* Mouse clicks go back through the same box into logical pixels, so click-to-walk follows the laid-out frame; a click inside a window whose layout sets `blocks_clicks` does not walk.
* Text size option: the profile's text size scales every text size (the surface's `t`, as before), and `t(n)` in an expression scales a length with it (`t(30)` is 30 at 100% and 45 at 150%). Lengths written without `t()` stay. The built-in layouts use `t()` exactly where the old code scaled its spacing. An element with `"scaled": false` draws its text at 100% whatever the option (the field HUD, the toasts, the battle). Bitmap fonts snap their factor from the scaled size ([`assets.md`](assets.md), bitmap_fonts).
* Natural-size battle figures take the logical height over `battle_figures.reference_height`.
* What the code draws itself takes its sizes from the layouts: the field's cell, its shake and the notices' boxes from the styles' `params` (below), the dialogue box's place from the dialogue file's `params`, the comic panels' margins from the panels file's `params`, the battle stage's enemy labels, bars, status icons, target pointer, floating numbers, effects, front backdrop, figure motion and the developer badge from the battle files' `params`. The built-in values are the 1280x720 look; another resolution sets its own (the small example set below runs 320x224 at 16 pixels a cell).

### Colors, sizes, text styles

* `colors`: name to `#rrggbb` or `#rrggbbaa`. The built-in names are the sample's palette: `background`, `panel`, `panel_top`, `panel_edge`, `panel_shadow`, `row`, `text`, `dim`, `accent`, `bad`, `good`, `tp`, `track`, `black`, `white`. A color property takes a name, a hex literal, or an expression giving one (`"hit ? 'bad' : 'accent'"`).
* `sizes`: named text sizes, read in expressions as `size_NAME` (`size_small` 16, `size_body` 20, `size_title` 28, `size_line` 26, the wrapped line step).
* `text_styles`: name to `{size, color, role}`; `size` an expression, `role` one of the bitmap font roles (`menu`, `dialogue`, `battle_numbers`, `hint`): the text switches to that role's font. An element's own `size` and `color` win over its style's.

### Frames and cursors

* `frames`: name to a frame style, which a window's `frame` or a `box` element names. `type`: `generated` (the sample's window: shadow, light border with clipped corners, dark body, lighter top band; the asset directory's UI skin frame when one is on, [`assets.md`](assets.md), ui), `nine_slice` (`image` a PNG in the asset directory, `border` pixels square corners drawn at `scale`, edges and middle stretched; the generated frame while the picture cannot be read), `fill` (`color`), `outline` (`color`, `thickness`), `none`. `panel` and `none` must exist.
* `skin` (generated frames only): which UI skin frame stands in for the generated window, `menu` (the default: `ui.frame`) or `battle` (`ui.battle_frame`, else `ui.frame`; [`assets.md`](assets.md), ui). The built-in styles have `"battle": {"type": "generated", "skin": "battle"}`, which the built-in battle layouts' windows and boxes name; a styles file that leaves `battle` out gets that one. Without a skin both draw the generated window, so the battle looks as before; with `ui.battle_frame` the battle's windows take it and every other screen keeps `ui.frame`. A battle layout can as well name any frame of its own, a `nine_slice` included.
* `cursors`: name to `{type: generated|image, image, size, color, blink_ms}`, used by `pointer` elements and list rows (`cursor`). An image cursor draws the PNG `size` square; `blink_ms` above 0 hides it every other period.

### Timings

Milliseconds unless said. Defaults are the sample's values.

| Key | Default | What |
| --- | --- | --- |
| `text_speed` | `{"slow": 25, "normal": 50, "fast": 100}` | characters a second for each text speed option (Instant shows at once) |
| `punctuation_pauses` | `{}` | per character, the extra pause after it in the dialogue reveal |
| `auto_advance_ms_per_char`, `auto_advance_min_ms` | 50, 1000 | auto-advance waits the larger of the minimum and the characters times this |
| `text_tick_ms` | 70 | the text cue's interval while a line reveals |
| `next_arrow_ms` | 260 | the dialogue arrow's bob period |
| `wait_unit_ms` | 16 | the unit of `{wait:N}` |
| `caret_ms` | 400 | the text entry caret's blink |
| `cursor_blink_ms` | 0 | every menu cursor blinks with this period (0: steady) |
| `toast_ms`, `toast_fade_ms` | 4000, 400 | how long a notice stays, and its fade at the end |
| `panel_transition_ms` | 350 | a comic panel's slide when its action names no duration |
| `list_repeat_delay_ms`, `list_repeat_ms` | 0, 0 | a held direction repeats in every screen but the field and dialogue after the delay, then every `list_repeat_ms` (0: no repeat) |
| `music_fade_ms` | 800 | the fallback when `game.json`'s `presentation.music.fade_ms` gives none (`GameInfo.MusicFadeMs` -1): a track's cross-fade into the next on an area change, a script's music, a battle's start and end, and the dialogue's `music` tag (also the ambient loop's); 0 cuts (`"timings": {"music_fade_ms": 0}`). Stopping the music keeps its 150 ms fade |
| `game_over_return_ms` | 0 | a lost battle's result screen goes back to the title by itself this long after it comes up; 0 waits for Confirm (`"timings": {"game_over_return_ms": 1500}`). The battle layouts' `game_over_confirm` decides whether Confirm also returns |
| `run_speed_percent` | 200 | the real-time field input's run: a step takes the area's (or vehicle's) step time times 100 over this, 100 at least (`"timings": {"run_speed_percent": 250}`) |

### Params

Numbers the code's own drawing reads, in whole logical pixels; a key left out takes the default, the sample's value. Several screens draw the notices through their `toasts` slot (title, Load, options, the field menu, shop, travel, Extras); the field draws them from its own layout.

Each notice is drawn once, by the topmost screen with a `toasts` slot: while the Extras menu is up over the title or the field menu, the screen under it (its `scene` slot) leaves its notices out when the Extras layout has a `toasts` slot element, and the Extras menu draws them over everything; an Extras layout without one leaves them to the screen under it. The confirm box draws none, so the screen under it keeps them. The pause draws the field without its HUD and notices, as before.

| Key | Default | What |
| --- | --- | --- |
| `field_cell` | 48 | the field's cell in logical pixels, 8 to 256: tiles, people, objects, whole-area pictures, overlays, the camera, click-to-walk, the real-time glide, the interaction prompt's `prompt_x`/`prompt_y` and pointer, the title's field scene and every comic panel (its camera's zoom times this) follow it. A multiple of 16 keeps the generated 16-pixel pictures whole; sheets with a `scale` snap to it ([`assets.md`](assets.md), "How big pictures are drawn") and a scaled tileset must fill it exactly |
| `field_shake` | 10 | pixels a scene's shake of strength 1 moves the field and its panels |
| `toast_h` | 32 | a notice's box height |
| `toast_gap` | 6 | the gap between two stacked boxes |
| `toast_pad` | 14 | the text's inset from the box's left edge; a box is the text's width plus twice this, and the text is cut to the room between the margins less twice this |

Every screen's notices (the field's through its layout, the menu, shop, travel, options, title and Load screen through their `toasts` slots) draw their text at the size of the styles' `small` text style, so a set whose `sizes.small` is not 16 gets it on every screen. The state probe `toastboxes` reads the boxes the code drew in the last frame and that size: `text 10: 120 180 190x14, ...`, or `none`.
| `toast_text_y` | 7 | the text's top below the box's |

```json
"params": {"field_cell": 16, "field_shake": 3, "toast_h": 14, "toast_gap": 2, "toast_pad": 4, "toast_text_y": 2}
```

The field's other small touches (wall edges, foam, shadows, bubbles, the click marker and the pointer's bob) are drawn at 48-pixel sizes scaled to the cell. The probe `leaderpos` reads the leader in pixels of the cell drawn (`48 64` at 16 pixels for cell 3, 4); `paneltiles` reads each panel's cell.

The profile's options still apply on top: the text speed picks the row of `text_speed`, auto-advance switches auto-advance, the battle speed makes window transitions in battle that much faster (and drives the battle playback as before), reduced flash damps the battle's flash and shake as before.

## Screen files

```json
{
	"version": 1,
	"params": {"enemy_row_max": 4},
	"windows": [
		{"name": "box", "anchor": "center", "w": 600, "h": "t(132) + len(options) * t(32)", "frame": "panel", "title": "ui.title.menu",
			"visible": "!game_over", "open": {"kind": "fade", "ms": 200}, "elements": [
			{"type": "text", "value": "title", "y": "t(26)", "w": "cw", "align": "center", "style": "title"},
			{"type": "rows", "items": "options", "selected": "cursor", "x": 80, "y": "t(132)", "w": "cw - 140", "row_height": "t(32)", "height": "t(28)"}
		]}
	]
}
```

### Windows

Drawn in `z` order (default 0; equal `z` in file order), each as its frame, its title plate, then its elements in order.

| Field | What |
| --- | --- |
| `name` | required, unique in the file; shown by the overlay and the probes, and some screens' code places things by a named window (each screen's list below) |
| `x`, `y`, `w`, `h` | expressions; `w` and `h` default to the parent's size, `x` and `y` to 0. `x` and `y` may read the window's own `w` and `h` |
| `anchor` | `top_left` (default), `top`, `top_right`, `left`, `center`, `right`, `bottom_left`, `bottom`, `bottom_right`: the point of the parent the window sits on; `x` and `y` then move it inward from that edge (toward the middle for right and bottom anchors) |
| `relative_to` | another window of the file as the parent instead of the screen; `pw` and `ph` are the parent's size |
| `z` | whole number, -1000 to 1000 |
| `frame` | a frame style name (default `none`) |
| `opacity` | expression, 0 to 1, for the frame |
| `title` | a text key, or a `plate` element (`key`/`value`, `x` default 16, `y` default -14, relative to the window, `size` default small, `color` default accent) |
| `padding` | `[left, top, right, bottom]` expressions: the elements' origin and the content size `cw`, `ch` |
| `visible` | a condition (below); a hidden window draws nothing |
| `open`, `close` | transitions: `{"kind": "none|fade|grow|grow_x|grow_y|slide", "ms": 200, "from": "left|right|top|bottom", "step": 0}`. Open plays when the window becomes visible, or when its screen comes up; close goes the way it came. One rule for the content: a window's title and elements draw on every frame its frame has its full size, with the transition's fade and offset (fade and slide carry the content in and out with the frame); `grow` grows both sides from the middle, `grow_x` only the width and `grow_y` only the height, and a growing or shrinking window shows its frame only, its content from the frame it has its size. `step` (grow kinds only, 0 to 1024 logical pixels, default 0: smoothly) grows by whole steps, at least one step, never past the window's size. In battle, the battle speed shortens them |
| `blocks_clicks` | true: a mouse click inside it does not reach click-to-walk |
| `elements` | the list below |

### Conditions and expressions

Every geometric property, `visible`, `opacity`, `alpha` and most numbers are expressions: numbers, `'strings'`, `true`, `false`, names, `+ - * / %` (whole numbers divide toward zero as C# does; a decimal makes the result decimal), `== != < <= > >=`, `&& || !`, `a ? b : c`, and the functions `t(n)` (a length at the text size), `min(...)`, `max(...)`, `abs(x)`, `clamp(x, lo, hi)`, `len(x)` (a string's or a list's length), `if(c, a, b)`, `measure(text, size)` (the text's width at the text size), `measure_unscaled(text, size)` (at 100%), `fit(text, width, size)` (the text cut with an ellipsis to fit). `+` joins strings. A condition is any expression: a number is true when not 0, a string or list when not empty (`choosing`, `playback && has_followers`, `party_size >= 4`, `vehicle_battle`, `tab == 'items'`).

Names an expression may read, checked when the file loads:

* the screen's fields (each screen below), and on every screen `clock_ms` (the animation clock), `text_scale` (the text size option in percent), `dev` (the developer tools are on), `gamepad` (the last input was the gamepad);
* `W`, `H`: the logical frame (in a screen's params too); `pw`, `ph`: the window's parent's size (window rectangles); `w`, `h`: the window's own size (in its `x` and `y`); `cw`, `ch`: the window's content size; `tw`: a text element's measured width (in its own `x`);
* another window's edges: `NAME.x`, `NAME.y`, `NAME.w`, `NAME.h`, `NAME.right`, `NAME.bottom` (absolute logical pixels);
* `size_NAME` for each of the styles' `sizes`;
* inside a `list`'s `row`, a `rows` or `list`'s `text` and `enabled`, and a `repeater`'s `template`, `empty` and `slots`: the item's fields, `i` (its index), `count` (the rows' count, unless the item has a `count` of its own: "Item rows and counts", below), `selected`, `row_y` (a list row's absolute y), `col`, `row` (a repeater's grid cell), `item` and `text` (a string item).

### Elements

Common to every element: `type` (required), `name` (optional; the overlay and probes show it, and some code measures a named element), `visible`, `x`, `y` (relative to the window's content origin), `w`, `h` (default the content size), `alpha` (multiplies its colors), `scaled` (false: text at 100%).

| Type | Draws | Properties |
| --- | --- | --- |
| `fill` | a rectangle | `color` |
| `outline` | a rectangle's border | `color`, `thickness` (2) |
| `box` | a frame | `frame` (`panel`), `opacity` |
| `plate` | a small framed label | `key` + `args` or `value`, `color` (accent), `size` (small), `style` |
| `text` | text | `key` (a text table key) + `args` (expressions), or `value` (an expression); `format` (a .NET number format for a number value, `N0`, `D3`); `style`, `size` (body), `color` (text), `scale` (times the size); `align` `left`, `center` or `right` within `w`; `max_width` with `ellipsis` (true: cut with `...`; false: cut); `wrap` true to break into lines `max_width` (or `w`) wide, `lines` at most, `line_height` apart (default `t(size_line)`), `reveal` characters shown |
| `bar` | a value bar | `value`, `max`, `color` (good); the UI skin's bar pictures when on |
| `value_bar` | label, `value/max` at the right, a bar under them | `key`, `value`, `max`, `color`, `gap` (22), `bar_h` (10) |
| `image` | a picture the screen gives as a field, or one named by a string | `image` (an image field, -1 drawing nothing, or a string naming a picture: "More layout flows", image elements), `tint` (white), `src` (`[x, y, w, h]` expressions: the part of the picture in its own pixels), `repeat` (true: `src` wraps around the picture) |
| `pointer` | the cursor | `cursor` (a cursor style), `color` (accent), `size` (12) |
| `rows` | a fixed list of menu rows, row `i` at `y + i * row_height` | `items` (a list of strings or items), `text` (per item, default the string or the item's `text`), `selected` (index), `enabled` (per item), `size` (body), `row_height` (`t(size + 10)`), `height` (the highlight's; default size + 8), `first`, `most` (a page of the rows), `per_column`, `column_w` (columns top to bottom first); highlight style below |
| `list` | a scrolling list with a scroll bar, through a screen's scroll state | `items`, `state` (the screen's list field), `active` (false: the cursor's row is not highlighted and the list does not set the `selected` probe), `text`, `enabled`, `size`, `row_height`, `height`, `fit` (the text's width, `w - 40`), `row_w` (the highlight's width, `w - 14`), `view` (true: a list with nothing to pick, the cursor is the first row), `probe` (false: never sets the `selected` and `top` probes), `row` (elements drawn per row instead of the standard row: origin at the row, `row_y`, `selected`); highlight style below. The rows that fit in `h` show; `ScrollList` follows the cursor |
| `hints` | the keys that work here, dim and cut to `w` | `key` + `args` or `value` |
| `tabs` | a row of tabs, the run around the selected one when they do not fit | `items`, `selected` |
| `scroll_bar` | a scroll bar | `top`, `rows`, `count` |
| `repeater` | its `template` per item, `empty` per slot past the items | `items`, `count` (slots; default the items'), `layout` (`row`: `dx` apart, `column`: `dy` apart, `grid`: `columns` wide, `dx` and `dy` apart, `slots`: explicit `[[x, y], ...]` per slot) |
| `group` | its `elements`, offset by its `x` and `y` | `elements` |
| `slot` | whatever the screen's code draws into the rectangle (the map, the stage's figures, a character grid) | `slot` (one of the screen's slots) |
| `role` | nothing: switches the text role for what follows (`menu`, `dialogue`, `battle_numbers`, `hint`), as the old code did between parts | `role` |

Row highlight style (`rows` and `list`): `highlight` (the bar's color, `row`), `cursor` (a cursor style), `cursor_dx`, `cursor_dy` (the cursor's offset), `selected_color` (accent), `text_color` (text), `disabled_color` (dim). Without any of them a row is the sample's standard row.

```json
"open": {"kind": "grow_y", "ms": 160, "step": 8}, "close": {"kind": "fade", "ms": 120}
```

### Params

Some screens' code reads numbers that are not elements (the battle stage's placement rules, the log's wrap width): they are the file's `params`, an object of expressions, listed per screen. A param a file leaves out takes the built-in file's value.

### Dimming behind a screen

Where the screens dim what lies behind them:

* `backdrop_dim` (param; menu, the field menu's frame; shop; travel; save_prompt; name_entry; quest_log; text_log; options, away from the title): the opacity, 0 to 1, of the black the `backdrop` slot draws over the field (0.55 when the files leave it out, as the built-in ones do; 0 draws none). Its expression reads the names every screen has, not the screen's fields.
* pause (`dim` window), confirm (`dim` window), sound_test, load_game and options from the title (the `scene` window's second element): `fill` elements in the files, 0.55, 0.4 and 0.5 in the built-in ones; change their `alpha`.
* panels: the field's `dim` (0.45 times the most shown panel), which the built-in `panels` file's `fill` reads as its `alpha`.
* The title's generated field scene is tinted in code under the title, Load, Options and the sound test; `title_art.background` replaces the scene and its tint ([`assets.md`](assets.md), "title_art").
* The inn, the dialogue and the field HUD draw over the field undimmed; the battle has no dimming.

```json
{"version": 1, "params": {"backdrop_dim": 0.3}, "windows": [...]}
```

The state probe `backdropdim` reads the opacity the last frame dimmed with (`0.3`), `-` when no `backdrop` slot drew the field.

## Probes, the overlay and the example set

* `expect state windows`: every window the last frame drew, as `SCREEN/NAME X Y WxH`, joined by `, ` (`none`); `expect state window.NAME` (or `window.SCREEN/NAME`): one of them, `-` when not drawn.
* With `--dev`, F1 or `overlay windows [on|off]` outlines every window drawn with its screen and name over the frame (`overlay all` and `overlay none` leave it as it is; `overlay.windows` reads how many it outlined; the console's `help overlays` lists it).
* `expect state overlaps`: off the battle, each window of the last frame that a notice's box covers part of, as `toast over SCREEN/NAME`, joined by `, ` (`none`). A window that holds the whole box (the screen-wide ones, the one the box stands in) does not count; a notice over the field menu's page or a field window does. In the battle it is the front layout's probe (below).
* `expect state uiframes`: each frame style the layouts drew in the last frame as `NAME HOW COUNT`, by name: `HOW` is `generated`, `skin` (a generated or unreadable nine-slice frame drawn with the asset directory's UI skin frame), `nine_slice` (the picture), `fill` or `outline` (`none`).
* `scripts/make_layout_assets.py` writes a second, deliberately different layout set (below, "An example layout set"), and with `--small` a third at 320x224 (below, "A small example set"); `make bindings` checks both and plays them.
* `scripts/make_test_assets.py` breaks the export's layouts in the broken copy (a file that does not parse, one with every kind of problem, a missing file with the entry on, broken styles, a file of no screen, and a hostile but valid pause layout with negative sizes, cycles, division by zero and huge counts); `--check-assets` must print `scripts/check_assets_broken.expected`, and the saves run passes on that directory.

# The screens

Each screen's file, its windows, the fields it exposes, item fields, slots and params.

## Title (`ui/title.json`)

Windows: `scene` (the title's field scene, slot `scene`: the title art's background and layers when the assets give them, `assets.md`, "title_art"), `box` (the title, or the logo in its place, the subtitle, the traveler and creature, the game over line, the options), `corner` (the version stamp at the top right and the toasts, slot `toasts`), `hint` (the keys' bar).

Fields: `title` (string, the game's title), `subtitle` (string), `hero_frame`, `creature_frame` (images, animated by the clock; the title art's traveler and creature when given), `hero_w`, `hero_h`, `creature_w`, `creature_h` (int: the sizes to draw them at, 64 by 64 unless the title art gives a reference height, then their frames' size times `art_scale`), `logo` (image, the title art's logo, -1 without one), `logo_w`, `logo_h` (int, its size in pixels times `art_scale` when there is one, 0 without a logo), `art_scale` (int, the title art's natural multiple, 0 without a reference height), `game_over` (bool, the last game ended in defeat), `options` (list of strings), `cursor` (int), `version` (string, the library version), `hint` (string).

Erase: the title offers Erase when its layout's `options` lists `erase` (below, "Title start state and entries"), or, in the built-in order, when its param `erase` is not 0 (the older switch, kept as an alias; the built-in file sets neither, so the built-in title's options are as before). Erase lists every slot with a file, damaged ones too (`Saves()`, manual slots first), asks `ui.title.erase_confirm` with the slot through the confirm screen, calls `EraseSave` and shows the session's `game.save_erased` notice or the refusal, then reads the list again; Back returns to the options. Fields: `erasing` (bool, the list is up), `erase_rows` (list of strings: the slot, `(autosave)`, then the area and play time or `(damaged)`; `No saves to erase.` when none), `erase_cursor` (int). The built-in file draws them in a window named `erase` over the box.

Continue: the param `continue` (a plain string, not an expression): `"newest"` (default: Continue loads the newest save through `ContinueGame`) or `"choose"` (Continue opens the Load list, the `load_game` screen the Load entry opens, and Back returns to the title; with no save Continue still calls `ContinueGame`, so the session's refusal shows). Any other value is a problem.

```json
{"version": 1, "params": {"continue": "choose"}, "windows": [...]}
```

## Credits (`ui/credits.json`)

The credits a script's `credits` plays (`MenuKind.Credits`). Windows: `backdrop` (the dark fill), `roll` (the lines; the code scrolls them so the newest stays inside this window's height), `hint`. Fields: `lines` (items: `text`, the entry's rendered text; `heading` (bool); `y`, its top in pixels from the roll's top, each entry after its `gap` of empty lines), `scroll` (int, pixels the roll has moved up), `shown`, `total` (int: entries come up and in all), `hint` (string). Confirm or Back ends them (`CloseMenu()`), and so does the session's end once every entry has had its time; the credits' `music` plays meanwhile. State probe `credits`: `SHOWN/TOTAL scroll N`.

The `scene` slot's rectangle is where the scene goes: the title art's background is stretched over it, each layer's `at` counts from its top left and its scrolling repeats across it, the generated field scene fills it, and what reaches outside it is clipped (the built-in files give the whole screen). The same holds for the `scene` slots of the sound test, the Load screen and the options from the title.

The built-in box draws the logo at its size, centered on the title's line, and the title's text only without a logo:

```json
{"type": "text", "value": "title", "visible": "logo < 0", "x": "-box.x", "w": "W", "align": "center", "y": "t(26)", "style": "title"},
{"type": "image", "image": "logo", "x": "(cw - logo_w) / 2", "y": "t(42) - logo_h / 2", "w": "logo_w", "h": "logo_h"}
```

The traveler and the creature stand on the bottom of today's 64-pixel squares, so larger natural sizes rise out of them:

```json
{"type": "image", "image": "hero_frame", "x": "28 + (64 - hero_w) / 2", "y": "82 - hero_h", "w": "hero_w", "h": "hero_h"}
```

The state probe `titlesizes` reads what the last title frame gave the layout: `logo 480x80 traveler 32x32 creature 32x32 scale 2`.

## Sound test (`ui/sound_test.json`)

Windows: `scene` (the title scene, slot `scene`, and the dimming), `box` (the track list, the playing line, the keys).

Fields: `tracks` (list of strings, every audio id's name), `list` (the list's scroll state), `playing` (string, the track playing, empty for none), `hint` (string).

## Inn (`ui/inn.json`)

The map draws first, in code. Window: `box` (the bed, the cost and money line, Rest and Leave).

Fields: `top` (int, the box's top as the dialogue box would sit), `bed` (image, the area tileset's bed), `cost`, `money` (int), `options` (list of strings), `cursor` (int), `keeper` (image: the portrait the assets' `keepers` give the rest point, -1 without one; [`assets.md`](assets.md), "keepers"; the built-in file does not draw it), `per_member` (bool: the area data's `per_member`, so `cost` is the whole lineup's) and `cost_each` (int: the price per member, `cost` otherwise), `voice_line` (string: the keeper's line, below). The built-in file draws none of the last three. `name` (string: the rest point's display name, `InnInfo.Name` from the area data's `name`, `""` without one) and `title` (string: `name`, else `ui.inn.title`), which the built-in file shows as the box's title.

The keeper's line (`voice_line`, inn and shop): the point's voice (`InnInfo.Voice`, `ShopInfo.Voice`, the area data's `voice`) followed by `.welcome` when a shop opens, `.confirm` (with the cost) when an inn opens, `.thanks` after a purchase, sale or rest (the inn closes, so its thanks shows as a notice), `.cannot_afford` after a refusal for money; the tables' text for that key, else `ui.shop.*` or `ui.inn.*` with the same ending. A shop's `confirm` and `thanks` first try `MOMENT_buy` or `MOMENT_sell` by the side of the trade (`VOICE.confirm_buy`, `VOICE.thanks_sell`, then `ui.shop.confirm_buy` and so on), falling back to the plain ending, so data adds the split only where it wants it. A shop's line takes, in order, `{0}` the shop's name (`ShopInfo.Name`; its kind word when the data names none), `{1}` the item's name, `{2}` the quantity, `{3}` the total price and `{4}` the price of one: those of the trade for `thanks`, those of the row and count chosen for `confirm`, empty for `welcome` and `cannot_afford`. An inn's line keeps the library's `{0}`, the price (`ui.inn.confirm` is the library's text with that parameter), and has no others. The state probe `voice` reads the key the last frame used.

```json
"voice.stall.confirm_buy": "{2} {1} at {4} each, {3} in all?",
"voice.stall.thanks_sell": "{3} for {2} {1}. A fair trade."
```

The state probe `keeper` reads the keeper's portrait id the last shop or inn frame gave its layout, `-` for none.

## Save prompt (`ui/save_prompt.json`)

Windows: `backdrop` (the dimmed field, slot `backdrop`), `box` (the question, the slot list in slot `slot_list`, drawn by the `save_slots` layout, and the Don't save row).

Fields: `leave` (list of one string, the Don't save row), `leave_selected` (bool).

## Confirm (`ui/confirm.json`)

The Yes/No question over any screen. Windows: `dim`, `box` (sized by the question).

Fields: `question` (string), `on_yes` (bool, Yes is highlighted), `options` (list of strings, Yes and No).

## Field (`ui/field.json`)

The map draws first, in code (`DrawField`), outside the layout: the banner's state advances while the map draws, so the
fields are read after it. There is no field view window.

Windows:

* `hud`: the area plate at the top left and the money and play time plate at the top right, both on the unscaled surface.
* `banner`: the area banner, centered, sized by its two texts at 100% text size, faded and dropped by its fields.
* `toasts`: the newest notices stacked up from the bottom left, newest lowest.
* `prompt`: the interaction prompt, a plate with the key and a word centered above the hint bar, sized by its text
  (`visible` is `prompt_visible`).
* `hint`: the keys' hint bar across the bottom, on the unscaled surface.

Fields:

* `hud` (bool): the field has an area to show (the HUD windows' `visible`).
* `area` (string): the area's name.
* `status` (string): the money and the play time, formatted.
* `banner_visible` (bool): the banner is up.
* `banner_title` (string): the area's name as the banner shows it.
* `banner_kind` (string): the area's kind, worded (empty while the banner is not up).
* `banner_alpha` (number): the banner's opacity, 0 to 1: in over 300 ms, out over the last 500 ms.
* `banner_drop` (int): pixels the banner sits above its resting place while it fades, 16 at opacity 0.
* `banner_ms` (int): milliseconds since the banner came up.
* `banner_left_ms` (int): milliseconds until it goes (2600 in all).
* `toasts` (items): the newest four notices, newest first.
* `hint` (string): the keys' hint.
* `prompt_visible` (bool): the real-time field input is on, its `Interaction prompt` option is on (the default), the
  leader stands (no step under way) and Confirm would act on something from where it stands. Always false with the
  classic field input, so no existing run draws the window.
* `prompt_key` (string): Confirm's first binding on the device last used (`Enter`, `A`), empty without a prompt.
* `prompt_text` (string): what Confirm would do: `talk` (`ui.hint.talk`), `open`, `board`, `shop`, `rest` or `travel`
  (`ui.prompt.*` keys, English built-ins while the text tables lack them), empty without a prompt. Under game.json's
  `interact: "nearby"` it comes from the session's `InteractTarget()` (`read` for a hook with no actor, `talk` for an actor)
  and the marker goes over the target's cell, beyond a counter when it is used across.
* `prompt_x`, `prompt_y` (int): the screen pixel at the center of the target cell, as the last frame drew the map.
* `prompt_ready` (bool): false when the session's target would refuse (`InteractTarget.Ready`, under `interact: "nearby"`); the built-in plate is drawn at half opacity then. Always true with the sample's own search.

The built-in prompt:

```json
{"name": "prompt", "visible": "prompt_visible", "x": "(W - w) / 2", "y": "H - t(30) - 46", "w": "measure_unscaled(prompt_key + '  ' + prompt_text, size_small) + 24",
	"h": 28, "elements": [
	{"type": "plate", "value": "prompt_key + '  ' + prompt_text", "color": "accent", "scaled": false}
]}
```

The map also draws the `pointer` sprite bobbing over the target cell while the prompt shows (left out, like a speech
bubble, while the leader stands on the cell above the target); the asset manifest's `sprites.pointer` replaces it
([`assets.md`](assets.md)). A layout file without a `prompt` window shows only the pointer. The example set puts the
prompt as a card under the target's right edge from `prompt_x` and `prompt_y`.

Item fields of `toasts`: `text` (the notice cut to the width between 16-pixel margins), `alpha` (its opacity as it
expires), `box_alpha` (its box's opacity, `alpha` times 0.95), `width` (its box's width: the text's plus twice the
styles' `toast_pad`). The built-in file names each notice's `box` element `toast`, which the `overlaps` probe reads as a
notice's box.

Click-to-walk ignores a click inside a window drawn last frame whose layout sets `blocks_clicks` (none does by default).

## Dialogue (`ui/dialogue.json`)

The map draws first, in code, then the layout. The box's top (`top`) is the code's `DialogueTop`: the bottom of the
screen, or the top when the leader would be covered, or over panels where it covers the fewest. The Inn uses the same.

Windows:

* `banner`: the area banner as on the field, shown while the box sits at the bottom and no panels are up.
* `box`: the dialogue box: the speaker plate, the portrait frame and portrait, the revealed line, the next/skip hint and
  the bobbing arrow; or a choice's prompt and options. It switches the text role to `dialogue` first and back to `menu`
  last, so the speaker, the line and the options draw in the dialogue's font.

Fields:

* The banner's fields as on the field (`banner_visible` also needs the box at the bottom and no panels).
* `top` (int): the box's top edge.
* `box_visible` (bool): a line or a choice is up (between lines the field shows without a box).
* `has_line` (bool): a line is up.
* `speaker` (string): the speaker's name, empty for narration or a choice.
* `portrait` (image): the speaker's portrait, -1 without a speaker.
* `line` (string): the whole line.
* `revealed` (int): characters of it shown so far (the `text` element's `reveal`).
* `all_shown` (bool): the whole line is shown (the arrow's `visible`).
* `line_step` (int): the step between wrapped lines in the dialogue's font: its bitmap font's line height when one draws
  the text, else 26 at the text size.
* `hint` (string): the next and skip keys.
* `next_arrow_ms` (int): the arrow's bob period (`timings.next_arrow_ms`).
* `arrow` (image): the arrow picture.
* `has_choice` (bool): a choice is up instead of a line.
* `prompt` (string): the choice's prompt, or the default one.
* `options` (list of strings): the choice's options.
* `choice_header` (string): the choice's `header` (`../events/FORMAT.md`, "Choices"), empty for none; the built-in
  layout does not draw it, a layout that wants it inside the list window puts a `text` element there.
* `cursor` (int): the option under the cursor.
* `picture` (image): the still picture the `picture` tags show (below, "The `tags` table"), -1 for none. `picture_w`,
  `picture_h` (int): its size in pixels, 0 for none.
* `shown_ms` (int): milliseconds since the box's text was all shown, 0 while it reveals (`"visible": "shown_ms > 400"`).
* `line_key` (string): the line's text key (`harbor.welcome`), empty without a line.
* `picture_top` (string): the name of the picture on top (the one `picture` shows), empty for none; `picture_names`
  (list of strings): the stack's names, the top last.
* `page_arrow` (image): the UI skin's `page_arrow` picture ([`assets.md`](assets.md), ui), else the generated pointer
  the `arrow` field gives; the battle layouts have it too, for `result_more`.

```json
{"type": "image", "visible": "all_shown", "image": "page_arrow", "x": "cw - 48", "y": "ch - 40 + clock_ms / max(1, next_arrow_ms) % 2 * 3", "w": 24, "h": 24}
```

Params (the code's, with these defaults; the built-in file leaves them out): `box_h` (176, the box's height at 100% text size, scaled with the text size option), `box_margin` (16, from the screen's bottom edge to the box at the bottom), `box_top` (36, the box's top while the leader would be covered). `top` is the screen's height less the scaled `box_h` and `box_margin`, or `box_top`; the inn's `top` and the comic panels' room at the bottom (below) follow them. Expressions of the common names (`W`, `H`, `dev`...).

```json
"params": {"box_h": 70, "box_margin": 14, "box_top": "dev ? 42 : 4"}
```

The built-in file draws the picture at its size in a framed window `picture` that fades in and out, centered in the room
the box leaves above it (box at the bottom) or under it (box at the top):

```json
{"name": "picture", "visible": "picture >= 0", "anchor": "top", "y": "top > H / 2 ? max(16, (top - h) / 2) : top + t(176) + max(16, (H - top - t(176) - h) / 2)",
	"w": "picture_w + 16", "h": "picture_h + 16", "frame": "panel", "open": {"kind": "fade", "ms": 200}, "close": {"kind": "fade", "ms": 200}, "elements": [
	{"type": "image", "name": "picture", "image": "picture", "x": 8, "y": 8, "w": "picture_w", "h": "picture_h"}
]}
```

The probe `wrapped` reads the line's `text` element as drawn: the renderer reports the line count and then each line's
width through `Probe` (`wrapped`, then `wrapped_width` per line) while the dialogue role is active. `dialogue` (top or
bottom) stays in code.

## Name entry (`ui/name_entry.json`)

Windows:

* `backdrop`: the dimmed field (slot `backdrop`).
* `box`: the window: the member's portrait, the prompt, the text box and letter grid (slot `entry`), the keys' hint.

Fields: `portrait` (image) the member's portrait; `member_name` (string) the member's name, the prompt key's argument;
`entry_h` (int) the height the text box and grid take at the text size; `hint` (string) the keys' hint (keyboard or
gamepad).

Slots: `backdrop`, `entry` (the text box and grid, drawn at the slot's x, y and width).

Params, the name pad's sizes in logical pixels (also `menu_macros`' params for its `name_pad` slot); each left out keeps
the built-in size at the text size option: `pad_box_h` (the text box's height, 50), `pad_gap` (between the box and the
grid, 14), `pad_cell_w` (a letter cell's width; 0 or left out: the slot's width over 13, 44 at most), `pad_cell_h` (36),
`pad_entry_size` (the typed name's text size, 28), `pad_label_size` (the cells' text size, 20), `pad_text_x`, `pad_text_y`
(the typed name's inset, 16 and 8), `pad_label_y` (a label's top in its cell; left out: centered for a 20-pixel text).
`entry_h` follows them. Text sizes still scale with the text size option, as the surface draws them.

```json
"params": {"pad_box_h": 22, "pad_gap": 4, "pad_cell_w": 22, "pad_cell_h": 17, "pad_entry_size": 16, "pad_label_size": 16, "pad_text_x": 4, "pad_text_y": 3, "pad_label_y": 1}
```

## Pause (`ui/pause.json`)

Windows:

* `scene`: the battle, or the field (slot `scene`).
* `hud`: the field HUD as on the field (not over a battle).
* `dim`: the screen dimmed.
* `box`: the pause window with its rows.
* `hint`: the keys' hint bar.

Fields: `hud`, `area`, `status` as on the field (`hud` is false over a battle); `rows` (list of strings) resume,
options, quit; `cursor` (int) the row under the cursor; `hint` (string).

Slots: `scene`. The `selected` probe stays in code.

The built-in file takes its sizes from the styles' `size_body` and `size_small` (the box `t(size_body * 18)` wide, at most
`W - 16`, rows `t(size_body + 12)` apart; the hint bar `t(size_small + 14)` high), so a set at another resolution that
leaves `pause.json` out gets a pause that fits; with the built-in sizes it is the 1280x720 one as before.

## Travel (`ui/travel.json`)

The ferry, the ship's helm and the town or way-out destinations of a travelling item or technique.

Windows:

* `backdrop`: the dimmed field (slot `backdrop`).
* `box`: the window as tall as its rows (at most 8 shown): the question, the destination list (with the leave or cancel
  row last), a rule and the description of the highlighted row.
* `hint`: the keys' hint bar.
* `toasts`: the notices (slot `toasts`: x is the margin from the right edge, y the bottom they stack up from).

Fields: `title` (string) ferry, ship, the item's or technique's name, or travel; `question` (string); `labels` (list of
strings) the destinations and the leave row; `list` (scroll list); `description` (string) what the highlighted row
does, or the no-description text; `description_missing` (bool) the data had no description; `hint` (string).

Slots: `backdrop`, `toasts`. The list probes `selected` and `top`; `description` stays in code.

## Shop (`ui/shop.json`)

Windows:

* `backdrop`: the dimmed field (slot `backdrop`).
* `box`: the shop window, titled with the shop's kind. Without a shop only the window shows. Inside: the buy/sell tabs,
  the money, the stall picture, the column heads, the stock or sellable list (name, price at the right, count), the
  rule, the description and, for equipment, the leader's comparison and who can wear it; or the nothing-to-buy or
  nothing-to-sell line; the notices (slot `toasts`, x the margin from the right edge, y the bottom); the keys' hint.

Fields: `title` (string); `open` (bool) a shop is open; `tab` (int) 0 buy, 1 sell; `tabs` (list of strings); `money`
(string, formatted); `stall` (image); `rows` (items); `list` (scroll list); `description` (string) and
`description_missing` (bool) as on the travel screen; `compare` (string) the equipment comparison and wearers, empty
when not equipment; `hint` (string); `money_value` (int, the money as a number); `keeper` (image, the shop's keeper as on
the inn, -1 without one or without a shop; not drawn by the built-in file); `voice_line` (string, the keeper's line as on
the inn; not drawn by the built-in file); `name` (string, the shop's display name, `ShopInfo.Name` from the area data's
`name`, `""` without one; `title` is it when set, else the kind word); `bag_used`, `bag_slots` (int, `WorldInfo.BagUsed`
and `GameInfo.BagSlots`, game.json's `bag_slots`, 0 for no limit) and `bag` (string, `Bag {0}/{1}` in the sample's words,
key `ui.common.bag`, `""` when the bag has no limit), which the built-in file draws left of the money. A purchase the
bag cannot take is refused with the game's `game.refuse.bag_full` as a notice (or `result_text` in the steps), and a
chest that stays shut says `game.bag_full` among the notices.

```json
{"type": "text", "visible": "bag != ''", "value": "bag", "w": "cw - 260", "align": "right", "y": 34, "style": "small_dim"}
```

```json
{"type": "group", "visible": "keeper >= 0", "x": 480, "elements": [{"type": "image", "image": "keeper", "w": 64, "h": 64}]}
```

Item fields of `rows`: `text` (the name; the list's row text), `price` (int: the price, or the sell price on the sell
tab), `have` (int: how many the party has). `count` is not used for it: inside a list row it names the row count.

Slots: `backdrop`, `toasts`. The list has `probe: false`: the code sets `selected` (and leaves `top`) as before.

### The shop in steps

Param `steps` (0 or 1, default 0: the two tabs as before). With 1 the shop goes in steps, and the field `step` says which
(`choose`, `list`, `quantity`, `result`; `tabs` without steps):

* `choose`: Buy or Sell (`choices`, list of strings; `choice_cursor`, int); Up or Down switch, Confirm opens the list
  (`tab` follows), Back leaves the shop. The keeper says `welcome`.
* `list`: the stock or the sellable items as on the tabs; Back goes back to the choice, Confirm takes the row.
* `quantity`: how many (`quantity`, int, from 1), at most `quantity_max` (what the money buys, at least 1 so a refusal can
  show, or what is held; 99 at most); Right or Up adds one, Left or Down takes one, wrapping; `unit_price` and `total`
  (int) the price each and times the quantity. The keeper says `confirm`. Confirm trades the quantity at once
  (`psiv_shop_buy` and `psiv_shop_sell` with a count), Back goes back to the list.
* `result`: the trade's outcome; the keeper says `thanks` or `cannot_afford`. Confirm or Back goes back to the list.

The keeper's line is `voice_line` (VOICE.MOMENT, else `ui.shop.MOMENT`, with the buying and selling keys and parameters of "Inn", above). `result_text` (string) is the last trade's
outcome, the session's notice (`Bought 3 Tonic for 24.`) or refusal, and `result_ok` (bool) whether it went through.
When the layout shows `result_text` (any element reads it), trade outcomes and the shop's other notices go there and
to the log instead of the notices, in steps or on the tabs.

```json
{"version": 1, "params": {"steps": 1}, "windows": [
	{"name": "quantity", "visible": "open && step == 'quantity'", "anchor": "center", "w": 420, "h": 150, "frame": "panel", "elements": [
		{"type": "text", "value": "'x ' + quantity + ' / ' + quantity_max", "w": "cw", "align": "center", "style": "title"},
		{"type": "text", "value": "'Total ' + total", "y": 76, "w": "cw", "align": "center", "color": "accent"}]},
	{"name": "result", "visible": "open && step == 'result'", "anchor": "center", "w": 520, "h": 110, "frame": "panel", "elements": [
		{"type": "text", "value": "result_text", "wrap": true, "lines": 2, "color": "result_ok ? 'text' : 'bad'"}]}
]}
```

`scripts/make_shop_assets.py` writes a test asset copy with such a layout (choice, list, quantity, result, the keeper's
line on top) and `alt/shop_tabs.json`, the built-in tabs with a `result_text` line; `scripts/shop_steps.txt` buys three,
is refused for money, sells all held, then buys one on the tabs, with no notice shown; screenshots under
`build/shots/shop/`.

## Comic panels (`ui/panels.json`)

Drawn by the field draw whenever a scene shows panels, on whichever screen shows the field (all on the unscaled
surface).

Windows:

* `panels`: the field dimmed behind the panels, then each panel: its frame (the UI skin's nine-slice frame through slot
  `frame`, or the generated shadow, black border and light inner border), its background, and the live scene (slot
  `scene`, which clips to the slot's rectangle and darkens the scene while the panel comes in or goes).

Fields: `dim` (number) the dimming's opacity, 0.45 times the most shown panel; `panels` (items).

Item fields of `panels`: `left`, `top`, `width`, `height` (int: the panel's rectangle as it slides in or out), `shown`
(number, 0 to 1: how far it has come in), `shadow_alpha` (number: the shadow's opacity, 0.7 times `shown`), `skinned`
(bool: the UI skin has a frame for the panel's camera or for panels).

Slots (per item): `frame` (the skin's frame drawn over the slot's rectangle), `scene`.

Params (the code's, with these defaults; the built-in file leaves them out): `margin` (24, the panels' slots stand this far in from the screen's edges), `gutter` (16, between two slots), `slide` (64, how far a panel slides in from the nearest edge), `bottom` (the band the slots leave free at the bottom for the dialogue box: the dialogue file's `box_h` and `box_margin` and `gutter`, 208 by default). A panel's cell is the styles' `field_cell` times its camera's zoom, and a third more while its camera is the scene's (the reed path's lookout: 16, 20 and 24, the pushed-in east panel 27, at a 16-pixel cell).

```json
{"version": 1, "params": {"margin": 6, "gutter": 4, "slide": 16}, "windows": [...]}
```

## The field menu, the logs, the Load screen and the options

Every element of these layouts draws on the text-size surface (`scaled` is never false). Several are drawn into a rectangle that another layout's `slot` gives: those layouts read the rectangle from the area fields and place a window on it.

Area fields (int): `area_x`, `area_y`, `area_w`, `area_h`, the rectangle the parent slot gave.

The ListRow look (highlight, pointer, text color) is spelled out with `fill`, `pointer` and `text` elements in the row templates that draw more than one text per row (`save_slots`, `menu_status` abilities, `options`, `controls`): the highlight is `fill` at (-6, -3), `w - 14` wide, `t(28)` high (slots: `t(56) - t(6)`), the pointer at (-4, `(height - 8) / 2 - 6`), the text at x 14.

Toast slots (`menu`, `load_game`, `options`): the toasts stack up from the slot's bottom edge (`y + h`) and stand right-aligned to its right edge (`x + w`); `h` is 0 in the built-in files.

### menu (`ui/menu.json`)

The frame every field menu page shares.

Windows: `backdrop` (the dimmed field), `box` (the panel with the title plate, tabs, money, the page and the key hints), `toasts`.

Fields:
- `tabs` (list of strings): the tab names.
- `tab` (int): the selected tab.
- `money` (string): the money, formatted.
- `money_value` (int): the money as a number (a `text` element's `format` can group it, `N0`).
- `hint` (string): the key hints for the page.
- `page_ids` (list of strings): each tab's page id (below, "Menu entries").
- `party` (list of items): the lineup in marching order; item fields `name` (string), `level`, `hp`, `max_hp`, `tp`, `max_tp` (int), `portrait` (image), `alive` (bool, HP above 0). The built-in file draws neither of the last two fields.

```json
{"type": "repeater", "items": "party", "y": 240, "dy": "t(28)", "template": [
	{"type": "text", "value": "name", "style": "small"},
	{"type": "text", "key": "ui.common.level_short", "args": ["level"], "w": "cw", "align": "right", "style": "small_dim"},
	{"type": "bar", "y": "t(13)", "w": "cw", "h": 3, "value": "hp", "max": "max_hp"}
]}
```

Slots: `backdrop`, `page` (draws the selected tab's page layout into its rectangle), `toasts`.

Params (also on `menu_status`, for its Stats/Abilities tabs): the `tabs` element's sizes, in logical pixels scaled with the text size like the rest: `tab_h` (32, a tab's height), `tab_pad` (28, the room beside a tab's text, half on each side), `tab_gap` (8, the gap after a tab), `tab_text_y` (5, the text's top in the tab).

```json
"params": {"tab_h": 36, "tab_pad": 36, "tab_gap": 12, "tab_text_y": 7}
```

### menu_children (`ui/menu_children.json`)

The list a field menu entry's `children` open under its tab ("Menu entries", below), drawn into the page's rectangle while the tab is selected and no child is open. The built-in file is a framed list 300 pixels wide (at 100%) under the tab, kept inside the page.

Window: `list`.

Fields: area fields; `label` (string, the tab's name); `children` (list of strings, the children's names); `child_rows` (list of items: `text`, the name, and `enabled`, false for a child its `enabled` turns off, which the built-in file dims); `list` (scroll list); `tab_x`, `tab_w` (int: the selected tab's left edge and width as the `tabs` element drew it, on the same surface); `list_w`, `row_h`, `rows_max`, `list_pad`, `text_size` (int: the params below, 0 for `list_w`, `row_h` and `text_size` when not given).

Params, for small resolutions (each left out keeps the built-in size): `list_w` (the list's width; `t(300)`), `row_h` (a row's height, the highlight 2 pixels less; `t(30)`), `rows` (the rows shown before it scrolls; 8), `pad` (the frame's padding; 24), `text_size` (the rows' text size; the body size).

```json
{"version": 1, "params": {"list_w": 110, "row_h": 14, "rows": 5, "pad": 6, "text_size": 16}, "windows": [
	{"name": "list", "x": "max(area_x, min(tab_x - 16, area_x + area_w - (list_w > 0 ? list_w : t(300))))", "y": "area_y", "w": "list_w > 0 ? list_w : t(300)",
		"h": "min(len(children), rows_max) * (row_h > 0 ? row_h : t(30)) + t(8) + 2 * list_pad", "frame": "panel", "elements": [
		{"type": "list", "items": "child_rows", "state": "list", "enabled": "enabled", "x": "list_pad", "y": "list_pad", "w": "cw - 2 * list_pad",
			"h": "min(len(children), rows_max) * (row_h > 0 ? row_h : t(30)) + t(8)", "row_height": "row_h > 0 ? row_h : t(30)",
			"height": "row_h > 0 ? row_h - 2 : t(28)", "size": "text_size > 0 ? text_size : 20"}]}
]}
```

### menu_status (`ui/menu_status.json`)

Windows, all on the area: `members` (the member list), `header` (portrait, name and level, experience, HP and TP bars, the Stats/Abilities tabs), `stats` (two columns of stat rows), `abilities` (the ability list, its description, the target list).

Fields:
- area fields.
- `view` (string): `members`, `stats` or `abilities`; nothing is drawn when the member is gone.
- `members` (list of strings), `list` (scroll list): the member list.
- `portrait` (image), `name` (string), `level`, `experience`, `experience_next`, `hp`, `max_hp`, `tp`, `max_tp` (int): the header.
- `pages` (list of strings), `page` (int): the Stats/Abilities tabs.
- `sub_list` (scroll list): the stats view's scroll and the ability list's cursor.
- `stat_rows` (list of items): one per row; item field `cells` (list of two items, left and right column), each with `shown` (bool, false past the column's end), `label` (string), `value` (string; empty for a heading, which draws in the accent color; an empty `label` is a bonus line under a piece).
- `picking` (bool): choosing whom a field ability is used on; the ability list narrows to 3/5.
- `abilities` (list of items): item fields `text` (the name, for the probe), `label` (the name with ` *` for field abilities), `usable` (bool, field ability), `kind`, `cost`, `uses` (strings).
- `description` (string): the highlighted ability's description, empty when it has none.
- `target_name` (string), `targets` (list of strings), `target_list` (scroll list): the target choice.
- `target_rows` (list of items): the same choice as items, `text` (as in `targets`), `name` and `reachable` (false for a member the ability cannot reach, `AlliesOutOfReach`, greyed before the pick; Confirm on one does nothing). The built-in files draw `target_rows` with `"enabled": "reachable"`, and so does the Items page's `member_rows`.

The stats rows fit the label to the column less the value's width, then the value to what the fitted label leaves (`fit()` in the expression).

### menu_items (`ui/menu_items.json`)

Window: `page` (the area).

Fields:
- area fields.
- `picking` (bool): choosing whom the item is used on; the item list narrows to half.
- `sort_name` (string): the sort order's name.
- `item_count` (int), `item_total` (int): the kinds of items and their sum.
- `items` (list of strings), `list` (scroll list): the bag.
- `description` (string): the highlighted item's description, empty when it has none.
- `target_name` (string), `members` (list of strings), `member_list` (scroll list): the target choice; `member_rows` (list of items: `text`, `name`, `reachable`) the same choice with the members the item cannot reach (`AlliesOutOfReach`) greyed before the pick.
- `acting` (bool), `actions` (list of strings), `action_ids` (list of strings), `action_list` (scroll list): the highlighted item's actions while they are open (param `actions`): `use`, `look` and, with the param `discard`, `discard` for an item the data lets the bag throw away (`InventoryEntry.Discardable`), as the sample's words `Use`, `Look` and `Discard` (keys `ui.items.action.use`, `.look`, `.discard`).
- `discarding` (bool), `discard_step` (string: `count`, `confirm` or `''`), `discard_count`, `discard_max` (int: how many are chosen, of the stack), `discard_text` (string: the step's line, `ui.items.discard_count` "Throw away how many Tonic?  3 of 8" or `ui.items.discard_confirm` "Throw away 3 Tonic?  Enter yes, Esc no", the sample's words): Discard's steps.
- `looking` (bool), `look_text` (string): the item's description (or `ui.common.no_description`) while `look` shows it, `''` otherwise.
- `bag_used`, `bag_slots` (int), `bag` (string): the bag's slots as in the shop; the built-in file draws `bag` in the middle of the line over the list when the bag has a limit.
- `result_text` (string), `result_ok` (bool): the last use's outcome, the session's notice (the last one when it gives several) or its refusal, and whether the use went through. A file that reads `result_text` takes the uses' outcomes there instead of the notices, as the shop does.

Param `actions` (an expression, default 0): not 0, Confirm on an item opens its actions instead of using it. `use` goes on as Confirm did before (the target choice for an item used on one ally, the travel choice for a travel item, else the use); `look` shows the description until Confirm or Back; Back closes the actions, as does the item running out. A file that turns `actions` on with a number but draws no element that reads `actions` is a problem (the list would not show). The page's `page` probe reads `actions` and `look` for the two steps.

Param `discard` (an expression, default 0): not 0, the actions of a discardable item end with `discard`. For a stack it asks how many first (`discard_step` `count`: Up and Right one more, Down and Left one fewer, 1 to the count held; Confirm goes on, Back returns to the actions), then for a confirm (`confirm`: Confirm throws them away through `Discard(id, count)`, Back returns to the count, or to the actions for a single item). The session's notice (`game.discarded`) or refusal (`game.refuse.discard_not_held` and the others) goes to `result_text` when the file reads it, else to the notices, as a use's does. A file that turns `discard` on with a number but draws no element that reads `discard_text` is a problem. The `page` probe reads `discard_count` and `discard_confirm` for the two steps. `make_host_assets.py` draws them:

```json
{"version": 1, "params": {"actions": 1, "discard": 1}, "windows": [
	...,
	{"name": "discard", "visible": "discarding", "x": "area_x + 40", "y": "area_y + t(150)", "w": "area_w - 80", "h": "t(70)", "z": 6, "frame": "panel", "elements": [
		{"type": "text", "value": "discard_text", "w": "cw", "color": "discard_step == 'confirm' ? 'bad' : 'accent'"},
		{"type": "bar", "visible": "discard_step == 'count'", "value": "discard_count", "max": "discard_max", "y": "t(30)", "w": "cw", "h": 6}]}
]}
```

```json
{"version": 1, "params": {"actions": 1}, "windows": [
	{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
		{"type": "list", "items": "items", "state": "list", "w": "cw", "h": "ch - t(56)", "active": "!picking && !acting"},
		{"type": "text", "visible": "result_text != ''", "value": "result_text", "y": "ch - t(86)", "style": "small", "color": "result_ok ? 'good' : 'bad'"}]},
	{"name": "actions", "visible": "acting", "x": "area_x + area_w - 300", "y": "area_y + t(20)", "w": 260, "h": "t(110)", "z": 5, "frame": "panel", "elements": [
		{"type": "list", "items": "actions", "state": "action_list", "w": "cw", "h": "ch", "active": "acting && !looking"}]},
	{"name": "look", "visible": "looking", "x": "area_x + 40", "y": "area_y + t(150)", "w": "area_w - 80", "h": "t(110)", "z": 6, "frame": "panel", "elements": [
		{"type": "text", "value": "look_text", "w": "cw", "wrap": true, "lines": 3}]}
]}
```

### menu_equip (`ui/menu_equip.json`)

Windows: `page` (the area: the member list, or the member's name, the six places and the pieces for the chosen place), `worn` (the right column while choosing a place: what is worn there and the last change), `piece` (the right column while choosing a piece: what is worn now, then the comparison or what removing changes). `piece` is required: the code reads its width to count the effects' wrapped lines.

Fields:
- area fields.
- `view` (string): `members`, `slots` or `pieces`.
- `members` (list of strings), `list` (scroll list): the member list.
- `member_name` (string).
- `slot_rows` (list of strings), `slot_list` (scroll list): the places and what is worn in each.
- `slot_name` (string): the chosen place.
- `choices` (list of items, item fields `text`, `enabled`: the member can wear it), `choice_list` (scroll list).
- `right` (string): `worn`, `piece` or empty (no pieces fit).
- `worn_text` (string): the piece worn at the place and its bonuses, or nothing.
- `has_change` (bool), `change_text` (string): the last change made here.
- `removing` (bool): the highlighted choice takes the piece off.
- `has_preview` (bool), `piece_name`, `description`, `effects`, `wearers` (strings): the comparison's head.
- `effect_lines` (int): the lines `effects` wraps to at the `piece` window's width; the lines under it stand below them.
- `can_wear` (bool), `refusal` (string): why the member cannot wear it.
- `with_it` (string): what it replaces.
- `compare_rows` (list of items, item fields `label`, `before`, `after` (int)): the stats that change; one placeholder row when none do.
- `notes` (list of strings): element, attack, ability and resistance changes.

### save_slots (`ui/save_slots.json`)

The slot list the field menu's Save page, the save prompt and the Load screen share (`DrawSlots`). Drawn through the one surface the caller gives.

Window: `slots` (the area).

Fields:
- area fields.
- `slots` (list of items): item fields `text` (the probe: slot and area, `empty` or `damaged`), `head` (the first line), `enabled` (false when damaged), `detail` (the second line, empty for none), `detail_color` (color name: `dim`, or `bad` for a damaged save's error), `newest` (bool: the save Continue loads, `SaveSummary.Newest`), `write_time` (int: the save's `WriteTime`, Unix seconds, or the session's counter without the wall clock; 0 for an empty slot).

```json
{"type": "text", "visible": "newest", "key": "ui.title.continue", "x": "cw - 150", "w": 120, "align": "right", "style": "small", "color": "'good'"}
```
- `list` (scroll list), `active` (bool): whether the list has the cursor.

### menu_abilities, menu_system (`ui/menu_abilities.json`, `ui/menu_system.json`)

The technique, skill and ability pages and the system page ("Menu entries", below).

- menu_abilities. Windows `members`, `abilities` (the area). Fields: area fields, `view` (`members` or `list`), `kind` (`techniques`, `skills` or `abilities`), `members` (list of strings), `list` (scroll list), `name` (string, the member), `abilities` (list of items: `text`, `label` (the name with `  *` when usable here), `usable` (bool), `kind`, `cost`, `uses` (strings)), `sub_list`, `description`, `picking` (bool), `target_name`, `targets` (list of strings), `target_rows` (as menu_status's), `target_list`.
- menu_system. Window `page` (the area). Fields: area fields, `rows` (list of items: `id`, `text` (label and value), `label`, `value` (strings), `changeable` (bool)), `list` (scroll list), `description` (a settings row's help). The slot list draws through `save_slots`.

Params:
- menu_abilities `unusable`: `"dim"` (default: the abilities not usable in the field are listed, their names dimmed by the built-in file's `usable` color) or `"hide"` (they are left out of the list, so the cursor, the description and the `abilities` items have only those usable here; a member with none shows `ui.status.no_abilities`).
- menu_system `system_action` (0 or 1, default 0): 1 turns on the `System` action ("Menu entries", below), which opens this page over the field.

```json
{"version": 1, "params": {"unusable": "hide"}, "windows": [...]}
```

```json
{"version": 1, "params": {"system_action": 1}, "rows": ["save", "text_speed", "quit"], "windows": [...]}
```

### extras (`ui/extras.json`)

The Extras menu ("Menu entries", below). Windows: `scene` (what it opened over, then the dimming), `box`, `toasts`, `hint`. Fields: `rows` (list of items, as menu_system's), `list` (scroll list), `view` (`list` or `notice`), `notice` (the photosensitivity notice), `description`, `hint` (strings), `in_battle`, `on_title` (bool). Slots: `scene`, `toasts`.

### menu_order, menu_options (`ui/menu_order.json`, `ui/menu_options.json`)

Window: `page` (the area).

- menu_order: `rows` (list of strings), `list` (scroll list): the lineup.
- menu_options: `options` (list of strings): the one "open options" row, always selected.

The `party_talk` page draws through menu_options too. Its param `party_talk` is `"close"` (default: Confirm closes the menu and starts the party talk, its lines shown as notices, as before), `"speakers"` (one speaker at a time, below) or `"page"` (the menu stays open on the page and the lines show there, from `PartyTalkLines()`, which shows and marks nothing; the field's party talk still goes through `PartyTalk()`). With `"page"` the page also gets `talking` (bool: lines to show), `talk` (list of items: `speaker` (the member's name), `speaker_id` (the character id), `portrait` (image: the speaker's portrait), `line` (what they say), `pages` (list of strings: the line's pages), `text` (name and line, as the notice reads)) and the slot `talk_box`, which draws each speaker's name in the accent color and their line wrapped under it, each of its pages starting a row of its own, as many rows as fit its rectangle (`talk_rows` cuts the rest; the box does not page with Confirm, which talks again). A line's control tags are acted on as the text log does: `{var:...}` filled, presentation tags dropped, `{page}` the break between pages. Its sizes come from the layout: the styles' text styles `talk` (the lines) and `talk_name` (the names), size and color, when styles.json defines them, and the menu_options params `talk_size` and `talk_name_size` (text sizes; the body size without them), `talk_row_h` (the step between rows; the line height for the size), `talk_pad` (room inside the slot; 0) and `talk_rows` (at most this many rows; 0: as many as fit). `make_small_more_assets.py` sets them for 320x224 (`"talk_pad": 2, "talk_row_h": 11, "talk_rows": 6`, styles `talk` and `talk_name` at `size_small`). The lines stay until the tab changes, the menu closes or Confirm talks again; with nobody to talk, `game.refuse.nobody_talks` is a notice with the refusal cue. The built-in file draws none of them, so a layout that turns the page on adds the box:

```json
{"version": 1, "params": {"party_talk": "page"}, "windows": [
	{"name": "page", ...},
	{"name": "talk", "visible": "talking", "x": "area_x + 20", "y": "area_y + t(96)", "w": "min(area_w - 40, t(640))", "h": "t(150)", "frame": "panel", "elements": [
		{"type": "slot", "slot": "talk_box", "x": 24, "y": 20, "w": "cw - 48", "h": "ch - 40"}
	]}
]}
```

With `"speakers"`, Confirm on the row (or the entry's `immediate`) steps through `PartyTalkLines()` one speaker at a time in the layout's window named `talk_speaker`, which the menu keeps open behind. Each speaker shows their portrait (by the speaker's id through the assets' `portraits`, else the person's front; a `{portrait:ID}` or `{portrait:ID:EXPRESSION}` tag in the line switches to that picture, an expression included, when the reveal reaches it), their name and their line page by page: `{page}` breaks it, and a page longer than the rows of the window's wrapped `text` element named `talk_text` (its `lines`) goes on in the next box, as the dialogue box does ("Dialogue control tags", Boxes). Each box reveals at the profile's text speed with the `punctuation_pauses` and `{wait:N}` waits, Instant text showing it at once; Confirm on a box still revealing shows all of it, and on a shown box goes to the next box, the next page, the next speaker, and after the last speaker leaves. Back leaves at once. Leaving returns to the page with its row (Confirm talks again; the `immediate` entry acts again only when the tab opens again). The `text` cue ticks while a box reveals and the `line` cue plays on each step; auto-advance does not apply. Other tags are dropped as on the page, and `{var:...}` is filled. With nobody to talk, `game.refuse.nobody_talks` is a notice with the refusal cue, as with `"page"`. The page's fields while the speakers run (each empty, 0 or false otherwise):

- `talk_speaker_name` (string): the speaker's name; `talk_speaker_id` (string): their character id.
- `talk_portrait` (image): the speaker's portrait, as above; -1 while nobody speaks.
- `talk_text` (string): the box's text, from the box's start to the page's end (tags out, values in); the `talk_text` element shows the rows that fit.
- `talk_shown` (int): characters of it revealed (the element's `reveal`).
- `talk_page`, `talk_pages` (int): the box among the speaker's boxes, from 1, every page's boxes counted.
- `talk_index`, `talk_count` (int): the speaker among the speakers, from 1, and how many speak; `talk_count > 0` is the window's `visible`.
- `talk_more` (bool): the box is all shown and Confirm goes on to another box or speaker (the page arrow's `visible`).
- `page_arrow` (image): as the dialogue's.

Position, size, frame, fonts, the portrait's size, the rows, the padding and the arrow are the window's: its elements' `style`, `size`, `w`, `h`, `lines` and `padding`. A layout with `"speakers"` and no `talk_text` element in a window named `talk_speaker` is a problem (`params.party_talk: no element of a window 'talk_speaker' draws the field 'talk_text'...`); a `party_talk` word other than the three is a problem too. The param `talk_hide_menu` (default 0) set to 1 hides the menu's windows while the speakers run: the field draws undimmed and the `talk_speaker` window alone over it, given the whole frame as its area fields (`area_x` and `area_y` 0, `area_w` and `area_h` the logical frame), so a window placed by the area sits on the page in the menu and at the frame's foot over the field. The `page` probe reads `speakers` while they run; the `talk` probe reads `SPEAKER/SPEAKERS ID page BOX/BOXES KEY SHOWN/END` (`2/3 medic page 1/1 portrait:medic:worried 51/51`: the portrait's key as `shownportrait` reads it, the box's characters revealed of those it holds), `-` otherwise. `make_talk_assets.py` writes this window for 1280x720 and, with `--small`, for 320x224 (two rows a box); the built-in file draws none of it:

```json
{"version": 1, "params": {"party_talk": "speakers", "talk_hide_menu": 0}, "windows": [
	{"name": "page", "visible": "talk_count == 0", ...},
	{"name": "talk_speaker", "visible": "talk_count > 0", "x": "area_x", "y": "area_y + area_h - h", "w": "area_w", "h": "t(176)", "frame": "panel", "elements": [
		{"type": "role", "role": "dialogue"},
		{"type": "plate", "value": "talk_speaker_name", "x": 18, "y": "-t(16)", "size": "size_body"},
		{"type": "image", "name": "portrait", "image": "talk_portrait", "x": 30, "y": "t(34) + 8", "w": 80, "h": 80},
		{"type": "text", "name": "talk_text", "value": "talk_text", "x": 144, "y": "t(30)", "max_width": "cw - 174", "wrap": true, "lines": 4,
			"reveal": "talk_shown", "line_height": "t(size_line)"},
		{"type": "role", "role": "menu"},
		{"type": "text", "value": "talk_index + '/' + talk_count + '   ' + talk_page + '/' + talk_pages", "y": "ch - t(32)", "w": "cw - 70", "align": "right", "style": "small_dim"},
		{"type": "image", "visible": "talk_more", "image": "page_arrow", "x": "cw - 48", "y": "ch - 40 + clock_ms / 260 % 2 * 3", "w": 24, "h": 24}
	]}
]}
```

### menu_macros (`ui/menu_macros.json`)

Windows: `macros` (the area's left 2/5: the count and the macro list), `detail` (the rest: help, the macro's title, the name entry, its actions and steps, the step being built).

Fields:
- area fields.
- `macro_count` (int), `macro_rows` (list of strings), `list` (scroll list).
- `stage` (string): the stage in lower case (`list`, `actions`, `steps`, `stepactions`, `member`, `command`, `pick`, `target`, `targetmember`, `targetenemy`, `name`, `options`).
- `on_trouble`, `recover_who`, `unnamed`, `policy` (string): the highlighted macro's options by their library names (`default`, `continue`, `recover`, `stop`; `default`, `all`, `uncovered`, `troubled`; `default`, `attack`, `none`, `auto`; `default`, `power`, `expected`), `""` on the "new" row; `option_rows` (list of strings): the same as the Options stage's rows.
- `building` (bool): a step is being built (any stage past `steps` but `name`).
- `has_macro` (bool): the cursor is on a macro, not on the "new" row.
- `title` (string): the macro's name, marked when it is the auto macro.
- `name_prompt` (string): the name entry's question.
- `sub_list` (scroll list): the actions' and the steps' cursor.
- `actions` (list of strings).
- `step_notes` (list of items, item fields `text`, `ok`: the step can run now): the steps under the actions.
- `steps` (list of items, item fields `text`, `enabled`): the steps and the "add" row.
- `what` (string): the question of the stage being built.
- `stage_rows` (list of strings), `stage_list` (scroll list): its choices.

Slot: `name_pad` (the name entry grid; `w` is its width).

Param `options` (an expression, default 0): not 0, the macro's actions end with `Options` (key `ui.macros.options`), whose stage lists the four options (`In trouble`, `Recover`, `Without a step`, `Choices`, the sample's words) in the `building` group's `stage_rows` with `what` its title; Right or Confirm steps the highlighted option forward and Left back, through `SetMacroOptions` (on_trouble, recover_who, unnamed) and `SetMacroPolicy` (policy), so the save keeps them; Back returns to the actions. `Default` leaves each to the next level (the caller's mode, then rules.json).

```json
{"version": 1, "params": {"options": 1}, "windows": [...]}
```

### menu_combos (`ui/menu_combos.json`)

Window: `page` (the area).

Fields:
- area fields.
- `discovered` (int), `log_total` (int), `total` (int): combinations found, the log's own total, and rows shown.
- `rows` (list of items, item fields `text`, `enabled`: found), `list` (scroll list).
- `has_entry` (bool): the highlighted row is a found combination.
- `members_text`, `abilities_text`, `found_time` (strings), `make_key` (string, the key that makes a macro of it).

### menu_quests (`ui/menu_quests.json`)

The quest log's rows, on the menu's Quests page and in the quest log overlay.

Window: `page` (the area).

Fields: area fields; `rows` (list of items, item fields `text`, `heading` (bool)); `list` (scroll list, shown as a view: the cursor is the first row).

### menu_codex (`ui/menu_codex.json`)

The Codex page: the entries' titles and the highlighted entry's text.

Window: `page` (the area).

Fields: area fields; `titles` (list of strings); `list` (scroll list); `title` (string, the highlighted entry's title); `body` (string, its text, wrapped by the layout).

### menu_log (`ui/menu_log.json`)

The text log's lines, on the menu's Log page and in the text log overlay.

Window: `page` (the area).

Params: `wrap_width` (the width the log wraps to), `rows` (the lines shown, at least 1).

Fields: area fields; `lines` (list of strings: the lines shown); `first` (int: the first shown); `rows` (int: the lines that fit); `total` (int: all wrapped lines).

Each logged line shows without its control tags, as the dialogue showed it: `{page}` joins the pages with a space, `{wait:N}`, `{portrait:...}` and the layout's tags are gone and `{var:ID}` shows the value. Lines wrap to `wrap_width` at the styles' `small` text size (16 in the built-in styles). The built-in file steps lines `t(size_small + 6)` apart (22 at 100%). Probe: `loglines`, the lines the last frame showed joined by ` | ` (`none` for an empty log, `-` before the log was drawn).

### quest_log, text_log (`ui/quest_log.json`, `ui/text_log.json`)

The overlays from the field.

Windows: `backdrop`, `box` (the panel and its title).

Slots: `backdrop`; `quests` (draws `menu_quests` into its rectangle) or `log` (draws `menu_log`).

The built-in files place the box and the slot by the frame's size, so the logs open laid out at any resolution (the 1280x720 rectangles are as before): the box `min(24, W / 40)` in from the sides, `min(40, max(16, H / 12))` from the top (the title plate stays on screen) and `min(32, H / 18)` from the bottom; the slot `min(32, cw / 16)` in from the box's sides and `min(32, ch / 3)` down (under the panel's top band), with `min(36, ch / 9)` (log) or `min(16, ch / 9)` (quests) left under it. `menu_quests` rows are `t(size_body + 10)` high.

```json
{"name": "box", "x": "min(24, W / 40)", "y": "min(40, max(16, H / 12))", "w": "W - 2 * min(24, W / 40)", "h": "H - min(40, max(16, H / 12)) - min(32, H / 18)", "frame": "panel"}
```

`scripts/small_logs.txt` (on the control tags data copy and a copy of the small set, with `--dev`) opens both after the tags scene at 320x224 and at 192x144 (`alt/styles_tiny.json`), checking the rectangles and `loglines`.

### load_game (`ui/load_game.json`)

Windows: `scene` (the title's scene, darkened), `box` (the panel, the slot list, the toasts and the hints).

Fields: `has_saves` (bool), `hint` (string).

Slots: `scene`, `slot_list` (draws `save_slots` into its rectangle), `toasts`.

Param `select_newest` (an expression, default 0): not 0, the list opens on the save Continue would load (`SaveSummary.Newest`) instead of its first row. The list the title's Continue opens under `continue` "choose" always opens there. The slots as blocks of the layout's own, a grid or a row, and empty slots: "More layout flows", the Load screen.

```json
{"version": 1, "params": {"select_newest": 1}, "windows": [...]}
```

### options (`ui/options.json`)

Windows: `scene` (the title's scene, darkened, from the title), `backdrop` (the dimmed field, elsewhere), `box` (the panel with the settings or the Controls page, the toasts and the hints).

Fields:
- `title_mode` (bool): opened from the title.
- `title` (string): the panel's title.
- `on_controls` (bool): the Controls page is up.
- `rows` (list of items): item fields `text` (the probe, `LABEL: VALUE`), `label`, `value` (string, empty for none), `changeable` (bool: shown as `<  VALUE  >` when selected), `enabled` (bool). With the real-time field input on, `Always run` (the profile's `always_run`, for the session's run and the sample's own) and `Interaction prompt` (kept in `controls.json`) come before `Controls`.
- `list` (scroll list).
- `description` (string): the selected setting's help, two lines at most.
- `hint` (string).

Slots: `scene`, `backdrop`, `controls` (draws `controls` into its rectangle), `toasts`.

### controls (`ui/controls.json`)

Windows: `page` (the area: column heads and one row per action, then reset), `action` (rebind or clear the picked action), `capture` (the "press a key" box, centered on the screen).

Fields:
- area fields.
- `actions` (list of items): item fields `label`, `keys`, `buttons` (strings), `reset` (bool: the reset row, which has no bindings). With the real-time field input on, `Interact`, `Run` and `RunToggle` come after `FastForward`, before the reset row; with it off they are left out, so the page is as it was. `Extras` comes next, before the reset row, unless the param `list_extras` is 0, and `System` after it while the menu_system layout's `system_action` is 1.
- `list` (scroll list), `active` (bool).
- `picking` (bool), `action_name` (string), `choices` (list of strings), `choice` (int): the `action` box.
- `capturing` (bool), `ask` (string): the `capture` box.

The probes (`selected`, `top`) of this page stay in code; its list has `probe: false`.

Param: `list_extras` (0 or 1, default 1): the page lists the `Extras` action.

```json
{"version": 1, "params": {"list_extras": 0}, "windows": [...]}
```

## Battle: `battle_front` and `battle_side`

Files: `ui/battle_front.json`, `ui/battle_side.json`. The presentation table's `layout` (`front` or `side`, per encounter and area), `--battle-layout` and the `battlelayout` script verb choose which file draws the battle. Both files read the same fields, slots and params; each is a preset of the stage rules (its `params`) and of the windows.

The battle draws all its text at 100%: the screen hands the raw surface as both surfaces, so `scaled` changes nothing here and `measure`/`fit` measure what is drawn.

Window order is draw order. The stage is code drawn into two slots: `stage` (the backdrop, the figures, a vehicle battle's overlay) and `effects` (formation-change effects, effects on targets, enemy labels with their HP bars and icons, the target cursor, the instant defeat's mark, floating numbers, the full-screen flash). Windows listed before `effects` lie under the effects and numbers, those after it over them.

### Windows

`battle_front`:

* `stage`: slot `stage`.
* `strip`: the status strip (y 580): a repeater over `members`, `count` 5, row layout 252 apart; `empty` draws the frame of a slot without a member. Each slot: box, acting fill, hit flash, highlight frame, name, `icons` slot, HP and TP label/numbers/bar, next command or state, and an `anchor` slot (128 high, 64 above the slot) where effects land on a member without a figure.
* `vehicle`: the vehicle's plate over the strip's right end, a repeater over `vehicle` (0 or 1 item).
* `name_plate`: the action's name (and a cancel) as a framed plate, at the top of the stage.
* `effects`: slot `effects`.
* `log`: the last log lines, when the log is shown.
* `banner`: the opening, the round, a notice or the outcome, fading in and out.
* `command`: the command window beside the member choosing (two columns of four rows, paged by 8, with a page count); its x is `command_center - w / 2` when the members made room for it, else beside the actor's figure. Holds slot `target_pointer` last.
* `description`: the highlighted option's or targeted enemy's description, two small lines.
* `keys`: the keys' line at the bottom.
* `result`: the result window, in phase `ended`.

`battle_side`: `stage`, `effects`, `log`, `banner` (as above, without the plate), `party` (one column per party member: portrait, name, icons, state, HP and TP value bars, next command; a repeater over `party`, row layout), `menu` (the command window at the right, its rows scrolling with the cursor), `description`, `status` (in the menu's rectangle: `ui.battle.playing` or `ui.battle.auto` and the playback's key hints, or the outcome and `ui.battle.hint.continue`), `result`.

Windows the code places by (`Ui.WindowRect`): front `command` (its width for the members' step aside, and with its title plate's 14 pixels for the `overlaps` probe), `description`, `keys`, `name_plate` (the `overlaps` probe), `strip` (the `overlaps` probe: a party figure counts as far as it is on the screen and, where the strip stands across its lower part, above the strip's top), `log` (both files: the log's wrap width).

### Fields

* `phase` string: `command`, `option`, `target`, `macro`, `playback` or `ended`.
* `auto` bool: the auto battle runs.
* `cursor` int: the menu's cursor.
* `menu_shown` bool: the phase has a menu (command, option, target, macro).
* `menu_title` string: the actor's name in phase `command`, the command chosen in `option`, else the phase's `ui.battle.title.*` text.
* `menu_items` items: the menu's rows (`text`, `usable`).
* `description_shown` bool, `description` string: the description strip shows; its text, `''` when the data has none.
* `banner` string: the step's words, `''` for none. `banner_plate` bool: an action's name or a cancel (the front layout's name plate). `banner_big` bool: the outcome, opening or round (title size). `banner_cancel` bool: a cancel. `banner_alpha` number: its fade.
* `log_shown` bool; `log_lines` items (`text`, `newest`): the log's last `log_rows` lines, wrapped to the `log` window's width less `log_wrap_margin`.
* `hint` string: the front layout's keys' line for the phase.
* `key_confirm`, `key_back`, `key_fast_forward`, `key_text_log` strings: the keys' names, for the side layout's hints.
* `outcome` string: the outcome's heading, `''` while the battle goes on.
* `result_lines` items (`kind`, `text`): in phase `ended`, the result's lines; `kind` is `experience`, `money`, `found`, `level_up`, `learned` or `fallen`.
* `party` items: every party-side combatant (the vehicle too); `members` items: the party without the vehicle; `vehicle` items: the vehicle, when it fights.
* `target_pulse` number: a targeted highlight's pulsing alpha. `hit_flash` bool: a hit slot flashes now (not with reduced flash). `hit_fade` number: the hit flash's fade.
* `command_center` int: where the command window's middle goes after the members made room, -1 when they did not. `actor_left`, `actor_right`, `actor_center` int: the acting figure's box.
* `enemy_count` int, `enemy_back` bool: only while the `enemy_size` param is read (the formation's size, the enemy stands in the back rank).
* `enemy_groups` items: the enemies standing as the playback shows them, one item per name in the order the names first stand, with item fields `name`, `number` (how many share it), `level` (the first one's: its `enemies.json` `stats.level`, 1 when the data gives none; `Combatant.Level`) and `text` (`Gel Mite x2`, or the name alone). The built-in files do not draw it; with the stage's labels hidden (`enemy_labels`, below) a window can name the enemies instead.

```json
{"name": "enemies", "visible": "len(enemy_groups) > 0", "x": 8, "y": 176, "w": 140, "h": "len(enemy_groups) * 13 + 26", "frame": "panel", "elements": [
	{"type": "repeater", "items": "enemy_groups", "y": 17, "dy": 13, "template": [
		{"type": "text", "value": "name", "style": "small"},
		{"type": "text", "value": "number > 1 ? 'x' + number : ''", "w": "cw", "align": "right", "style": "small_dim"}
	]}
]}
```

Item fields (`party`, `members`, `vehicle`): `name`, `level` (the member's level, 0 for the vehicle), `alive`, `acting`, `targeted`, `hit` (damaged this step), `hp`, `max_hp`, `hp_low` (at most a quarter), `tp`, `max_tp`, `state` (fallen, defending, statuses), `next` (the command already chosen, as `ui.battle.next`, or `''`), `icon_count` (status icons shown, for their width), `portrait` (image), and the item slots `icons` (the status icons at x, y) and `anchor` (records where effects land on that member). `menu_items`: `text`, `usable`, `name` (an option's name without its cost and uses, else the row's text), `cost` (int, an option's TP cost, 0 for none), `uses` (int, a skill's uses left, -1 for none); a row also reads the screen's fields (`cursor`, `phase`), so a repeater over `menu_items` can draw a column of costs beside a `rows` element's page. `log_lines`: `text`, `newest`. `result_lines`: `kind`, `text`.

Item fields `command` and `command_icon` (`party`, `members`, `vehicle`): what the combatant chose for the round as one of `attack`, `technique`, `skill`, `combo`, `item`, `defend`, `escape`, or `''` before it chooses; and its picture, from the assets' `command_icons` or generated ([`assets.md`](assets.md), "command_icons"), -1 before it chooses. The built-in files draw neither. The state probe `commands` reads them: `Test Fighter attack, Test Mystic -`.

```json
{"type": "image", "visible": "alive && command_icon >= 0", "image": "command_icon", "x": 120, "y": 35, "w": 12, "h": 12}
```

The state probe `enemygroups` reads `enemy_groups` as `Gel Mite x2, Rust Hound` (`none`).

Item field `status_color` (`party`, `members`, `vehicle`): the color the `statuses` table (below) gives the highest-priority status the combatant has, `''` when none has one. The built-in slots' name reads `!alive ? 'dim' : acting ? 'accent' : status_color != '' ? status_color : 'text'`.

Slots: `stage`, `effects`, `target_pointer` (a party member being targeted gets its cursor over the command window), item slots `icons`, `anchor`.

The windows and the slots' boxes of both built-in files draw with the frame style `battle` (styles, "Frames and cursors"), so an asset directory's `ui.battle_frame` skins the battle apart from the menus.

### Status looks (`statuses`)

A battle file may have a `statuses` object (only `battle_front` and `battle_side` read one; elsewhere it is a problem): how each status shows on the party's slots, by status id (core's `statuses.json`, not `defeated`). Each value: `color` (a color name of the styles, `#rrggbb` or `#rrggbbaa`; the `status_color` field, for the name), `icon` (`picture`, the default: the status's icon from `status_icons` or the generated one; `none`: no icon) and `priority` (a whole number, 0 to 100, default 0). The color shown is that of the highest-priority status the member has that gives one (the earlier status on a tie); the icons in the `icons` slot leave out those with `"icon": "none"`, stand in priority order (highest first, then as before) and `icon_count` counts only them. A status the table leaves out has no color, its icon and priority 0, so a file without a table looks as before. The enemies' icons under their labels do not change. An unknown status id, a bad color, an icon not `picture` or `none`, a priority out of range or an unknown field is a problem, and the built-in file stands in.

```json
"statuses": {
	"jumble": {"color": "#f0a0f0", "priority": 3},
	"poison": {"color": "good", "priority": 2},
	"sleep": {"color": "tp", "icon": "none", "priority": 1}
}
```

Probe: `partystatus` reads each party combatant as `NAME: COLOR ICONS` (`-` for no color, `none` for no icon), joined by `, `: `Test Fighter: good poison, Test Medic: tp none`.

### Params

The stage's placement; a param a file leaves out takes the built-in file's value.

* `stage_bottom`: the stage's bottom (the overlay's and the flash's height; the side backdrop's height less 8).
* `party_x`, `party_dx`, `party_foot`, `party_dy`, `party_size`: member `i` stands centered at `party_x + i * party_dx`, feet at `party_foot + i * party_dy`, `party_size` square.
* `vehicle_x`, `vehicle_foot`, `vehicle_size`: the vehicle's place and size.
* `step_gap`, `step_margin` (front): while the command window is open the members on its side step along past it, `step_gap` apart, at least `step_margin` from the screen's edges; the window's width comes from the `command` window. The members are taken left to right as their slots stand (`party_slot_N_x` may put marching place 0 at the right), not in marching order.
* `step_aside` (front, built-in 1): 0 keeps every member on its slot while the command window is open (the window may then cover one; `overlaps` says so).
* `enemy_x`: the enemies' middle. Up to `enemy_row_max` enemies stand in one row, `min(enemy_row_spacing_max, enemy_row_span / n)` apart, feet at `enemy_row_foot` less `enemy_row_stagger` for each odd position. From one more on, two ranks (the first `(n + 1) / 2` positions in front, feet at `enemy_front_foot`; the rest at `enemy_back_foot`): with `enemy_ranks_interleaved` 1 the ranks interleave over `enemy_rank_span`, else each rank spreads `min(enemy_rank_spacing_max, enemy_rank_span / m)` apart and the back one shifts half a place.
* `enemy_size`: an expression of `enemy_count` and `enemy_back`, the enemy's square size.
* `enemy_slot_N_x`, `enemy_slot_N_foot` (N 0 to 7): formation position N's place, overriding the computed one; absent or -1 keeps it.
* `party_slot_N_x`, `party_slot_N_foot` (N 0 to 4): the member at marching place N stands centered at this x, feet at this y, in place of `party_x + N * party_dx` and `party_foot + N * party_dy`; absent or -1 keeps the computed one. In the front layout the members still step aside for the command window from these places. The state probe `partyplaces` reads each party figure drawn as `NAME X FOOT`.
* `enemy_labels`, `enemy_bars`: 0 leaves out the code-drawn enemies' labels (the dark box and the name), or their HP bars; the status icons stay. Absent, 1, both show. Expressions of the fields, so `"enemy_labels": "len(members) >= 5"` brings the labels back when a window that names the enemies has no room. The front layout's `overlaps` probe then reads an enemy's mark as what is drawn of it (its bar and icons); the state probe `enemymarks` reads what the last frame drew, `labels 0 bars 3`.

```json
"params": {"enemy_labels": 0, "enemy_bars": 1, "party_slot_0_x": 560, "party_slot_0_foot": 170, "party_slot_1_x": 488, "party_slot_1_foot": 200}
```
* `log_rows`: the log's lines; `log_wrap_margin`: the log window's width less this is the wrap width.
* `backdrop_h`, `ground_h` (front): the backdrop is drawn `backdrop_h` tall from 8 above the screen, and under it, down to the screen's bottom, its ground (below 62% of its height) again `ground_h` tall.
* `label_max_width`, `label_y`, `label_h`, `label_pad`, `label_text_y`: each enemy's label, cut to `label_max_width`, on a dark box `label_h` high at `label_y` under its feet, `label_pad` wider than the text on each side, the text `label_text_y` into it.
* `enemy_bar_y`, `enemy_bar_w`, `enemy_bar_h`, `enemy_icons_y`: the enemy's HP bar and status icons under its feet, centered on it.
* `icon_size`, `icon_gap`: every status icon (enemies' and the `icons` slots'), and the gap between two.
* `pointer_size`, `pointer_y`, `pointer_member_y`, `pointer_bob`: the target cursor, `pointer_y` over an enemy's figure bobbing `pointer_bob`, `pointer_member_y` over a member seen from behind (front).
* `number_size`, `number_critical_size`, `number_tp_size`, `number_word_size`: the floating numbers' text sizes (damage, healing and felled; a critical hit's; technique points restored; words such as misses and statuses).
* `number_y`, `number_member_y`, `number_rise`, `number_gap`: a number starts `number_y` over an enemy's figure, or `number_member_y` down a member seen from behind (front), rises `number_rise`, and the next on the same target stands its size and `number_gap` higher.
* `effect_scale`, `effect_scale_large`: the effects' factor: `effect_scale` on targets in the side layout and on members in the front one, `effect_scale_large` on enemies in the front layout, for formation changes and for the instant defeat's mark.
* `dev_badge_y`: with `--dev`, the developer badge's top, at the screen's right (the front layout's badge moves to the top while a preview plays).
* `effectiveness_marks`, `critical_marks`, `miss_marks` (default 1, not in the built-in files), each also per side as `NAME_party` (results on party members) and `NAME_enemy` (results on enemies), the side's param winning: 0 leaves out the mark. Without `effectiveness_marks` a weak, resisted or immune hit floats its plain number in the `damage` number style (`23`, not `23 Resisted`); without `critical_marks` a critical hit floats in the `damage` style and the log leaves out its `Critical hit!` line; without `miss_marks` a miss floats no word (its `evades` log line stays). The log's damage lines carry no effectiveness word in either case. The state probe `numbers` reads the floating texts the last frame drew as `LABEL TEXT STYLE`: `Gel Mite B 23 Resisted resisted, Rust Hound 52 damage`.

```json
"params": {"effectiveness_marks_enemy": 0, "critical_marks": 0, "miss_marks_party": 0}
```
* `motion_percent` (default 100, not in the built-in files): the figures' motion in percent of the 1280x720 distances: a step toward the foe and back, the chooser's rise, an attack's lunge, a hit's shake, a joining enemy's rise, a flier's bob, and the screen's shake (`battleshake` reads it scaled). Animation scripts' bare numbers and `"Nm"` distances are scaled by it; their `"Npx"` distances are not ([`battle-animation.md`](battle-animation.md), "Distances"). The backdrop, overlay and flash take the screen's width and `stage_bottom`; figure sizes are `party_size`, `vehicle_size` and `enemy_size`.

```json
"params": {"stage_bottom": 180, "party_size": 48, "vehicle_size": 64, "motion_percent": 25}
```

The built-in values, which both files share but for the front-only ones (`side` has no `backdrop_h`, `ground_h`, `pointer_member_y`, `number_member_y`, and its `dev_badge_y` is 376, over its party panels):

```json
"backdrop_h": 488, "ground_h": 640,
"label_max_width": 190, "label_y": 6, "label_h": 24, "label_pad": 8, "label_text_y": 3,
"enemy_bar_y": 34, "enemy_bar_w": 100, "enemy_bar_h": 6, "enemy_icons_y": 44, "icon_size": 20, "icon_gap": 2,
"pointer_size": 36, "pointer_y": 40, "pointer_member_y": 8, "pointer_bob": 5,
"number_size": 28, "number_critical_size": 36, "number_tp_size": 24, "number_word_size": 20,
"number_y": 26, "number_member_y": 36, "number_rise": 30, "number_gap": 6,
"effect_scale": 3, "effect_scale_large": 4,
"dev_badge_y": "H - 29"
```

## Fields the session cannot give yet

Loading a save away from the title (a Load entry on the Save page's system menu): the session refuses it
(`game.refuse.load_title`), as the spec lists the load screen only from the title (game-flow.md, the transition table:
Title to Menu "Settings, load screen", Field to Menu "save screen"; FLOW-013 allows no other transition). A player quits
to the title and loads there. The arrival switches, the keeper's voice, an enemy's level, erasing a save, member profile
rows and the flashing level, listed here before, are given now (above and in the sections they belong to).

## Layout items: optional fields, params and row models

Each of these is optional: a layout set without them draws and behaves as before (every existing run's transcript and frame digests are unchanged), and the export writes none of them. `--check-assets` checks each, F5 reloads them, and `scripts/make_items_assets.py` writes a set that uses them all, played by `scripts/items_battle.txt`, `items_menus.txt` and `items_dialogue.txt` (below, "The item runs").

The probe `uifield.SCREEN.NAME` reads any field as the screen was last drawn with it (`uifield.battle_front.actor_slot`), `uifield.SCREEN.LIST.N.FIELD` a field of a list's item (`uifield.battle_front.members.0.status_sleep`); a list reads as its length, a missing field as `-`.

### Battle fields

Screen fields of `battle_front` and `battle_side`:

* `list_kind` string: what the menu lists now: `top`, `command`, `technique`, `skill`, `item`, `macro`, `target`, or `''` outside the menus.
* `list_index` int: the command row whose list is open (in the option, macro and target phases), -1 otherwise; with `list_kind` a window can take a size per list.
* `actor_slot` int: the marching slot of the member choosing, or of the party member acting in the turn playing; -1 for none (an enemy's turn, the top-level step, between turns).
* `action_command` string: the command of the turn playing, as the `command` item field names it (`attack`, `technique`, `skill`, `combo`, `item`, `defend`, `escape`), `''` between turns. `action_chosen` bool: a party member acts with anything but a plain attack, for a name window shown only then.
* `result_page`, `result_pages` int and `result_more` bool: the result window's page (1-based), how many, and whether one follows (below, `result_page_lines`).
* `result_step` bool: the result (and level-up) screen is up. `transition_playing` bool: a battle-in or battle-out transition script runs ([`battle-animation.md`](battle-animation.md), transitions). Both are also animation script conditions, so a layout and the scripts can hide the party panel or the figures while they last: `"visible": "!result_step && !transition_playing"`.

Params `party_figures` and `enemy_figures` (expressions, default 1) on either battle file leave that side's figures (with their shadows; a vehicle is the party's) out of the `stage` slot while 0, the backdrop and the other side staying; their places still anchor effects and numbers, and the probe `figures` leaves hidden party figures out. Param `game_over_confirm` (default 1): 0 makes a lost battle's result screen ignore Confirm and Back, so only the timing `game_over_return_ms` returns to the title.

```json
"params": {"party_figures": "result_step ? 0 : 1", "game_over_confirm": 0}
```

Field `result_confirm` (bool): the result screen is up and Confirm or Back leave it (false after a game over under `game_over_confirm` 0, where the built-in side file's continue line and the `hint` field say nothing of Confirm).

Param `damage_numbers` (`"applied"` default, or `"rolled"`): what a hit's or a heal's floating number shows, the change made to the target (`BattleEvent.Amount`) or what the roll gave before the target's hit points, a shield's cap or the maximum took any off (`BattleEvent.Rolled`; a heal on a full member floats its roll instead of +0). The log keeps the change made.

```json
"params": {"damage_numbers": "rolled"}
```

Item field `max_uses` (int): a skill option's uses after a rest (`BattleOption.MaxUses`, its character's `skill_uses` by level), -1 for anything else; the built-in rows show a skill's uses as `x2/3`.

A one-ally ability or item the session refuses for the ally picked (`command.no_effect_on_target`: none of its effects can reach that ally) keeps the target list open with that ally's row not `usable` (greyed) and the refusal in the log; Confirm on a greyed row does nothing but the `refuse` cue.
* `page_arrow` image: as the dialogue's, for a next-page mark under `result_more`: `{"type": "image", "visible": "result_more", "image": "page_arrow", "x": "cw - 28", "y": "ch - 24", "w": 16, "h": 16}`.

Item fields of `party`, `members` and `vehicle`: `fallen` (bool, shown as down), `slot` (int, its index: the marching slot), and one `status_ID` bool per status of the game data (`status_poison`, `status_sleep`, ...; `defeated` excluded), true while the playback shows it. A name that is not `status_` and a status of the data is a problem. `menu_items` items gain `command` (the command row's id, `fight`/`macro`/`run` on the top-level step, `''` in lists) and `icon` (the command's picture from `command_icons` or generated, -1 off the command rows).

```json
{"name": "chosen", "visible": "phase == 'playback' && action_chosen", "x": "W - 236", "y": 16, "w": 220, "h": 34, "frame": "battle", "elements": [
	{"type": "text", "value": "'chosen ' + action_command", "style": "small"}]},
{"type": "plate", "visible": "status_sleep", "value": "'Zz'", "x": 180, "y": -14, "color": "tp"}
```

Params (numbers but `command_axis`):

| Param | Default | What |
| --- | --- | --- |
| `enemy_statuses` | 1 | the enemies' status icons follow the file's `statuses` table (icon `none` hides one, priority orders them); 0 keeps them off it (every icon, the old order) |
| `command_axis` | `"column"` | `"row"`: Left and Right move the command cursor (wrapping) and Up and Down do nothing; Confirm picks the highlighted command either way. A plain string, not an expression |
| `command_top` | 0 | 1: each round opens on a top-level step (phase `top`): `fight` (the members' commands, from which Back on the first member returns here), `macro` (the macro list for the party; with no macros the description strip and the log say so and the step stays), `run` (every member who takes commands is set to escape, one choice for the party) |
| `result_page_lines` | 0 | above 0: the result window shows that many lines a page; Confirm turns the page, and on the last page ends the battle; Back ends it at once |
| `item_order` | `"items_then_equipment"` | the Item command's list: the bag's items (the session's `BattleOptions` order), then the member's worn pieces that have a battle use (`(worn)`); `"equipment_then_items"` puts the worn pieces first. A plain string, not an expression |
| `macro_auto_first` | 0 | 1: the macro list (a member's Macro and the top-level step's) opens with the macro auto-battle plays (`Settings().AutoMacro`, set on the Macros page) first, marked `{0} (auto)` (the sample's words, key `ui.battle.auto_macro_row`); the rest keep the party's order. Without an auto macro the list is as before |
| `macro_check` | 0 | 1: while the macro list is open, the highlighted macro's steps as the session would play them this round (`CheckMacro`): field `macro_steps` (items: `text` the step, with its issue after it when it cannot run; `ok`; `issue` the refusal text, else the issue word with the status behind it, `""` when ok), `macro_trouble` (bool, `TroubleReport(name).Holds`) and `macro_trouble_text`; `description_shown` is true then. The built-in files list them in the `description` window, a step that cannot run dimmed, the trouble line under them in `bad`. Probe `macrocheck`: `ok` or the issue per step, then `trouble` |
| `macro_failed` | the Macros page's choice | what a macro picked from the battle menu does with a step that cannot run (`ApplyMacro`'s fallback): `"automatic"` (core's automatic choice), `"attack"` (a basic attack) or `"lose_turn"` (nothing this round, at no cost; the log shows the member as `waiting (turn lost)`). Left out, the Macros page's stuck-steps action decides (basic attack or automatic), as before. A plain string |
| `macro_flow` | `"reset"` | `"reset"`: after a macro's round the command menu (or the top-level step) and the macro list open at their first row, as before. `"remember"`: they open on Macro and on the macro played last; the round itself is always exactly one per selection (`ApplyMacro`). A plain string |
| `auto_choose` | 0 | 1: the Auto row (a member's and `command_rows.vehicle`'s) asks first the trouble mode (`continue`, `recover`, `stop`; phase `autochoice`, `list_kind` `auto`, titles `ui.battle.title.auto_trouble` and `.auto_policy`) and then the policy (the rules', `power`, `expected`), Back stepping back, then plays every round through `SetAutoPlayMacro(name or null, mode, policy)` and `FillAutoRound` (the auto play macro chosen on the Macros page, else the save's auto macro), logging the choice |

A battle file that lays the menu's rows out across, a `repeater` over `menu_items` in a row (no `dy`) or grid, or `rows` over it with `per_column` 1, without `"command_axis": "row"` is a problem (`windows.icons: lays the menu's rows out across: set the param 'command_axis' to "row" so Left and Right move the cursor`): the cursor would not follow the keys the layout shows.

```json
"params": {"command_axis": "row", "command_top": 1, "result_page_lines": 2, "enemy_statuses": 1}
```

```json
"params": {"item_order": "equipment_then_items", "macro_auto_first": 1}
```

```json
"params": {"macro_check": 1, "macro_failed": "lose_turn", "macro_flow": "remember", "auto_choose": 1}
```

Auto play under a trouble mode or policy (the Auto row's choice, or the settings rows `auto_trouble` and `auto_policy` below) fills each round with `FillAutoRound`, and a round where trouble holds (`TroubleReport(null)`, the round's `AutoRound.Trouble`) adds a log line: the mode, the members in trouble (fallen or with a troubled step) and those core's choice took over (`Trouble ({0}): {1}; automatic choice for {2}.`, the sample's words).

`command_rows` (a top-level object of either battle file, with `command_top` on): which commands each step shows, in order. `top` from `fight`, `macro`, `run`; `member` from `attack`, `technique`, `skill`, `item`, `defend`, `escape`, `macro`, `auto`; a step left out shows all. `vehicle` (with or without `command_top`) from `attack`, `skills` (one row per skill of the vehicle), `weapons` or `options` (one row listing the skills; "More layout flows"), `macro` (the party's macros through `ApplyMacro`, a step the vehicle cannot run taking the layout's macro fallback), `auto` (auto-battle through `StartAuto`) and `escape` lists a vehicle's rows in a vehicle battle; without it `attack`, `skills`, `escape` as before. An unknown or repeated command, an empty list, an unknown field, or `top` or `member` without `command_top` is a problem.

```json
"command_rows": {"vehicle": ["attack", "skills", "macro", "auto", "escape"]}
```

```json
{"version": 1, "params": {"command_top": 1}, "command_rows": {"top": ["fight", "run"], "member": ["attack", "technique", "skill", "item", "defend"]}, "windows": [...]}
```

The words of the top-level step are the text tables' `ui.battle.top.fight`, `.macro`, `.run`, `ui.battle.title.top` and `ui.battle.no_macros`. Probe: `enemyicons` reads the icons the last frame drew under each enemy with a status (`Gel Mite B sleep`, `none`).

### Title start state and entries

Two top-level fields of `title.json` (only that file takes them):

* `start` (true or false, default false): the title opens on a start state: the field `start` is true and the menu takes no input until any key or button (not a function key or the console key) is pressed, then `start` is false. It comes back each time the title does (not after its overlays). `start_hint` is the prompt's words (the text tables' `ui.title.press_start`).
* `load` shows while the session accepts a load from here (`CanLoad`; the field `can_load`, true on the title), and the field menu's Save page offers none.
* `options` (a list): the menu's entries in order, from `retry`, `new_game`, `continue`, `load`, `sound_test`, `erase`, `options`, `quit` (default: that order). An entry still shows only when it can be chosen: `retry` after a lost battle that allows one, `sound_test` after a finished story, `erase` always (the erase list, above, under Title). An unknown or repeated entry is a problem. The field `option_ids` lists the ids shown. An entry may be an object `{"id": ID, "when": CONDITION}`: it shows only while the condition holds too (an expression over the title's fields: `has_save` (bool, a save of any kind exists), `save_count` (int), `has_clear` (bool, a finished story's record exists); every title layout may read them).

```json
"options": [{"id": "continue", "when": "has_save"}, "new_game", {"id": "load", "when": "save_count > 1"}, "options", "quit"]
```

```json
{"version": 1, "start": true, "options": ["continue", "new_game", "load", "erase", "options", "quit"], "windows": [
	{"name": "press", "visible": "start", "anchor": "center", "w": 420, "h": 120, "frame": "panel", "elements": [
		{"type": "text", "visible": "clock_ms / 500 % 2 == 0", "value": "start_hint", "y": 76, "w": "cw", "align": "center"}]},
	{"name": "box", "visible": "!start", "anchor": "center", "w": 600, "h": 400, "frame": "panel", "elements": []}
]}
```

### Menu entries

Three optional fields; the export writes none of them, and without them the menus are as before:

* `entries` (`menu.json`): the field menu's tabs in order, each `{"page": ID, "label": TEXT_KEY}` (`label` optional, default `ui.menu.tab.ID`). Pages: `status`, `items`, `techniques`, `skills`, `abilities` (both kinds, as the Status page's abilities), `equip`, `order`, `macros`, `combos`, `quests`, `codex`, `log`, `options`, `save`, `system`, `party_talk`, `extras`. Default: `status`, `items`, `equip`, `save`, `order`, `macros`, `combos`, `quests`, `codex`, `log`, `options`, the menu as it was. The `tab` probe reads the page id with its first letter raised (`Techniques`, `Party_talk`); `page` reads `members`, `list` or `target` on the technique, skill and ability pages and `system` or `slots` on the system page.
  * `techniques`, `skills`, `abilities` (screen `menu_abilities`): a member, then their abilities of that kind with the TP cost and uses; Confirm on one usable in the field uses it (a target first when it takes one ally), as on the Status page.
  * `system` (screen `menu_system`): rows from its `rows`; `save` opens the slot list (the `save_slots` layout, Back returns), `quit` asks and quits to the title, a settings row steps its value with Confirm (Left and Right turn the tabs).
  * `party_talk`: one row (the `menu_options` layout); Confirm closes the menu and starts the party talk, or with that layout's `party_talk` "page" shows the lines on the page with the menu kept open, or with "speakers" steps through the speakers one at a time ("menu_order, menu_options").
  * `extras`: one row; Confirm opens the Extras menu over the field menu.
* `children` (an entry of `menu.json`'s `entries`): `[{"page": ID, "label": TEXT_KEY}]`, the same shape and pages as the entries (`label` default `ui.menu.tab.ID`). The tab still takes its name from its own entry, but in place of its page it lists the children under the tab (the `menu_children` layout); Up and Down choose, Confirm opens that page in the page's place (the `tab` probe then reads the child's page id) and Back at the page's top returns to the list, which keeps its cursor. Left and Right turn the tabs from the list and from a child page's top; Back on the list closes the menu. The entry's own page is not opened unless it is also one of its children; the `page` probe reads `children` on the list. A page only a child lists counts as one the entries leave out for the Extras menu and the System action (it opens alone).
* `immediate` (an entry or child of `menu.json`, `true` or `false`): a page that needs no further choice acts at once: `party_talk` starts the party talk as its tab (or child) opens, as Confirm on its row would (with "speakers", on the first speaker), and `extras` opens the Extras menu; on an entry with `children`, a single child opens in place of the list (Back then closes the menu). It acts once each time the page opens; a Confirm in the frame it opens is that once. On any other page, or not a boolean, it is a problem. `small_more.txt` checks both on the small set.
* `rows` (`menu_system.json`): the system page's rows in order from `save`, `quit`, `text_speed`, `battle_speed`, `flashing`, `auto_advance`, `text_size`, `auto_trouble`, `auto_policy` (default `save`, `text_speed`, `battle_speed`, `auto_advance`). `auto_trouble` and `auto_policy` are the sample's own settings, kept in `controls.json`'s `battle` object (`"auto_trouble": "continue"`, `"auto_policy": "power"`, written only when one is not `default`): the trouble mode and policy auto play goes under (`SetAutoPlayMacro(name or null, mode, policy)`), `default` leaving both to the macro and rules.json as before; the state probes `auto_trouble` and `auto_policy` read them.
* `entries` (`extras.json`): the Extras menu's entries, the same shape, from the field menu's pages but `extras`, and `controls` (the Controls page; Back leaves it for the Extras menu), `photosensitivity` (the flashing notice, `ui.options.flashing_notice`, in the box), `dev` (the developer console; shown only with `--dev`), and the settings rows above (Left, Right or Confirm change them in place). Default: `options`, `controls`. A field menu page opens the field menu on that page; when the menu's entries leave the page out, the field menu opens with that page as its only tab, until it closes (the menu opened again shows its own entries). On the title the field menu's pages but `options` are left out; in a battle only `text_speed`, `battle_speed`, `flashing`, `auto_trouble` and `auto_policy` show.

An unknown or repeated page or row, a missing `page`, an unknown field, `children` that are not a non-empty list or on the Extras menu's entries, a child's unknown or repeated page or missing `page`, children under a child, or `entries` or `rows` on another screen is a problem.

```json
{"version": 1, "entries": [{"page": "items"}, {"page": "system", "children": [{"page": "save"}, {"page": "options"}, {"page": "quests", "label": "ui.quests.jobs"}]},
	{"page": "abilities"}, {"page": "party_talk"}, {"page": "status"}], "windows": [...]}
```

The `System` action (S, no button: Start is `Menu`; [`../bindings/README.md`](../bindings/README.md), controls) is read only while the menu_system layout's param `system_action` is 1. It then opens the field menu from the field with the system page as its only tab, whatever the entries list (as the Extras menu opens a page they leave out), and it or Back closes it again. The Controls page and `controls list` show it after `Extras` only while it is on, and it rebinds like any action. Its label is `ui.action.system` (the sample's own English when the text table has none).

```json
{"version": 1, "entries": [{"page": "items"}, {"page": "techniques"}, {"page": "skills"}, {"page": "system"}, {"page": "party_talk"},
	{"page": "extras"}, {"page": "quests", "label": "ui.quests.jobs"}], "windows": [...]}
```

```json
{"version": 1, "rows": ["save", "text_speed", "flashing", "quit"], "windows": [...]}
```

```json
{"version": 1, "entries": [{"page": "text_speed"}, {"page": "battle_speed"}, {"page": "flashing"}, {"page": "controls"}, {"page": "photosensitivity"},
	{"page": "dev"}, {"page": "quests"}], "windows": [...]}
```

The Extras menu opens with the `Extras` action (C; [`../bindings/README.md`](../bindings/README.md), controls) from the title (after its start state), the field and a battle's command menus (the battle waits), and with it or Back closes. Words without a text table key come from the sample's own English (`ui.menu.tab.techniques`, `.skills`, `.abilities`, `.system`, `.party_talk`, `.extras`, `ui.menu.party_talk`, `ui.menu.open_extras`, `ui.extras.title`, `ui.extras.photosensitivity`, `ui.extras.dev`, `ui.action.extras`).

### Dialogue: narration without a box

A dialogue file with a window named `narration` lays out narration (`narrate` lines, events/FORMAT.md: a line without a speaker) in that window instead of the box: the box's `box_visible` is false while one shows, and each narration line joins the ones already in the window, up to `narration_lines` wrapped lines; a line that would not fit starts the window afresh, and a speaker's line or a choice clears it, as leaving the event does. A line longer than the window shows its first `narration_lines` lines. The window needs no frame; draw it at any rectangle, over the picture stack by `z`.

Params (expressions of the common names): `narration_lines` (4), `narration_cps` (0: the text speed's characters a second; above 0, the characters a second at Normal, scaled by the text speed option as the `text_speed` timings scale: Slow halves it with the built-in timings, Instant shows each line at once), `narration_delay_ms` (0: how long each line waits before it reveals), `narration_width` (the width lines wrap to; default the `narration` window's width). Lines wrap at the styles' `narration` text style's size (else the body size) in the `narration` font role, which falls back to `dialogue` ([`assets.md`](assets.md), bitmap_fonts).

Fields: `narration_visible` (bool, the window has lines), `narration_rows` (items: `text`, the row as revealed so far; `full`, the whole row; `shown`, it is all shown), `narration_all_shown` (bool, the line now is all shown, for a next hint). Confirm shows the rest of the line, then goes on; auto-advance waits as for a box; Skip, the text log and every tag act as for the box (a narration line's tags act when it comes up). Probes: `narration` (the rows as shown, joined by ` | `, `none`), `narrationreveal` (`SHOWN/TOTAL` of the line now, `-`).

```json
{"version": 1, "params": {"picture_stack": 1, "narration_lines": 4, "narration_cps": 40, "narration_delay_ms": 200}, "windows": [
	{"name": "narration", "visible": "narration_visible", "x": 120, "y": 200, "w": "W - 240", "h": "4 * t(32)", "z": 10, "elements": [
		{"type": "role", "role": "narration"},
		{"type": "repeater", "items": "narration_rows", "dy": "t(32)", "template": [{"type": "text", "value": "text", "style": "narration"}]},
		{"type": "role", "role": "menu"},
		{"type": "text", "visible": "narration_all_shown", "value": "hint", "y": "4 * t(32) + 8", "w": "cw", "align": "right", "style": "hint"}]}
]}
```

`narration_when` (param) chooses which lines go to the window: `"all"` (the default: every line without a speaker), `"tagged"` (only lines that start with `{narration}`; the tag draws nothing) or an expression over the dialogue's fields, read as the line shows: `line_key`, `speaker`, `picture_top`, `picture_names` and the common names (`"starts_with(line_key, 'tale.') || picture_top == 'tower'"`). Whatever the mode, a line starting with `{narration:on}` goes to the window and one starting with `{narration:off}` to the box. Lines not routed use the box as usual. `starts_with(a, b)` is an expression function on every screen. `narration` is a built-in tag, which a `tags` table cannot map.

`narration_wait` (param, `"press"` default or `"auto"`) and the line tag `{narration:nowait}`: under `auto`, or on a line carrying the tag, a narration line goes on by itself once revealed and held `narration_hold_ms` (param; else `narration_delay_ms`), and the next joins the window without a press, unless the line leaves the window full, which waits for Confirm as before. When the next is a speaker's line or a choice, the window stays (box hidden) until a press, then clears. Under `auto` a line goes on by itself only while the event shows another line next (`NextEventOutput` is `Line`): before the event's end, a choice or anything else (a wait, an action, a battle) it waits for a press, so the window no longer closes with the event; a line carrying `{narration:nowait}` still goes on, so give an event's last narration line no `nowait`. Confirm still shows a revealing line at once and Skip still skips. `{narration:nowait}` routes as `{narration}` does under `tagged` and draws nothing. `make_narration_assets.py` writes `alt/dialogue_auto.json` (`auto`, hold 400 ms); `scripts/narration_auto.txt` checks four lines gathering with no press, the full window waiting, the hurry, and the held window before the guide's line.

```json
"params": {"narration_wait": "auto", "narration_hold_ms": 400}
```

```json
"params": {"narration_when": "tagged"}
```

`make_narration_data.py --routing` adds an event `routing` (a plain line, a `{narration}` one, a speaker's `{narration:on}`, a `{narration:off}`, lines under the stacked pictures, a `tale.` key) and `make_narration_assets.py --route` an asset copy with `alt/dialogue_tagged.json`, `alt/dialogue_expr.json`, a UI skin `page_arrow` and `shown_ms` in the box; `scripts/narration_route.txt` checks each mode (swapping the files in, so hot reload too); screenshots under `build/shots/narration/route_*.png`.

The built-in file has no `narration` window, so narration stays in the box. `scripts/make_narration_data.py` writes a data copy with the event `narration` (five narration lines, the first two showing still pictures, then the guide's line) and `make_narration_assets.py` an asset copy whose dialogue layout keeps the pictures as a stack side by side and has the window above (with a 0.45 black fill behind the rows); `scripts/narration.txt` and `narration_ja.txt` (`--language ja --strict-text`) check four lines shown together without a box over both pictures, the 200 ms delay and 40 characters a second (8 characters 416 ms into a line), the fifth line starting afresh and the guide's line back in the box; screenshots under `build/shots/narration/`.

### Dialogue: cases and the picture stack

A `tags` entry may have `cases`, tried in order before the entry's own action; the first that holds the tag's value acts instead. A case is `{"value": TEXT}` or a whole-number range `{"from": N, "to": M}` (both ends in, -1000000 to 1000000), with `action` (any tag action), `id`, `ids` (on a range: the id for each value from `from` on; past the list, `id` or the value) and `slot` (`panel_show`). An entry with cases may leave out `action`: a value no case holds is then logged as `tag: NAME VALUE`. A case with no value or range, both, `from` past `to`, or an id on an action that takes none, is a problem.

```json
"tags": {"snd": {"cases": [
	{"value": "0", "action": "music_stop"},
	{"from": 1, "to": 2, "action": "music", "ids": ["tag_theme", "title"]},
	{"from": 10, "to": 19, "action": "cue", "id": "tag_gong"}
]}}
```

Cue and music ids a tags table names (an entry's or case's `id`, `ids`, `values` and `default`) count as played: `--check-assets` accepts an `audio` entry for such an id though no data plays it, and the line plays it ([`assets.md`](assets.md), audio).

The dialogue param `picture_stack` (default 0): 1 keeps the still pictures as a stack: `picture` puts one on top (moving it up if it is already there), `picture_hide` takes that one off wherever it is (the one under it shows again), a `picture_hide` with no id (no value and no `id`, `values` or `default` in its entry) takes the top one off, and only `picture_clear` takes all. Without the stack a `picture_hide` with no id hides the picture up. Without it a picture replaces the one up, as before. The field `pictures` lists the stack bottom first, each item with `picture`, `picture_w`, `picture_h`; `picture` stays the top one. Probe: `pictures` (`chart tower`, `-`).

The script's story pictures (`picture show|hide|clear` in events, `GameSession.Pictures()`, saved with the game) and the tags' pictures make one stack: the script's at the bottom in the session's order, then the tags' that the script has not put up, the top last. `picture`, `pictures`, `picture_top`, `picture_names` and the probes `picture` and `pictures` read that stack; without `picture_stack` only the top one shows, as before. A tag's `picture_hide` and `picture_clear` take only the tags' pictures; the script's go only by its own commands. The tags' pictures go with the event; the script's stay up over the field afterwards, drawn through the dialogue layout's windows with no box (`box_visible` false), above the map and under the field's own windows, until a script hides them, and they come back with a load. Nothing of the dialogue layout is drawn on the field while there are none, so runs without script pictures draw as before. A script picture the assets' `pictures` lack is logged once (`picture problem: ...`) and left out.

```json
"params": {"picture_stack": 1}
```

### Draw order: the scripts' fades

A script's `fade out` and `fade in` paint black over the screen. The order, back to front: the field (map, people, overlays) and its comic panels; the fade; then, on the dialogue, name entry and text log screens, those screens' layouts (box, speaker plate, portrait, choices, the area banner, the entry grid, the log); then the confirm prompt and the developer tools. On every other screen (field HUD and notices, menus, shop, battle) the fade covers the whole screen as before. A fade of 0 seconds applies at once (before, at least 150 ms). Probe: `darkness` (`1.00 under dialogue`, `none`).

### Field: arrival notice and banner

Params of `field.json`: `arrive_toast` (default 1; 0: the arrival notice, `game.arrive`, is logged but shows no notice) and `area_banner` (default 1; 0: the area banner shows on neither the field nor the dialogue). Both are expressions of the common names. Each only takes away: the session decides first whether an entry announces itself (`game.json` `arrive_notice` and the area's own, `FieldInfo.AnnounceArrival`; it emits no `game.arrive` otherwise), so a notice or banner shows only when both the data and the layout allow it.

```json
{"version": 1, "params": {"arrive_toast": 0, "area_banner": 0}, "windows": []}
```

Followers stand where the session puts them (`Followers()`, each lineup member after the leader and the follower actor on a cell of the leader's trail, R166): the sample draws each gliding to its cell, the leader drawn a step ahead of the session's cell while the real-time glide leads it, and a script's walk of the leader during a scene adding its cells in front of the session's trail (the session's leader does not move in a scene). A load puts them back on the trail the save kept. Where they stand after an area entry is the exit's own `followers`, else game.json's `followers_arrive` (`stacked`, the default, `behind`, `up`, `down`, `left` or `right`); entries that take no exit (a script warp, travel, boarding, a new game) use `followers_arrive_other`.

`followers_arrive` (param of `field.json`): the fallback for data whose game.json does not set the key. `"stacked"` (default: after an exit, a warp, travel or a load the followers stand on the leader, not drawn, until it walks) or `"behind"` (each follower stands a cell behind the one before it, against the leader's facing, drawn at once, while those cells can be walked; the rest stay stacked; aboard a vehicle they ride inside as before; the session's trail then grows in front of those cells). With game.json's key set the param is ignored. Any other value is a problem. `scripts/arrive_behind.txt` checks an exit, a warp and a load with the param; `scripts/library_followers.txt` the session's own placing on data that sets the key.

```json
{"version": 1, "params": {"followers_arrive": "behind"}, "windows": []}
```

`click_actor` (param of `field.json`): what a click on an actor does. `"walk"` (default: the leader walks up to a cell beside the actor, choosing its way again each step, and faces it) or `"interact"` (the same, then interacts as Confirm would). A click on any other cell walks there as before. Any other value is a problem.

```json
{"version": 1, "params": {"click_actor": "interact"}, "windows": []}
```

### Row models

`save_slots`:

* Item fields of `slots`: `number` (1-based), `slot` (its id), `empty`, `damaged` (bool), `area`, `play_time`, `difficulty`, `party_text`, `leader` (strings, `''` for an empty or damaged slot), `level` (the leader's, 0 for none), `written` (the time written, `-` without one).
* Param `system_menu` (default 0): 1 opens the field menu's Save page on a system menu, `save` (then the slots; Back returns) and `quit` (to the title after the quit question). Fields `system` (bool, the system menu is up), `system_rows`, `system_cursor`, `mode` (`save` or `''`). The session loads saves only from the title, so the system menu has no load entry. The save prompt and the Load screen do not use it.

```json
{"version": 1, "params": {"system_menu": 1}, "windows": [
	{"name": "slots", "visible": "!system", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
		{"type": "list", "items": "slots", "state": "list", "active": "active", "row_height": "t(32)", "row": [
			{"type": "text", "value": "number + '.'", "x": 8},
			{"type": "text", "value": "empty ? '-' : area + '   ' + play_time + '   ' + leader + ' ' + level", "x": 48}]}]},
	{"name": "system", "visible": "system", "x": "area_x", "y": "area_y", "w": 260, "h": 84, "frame": "panel", "elements": [
		{"type": "rows", "items": "system_rows", "selected": "system_cursor"}]}
]}
```

`menu_equip`:

* Top-level `places`: the places offered, in order, each `{"slot": PLACE, "label": TEXT_KEY}` (`label` optional, else the place's own name), PLACE one of `weapon` (the right hand), `left_hand`, `head`, `body`, `arms`, `accessory`; a repeated or unknown place is a problem. The field `places` lists them with `slot`, `label`, `piece` (the worn piece's name), `worn` (bool) and `text` (the row as `slot_rows` has it); `slot_rows` follows them.
* Param `members_first` (default 1): 0 opens the page on the first member's places: the tab keys still turn the tabs and Back closes the menu; the PartyTalk key (the Items page's sort key) goes to the next member. Fields `member_index`, `member_count`.
* Param `stats_preview_only` (default 0): 1 leaves the last change out of the places view (`has_change` false, no `compare_rows`), so the stats show only while a piece is browsed.

```json
{"version": 1, "params": {"members_first": 0, "stats_preview_only": 1}, "places": [{"slot": "body"}, {"slot": "weapon"}, {"slot": "head"}, {"slot": "accessory", "label": "ui.equip_slot.accessory"}], "windows": []}
```

`menu_order`: the field `lineup`, one item per member with `name`, `level`, `hp`, `max_hp`, `tp`, `max_tp`, `number` (1-based), `picked` (the first of a swap) and `statuses` (their names) and `text` (the row without its number), for rows formatted by the layout:

```json
{"type": "list", "items": "lineup", "state": "list", "text": "(picked ? '> ' : '') + name + ' L' + level + ' ' + hp + '/' + max_hp"}
```

`menu_status`:

* Param `stat_bonus` (default 1): 0 leaves the equipment bonus out of each stat's `value`. The cells of `stat_rows` gain `total` (the stat's number) and `bonus` (the equipment's, signed, `''` for none) either way.
* Fields `portrait_w`, `portrait_h`: the portrait's size in pixels, for a portrait drawn at a whole multiple.
* Fields from the member detail (`MemberDetail`): `attack` (strength plus both hands' weapon attack, what a basic attack hits with), `base_hp`, `base_tp`, `base_strength`, `base_mental`, `base_agility`, `base_dexterity`, `base_defense` (the level's own numbers, before equipment, to set against the totals), `experience_for_level` (the experience the current level takes from start to end, 0 at the cap; with `experience_next` an experience bar), `total_experience` and `kind` (`living`, `spirit`, `machine`). The built-in file shows none of them.

```json
{"type": "bar", "x": "cw - t(300)", "y": "t(54)", "w": "t(300)", "h": 6, "value": "experience_for_level - experience_next", "max": "max(1, experience_for_level)", "color": "accent"},
{"type": "text", "value": "'ATK ' + attack + '  STR ' + base_strength + ' base  ' + kind", "x": "cw - t(300)", "y": "t(32)", "style": "small_dim"}
```
* Top-level `profile`: the profile rows, in order, from `name`, `level`, `experience`, `experience_next`, `kind`, `hp`, `max_hp`, `tp`, `max_tp`, `strength`, `mental`, `agility`, `dexterity`, `defense`, `critical`, `evasion`, `magic_defense`, `attack_element`, `data` (default: all); the field `profile` lists them with `key`, `label` and `value`. These are every member detail the session gives (`Member`, `MemberDetail`, the creature kind); `data` stands for the character's own rows from `party.json` `profile` (`MemberDetail.Profile`, none in the shipped data), each keyed by the data's key with the session's text for both sides. The labels are the text tables' `ui.status.profile.*`, `ui.status.attack_element` and `ui.stat.*`.

```json
{"version": 1, "params": {"stat_bonus": 0}, "profile": ["name", "level", "kind", "experience_next", "attack_element"], "windows": [
	{"name": "profile", "visible": "view == 'stats'", "x": "area_x + area_w - 280", "y": "area_y", "w": 280, "h": 280, "frame": "panel", "elements": [
		{"type": "image", "image": "portrait", "w": "portrait_w * 2", "h": "portrait_h * 2"},
		{"type": "repeater", "items": "profile", "y": "portrait_h * 2 + 8", "dy": "t(22)", "template": [
			{"type": "text", "value": "label"}, {"type": "text", "value": "value", "w": "cw", "align": "right"}]}]}
]}
```

### Flashing

The options' Flashing row replaces the Reduced flash row: `Reduced` (the profile's reduced flash on: the animation scripts' `reduced` replacements and damping, and the flash limit), `Standard` (the flash limit, no replacements: the old setting off) and `As authored` (no limit, no replacements). The level and whether the photosensitivity notice was seen are the profile's `flashing` and `flash_notice_seen` (`Profile.Flashing`, `Profile.FlashNoticeSeen`, written only once set); a level sets the profile's reduced flash to match. A profile that never chose (`FlashLevel.Unset`, an older profile) maps reduced flash on to Reduced and off to Standard. `controls.json` keeps no flashing entry.

* Choosing As authored asks the photosensitivity question (`ui.options.flashing_warning`) until it has been answered yes once, or the notice below has been seen.
* The options layout's param `flashing_original` (default 1) at 0, or `game.json`'s `presentation.flash_original` false (`GameInfo.FlashOriginal`), leaves As authored out of the row.
* A new profile in normal play (no script; or a scripted run with `--play-defaults`) starts at the data's default: `game.json` `presentation.flash_default` (`reduced`, `standard` or `original`; `GameInfo.FlashDefault`), and Reduced when the data hides original. Without the key the default is Reduced, and the title says once where the option is (`ui.title.flashing_notice`). With `original` (and the choice not hidden) the photosensitivity notice comes first, on black before the title or anything that moves: the game has flashing effects, the setting is under Options > Flashing, and three answers, Reduced (the cursor's start: one Confirm), Standard, Continue as authored (also Back). It is drawn through the `confirm` layout, so a layout restyles it but cannot leave it out, and the data cannot skip it; seeing it counts as the warning's answer. The game's loader refuses any other value (`data.bad_flash_default`).
* Scripted runs (headless or in the window) start at the profile's default, Standard, so every transcript and digest stays as before; that is the difference from normal play. The probe `flashing` reads `reduced`, `standard` or `original` (`original notice` while the notice is up).
* The confirm layout gains the field `cursor` (the answer under the cursor; Yes 0 and No 1 for a question) and `options` holds the notice's three answers; the built-in file sizes its box by `len(options)` and highlights `cursor`, which draws a yes/no question as before.

The words are the text tables' `ui.options.flashing`, `ui.options.flashing.reduced`, `.standard`, `.original`, `ui.options.flashing_help`, `ui.options.flashing_warning`, `ui.options.flashing_notice`, `ui.options.flashing_keep` and `ui.title.flashing_notice`.

`scripts/flash_safety.txt` asserts the three levels on a strobing script; `make_flash_data.py` writes a data copy whose default is original, on which `flashing_notice.txt` (Standard chosen) and `flashing_notice_keep.txt` (Back keeps it, Original later asks nothing) run with `--play-defaults`, and `flashing_new.txt` the shipped data's Reduced default with its notice and the warning.

### The developer tools at small resolutions

Two `styles.json` params: `dev_scale` (10 to 100, default 100) draws every developer tool (badge, console, picker, overlays, the frame ruler, the windows overlay's labels) at that percent of its 1280x720 sizes, laid out in a frame of the logical size over that percent; `dev_draw` (0 or 1, default 1) at 0 draws none of them, while their keys and commands still work. A 320x224 set with `"dev_scale": 60` draws the tools' text at 10 pixels, its own text size ([`dev-tools.md`](dev-tools.md)). `dev_scale_bitmap` (0 or 1, default 0) at 1 scales a bitmap font's text by `dev_scale` too (it snaps to the font's whole multiples, 1 at least). Off the battle the badge sits at `dev_badge_x` (-1, the default: centered) and `dev_badge_y` (default 12), in the tools' units (the frame's pixels times 100 over `dev_scale`); in battle the battle layouts' `dev_badge_y` places it as before.

```json
"params": {"field_cell": 16, "dev_scale": 50, "dev_scale_bitmap": 1, "dev_badge_x": 4, "dev_badge_y": 420}
```

### The item runs

`make_items_assets.py EXPORT TEST OUT` copies the test assets with `alt/assets_more.json` as the manifest, a cue `tag_gong` and a track `tag_theme` that only the tags play, and the item layouts; `make_items_data.py` writes a data copy with the event `dialogue_items` and no party macros. `items_battle.txt` (front data copy): the title's start state and entries, the top-level step, the command row walked with Left and Right, the item and technique lists' sizes, the actor's slot, the chosen-command window, the fallen and status flags, the enemies' icons by the table and (side) off it, Run for the party, the result in two pages. `items_menus.txt`: the profile rows, the Equip page on the first member, the system menu and numbered slots, the unnumbered lineup. `items_dialogue.txt` (item data copy): a line while faded out, the cases, the picture stack, the top-level Macro without macros, a warp without notice or banner. `small_dev.txt`: the small set with `dev_scale` and `dev_draw`. Screenshots under `build/shots/items/` with `--window`.

`make_choices_assets.py EXPORT TEST OUT` copies the test assets with a title whose Continue opens the Load list, a front battle layout with `item_order` `"equipment_then_items"` and `macro_auto_first`, and an Items page with `actions` and a `result_text` line; `alt/` holds the built-in title and battle files for the run to swap in, and `alt/title_broken.json` and `alt/menu_items_broken.json`, which the Makefile checks on a copy. `layout_choices.txt` (front data copy, `--dev`): the worn Storm Rod before the Tonic and `tempest (auto)` first, then the built-in order after the swap; the Items page's look, a use and a refusal on the result line with no notice; Continue opening the Load list, then loading the newest save with the built-in title. Screenshots under `build/shots/choices/` with `--window`.

`make_library_data.py GAME CORE OUT [--bag] [--no-autosave]` copies the data with game.json's `followers_arrive` "behind", Tidewater's stall and inn named (the stall with a voice whose welcome takes the name as `{0}`), two test yards with a 2x2 block of exits each (`exit_yard_a` with `field_poison`, `exit_yard_b` with `field_paralysis`), rules.json's field poison and paralysis, and test items that poison, paralyze and drain; `--bag` adds `bag_slots` 4 and `--no-autosave` `autosave` false. `make_library_assets.py EXPORT TEST OUT` copies the test assets with the Macros page's `options`, a front battle layout with `macro_check`, `macro_failed` "lose_turn", `macro_flow` "remember" and `auto_choose`, and an Extras menu with the `auto_trouble` and `auto_policy` rows; `alt/battle_front_broken.json` and `alt/menu_macros_broken.json` are checked on a copy. Runs, with `--dev`: `library_followers.txt` (playthrough saves: the session's follower placing after warps and the trail, `dev rest`), `exit_block.txt` (the block crossed and left with keys and clicks in both field inputs), `library_shop.txt` (`--bag`: names, the keeper's `{0}`, the bag line in the shop and on the Items page, a refused purchase, the cache staying shut, the inn's title), `library_autosave.txt` (`--no-autosave`: no autosave marks, no retry), `field_status.txt` (from the save `field_status_cli.txt` writes with game_cli: `game.poison_fell` and `game.paralysis_worn_off`) and `library_macros.txt` (shipped data: the options stepped, the step checks, a macro's round with the menus back on Macro and the last macro, the Extras rows in battle, the Auto row's choice). Screenshots under `build/shots/library/` with `--window`.

`make_host_data.py GAME CORE OUT [--consume]` copies the data with an item the bag may not throw away (`host_charm`, `discardable` false), Tidewater's east exit firing `always`, the stall's keeper voice with a two-page welcome, and control tags in a party talk line, the Tidewater codex entry, the quest objectives, the Tonic's description and the discard notice; `--consume` adds core's `item_no_reach` "consume" and the Tonic's `consume_when_no_effect`. `make_host_assets.py EXPORT TEST OUT` copies the test assets with the menu's Journal and Records tabs (children with `when`, `enabled` and `immediate`), the Items page's `discard`, the party talk page with portraits, the `newest` mark and `write_time` in the slot rows, `select_newest`, Continue "choose", the field's `exit_markers` and a palette track on the `shadow` sprite; `alt/menu_broken.json` is checked on a copy. Runs, from playthrough saves, with `--dev`: `host_menus.txt` (the newest save preselected, the children, Discard with its count, confirm, result and a refusal, the party talk page, Mend and the Tonic greyed on the Cogwright before the pick, the exit markers, the shadow stepping in battle, `combo` on the console), `host_tags.txt` (`expect notext {` on each screen that shows the tagged text: the party talk as a notice and on its page, the Items page's description, look and result, the quest log, the codex, the text log and the shop's keeper line and description) and `host_reach.txt` (`--consume`: the Tonic selectable on the Cogwright and spent, Mend still greyed, in the field and in battle). Screenshots under `build/shots/host/` with `--window`.

`make_talk_data.py GAME CORE OUT` copies the data with the `chapter_one` party talk only in Tidewater and three speakers there: the mystic in two pages, the medic with `{portrait:medic:worried}` and a wait, the scout in one long line, in both languages. `make_talk_assets.py EXPORT BASE OUT [--small]` copies the test assets (or the small set) with `party_talk` "speakers" and a `talk_speaker` window, the Talk tab `immediate`, the party talk first in Extras and the medic's portrait with a `worried` expression; `alt/menu_options_hide.json` (`talk_hide_menu` 1), `alt/menu_plain.json` (no Talk tab) and `alt/menu_options_broken.json` (a bad `party_talk` word, checked on a copy). Runs, from playthrough saves, with `--dev`: `talk_speakers.txt` (1280x720: the reveal at the normal speed, Confirm hurrying and stepping through the pages and speakers, the expression, `expect notext {`, Back, the menu hidden, the page from Extras by Confirm, nobody to talk) and `small_talk.txt` (320x224: the scout's line in two boxes of two rows, the expression, the menu hidden). Screenshots under `build/shots/talk/` and `build/shots/small/` with `--window`.

Text from the game's tables shown outside the dialogue box, narration and their logs (notices, the party talk page, keeper lines, shop and Items results, descriptions, the quest log, the codex, credits, a choice's prompt and answers, the battle's own event lines) goes through the same reading as the text log (`Shown` in `SampleController.Text.cs`): `{var:...}` filled, presentation tags dropped, `{page}` a break (a space, or a row of its own on the party talk page), a `{name}` token the glossary lacks kept as written. Text with no `{` is shown as it is.

## Layout flows

Each of these is optional, as the layout items above are: a set without them behaves as before and the export writes none of them. `--check-assets` checks each and `--capabilities` lists them (`shop_flow_steps`, `inn_flow_steps`, `result_phases`, the screens' fields and params). `scripts/make_flows_assets.py EXPORT BASE OUT [--small]` writes a set that uses them all (below, "The flow runs").

### Field menu: entries as a list

`menu.json` param `navigation`: `"tabs"` (default: the tabs as before) or `"list"`. With `list` the entries are a list: Up and Down choose (wrapping), Left and Right do nothing, Confirm opens the highlighted entry's page (an entry with `children` its children's list, `immediate` acting as the page opens), Back on the page's top returns to the list and Back on the list closes the menu. Inside an open page every key does what it does there (a child page's Back returns to its children's list as before). A page the Extras menu or the System action opens is open from the start (Back from an entry's page returns to the list, from a page the entries leave out closes the menu). The `page` slot draws nothing while the list has the keys; a layout previews the highlighted entry through its fields. Leaving the menu for another screen (a travel list) and coming back opens it on the list. Lists are not clickable, so the mouse does nothing here.

Fields of `menu` (also read by a child's `when` and `enabled`): `navigation` (string), `list_cursor` (int, the highlighted entry; `tab` is the same index), `page_open` (bool, a page is open; always true with tabs), `entry_label`, `entry_page` (string: the highlighted entry's name and page id). The `page` probe reads `entries` while the list has the keys; `selected` reads the entry's name.

```json
{"version": 1, "params": {"navigation": "list"}, "entries": [{"page": "items"}, {"page": "status"}, {"page": "system", "children": [{"page": "save"}, {"page": "options"}]}], "windows": [
	{"name": "entries", "x": 12, "y": 12, "w": "W / 4", "h": "len(tabs) * 30 + 48", "frame": "panel", "padding": [24, 24, 24, 24], "elements": [
		{"type": "rows", "items": "tabs", "selected": "list_cursor", "row_height": 30, "selected_color": "page_open ? 'dim' : 'accent'"}]},
	{"name": "preview", "visible": "!page_open", "x": "W / 4 + 24", "y": 12, "w": "W - W / 4 - 36", "h": 54, "frame": "panel", "elements": [
		{"type": "text", "value": "'Open ' + entry_label + '?'", "x": 24, "y": 12, "color": "dim"}]},
	{"name": "box", "visible": "page_open", "x": "W / 4 + 24", "y": 12, "w": "W - W / 4 - 36", "h": "H - 66", "frame": "panel", "elements": [
		{"type": "slot", "slot": "page", "x": 24, "y": 24, "w": "cw - 48", "h": "ch - 48"}]}
]}
```

### Item rows and counts

Row fields that draw an item's count apart from its name: `name` (string), `count` (int) and `count_text` (string: the count by the screen's `count_format`, a .NET format of `{0}`, default `"{0}"`, or for a count of 1 by `count_format_one` when the file gives it, `""` hiding it). Inside a row, an item that has its own `count` reads it as `count`; the rows' count is then `len(items)` (rows of other items keep `count` for the rows' count as before). The formats are plain strings (no `'`, no placeholder but `{0}`).

* `menu_items`: field `item_rows` (items: `text` as the plain list's row, `name`, `count`, `count_text`); the target choice's `target_count` (int) and `target_count_text`; Discard's `discard_name` and `discard_count_text` (the count chosen). Param `stack_rows`: `"stacked"` (default: one row per item with its count) or `"single"` (one row per copy, each with a count of 1, the plain list's text the name alone; the cursor, actions, uses and Discard work on the copy's item; `item_count` and `item_total` count kinds and copies as before).
* `shop`: the `rows` items gain `name`, `count` (how many the party has, `have`) and `count_text`.
* `battle_front`, `battle_side`: an item option's row in `menu_items` gains `count` (the copies in the bag) and `count_text` (`""` on other rows and worn pieces).

```json
{"version": 1, "params": {"stack_rows": "single", "count_format": "x{0}", "count_format_one": ""}, "windows": [
	{"name": "page", "x": "area_x", "y": "area_y", "w": "area_w", "h": "area_h", "elements": [
		{"type": "list", "items": "item_rows", "state": "list", "w": "picking ? cw / 2 : cw", "h": "ch - 90", "row_height": 30, "active": "!picking", "row": [
			{"type": "text", "value": "name", "x": 4, "color": "selected ? 'accent' : 'text'"},
			{"type": "text", "value": "count_text", "w": "(picking ? area_w / 2 : area_w) - 16", "align": "right", "color": "dim"}]}]}
]}
```

### Travel from the menu

Params of `travel.json`:

* `one_destination`: `"list"` (default) or `"go"`: an item or technique from the field menu with exactly one place to go (a town, the way out of a dungeon) goes there at once, with no list (a Back on its message before leaving returns to the menu). The top-level `one_destination_for` decides it per thing or kind ("More layout flows").
* `cancel_row` (expression, default 1): 0 leaves the Cancel row (and a port's Leave row) out of the list; Back cancels. Field `cancel_row` (bool); `labels` has no last row then.
* `leave_key`, `arrive_key` (text keys, plain strings): a message before leaving and after arriving, for an item or technique from the menu: the text of `KEY.THING` (the item's or technique's id) when the tables have it, else `KEY`, with `{0}` the thing's name and `{1}` the destination's. The arrival's message stays over the field until it goes on. The sample's own words give `ui.travel.leaving` and `ui.travel.arrived`. `message_ms` (expression, default 0: Confirm goes on): above 0 a message goes on by itself after that long (Confirm still goes on).

Fields: `destinations` (items: `text` the plain row, `name` the area's name, `region`, `area` the area id), `phase` (`list`, `leave` or `arrive`), `message` (string), `message_ms` (int, how long it has shown). A port uses `cancel_row` and `destinations` only.

```json
{"version": 1, "params": {"one_destination": "go", "cancel_row": 0, "leave_key": "ui.travel.leaving", "arrive_key": "ui.travel.arrived", "message_ms": 0}, "windows": [
	{"name": "box", "visible": "phase == 'list'", "x": 60, "y": 80, "w": 520, "h": "len(destinations) * 34 + 90", "frame": "panel", "elements": [
		{"type": "list", "items": "destinations", "state": "list", "x": 24, "y": 60, "w": "cw - 48", "h": "len(destinations) * 34", "row_height": 34, "row": [
			{"type": "text", "value": "name", "color": "selected ? 'accent' : 'text'"},
			{"type": "text", "value": "region", "w": "cw - 64", "align": "right", "style": "small_dim"}]}]},
	{"name": "message", "visible": "phase != 'list'", "x": 120, "y": "H - 200", "w": "W - 240", "h": 110, "frame": "panel", "elements": [
		{"type": "text", "value": "message", "x": 28, "y": 24, "w": "cw - 56", "wrap": true, "lines": 2}]}
]}
```

### Shop and inn flows

A top-level `flow` (`shop.json`, `inn.json`): the steps the screen goes through, a selection kept in this order; Back goes to the step before that takes a choice, and leaves from the first.

* Shop, from `welcome` (the keeper's line alone; Confirm goes on), `mode` (Buy or Sell; without it the list starts on buying and the tab keys switch), `list` (required), `quantity` (how many, as in the steps), `question` (the keeper's question alone before the YES/NO; "More layout flows"), `confirm` (Yes or No on the row: Up, Down, Left or Right switch, Yes trades one, or the quantity when a `quantity` step comes first; No or Back return to the list), `result` (the trade's outcome until Confirm; without it a trade stays on the list), `farewell` (the keeper's line on leaving; Confirm or Back close the shop). Without `flow`, `steps` 0 keeps the tabs and `steps` 1 is `mode`, `list`, `quantity`, `result`. Fields: `flow_step` (the step's name, `''` on the tabs), `step` as before (`choose` for `mode`), `confirm_options` (Yes, No), `confirm_cursor` (int).
* Inn, from `welcome`, `price` (the price line; Confirm goes on), `choice` (Rest or Leave; required), `result` (the rest's outcome until Confirm: after a rest the session has closed the inn, so the inn's screen stays over the field with the rest point it was; after a refusal back to the choice), `decline` (on leaving without a stay; "More layout flows"), `farewell` (on leaving, or after the result of a rest; only after a stay when the flow has `decline`). Default `["choice"]`, as before. Fields: `step` (string), `rested` (bool).

The keeper's line (`voice_line`) by step: `welcome` (welcome, mode, list), `confirm` (`confirm_buy` and so on for a shop's quantity and confirm; an inn's price and choice), `thanks` or `cannot_afford` (result), `farewell` (`VOICE.farewell`, else `ui.shop.farewell` or `ui.inn.farewell`, the sample's own words when the tables have none), with the parameters of "Inn", above.

```json
{"version": 1, "flow": ["welcome", "mode", "list", "confirm", "result", "farewell"], "windows": [...]}
```

```json
{"version": 1, "flow": ["welcome", "price", "choice", "result", "farewell"], "windows": [
	{"name": "box", "x": 12, "y": "top", "w": "W - 24", "h": 168, "frame": "panel", "elements": [
		{"type": "text", "value": "voice_line", "x": 24, "y": 24, "w": "cw - 48", "wrap": true, "lines": 2},
		{"type": "rows", "visible": "step == 'choice'", "items": "options", "selected": "cursor", "x": 54, "y": 84}]}
]}
```

### Title: intro and where it opens after a defeat

`title.json` top-level `intro`: steps `{"name": NAME, "ms": 1 to 60000}` (names unique), played in order on the first title since the program started, before the start state (or the menu without one); any key or button but the function keys skips to the start state. Fields: `intro` (bool, it plays), `intro_step` (the step's name), `intro_index` (int), `intro_ms` (int, milliseconds into the step), `intro_step_ms` (int, its length). The layout's windows draw each step by its name (pictures through the title's image fields).

Param `after_defeat` (a plain string): where the title opens after a defeat (`game_over`): `"menu"` (the menu at once), `"start"` (the start state; needs the file's `start`), `"intro"` (the intro; needs the file's `intro`) or `"intro:STEP"` (the intro from that step; "More layout flows"). Left out, the title opens as each time it comes up, the start state when the file has one (the built-in title has neither, so it opens on the menu).

```json
{"version": 1, "start": true, "intro": [{"name": "studio", "ms": 1500}, {"name": "story", "ms": 2500}], "params": {"after_defeat": "intro"}, "windows": [
	{"name": "intro", "visible": "intro", "z": 10, "elements": [
		{"type": "fill", "w": "W", "h": "H", "color": "black"},
		{"type": "text", "visible": "intro_step == 'studio'", "value": "'A studio'", "y": "H / 2", "w": "W", "align": "center", "alpha": "min(1, intro_ms * 3.0 / intro_step_ms)"}]},
	{"name": "box", "visible": "!start && !intro", "anchor": "center", "w": 600, "h": 400, "frame": "panel", "elements": []}
]}
```

### Vehicle battles

Screen keys `battle_front_vehicle` and `battle_side_vehicle` (no built-in file, not exported, never missing): in a vehicle battle the stage layout's `_vehicle` file draws in place of its own when the asset directory gives one, so the first menu, the windows, their frames and colors and every param (placement included) can differ; without one the stage layout's file draws as before. Each is a whole battle file with the same fields, slots, params, required windows, `statuses`, `command_rows` (`vehicle` included) and `result_sequence`. Probes name its windows by its key (`battle_front_vehicle/command`).

```json
{"version": 1, "windows": [..., {"name": "command", "visible": "menu_shown", "x": "W - w - 16", "y": 16, "w": 360, "h": 114, "frame": "vehicle", "elements": [...]}, ...]}
```

### Battle windows' timing and order

What the battle's windows already take their timing from:

* The command window opens when the playback queued before the members choose has played: `battle:transition_in` (an animation script, 0 frames built in), the intro step (`timings.intro`, 900 ms; `step:intro`), an opening banner when the battle opens with one (`timings.opening`, 1000); each later round after its steps (`timings.round`, `roundend`, ...). The `command` window's own `open` and `close` transitions (kind, `ms`, `step`) then play, shortened by the battle speed.
* The action's name (front `name_plate`, side `banner` with `banner_plate`) shows during the turn's `banner` phase (`timings.banner`, 420 ms, or a turn script's `banner` phase), with the window's `open` and `close`; a turn script with a `name_window` track shows it only while one plays, fading over its `fade` ([`battle-animation.md`](battle-animation.md), `name_window`).
* The result: the outcome step (`timings.outcome`, 900; `step:outcome.*`), each level-up and learned notice as a step after it (`timings.notice`, 700 each; `step:notice.game.level_up`), then the result window in phase `ended` (its `open` transition), paged by `result_page_lines`, back to the field on Confirm or Back (`game_over_confirm`, and the timing `game_over_return_ms` after a defeat), then `battle:transition_out`.

Added to either battle file:

* Param `command_open_delay` (expression, milliseconds, default 0): after the battle's opening playback, the first command window waits this long (phase `playback`, no menu); later rounds do not wait.
* Top-level `result_sequence`: the result screen as steps in order, each `{"phase": PHASE, "ms": 0 to 60000}` with PHASE from `victory` (the outcome alone), `result` (the result window; required) and `level_ups` (the level-ups and what they taught; skipped when nobody gained a level); a step goes on with Confirm, or by itself after `ms` when above 0 (Confirm still goes on), the result step turning its pages first, and Back ends the battle from any step. With a `level_ups` step the level-up and learned notices do not play as steps after the outcome (their lines go to the log, the level-up cue plays as the step comes up) and the result window's `result_lines` leave them out. Fields: `result_phase` (the step's phase in phase `ended`, `result` there without a sequence, `''` before), `result_phase_ms` (int, its time), `level_lines` (items `kind`, `text`: the level-up and learned lines).

```json
{"version": 1, "params": {"command_open_delay": 600}, "result_sequence": [{"phase": "victory", "ms": 800}, {"phase": "result"}, {"phase": "level_ups"}], "windows": [
	...,
	{"name": "victory", "visible": "result_phase == 'victory'", "anchor": "center", "w": 640, "h": 108, "frame": "battle", "elements": [
		{"type": "text", "value": "outcome", "y": 24, "w": "cw", "align": "center", "style": "title"}]},
	{"name": "result", "visible": "result_phase == 'result'", ...},
	{"name": "levels", "visible": "result_phase == 'level_ups'", "anchor": "center", "w": 853, "h": "len(level_lines) * 30 + 78", "frame": "battle", "elements": [
		{"type": "repeater", "items": "level_lines", "x": 24, "y": 54, "dy": 30, "template": [{"type": "text", "value": "text"}]}]}
]}
```

### The flow runs

`make_flows_assets.py EXPORT BASE OUT [--small]` copies the test assets (or with `--small` the small set) with every flow above: the menu's entries as a list with a System entry's children, the System action and Extras entries in and out of the menu's; Items one row per copy (`alt/menu_items_stacked.json` stacked); the shop's and inn's full flows; the title's two-step intro with `after_defeat` "intro"; and the battle's delay and result steps; at 1280x720 also the travel flow (`alt/travel_timed.json` with `message_ms` 400) and a vehicle front layout in a teal outline frame. `alt/title_broken.json`, `alt/title_no_intro.json` and `alt/menu_broken.json` are checked on copies. Runs on the front data copy with `--dev`: `flows.txt` and `small_flows.txt` (README, step 10). Screenshots under `build/shots/flows/` with `--window`.

## More layout flows

Each of these is optional, as the flows above are: a set without them behaves and draws as before (every existing run's transcript and frame digests are unchanged) and the export writes none of them. `--check-assets` checks each and `--capabilities` lists them (the steps in `shop_flow_steps` and `inn_flow_steps`, `vehicle_commands`, `image_names`, `travel_kinds`, the screens' fields and params). `scripts/make_more_flows_assets.py` writes a set that uses them all (below, "The second flow runs").

### Vehicle battles: a grouped row

`command_rows.vehicle` may list `weapons` or `options`: one row (the sample's words `Weapons`, `Options`; keys `ui.battle.command.weapons`, `.options`) that opens the vehicle's skills as a list (phase `option`, `list_kind` the row's id, `menu_title` the row's words), in place of `skills`' one row per skill. Back from the list returns to the row. The list's rows are `menu_items` with `name`, `uses`, `max_uses` and `cost`, as every option list's. Listing more than one of `skills`, `weapons` and `options` is a problem.

Battle field `command_items` (items: `text`, `usable`, `command`): the command rows of the member or vehicle choosing, in the command phase and kept while a list one of them opened (or its target choice) is up, `[]` otherwise; with `list_index` (the row whose list is open) a layout keeps the first menu in its window and draws the list in a window of its own.

```json
{"version": 1, "command_rows": {"vehicle": ["attack", "weapons", "escape"]}, "windows": [...,
	{"name": "command", "visible": "menu_shown", "x": 480, "y": 460, "w": 360, "h": 114, "frame": "battle", "elements": [
		{"type": "rows", "items": "list_kind == 'weapons' ? command_items : menu_items", "selected": "list_kind == 'weapons' ? list_index : cursor", "enabled": "usable",
			"x": 22, "y": 18, "size": "size_small", "row_height": 22}]},
	{"name": "weapons", "visible": "list_kind == 'weapons'", "x": "command.right + 8", "y": "command.y", "w": 280, "h": "len(menu_items) * 22 + 36", "frame": "battle",
		"padding": [18, 18, 18, 18], "elements": [
		{"type": "rows", "items": "menu_items", "selected": "cursor", "text": "name", "enabled": "usable", "size": "size_small", "row_height": 22},
		{"type": "repeater", "items": "menu_items", "dy": 22, "template": [
			{"type": "text", "value": "uses >= 0 ? uses + '/' + max_uses : ''", "w": "cw", "align": "right", "style": "small_dim"}]}]}
]}
```

### Inn: decline

The inn's `flow` may have `decline` (between `result` and `farewell`): leaving without a stay (Leave, or Back from any step before one) shows the keeper's decline line until Confirm or Back, and `farewell` then shows only after a stay. Without `decline` the farewell shows on both, as before. The line is `VOICE.decline`, else `VOICE.farewell`, then `ui.inn.decline`, else `ui.inn.farewell` (the probe `voice` reads the key used).

```json
{"version": 1, "flow": ["welcome", "choice", "decline", "farewell"], "windows": [...]}
```

### Level-ups by page

Battle param `level_page_lines` (expression, default 0: every line at once): the `level_ups` step of a `result_sequence` shows that many lines a page; Confirm turns the page, or the step's `ms` (above 0) does after that long on each page, and the last page's Confirm (or `ms`) goes on. Fields `level_page` (1-based), `level_pages` and `level_more` (bool, a page follows); `level_lines` is the page's lines. The window's size is its own `h`: a fixed number keeps one size whatever the page holds, `len(level_lines)` grows it with the page, and `min(len(level_lines), 8)` grows it to a cap; the code has no growth rule of its own.

```json
{"version": 1, "params": {"level_page_lines": 2}, "result_sequence": [{"phase": "victory", "ms": 600}, {"phase": "result"}, {"phase": "level_ups"}], "windows": [...,
	{"name": "levels", "visible": "result_phase == 'level_ups'", "anchor": "center", "w": 960, "h": 168, "frame": "battle", "padding": [24, 24, 24, 24], "elements": [
		{"type": "text", "value": "level_page + ' / ' + level_pages", "w": "cw", "align": "right", "style": "small_dim"},
		{"type": "repeater", "items": "level_lines", "y": 30, "dy": 30, "template": [{"type": "text", "value": "text"}]},
		{"type": "image", "visible": "level_more", "image": "'ui:page_arrow'", "src": [0, 0, 16, 8], "x": "cw / 2 - 30", "y": 90, "w": 60, "h": 30}]}
]}
```

### Image elements: a source rectangle and named pictures

An `image` element's `src` (`[x, y, w, h]`, each an expression) draws that part of the picture, in the picture's own pixels, stretched nearest-neighbor over the element's `w` and `h`; the parts outside the picture are left out, the drawn rectangle shrinking with them. With `repeat` true the picture wraps on both axes, so a moving `x` or `y` scrolls it without end. The expressions read the screen's fields, so `clock_ms`, the title's `intro_ms` and any other field move or crop it over time. Without `src` the element draws the whole picture as before.

The `image` property may give a string instead of an image field: `picture:NAME` (the manifest's `pictures`), `title:background`, `title:logo`, `title:layer:N` (layer N from 0, its frame now), `ui:cursor`, `ui:bar_track`, `ui:bar_fill`, `ui:page_arrow` (the UI skin's pictures), or the path of a PNG in the asset directory (`ui/sign.png`). A name with nothing behind it draws nothing; a plain string literal that is none of these forms is a problem. `src` that is not four expressions, or `repeat` without `src`, is a problem.

```json
{"type": "image", "visible": "intro_step == 'story'", "image": "'picture:strip'", "src": ["intro_ms / 225 * 8", 0, 16, 16], "repeat": true, "x": 576, "y": 296, "w": 128, "h": 128},
{"type": "image", "image": "'title:layer:0'", "src": [0, 0, 8, 8], "x": 608, "y": 328, "w": 64, "h": 64},
{"type": "image", "image": "'picture:clouds'", "src": ["clock_ms / 40", 0, 320, 60], "repeat": true, "w": "W", "h": 240}
```

### The Load screen: slot blocks

`load_game` fields (the screen's own, beside the `slot_list` slot, which draws the `save_slots` layout as before): `slots` (items, made only when the layout reads them), `cursor` (int, the slot under the cursor), `navigation` (string) and `columns` (int). Item fields of `slots`: every `save_slots` row field (`number`, `slot`, `empty`, `damaged`, `area`, `play_time`, `difficulty`, `party_text`, `leader`, `level`, `written`, `newest`, `write_time`, `head`, `detail`, `detail_color`, `enabled`), and `autosave` (bool), `selected` (bool, the cursor is on it), `member_count` (int) and `members` (items: `id` the character, `name`, `level`, `portrait` and `sprite` (images: the member's portrait as the menus draw it, and the standing frame of its field sheet facing south, or the generated look)).

Params:

* `navigation` (a plain string): `"list"` (default: Up and Down, as before), `"grid"` (Left and Right step, Up and Down move a row of `columns`, each stopping at the edges) or `"row"` (Left and Right, wrapping; Up and Down do nothing).
* `columns` (expression, default 1): the grid's width.
* `show_empty` (expression, default 0): not 0, the list holds every manual slot, empty ones too, before the autosaves; Confirm on an empty slot plays the `refuse` cue and stays. A block draws an empty slot by its own elements (`"visible": "empty"`), or a `repeater`'s `empty` template draws the slots past the items.

What `SaveSummary` gives is all a block shows: the slot, area, play time, difficulty, write time, autosave, newest, damaged with its error, and the party as character ids with levels. A richer block would need from the library the party's money, each member's hit and technique points, the story chapter or a flag text, and the area's region or a location picture.

```json
{"version": 1, "params": {"navigation": "grid", "columns": 2, "show_empty": 1}, "windows": [
	{"name": "box", "x": 12, "y": 12, "w": "W - 24", "h": "H - 24", "frame": "panel", "padding": [24, 24, 24, 24], "elements": [
		{"type": "repeater", "items": "slots", "layout": "grid", "columns": "columns", "dx": "cw / 2", "dy": 150, "y": "-(cursor / 8) * 600", "template": [
			{"type": "group", "visible": "i / 8 == cursor / 8", "elements": [
				{"type": "box", "frame": "panel", "w": "cw / 2 - 8", "h": 142},
				{"type": "fill", "visible": "selected", "x": 4, "y": 4, "w": "cw / 2 - 16", "h": 134, "color": "row"},
				{"type": "text", "value": "number + '  ' + slot", "x": 16, "y": 12, "style": "small"},
				{"type": "text", "visible": "empty", "value": "'No journey here yet'", "x": 16, "y": 42, "color": "dim", "style": "small"},
				{"type": "group", "visible": "!empty && !damaged", "x": 16, "y": 42, "elements": [
					{"type": "text", "value": "area + '  ' + play_time", "style": "small"},
					{"type": "repeater", "items": "members", "y": 32, "dx": 84, "template": [
						{"type": "image", "image": "sprite", "w": 32, "h": 32},
						{"type": "text", "value": "'L' + level", "x": 34, "y": 8, "style": "small"}]}]}]}]}]}
]}
```

### Shop: question

The shop's `flow` may have `question` before `confirm`: Confirm on a row (or on the quantity) shows the keeper's confirm line alone, with no YES/NO; the next Confirm opens the YES/NO (`confirm`), and so does the param `question_ms` (expression, default 0: Confirm only) after that long. Back returns to the list. So the press on the row never buys, nor the one that opens the YES/NO: the third, on Yes, does. `question` without `confirm` is a problem. The `confirm` step on its own already opens on one Confirm and buys only on a later one (each step reads the frame's Confirm once, and Confirm does not repeat while held).

```json
{"version": 1, "flow": ["list", "question", "confirm", "result"], "params": {"question_ms": 0}, "windows": [...,
	{"name": "question", "visible": "open && flow_step == 'question'", "anchor": "center", "w": 840, "h": 108, "frame": "panel", "padding": [24, 24, 24, 24], "elements": [
		{"type": "text", "value": "voice_line", "w": "cw", "wrap": true, "lines": 2}]}
]}
```

### Travel: by thing and kind, and over the menu

`travel.json` top-level `one_destination_for`: an object from a travel kind (`town`, `dungeon_exit`) or an item or technique id to `"list"` or `"go"`; an item or technique with one place to go takes its id's entry, else its kind's, else the param `one_destination`. A key that is not a kind or an id (lowercase letters, digits and `_`) or a value that is neither word is a problem.

Param `over_menu` (expression, default 0): not 0, an item or technique from the field menu draws the travel screen over the menu's windows, which stay as they were (the `backdrop` slot draws the field menu in place of the dimmed field); the menu's notices are left to the travel screen's `toasts` slot. A port and the message after arriving draw over the field as before.

```json
{"version": 1, "params": {"one_destination": "go", "over_menu": 1}, "one_destination_for": {"town": "list", "dungeon_exit": "go", "homing_shell": "go"}, "windows": [...]}
```

### Title: after a defeat, from an intro step

`after_defeat` also takes `"intro:STEP"`: the intro plays from that step (its own name in `intro`). Any other name, or no `intro`, is a problem.

```json
{"version": 1, "start": true, "intro": [{"name": "studio", "ms": 600}, {"name": "story", "ms": 900}, {"name": "logo", "ms": 700}], "params": {"after_defeat": "intro:logo"}, "windows": [...]}
```

### The second flow runs

`make_more_flows_data.py GAME CORE OUT` copies the data with Tidewater's inn voiced (`voice.more_inn`: welcome, confirm, decline and farewell lines in both languages) and the encounter `more_bounty` (a one-hit-point enemy worth 2400 experience). `make_more_flows_assets.py EXPORT BASE OUT [--small]` copies the test assets (or the small set) with generated pictures (a strip of four bands in `pictures`, a two-frame title art layer, a sign PNG, a UI skin page arrow) and every item above: the intro scrolling and cropping them, the Load screen in a grid (`alt/load_row.json` in a row), the inn's decline, the shop's question (`alt/shop_held.json` with `question_ms` 500, `alt/shop_plain.json` without the question), travel over the menu by kind (`alt/travel_id.json` by id), the level-ups two lines a page, and at 1280x720 the vehicle's Weapons row; `alt/title_broken.json`, `alt/shop_broken.json`, `alt/inn_broken.json` and `alt/load_broken.json` are checked on copies. Runs, from playthrough saves on the data copy, with `--dev`: `more_flows.txt` and `small_more_flows.txt` (README, step 10); the pictures are checked by `pixel.X.Y`. Screenshots under `build/shots/more_flows/` and `build/shots/small/` with `--window`.

## Dialogue control tags (`ui/dialogue.json`, `SampleController.Dialogue.cs`)

The session hands the sample each line with name tokens resolved and control tags as written (`events/FORMAT.md`,
"Control tags"). The sample acts on every tag; no tag is drawn, measured for wrapping or counted in the reveal.

* `{page}`: ends a page. The box waits for Confirm, clears, and shows the next page. A page with nothing to show acts
  on its tags and gives way to the next.
* `{wait:N}`: holds the reveal at that point for N times the `wait_unit_ms` timing (styles.json `timings`, default 16:
  one frame of the headless runner, about a 60 Hz frame). The format leaves the unit open and the event tools fix none
  (`event_cli time` counts the tag as characters). A wait after a page's last character holds the arrow and
  auto-advance. Instant text and Confirm-hurry skip waits. N not a whole number: a problem, no wait.
* `{portrait:ID}`: the portrait from that point on (the line starts with `say ... as`'s portrait). An ID that is not in
  `game.json`'s `presentation.portraits`, an actor, a character or the assets' `portraits` is a problem (the speaker's
  own picture stands in, as for any portrait without a picture). `ID` is `PORTRAIT` or `PORTRAIT:EXPRESSION`
  (`{portrait:guide:smile}`), an expression of the assets' portrait entry (`assets.md`, "portraits"). An expression the
  entry lacks shows what `{portrait:PORTRAIT}` shows and is a problem once per `ID` per run, not once per line.
* `{var:ID}`: replaced, when the line comes up, by `money` (the party's money), `leader` (the first lineup member's
  name) or the name the session gives the ID (`DisplayName`: a member's chosen name, a character, an item, an ability,
  an enemy, an area...). An ID it cannot name shows as written and is a problem.
* Name tokens the session left (glossary IDs it lacks) show as written.
* Any other `{word}` or `{word:value}`: looked up in the dialogue layout's `tags` table (below); a tag the table does
  not map is logged.

A tag acts when the reveal reaches its place (all at once on Confirm-hurry or instant text), once per line. Skipping
the event drops the rest of the line's tags, as it drops the line.

### Boxes

The line's `text` element (named `line`) shows at most `lines` lines (4 in the built-in file). A page that wraps to
more goes on in another box: the box breaks after the last line that fits, waits for Confirm (or auto-advance), and the
next box starts at the following line, wrapped the same way. The code measures the page as the layout draws it
(`UiRenderer.WrapOf`, with the window's `role` elements applied), so a layout's width, size, font and `lines` decide the
boxes. A `line` element without `lines` never breaks a page.

Each box reveals from its start at the profile's text speed, with the `punctuation_pauses` table and the waits in it.
Confirm on a box still revealing shows all of it; Confirm on a shown box goes to the next box, the next page, or the next
line. Auto-advance waits `max(auto_advance_min_ms, shown characters x auto_advance_ms_per_char)` per box. The `line`
cue plays on every box change; the `text` cue ticks while a box reveals. The arrow shows when the box is all shown.

### The `tags` table

A screen file may have a `tags` object (only the dialogue screen reads one; elsewhere it is a problem): each key a tag
name (letters, digits, `_`, not starting with a digit; not `page`, `wait`, `portrait`, `var` or the name tokens), each
value `{"action": ACTION, "id": ID, "slot": SLOT, "values": {VALUE: ID, ...}, "default": ID}`:

| action | does | id |
| --- | --- | --- |
| `cue` | plays the audio cue | the tag's value, or `id` |
| `music` | plays the music track as the scene's music (as `music play`) | the tag's value, or `id` |
| `music_stop` | stops the scene's music | - |
| `music_pause` | holds the music track where it is | - |
| `music_resume` | lets a held music track go on | - |
| `fx` | plays a screen effect: a step script of the battle animation set with only flash, color on stage or frame, shake and sound tracks, over the whole screen ([`battle-animation.md`](battle-animation.md), "The dialogue's fx tag"); `--check-assets` reports a name in the table that is no such script | the tag's value, or `id` |
| `panel_show` | shows a live panel at once, named and filmed by the id (a camera, actor or character), on `slot` (default `center`) | the tag's value, or `id` |
| `panel_hide` | takes the panel away at once | the tag's value, or `id` |
| `panel_clear` | takes every panel away at once | - |
| `picture` | shows the assets' still picture of that name ([`assets.md`](assets.md), "pictures") in the layout's picture window, in place of the one up | the tag's value, or `id` |
| `picture_hide` | takes the picture away when it is the one up | the tag's value, or `id` |
| `picture_clear` | takes the picture away | - |
| `portrait_off` | takes the portrait away for the rest of the line | - |
| `log` | only the transcript line | - |

`values` maps the tag's values to ids, and `default` gives the id for a value `values` leaves out (without it, the value
itself): `{bell:1}` and `{bell:2}` below play two cues and `{bell:9}` the default. Both are for the actions that take an
id and do not go with `id`, which ignores the value. A value that is not a non-empty string is a problem.

```json
"tags": {"bell": {"action": "cue", "values": {"1": "lookout_creak", "2": "cursor"}, "default": "confirm"}}
```

An id the game does not have (a cue or music no data or sample sound plays, no such camera or person, no such panel up,
no such picture in the assets, a hide of a picture that is not up) is a problem, not a crash. A shown picture stays,
between lines too, until a hide or clear or the end of the scene. A file's table falls back to the built-in one name by
name, as params do. Built in: `sound` (cue), `music` (music), `music_stop`, `panel` (panel_show, center), `panel_hide`,
`panels_clear` (panel_clear), `picture`, `picture_hide`, `pictures_clear` (picture_clear), `portrait_off`.

### Transcript lines

* `tag: NAME VALUE` (or `tag: NAME`): a tag no table maps, once per line.
* `tag problem: NAME:VALUE: REASON`: a tag the sample could not act on (an unknown id, a wait that is not a whole
  number, a `var` without a value), once per line; a portrait's unknown expression once per run.

### Probes (`expect state KEY VALUE`)

* `tags`: every tag the current (or, between lines, the last) line acted on, as written (`NAME` or `NAME:VALUE`, `page`
  for each page turn), joined by `, `; `none`.
* `pagetext`: the text the box holds (tags out, values in).
* `dialoguepage`: the box among the line's boxes, `N/TOTAL`.
* `lineportrait`: the portrait id shown, `off` after `portrait_off`, `-` for the speaker's own.
* `shownportrait`: the key of the picture shown: `portrait:ID`, `portrait:ID:EXPRESSION` (from the assets) or
  `person:ID` (the person's sheet or generated look); `off` after `portrait_off`.
* `reveal`: characters shown of the line's, tags left out and earlier boxes counted (`SHOWN/TOTAL`).
* `fontmissing`: as before, over the line's shown text.
* `wrapped`: the lines the box shows (at most `lines`) and their widths.
* `picture`: the still picture shown (`chart`), `-` for none.
* `dialogueoverlaps`: the box's named elements (`line`, `hint`, `arrow`, `portrait`) that reach outside the frame's
  inside (the window less the frame's border: 4 for the generated frame, a nine-slice's border times its scale), and the
  hint over the line, the arrow or the portrait: `hint frame`, `hint line`... or `none`; `-` without a box.

### Dialogue file changes

* `tags`: the table above.
* The line's text element is named `line` and has `"lines": 4`; the portrait, hint and arrow are named `portrait`,
  `hint`, `arrow` for `dialogueoverlaps`.
* The hint uses the text style `hint` (role `hint`), is fit to `cw - 84` (from 26 to `cw - 58`: inside the frame, left
  of the arrow) with an ellipsis, and a `role` element switches back to `dialogue` after it.
* Fields: `line` is the box's text, `revealed` and `all_shown` are the box's, `portrait` follows `{portrait:...}` and
  `portrait_off`.

## The `hint` text role

`TextRole.Hint` (`AssetManifest.RoleNames`: `menu`, `dialogue`, `battle_numbers`, `hint`). Text for docs/assets.md,
`bitmap_fonts`:

* `role` (optional): ... or `hint` (the dialogue box's key hint). A language without a `hint` font draws the hint in the
  `menu` role's font (its own, else the language's font without `role`).
* The check, for `hint`: the `ui.dialogue.next` and `ui.dialogue.skip` keys and the `ui.key.` and `ui.button.` keys.
* Asset key `bitmap_font:LANGUAGE:hint`. The `bitmapfonts` probe lists `hint FILE` only when the hint's font is not the
  menu's.

styles.json gains the text style `hint` (`size_small`, `dim`, role `hint`) and the timing `wait_unit_ms` (16).

## Scripts

* `scripts/make_tag_data.py GAME_DATA CORE_DATA OUT`: a test-only copy of the data with the event `dialogue_tags`
  (`events/dialogue_tags.ev`, on no hook) and its lines in `text/en.strings` and `text/ja.strings`: a portrait switch and
  a page, three waits and three values, a narration longer than the box, the `sound` tag (once with the data's cue
  `lookout_creak`, once with `foghorn`, which no data has), `shake` (in no table: logged) and `portrait_off`.
* `scripts/dialogue_tags.txt` (run with `--dev`): the scene in English at normal speed,
  then again with fast text, auto-advance and 150% text until Skip. `scripts/dialogue_tags_ja.txt` (same, plus
  `--language ja --strict-text`): the same in Japanese. Both save their screenshots under
  `build/shots/dialogue_tags/` with `--window` (run from `bindings/`). Headless runs and the window differ in text
  widths, so the window run reports failures where boxes break elsewhere (`reveal` at a box end, `dialoguepage`).

* `scripts/dialogue_pictures.txt` (run with `--dev` and `--assets build/pictures_assets`, a copy of `build/test_assets`
  with `alt/assets_more.json` as its manifest): the event `dialogue_pictures` shows the assets' chart, then the bell
  tower in its place, names a picture the assets lack and hides the chart when the tower is up (two problem lines),
  hides the tower and clears; with `alt/dialogue_pictures.json` swapped in as the dialogue layout, `{bell:1}` and
  `{bell:2}` play the cues their values map to and `{bell:9}` the default. Screenshots under
  `build/shots/dialogue_pictures/` with `--window`.

Run, from `bindings/`:

```
python3 monogame/Psiv.Sample/scripts/make_tag_data.py ../game/data ../core/data build/tag_data
dotnet monogame/Psiv.Sample/bin/Debug/net10.0/Psiv.Sample.dll --data build/tag_data/game/data --dev --saves build/saves_tags \
	--script monogame/Psiv.Sample/scripts/dialogue_tags.txt --frames 3400
```

## An example layout set (`make_layout_assets.py`)

A second, complete layout set, unlike the built-in one, that the headless runs play: it shows that every screen can be
placed by the asset directory's `ui/` files alone.

### How it is made

`bindings/monogame/Psiv.Sample/scripts/make_layout_assets.py EXPORT FONTS OUT` copies FONTS (the bitmap font directory
of `make_test_assets.py`) to OUT, sets each font's `scale` (0.6, the battle numbers' 0.8), adds
`"ui_layout": {"enabled": true}` to the manifest and writes `OUT/ui/styles.json`, one file per screen and the
alternates a script swaps in: `alt/styles.json` (the styles as written), `alt/styles_nine_slice.json` (the `panel`
frame the nine-slice picture `frames/nine_slice.png`, 24 pixels with an 8-pixel border) and `alt/assets_skin.json` (the
manifest with a UI skin whose `frame` is `frames/skin.png`, 12 pixels with a 4-pixel border), both pictures generated
by the script. The styles start from the export's built-in `ui/styles.json`; every screen file is written from the script's own window lists,
except `panels.json`, copied from the export (every rectangle comes from the screen). `make_test_assets.py` takes an
optional fifth argument, LAYOUT, and runs `make_layout_assets.py EXPORT FONTS LAYOUT` after the other directories.
`--check-assets --assets OUT` reports `32 layout files (640x480)` and no problems.

### What differs from the built-in set

* Styles: resolution 640x480, `"scaling": {"mode": "integer", "filter": "point"}`; the built-in colors plus `card` and
  `teal`; text style `label` (small, teal); frames `card` (a fill) and `edge` (a 1-pixel outline); `sizes.line` 14;
  `timings.toast_ms` 2500; `params` for notices 16 high, 3 apart, the text 6 in. With the fonts' scale, small and
  body text draw at one file pixel a screen pixel (10-pixel lines) and titles at two; the code's own text (hints, enemy
  labels, toasts) gets the same font, and the battle's numbers draw at two (`number_size` 20).
* No title plates: a window's name is its first line in the accent color over a rule.
* With `--dev` the windows near the top middle start 46 pixels down (`dev ? 46 : 8`), under the developer badge; the
  front battle's keys line leaves the badge's end of the line free (`dev_badge_y` puts it there).
* Title: a narrow window at the left, the hero and a creature in a card beside the options.
* Field: area, money and play time in a dark card at the top left; notices from the top right down, under the
  developer badge's line; the arrival banner over the hint line at the bottom; the hint line left-aligned with an
  accent mark.
* Dialogue and inn: a box at the top, 40 pixels in from the sides, portrait 48 pixels in a frame, lines `line_step + 4`
  apart, at most four; a choice's box is only as tall as its options; a still picture under the box, growing in by
  16-pixel steps; the inn's keeper at the box's right.
* Pause at the right edge, growing sideways in 12-pixel steps (`grow_x`); confirm centered, growing downward in 8-pixel
  steps (`grow_y`).
* Field menu: the tabs drawn as a vertical `rows` list in a column at the left (display only; Left and Right still turn
  them), the party under them (name, level, HP and TP with bars, from `party`), the money at its bottom as a number
  (`money_value`), the field behind dimmed at 0.3 (`backdrop_dim`), the page in a panel at the right (452x300 with `--dev`), which leaves the panel's bottom
  76 pixels to four notices. Lists show fewer, shorter rows (`t(17)`/`t(18)`, items 9 rows, abilities 8, save slots 7
  at `t(32)`); descriptions in `card` boxes.
* Shop: tabs as text with an underline beside the title, the stock list at the left (10 rows), the stall picture and the
  keeper's portrait (the manifest's `keepers` give the dockhand to Tidewater's stall and the lodgekeeper to its inn), the
  description and the comparison in a column at the right, over the field dimmed at 0.75.
* Load and sound test at the right of the screen; save prompt, quest log, text log, travel and name entry centered at
  the top.
* Battle, front: `strip` is a column of five cards (140x52, 4 apart) at the left edge, with empty frames for free places;
  each card's `anchor` slot is a 96-pixel square on it, so effects on a member without a figure land on its card, and
  the picture of the command chosen stands at its bottom right. The window `enemies` lists `enemy_groups` over the free
  places under the members' cards, with the stage's labels left out (`"enemy_labels": "len(members) >= 5"`: a full
  party leaves the window no room and brings them back). The technique list shows each option's cost or uses at the
  right, a repeater over `menu_items` beside the `rows` page. The
  vehicle's card stands under the column. The command window is one column of four rows that scrolls with the cursor,
  with a `cursor/count` line, beside the member choosing above the party's figures. Log, description and name plate
  over the stage right of the column; the result window low on the stage, above the party; the keys' line 30 high at the
  bottom, the developer badge at its right end. Params fit the stage: the backdrop 328 high with its ground 432 under
  it; members 84 pixels from x 196, 92 apart, feet at 448 just over the keys' line, whole on the screen; enemies around
  x 392, up to four in a row (104 or 96 pixels, feet at 200), six in two interleaved ranks (72 in front at feet 216, 56
  behind at feet 100); labels 14 high, bars 56 by 3, status icons 16, the pointer 20, numbers at size 20 rising 16,
  effects at twice their size.
* Battle, side: stage bottom 324; the party as one line per member (portrait, name and level, icons, HP and TP numbers
  and bars) in a panel at the bottom left, the commands at the bottom right; members 56 pixels in two staggered pairs at
  the right (`party_slot_N_x`, `party_slot_N_foot`), no HP bars under the enemies (`"enemy_bars": 0`),
  enemies 72 to 56 pixels around x 210; the stage's labels, icons, pointer, numbers and effects as in the front layout,
  the developer badge at 290, over the party panel.

### The scripts

`make bindings` runs each with `--dev` on its own copy of the set (the front battle on the six-enemy data copy) and
checks window rectangles with `expect state windows` and `expect state window.NAME` at each moment, with a `shot`, and
with `expect state overlaps none` that notices and the battle's windows cover no content.

* `layout_field.txt`: title; the arrival's narration and a speaker's line with the banner; field; the guide's line with
  a portrait, the yes/no choice, the name entry; the guide's scene's four notices; the shop's buy and sell tabs with
  its keeper (`keeper`) and dimming (`backdropdim`); the inn with its keeper; pause, its box checked twice while it grows;
  the reed path's banner.
* `layout_menus.txt`: status members (with `backdropdim 0.3`), stats and abilities; items with the target list; equip removing and comparing; a
  manual save with three notices under the page; order, macros, combos, quests, codex, log; options and the controls
  page; quest log and text log; the quit confirm, its box checked twice while it grows; the Load screen loading the save; Bell Cove's save prompt.
* `layout_battle_front.txt` (front data): the cache battle's command, target and technique windows (`overlaps none`),
  `enemygroups`, `enemymarks` with the labels left out, `commands` before and after the fighter chooses, a member hit, the sweep's name plate, the log, the result window; a party of five against six enemies in two ranks
  (`ranks 3+3`) with the labels back, auto, the pointer moving to a new target, sleep icons and words on three enemies, and the result with
  two level-ups.
* `layout_battle_side.txt`: a side battle's command (`partyplaces`, `enemymarks` without bars), technique description, playback hints and result; the reed
  skimmer's battles in the front layout (its card, hit) and the side layout (its menu); a defeat's result.
* `layout_frames.txt` (on saves with a cleared story, which the Makefile writes with `game_cli` first, so the title
  offers the sound test): the sound test and a ferry's travel screen with the generated frames, with the nine-slice
  `panel` (`assets swap ui/styles.json alt/styles_nine_slice.json`) and with the UI skin (`assets swap assets.json
  alt/assets_skin.json`, the styles swapped back), each checked with `uiframes`; then the console's `help overlays` and
  the windows overlay over the travel screen.

## A small example set (`make_layout_assets.py --small`)

A 320x224 set at 16 pixels a field cell, played at whole-number scales (3x in a 960x672 window, 4x in 1280x896), that
shows the field, the panels and the battle following the layouts' sizes.

### How it is made

`make_layout_assets.py --small EXPORT FONTS TEST OUT` copies TEST (`build/test_assets`: its tilesets, sheets, whole-area
pictures, objects, figures and backdrops) to OUT without its UI skin, adds FONTS' bitmap fonts at the 640x480 set's
scale (small and body text 10 pixels tall at one file pixel a pixel, titles at two) and writes `ui/styles.json` and the
files of `title`, `field`, `dialogue`, `confirm`, `pause`, `menu`, `menu_status`, `menu_items`, `panels`,
`battle_front` and `battle_side`. The manifest has no `ui_layout` entry, so the other screens keep the built-in files
(drawn for 1280x720; the runs do not show them). It also writes `alt/styles_cell20.json`, the styles at a 20-pixel
cell. `--check-assets --assets OUT` reports `12 layout files (320x224)` and no problems; with the alternate styles it
reports the test assets' tileset of 48-pixel tiles at scale 3, which draws 24 pixels a 20-pixel cell.

### What it sets

* Styles: 320x224, `integer`, `point`; `"params": {"field_cell": 16, "field_shake": 3, "toast_h": 14, "toast_gap": 2,
  "toast_pad": 4, "toast_text_y": 2}`; `sizes.line` 12. The test assets' field sheets and tileset at scale 3
  draw their 48-pixel frames on the 16-pixel cell (a third, snapped), the reed path's 16-pixel picture at one file
  pixel a pixel, Ashfall Market's 32-pixel picture at half.
* With `--dev` the windows near the top start 42 pixels down (`dev ? 42 : 4`), under the developer badge.
* Field: area and money in a card at the top left, the arrival banner under the badge's line, notices stacked up from
  the hint line at the right, the prompt as a card under the target cell (`prompt_y + 9`).
* Dialogue: `"params": {"box_h": 70, "box_margin": 14, "box_top": "dev ? 42 : 4"}`; the box at `top`, a 32-pixel
  portrait, two lines a box with a speaker and three without (longer lines go on in more boxes).
* Panels: the built-in windows with `"params": {"margin": 6, "gutter": 4, "slide": 16}`.
* Menu: the pages down a 66-pixel column, the page in the panel at the right; status and items with 11-pixel rows.
  Choosing whom an item is for, the bag shrinks to three rows and the members' list takes the page's width under it.
* Battle, front: `stage_bottom` 180, a strip of five 62x32 cards along the bottom over a 12-pixel keys line, members
  48 pixels (the test assets' 48-pixel back sheets at one pixel a pixel) at x 33 and 64 apart, enemies 32 pixels
  around x 160 with feet at 74 (the target pointer 11 over them stays under the two-line description at the top),
  labels 12 high, icons 8, the pointer 12, numbers at 16 (critical 28), effects at their file size,
  `"motion_percent": 25`, the developer badge at 104 in the stage's free band at the right. Side: `stage_bottom` 140,
  the party panel and the menu under the stage, members 32 pixels on a diagonal from (232, 76), the badge at 112 under
  the members' feet.

### The scripts

Each runs with `--dev` on its own copy of the set (`make bindings`), checks window rectangles with `expect state
windows` at each moment and saves its `shot` lines under `build/shots/small/` with `--window` (run from `bindings/`,
`--window-size 1280x896`).

* `small_field.txt`: the title; the narration and a speaker's line in the box at the bottom (`dialogueoverlaps none`);
  the field (`overlaps none`); the real-time glide (`leaderpos 48 64` to `64 64`); the prompt's card at the stall;
  click-to-walk to a cell; the reed path's picture (`fieldpicture`); the lookout's panels (`paneltiles west 16, east
  20`, then the cart's 24, then the east panel pushed in to 27) and a line in two boxes (`dialoguepage`); Ember Steps'
  objects (`objects`).
* `small_menus.txt`: the menu's status members, stats and abilities, the items with the target list, the pause box and
  the quit confirm.
* `small_battle.txt`: the front layout's command, technique description, target (`overlaps none`, `partyplaces`),
  playback and result; the side layout's command, description, playback and result.
