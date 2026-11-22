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

Screen keys: `title`, `sound_test`, `load_game`, `options`, `controls`, `confirm`, `field`, `dialogue`, `name_entry`, `pause`, `travel`, `shop`, `inn`, `save_prompt`, `panels`, `menu`, `menu_status`, `menu_items`, `menu_equip`, `menu_order`, `menu_macros`, `menu_combos`, `menu_quests`, `menu_codex`, `menu_log`, `menu_options`, `save_slots`, `quest_log`, `text_log`, `battle_front`, `battle_side`. The two battle files are the two stage layouts; the battle presentation table's `layout: front|side` (and its per-encounter and per-area overrides, `--battle-layout` and the `battlelayout` script verb) chooses which one draws.

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
| `run_speed_percent` | 200 | the real-time field input's run: a step takes the area's (or vehicle's) step time times 100 over this, 100 at least (`"timings": {"run_speed_percent": 250}`) |

### Params

Numbers the code's own drawing reads, in whole logical pixels; a key left out takes the default, the sample's value. Several screens draw the notices through their `toasts` slot (title, Load, options, the field menu, shop, travel); the field draws them from its own layout.

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
| `open`, `close` | transitions: `{"kind": "none|fade|grow|grow_x|grow_y|slide", "ms": 200, "from": "left|right|top|bottom", "step": 0}`. Open plays when the window becomes visible, or when its screen comes up; close plays the frame only, going the way it came. `grow` grows both sides from the middle, `grow_x` only the width and `grow_y` only the height; a growing window shows its frame only, its title and elements once it has its size. `step` (grow kinds only, 0 to 1024 logical pixels, default 0: smoothly) grows by whole steps, at least one step, never past the window's size. In battle, the battle speed shortens them |
| `blocks_clicks` | true: a mouse click inside it does not reach click-to-walk |
| `elements` | the list below |

### Conditions and expressions

Every geometric property, `visible`, `opacity`, `alpha` and most numbers are expressions: numbers, `'strings'`, `true`, `false`, names, `+ - * / %` (whole numbers divide toward zero as C# does; a decimal makes the result decimal), `== != < <= > >=`, `&& || !`, `a ? b : c`, and the functions `t(n)` (a length at the text size), `min(...)`, `max(...)`, `abs(x)`, `clamp(x, lo, hi)`, `len(x)` (a string's or a list's length), `if(c, a, b)`, `measure(text, size)` (the text's width at the text size), `measure_unscaled(text, size)` (at 100%), `fit(text, width, size)` (the text cut with an ellipsis to fit). `+` joins strings. A condition is any expression: a number is true when not 0, a string or list when not empty (`choosing`, `playback && has_followers`, `party_size >= 4`, `vehicle_battle`, `tab == 'items'`).

Names an expression may read, checked when the file loads:

* the screen's fields (each screen below), and on every screen `clock_ms` (the animation clock), `text_scale` (the text size option in percent), `dev` (the developer tools are on), `gamepad` (the last input was the gamepad);
* `W`, `H`: the logical frame (in a screen's params too); `pw`, `ph`: the window's parent's size (window rectangles); `w`, `h`: the window's own size (in its `x` and `y`); `cw`, `ch`: the window's content size; `tw`: a text element's measured width (in its own `x`);
* another window's edges: `NAME.x`, `NAME.y`, `NAME.w`, `NAME.h`, `NAME.right`, `NAME.bottom` (absolute logical pixels);
* `size_NAME` for each of the styles' `sizes`;
* inside a `list`'s `row`, a `rows` or `list`'s `text` and `enabled`, and a `repeater`'s `template`, `empty` and `slots`: the item's fields, `i` (its index), `count`, `selected`, `row_y` (a list row's absolute y), `col`, `row` (a repeater's grid cell), `item` and `text` (a string item).

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
| `image` | a picture the screen gives as a field | `image` (an image field; -1 draws nothing), `tint` (white) |
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

Fields: `top` (int, the box's top as the dialogue box would sit), `bed` (image, the area tileset's bed), `cost`, `money` (int), `options` (list of strings), `cursor` (int), `keeper` (image: the portrait the assets' `keepers` give the rest point, -1 without one; [`assets.md`](assets.md), "keepers"; the built-in file does not draw it).

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
  (`ui.prompt.*` keys, English built-ins while the text tables lack them), empty without a prompt.
* `prompt_x`, `prompt_y` (int): the screen pixel at the center of the target cell, as the last frame drew the map.

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
* `cursor` (int): the option under the cursor.
* `picture` (image): the still picture the `picture` tags show (below, "The `tags` table"), -1 for none. `picture_w`,
  `picture_h` (int): its size in pixels, 0 for none.

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
the inn, -1 without one or without a shop; not drawn by the built-in file).

```json
{"type": "group", "visible": "keeper >= 0", "x": 480, "elements": [{"type": "image", "image": "keeper", "w": 64, "h": 64}]}
```

Item fields of `rows`: `text` (the name; the list's row text), `price` (int: the price, or the sell price on the sell
tab), `have` (int: how many the party has). `count` is not used for it: inside a list row it names the row count.

Slots: `backdrop`, `toasts`. The list has `probe: false`: the code sets `selected` (and leaves `top`) as before.

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
- `party` (list of items): the lineup in marching order; item fields `name` (string), `level`, `hp`, `max_hp`, `tp`, `max_tp` (int), `portrait` (image), `alive` (bool, HP above 0). The built-in file draws neither of the last two fields.

```json
{"type": "repeater", "items": "party", "y": 240, "dy": "t(28)", "template": [
	{"type": "text", "value": "name", "style": "small"},
	{"type": "text", "key": "ui.common.level_short", "args": ["level"], "w": "cw", "align": "right", "style": "small_dim"},
	{"type": "bar", "y": "t(13)", "w": "cw", "h": 3, "value": "hp", "max": "max_hp"}
]}
```

Slots: `backdrop`, `page` (draws the selected tab's page layout into its rectangle), `toasts`.

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
- `target_name` (string), `members` (list of strings), `member_list` (scroll list): the target choice.

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
- `slots` (list of items): item fields `text` (the probe: slot and area, `empty` or `damaged`), `head` (the first line), `enabled` (false when damaged), `detail` (the second line, empty for none), `detail_color` (color name: `dim`, or `bad` for a damaged save's error).
- `list` (scroll list), `active` (bool): whether the list has the cursor.

### menu_order, menu_options (`ui/menu_order.json`, `ui/menu_options.json`)

Window: `page` (the area).

- menu_order: `rows` (list of strings), `list` (scroll list): the lineup.
- menu_options: `options` (list of strings): the one "open options" row, always selected.

### menu_macros (`ui/menu_macros.json`)

Windows: `macros` (the area's left 2/5: the count and the macro list), `detail` (the rest: help, the macro's title, the name entry, its actions and steps, the step being built).

Fields:
- area fields.
- `macro_count` (int), `macro_rows` (list of strings), `list` (scroll list).
- `stage` (string): the stage in lower case (`list`, `actions`, `steps`, `stepactions`, `member`, `command`, `pick`, `target`, `targetmember`, `targetenemy`, `name`).
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

### quest_log, text_log (`ui/quest_log.json`, `ui/text_log.json`)

The overlays from the field.

Windows: `backdrop`, `box` (the panel and its title).

Slots: `backdrop`; `quests` (draws `menu_quests` into its rectangle) or `log` (draws `menu_log`).

### load_game (`ui/load_game.json`)

Windows: `scene` (the title's scene, darkened), `box` (the panel, the slot list, the toasts and the hints).

Fields: `has_saves` (bool), `hint` (string).

Slots: `scene`, `slot_list` (draws `save_slots` into its rectangle), `toasts`.

### options (`ui/options.json`)

Windows: `scene` (the title's scene, darkened, from the title), `backdrop` (the dimmed field, elsewhere), `box` (the panel with the settings or the Controls page, the toasts and the hints).

Fields:
- `title_mode` (bool): opened from the title.
- `title` (string): the panel's title.
- `on_controls` (bool): the Controls page is up.
- `rows` (list of items): item fields `text` (the probe, `LABEL: VALUE`), `label`, `value` (string, empty for none), `changeable` (bool: shown as `<  VALUE  >` when selected), `enabled` (bool). With the real-time field input on, `Always run` and `Interaction prompt` come before `Controls`.
- `list` (scroll list).
- `description` (string): the selected setting's help, two lines at most.
- `hint` (string).

Slots: `scene`, `backdrop`, `controls` (draws `controls` into its rectangle), `toasts`.

### controls (`ui/controls.json`)

Windows: `page` (the area: column heads and one row per action, then reset), `action` (rebind or clear the picked action), `capture` (the "press a key" box, centered on the screen).

Fields:
- area fields.
- `actions` (list of items): item fields `label`, `keys`, `buttons` (strings), `reset` (bool: the reset row, which has no bindings). With the real-time field input on, `Interact`, `Run` and `RunToggle` come after `FastForward`, before the reset row; with it off they are left out, so the page is as it was.
- `list` (scroll list), `active` (bool).
- `picking` (bool), `action_name` (string), `choices` (list of strings), `choice` (int): the `action` box.
- `capturing` (bool), `ask` (string): the `capture` box.

The probes (`selected`, `top`) of this page stay in code; its list has `probe: false`.

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
* `enemy_groups` items: the enemies standing as the playback shows them, one item per name in the order the names first stand, with item fields `name`, `number` (how many share it) and `text` (`Gel Mite x2`, or the name alone). The built-in files do not draw it; with the stage's labels hidden (`enemy_labels`, below) a window can name the enemies instead.

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

Fields a layout might want that the C# `GameSession` does not expose, so no screen offers them: the actor who keeps a
shop or rest point (`FieldPoint` and `ShopInfo` name none; the assets' `keepers` map points to portraits instead), and
an enemy's level (`Combatant` has none; enemies have no portraits in the data either). Also: deleting a save (the title's `erase` entry needs a call such as `psiv_save_delete(session, slot)` /
`GameSession.DeleteSave(slot)`), loading a save away from the title (a load entry on the Save page's system menu), and
member profile fields beyond `Member`, `MemberDetail` and the creature kind (age, home, a biography: a member
key/value list such as `psiv_member_profile(session, member)` returning text keys and values); the flashing level and
the notice seen as profile fields (`psiv_profile`'s `flashing` and `flash_notice_seen`, kept in `controls.json` until
then); the data's default flashing level in `game.json` (`presentation.flash_default`); and the game's switches for the
arrival notice and banner per area, on script warps and for the starting area (a field such as `psiv_field_info`'s
`arrival_notice`, `area_banner`, or a notice flag the sample can read with the arrival).

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

A battle file that lays the menu's rows out across, a `repeater` over `menu_items` in a row (no `dy`) or grid, or `rows` over it with `per_column` 1, without `"command_axis": "row"` is a problem (`windows.icons: lays the menu's rows out across: set the param 'command_axis' to "row" so Left and Right move the cursor`): the cursor would not follow the keys the layout shows.

```json
"params": {"command_axis": "row", "command_top": 1, "result_page_lines": 2, "enemy_statuses": 1}
```

The words of the top-level step (`ui.battle.top.fight`, `.macro`, `.run`, `ui.battle.title.top`, `ui.battle.no_macros`) come from the text tables when they have them, else built-in English, not counted as missing. Probe: `enemyicons` reads the icons the last frame drew under each enemy with a status (`Gel Mite B sleep`, `none`).

### Title start state and entries

Two top-level fields of `title.json` (only that file takes them):

* `start` (true or false, default false): the title opens on a start state: the field `start` is true and the menu takes no input until any key or button (not a function key or the console key) is pressed, then `start` is false. It comes back each time the title does (not after its overlays). `start_hint` is the prompt's words (`ui.title.press_start`, built-in "Press any key").
* `options` (a list): the menu's entries in order, from `retry`, `new_game`, `continue`, `load`, `sound_test`, `erase`, `options`, `quit` (default: that order). An entry still shows only when it can be chosen: `retry` after a lost battle that allows one, `sound_test` after a finished story, `erase` never, since the session has no call that deletes a save (below, "Fields the session cannot give yet"). An unknown or repeated entry is a problem. The field `option_ids` lists the ids shown.

```json
{"version": 1, "start": true, "options": ["continue", "new_game", "load", "erase", "options", "quit"], "windows": [
	{"name": "press", "visible": "start", "anchor": "center", "w": 420, "h": 120, "frame": "panel", "elements": [
		{"type": "text", "visible": "clock_ms / 500 % 2 == 0", "value": "start_hint", "y": 76, "w": "cw", "align": "center"}]},
	{"name": "box", "visible": "!start", "anchor": "center", "w": 600, "h": 400, "frame": "panel", "elements": []}
]}
```

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

The dialogue param `picture_stack` (default 0): 1 keeps the still pictures as a stack: `picture` puts one on top (moving it up if it is already there), `picture_hide` takes that one off wherever it is (the one under it shows again), `picture_clear` takes all. Without it a picture replaces the one up, as before. The field `pictures` lists the stack bottom first, each item with `picture`, `picture_w`, `picture_h`; `picture` stays the top one. Probe: `pictures` (`chart tower`, `-`).

```json
"params": {"picture_stack": 1}
```

### Draw order: the scripts' fades

A script's `fade out` and `fade in` paint black over the screen. The order, back to front: the field (map, people, overlays) and its comic panels; the fade; then, on the dialogue, name entry and text log screens, those screens' layouts (box, speaker plate, portrait, choices, the area banner, the entry grid, the log); then the confirm prompt and the developer tools. On every other screen (field HUD and notices, menus, shop, battle) the fade covers the whole screen as before. A fade of 0 seconds applies at once (before, at least 150 ms). Probe: `darkness` (`1.00 under dialogue`, `none`).

### Field: arrival notice and banner

Params of `field.json`: `arrive_toast` (default 1; 0: the arrival notice, `game.arrive`, is logged but shows no notice) and `area_banner` (default 1; 0: the area banner shows on neither the field nor the dialogue). Both are expressions of the common names.

```json
{"version": 1, "params": {"arrive_toast": 0, "area_banner": 0}, "windows": []}
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
* Top-level `profile`: the profile rows, in order, from `name`, `level`, `experience`, `experience_next`, `kind`, `hp`, `max_hp`, `tp`, `max_tp`, `strength`, `mental`, `agility`, `dexterity`, `defense`, `critical`, `evasion`, `magic_defense`, `attack_element` (default: all); the field `profile` lists them with `key`, `label` and `value`. These are every member detail the session gives (`Member`, `MemberDetail`, the creature kind); keys the text tables lack (`ui.status.profile.*`) are built-in English.

```json
{"version": 1, "params": {"stat_bonus": 0}, "profile": ["name", "level", "kind", "experience_next", "attack_element"], "windows": [
	{"name": "profile", "visible": "view == 'stats'", "x": "area_x + area_w - 280", "y": "area_y", "w": 280, "h": 280, "frame": "panel", "elements": [
		{"type": "image", "image": "portrait", "w": "portrait_w * 2", "h": "portrait_h * 2"},
		{"type": "repeater", "items": "profile", "y": "portrait_h * 2 + 8", "dy": "t(22)", "template": [
			{"type": "text", "value": "label"}, {"type": "text", "value": "value", "w": "cw", "align": "right"}]}]}
]}
```

### Flashing

The options' Flashing row replaces the Reduced flash row: `Reduced` (the profile's reduced flash on: the animation scripts' `reduced` replacements and damping, and the flash limit), `Standard` (the flash limit, no replacements: the old setting off) and `As authored` (no limit, no replacements). An old profile maps reduced flash on to Reduced and off to Standard. The level beside the profile's switch, and whether the photosensitivity warning was answered, live in `controls.json` (`"flashing": {"level": "original", "original_confirmed": true}`), written only once a level is chosen; the session's profile has no field for them (below, "Fields the session cannot give yet").

* Choosing As authored asks the photosensitivity question (`ui.options.flashing_warning`) until it has been answered yes once, or the notice below has been seen.
* The options layout's param `flashing_original` (default 1) at 0, or the data's `flashing.json` `"original": false`, leaves As authored out of the row.
* A new profile in normal play (no script; or a scripted run with `--play-defaults`) starts at the data's default: `flashing.json` in the game data directory, `{"default": "reduced" | "standard" | "original", "original": true}`, which only the sample reads (the game's loader refuses unknown keys in `game.json`; `presentation.flash_default` there is the key to move it to once that loader takes it). Without the file the default is Reduced, and the title says once where the option is (`ui.title.flashing_notice`). With `original` (and the choice not hidden) the photosensitivity notice comes first, on black before the title or anything that moves: the game has flashing effects, the setting is under Options > Flashing, and three answers, Reduced (the cursor's start: one Confirm), Standard, Continue as authored (also Back). It is drawn through the `confirm` layout, so a layout restyles it but cannot leave it out, and the data cannot skip it; seeing it counts as the warning's answer. `--check-assets` reports a bad `flashing.json` as `data: flashing.json: ...`.
* Scripted runs (headless or in the window) start at the profile's default, Standard, so every transcript and digest stays as before; that is the difference from normal play. The probe `flashing` reads `reduced`, `standard` or `original` (`original notice` while the notice is up).
* The confirm layout gains the field `cursor` (the answer under the cursor; Yes 0 and No 1 for a question) and `options` holds the notice's three answers; the built-in file sizes its box by `len(options)` and highlights `cursor`, which draws a yes/no question as before.

Words the text tables do not carry yet, built-in English until they do: `ui.options.flashing`, `ui.options.flashing.reduced`, `ui.options.flashing.standard`, `ui.options.flashing.original`, `ui.options.flashing_help`, `ui.options.flashing_warning`, `ui.options.flashing_notice`, `ui.options.flashing_keep`, `ui.title.flashing_notice` (and, from the items above, `ui.battle.top.fight`, `ui.battle.top.macro`, `ui.battle.top.run`, `ui.battle.title.top`, `ui.battle.no_macros`, `ui.title.press_start`, `ui.saves.system.save`, `ui.saves.system.quit`, `ui.status.profile.name`, `.level`, `.experience`, `.experience_next`, `.kind`, `.hp`, `.tp`).

`scripts/flash_safety.txt` asserts the three levels on a strobing script; `make_flash_data.py` writes a data copy whose default is original, on which `flashing_notice.txt` (Standard chosen) and `flashing_notice_keep.txt` (Back keeps it, Original later asks nothing) run with `--play-defaults`, and `flashing_new.txt` the shipped data's Reduced default with its notice and the warning.

### The developer tools at small resolutions

Two `styles.json` params: `dev_scale` (10 to 100, default 100) draws every developer tool (badge, console, picker, overlays, the frame ruler, the windows overlay's labels) at that percent of its 1280x720 sizes, laid out in a frame of the logical size over that percent; `dev_draw` (0 or 1, default 1) at 0 draws none of them, while their keys and commands still work. A 320x224 set with `"dev_scale": 60` draws the tools' text at 10 pixels, its own text size ([`dev-tools.md`](dev-tools.md)).

```json
"params": {"field_cell": 16, "dev_scale": 60}
```

### The item runs

`make_items_assets.py EXPORT TEST OUT` copies the test assets with `alt/assets_more.json` as the manifest, a cue `tag_gong` and a track `tag_theme` that only the tags play, and the item layouts; `make_items_data.py` writes a data copy with the event `dialogue_items` and no party macros. `items_battle.txt` (front data copy): the title's start state and entries, the top-level step, the command row walked with Left and Right, the item and technique lists' sizes, the actor's slot, the chosen-command window, the fallen and status flags, the enemies' icons by the table and (side) off it, Run for the party, the result in two pages. `items_menus.txt`: the profile rows, the Equip page on the first member, the system menu and numbered slots, the unnumbered lineup. `items_dialogue.txt` (item data copy): a line while faded out, the cases, the picture stack, the top-level Macro without macros, a warp without notice or banner. `small_dev.txt`: the small set with `dev_scale` and `dev_draw`. Screenshots under `build/shots/items/` with `--window`.

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
