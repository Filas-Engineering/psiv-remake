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
	"frames": {"panel": {"type": "generated"}, "none": {"type": "none"}, "parchment": {"type": "nine_slice", "image": "ui/parchment.png", "border": 6, "scale": 2}},
	"cursors": {"pointer": {"type": "generated", "size": 12, "color": "accent"}},
	"timings": {"text_speed": {"slow": 25, "normal": 50, "fast": 100}, "punctuation_pauses": {".": 120}, "toast_ms": 4000}
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

### Colors, sizes, text styles

* `colors`: name to `#rrggbb` or `#rrggbbaa`. The built-in names are the sample's palette: `background`, `panel`, `panel_top`, `panel_edge`, `panel_shadow`, `row`, `text`, `dim`, `accent`, `bad`, `good`, `tp`, `track`, `black`, `white`. A color property takes a name, a hex literal, or an expression giving one (`"hit ? 'bad' : 'accent'"`).
* `sizes`: named text sizes, read in expressions as `size_NAME` (`size_small` 16, `size_body` 20, `size_title` 28, `size_line` 26, the wrapped line step).
* `text_styles`: name to `{size, color, role}`; `size` an expression, `role` one of the bitmap font roles (`menu`, `dialogue`, `battle_numbers`, `hint`): the text switches to that role's font. An element's own `size` and `color` win over its style's.

### Frames and cursors

* `frames`: name to a frame style, which a window's `frame` or a `box` element names. `type`: `generated` (the sample's window: shadow, light border with clipped corners, dark body, lighter top band; the asset directory's UI skin frame when one is on, [`assets.md`](assets.md), ui), `nine_slice` (`image` a PNG in the asset directory, `border` pixels square corners drawn at `scale`, edges and middle stretched; the generated frame while the picture cannot be read), `fill` (`color`), `outline` (`color`, `thickness`), `none`. `panel` and `none` must exist.
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
| `open`, `close` | transitions: `{"kind": "none|fade|grow|slide", "ms": 200, "from": "left|right|top|bottom"}`. Open plays when the window becomes visible, or when its screen comes up; close plays the frame only, going the way it came. In battle, the battle speed shortens them |
| `blocks_clicks` | true: a mouse click inside it does not reach click-to-walk |
| `elements` | the list below |

### Conditions and expressions

Every geometric property, `visible`, `opacity`, `alpha` and most numbers are expressions: numbers, `'strings'`, `true`, `false`, names, `+ - * / %` (whole numbers divide toward zero as C# does; a decimal makes the result decimal), `== != < <= > >=`, `&& || !`, `a ? b : c`, and the functions `t(n)` (a length at the text size), `min(...)`, `max(...)`, `abs(x)`, `clamp(x, lo, hi)`, `len(x)` (a string's or a list's length), `if(c, a, b)`, `measure(text, size)` (the text's width at the text size), `measure_unscaled(text, size)` (at 100%), `fit(text, width, size)` (the text cut with an ellipsis to fit). `+` joins strings. A condition is any expression: a number is true when not 0, a string or list when not empty (`choosing`, `playback && has_followers`, `party_size >= 4`, `vehicle_battle`, `tab == 'items'`).

Names an expression may read, checked when the file loads:

* the screen's fields (each screen below), and on every screen `clock_ms` (the animation clock), `text_scale` (the text size option in percent), `dev` (the developer tools are on), `gamepad` (the last input was the gamepad);
* `W`, `H`: the logical frame; `pw`, `ph`: the window's parent's size (window rectangles); `w`, `h`: the window's own size (in its `x` and `y`); `cw`, `ch`: the window's content size; `tw`: a text element's measured width (in its own `x`);
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

### Params

Some screens' code reads numbers that are not elements (the battle stage's placement rules, the log's wrap width): they are the file's `params`, an object of expressions, listed per screen. A param a file leaves out takes the built-in file's value.

## Probes, the overlay and the example set

* `expect state windows`: every window the last frame drew, as `SCREEN/NAME X Y WxH`, joined by `, ` (`none`); `expect state window.NAME` (or `window.SCREEN/NAME`): one of them, `-` when not drawn.
* With `--dev`, F1 or `overlay windows [on|off]` outlines every window drawn with its screen and name over the frame (`overlay all` and `overlay none` leave it as it is; `overlay.windows` reads how many it outlined).
* `scripts/make_layout_assets.py` writes a second, deliberately different layout set (below, "An example layout set"); `make bindings` checks it and plays it.
* `scripts/make_test_assets.py` breaks the export's layouts in the broken copy (a file that does not parse, one with every kind of problem, a missing file with the entry on, broken styles, a file of no screen, and a hostile but valid pause layout with negative sizes, cycles, division by zero and huge counts); `--check-assets` must print `scripts/check_assets_broken.expected`, and the saves run passes on that directory.

# The screens

Each screen's file, its windows, the fields it exposes, item fields, slots and params.

## Title (`ui/title.json`)

Windows: `scene` (the title's field scene, slot `scene`), `box` (the title, subtitle, the traveler and creature, the game over line, the options), `corner` (the version stamp at the top right and the toasts, slot `toasts`), `hint` (the keys' bar).

Fields: `title` (string, the game's title), `subtitle` (string), `hero_frame`, `creature_frame` (images, animated by the clock), `game_over` (bool, the last game ended in defeat), `options` (list of strings), `cursor` (int), `version` (string, the library version), `hint` (string).

## Sound test (`ui/sound_test.json`)

Windows: `scene` (the title scene, slot `scene`, and the dimming), `box` (the track list, the playing line, the keys).

Fields: `tracks` (list of strings, every audio id's name), `list` (the list's scroll state), `playing` (string, the track playing, empty for none), `hint` (string).

## Inn (`ui/inn.json`)

The map draws first, in code. Window: `box` (the bed, the cost and money line, Rest and Leave).

Fields: `top` (int, the box's top as the dialogue box would sit), `bed` (image, the area tileset's bed), `cost`, `money` (int), `options` (list of strings), `cursor` (int).

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

Item fields of `toasts`: `text` (the notice cut to the width between 16-pixel margins), `alpha` (its opacity as it
expires), `box_alpha` (its box's opacity, `alpha` times 0.95), `width` (its box's width: the text's plus 28).

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
when not equipment; `hint` (string).

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
- `hint` (string): the key hints for the page.

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
- `rows` (list of items): item fields `text` (the probe, `LABEL: VALUE`), `label`, `value` (string, empty for none), `changeable` (bool: shown as `<  VALUE  >` when selected), `enabled` (bool).
- `list` (scroll list).
- `description` (string): the selected setting's help, two lines at most.
- `hint` (string).

Slots: `scene`, `backdrop`, `controls` (draws `controls` into its rectangle), `toasts`.

### controls (`ui/controls.json`)

Windows: `page` (the area: column heads and one row per action, then reset), `action` (rebind or clear the picked action), `capture` (the "press a key" box, centered on the screen).

Fields:
- area fields.
- `actions` (list of items): item fields `label`, `keys`, `buttons` (strings), `reset` (bool: the reset row, which has no bindings).
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

Windows the code places by (`Ui.WindowRect`): front `command` (its width for the members' step aside, and with its title plate's 14 pixels for the `overlaps` probe), `description`, `keys`, `name_plate` (the `overlaps` probe), `strip` (its top for the `overlaps` probe), `log` (both files: the log's wrap width).

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

Item fields (`party`, `members`, `vehicle`): `name`, `alive`, `acting`, `targeted`, `hit` (damaged this step), `hp`, `max_hp`, `hp_low` (at most a quarter), `tp`, `max_tp`, `state` (fallen, defending, statuses), `next` (the command already chosen, as `ui.battle.next`, or `''`), `icon_count` (status icons shown, for their width), `portrait` (image), and the item slots `icons` (the status icons at x, y) and `anchor` (records where effects land on that member). `menu_items`: `text`, `usable`. `log_lines`: `text`, `newest`. `result_lines`: `kind`, `text`.

Slots: `stage`, `effects`, `target_pointer` (a party member being targeted gets its cursor over the command window), item slots `icons`, `anchor`.

### Params

The stage's placement; a param a file leaves out takes the built-in file's value.

* `stage_bottom`: the stage's bottom (the overlay's and the flash's height; the side backdrop's height less 8).
* `party_x`, `party_dx`, `party_foot`, `party_dy`, `party_size`: member `i` stands centered at `party_x + i * party_dx`, feet at `party_foot + i * party_dy`, `party_size` square.
* `vehicle_x`, `vehicle_foot`, `vehicle_size`: the vehicle's place and size.
* `step_gap`, `step_margin` (front): while the command window is open the members on its side step along past it, `step_gap` apart, at least `step_margin` from the screen's edges; the window's width comes from the `command` window.
* `enemy_x`: the enemies' middle. Up to `enemy_row_max` enemies stand in one row, `min(enemy_row_spacing_max, enemy_row_span / n)` apart, feet at `enemy_row_foot` less `enemy_row_stagger` for each odd position. From one more on, two ranks (the first `(n + 1) / 2` positions in front, feet at `enemy_front_foot`; the rest at `enemy_back_foot`): with `enemy_ranks_interleaved` 1 the ranks interleave over `enemy_rank_span`, else each rank spreads `min(enemy_rank_spacing_max, enemy_rank_span / m)` apart and the back one shifts half a place.
* `enemy_size`: an expression of `enemy_count` and `enemy_back`, the enemy's square size.
* `enemy_slot_N_x`, `enemy_slot_N_foot` (N 0 to 7): formation position N's place, overriding the computed one; absent or -1 keeps it.
* `log_rows`: the log's lines; `log_wrap_margin`: the log window's width less this is the wrap width.

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
  own picture stands in, as for any portrait without a picture).
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
value `{"action": ACTION, "id": ID, "slot": SLOT}`:

| action | does | id |
| --- | --- | --- |
| `cue` | plays the audio cue | the tag's value, or `id` |
| `music` | plays the music track as the scene's music (as `music play`) | the tag's value, or `id` |
| `music_stop` | stops the scene's music | - |
| `panel_show` | shows a live panel at once, named and filmed by the id (a camera, actor or character), on `slot` (default `center`) | the tag's value, or `id` |
| `panel_hide` | takes the panel away at once | the tag's value, or `id` |
| `panel_clear` | takes every panel away at once | - |
| `portrait_off` | takes the portrait away for the rest of the line | - |
| `log` | only the transcript line | - |

An id the game does not have (a cue or music no data or sample sound plays, no such camera or person, no such panel up)
is a problem, not a crash. A file's table falls back to the built-in one name by name, as params do. Built in:
`sound` (cue), `music` (music), `music_stop`, `panel` (panel_show, center), `panel_hide`, `panels_clear`
(panel_clear), `portrait_off`.

### Transcript lines

* `tag: NAME VALUE` (or `tag: NAME`): a tag no table maps, once per line.
* `tag problem: NAME:VALUE: REASON`: a tag the sample could not act on (an unknown id, a wait that is not a whole
  number, a `var` without a value), once per line.

### Probes (`expect state KEY VALUE`)

* `tags`: every tag the current (or, between lines, the last) line acted on, as written (`NAME` or `NAME:VALUE`, `page`
  for each page turn), joined by `, `; `none`.
* `pagetext`: the text the box holds (tags out, values in).
* `dialoguepage`: the box among the line's boxes, `N/TOTAL`.
* `lineportrait`: the portrait id shown, `off` after `portrait_off`, `-` for the speaker's own.
* `reveal`: characters shown of the line's, tags left out and earlier boxes counted (`SHOWN/TOTAL`).
* `fontmissing`: as before, over the line's shown text.
* `wrapped`: the lines the box shows (at most `lines`) and their widths.
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
`"ui_layout": {"enabled": true}` to the manifest and writes `OUT/ui/styles.json` and one file per screen. The styles
start from the export's built-in `ui/styles.json`; every screen file is written from the script's own window lists,
except `panels.json`, copied from the export (every rectangle comes from the screen). `make_test_assets.py` takes an
optional fifth argument, LAYOUT, and runs `make_layout_assets.py EXPORT FONTS LAYOUT` after the other directories.
`--check-assets --assets OUT` reports `32 layout files (640x480)` and no problems.

### What differs from the built-in set

* Styles: resolution 640x480, `"scaling": {"mode": "integer", "filter": "point"}`; the built-in colors plus `card` and
  `teal`; text style `label` (small, teal); frames `card` (a fill) and `edge` (a 1-pixel outline); `sizes.line` 14;
  `timings.toast_ms` 2500. With the fonts' scale, small and body text draw at one file pixel a screen pixel (10-pixel
  lines) and titles at two; the code's own text (hints, enemy labels, toasts) gets the same font.
* No title plates: a window's name is its first line in the accent color over a rule.
* With `--dev` the windows near the top middle start 46 pixels down (`dev ? 46 : 8`), under the developer badge; the
  front battle's keys line leaves the badge's end of the line free.
* Title: a narrow window at the left, the hero and a creature in a card beside the options.
* Field: area, money and play time in a dark card at the top left; notices from the top right down; the arrival banner
  over the hint line at the bottom; the hint line left-aligned with an accent mark.
* Dialogue and inn: a box at the top, 40 pixels in from the sides, portrait 48 pixels in a frame, lines `line_step + 4`
  apart, at most four; a choice's box is only as tall as its options.
* Pause at the right edge; confirm centered.
* Field menu: the tabs drawn as a vertical `rows` list in a column at the left (display only; Left and Right still turn
  them), money at its bottom, the page in a panel at the right (452x376 with `--dev`). Lists show fewer, shorter rows
  (`t(17)`/`t(18)`, items 9 rows, abilities 8, save slots 7 at `t(32)`); descriptions in `card` boxes.
* Shop: tabs as text with an underline beside the title, the stock list at the left (10 rows), the stall picture, the
  description and the comparison in a column at the right.
* Load and sound test at the right of the screen; save prompt, quest log, text log, travel and name entry centered at
  the top.
* Battle, front: `strip` is a column of five cards (140x52, 4 apart) at the left edge, with empty frames for free places;
  each card's `anchor` slot is a 96-pixel square on it, so effects on a member without a figure land on its card. The
  vehicle's card stands under the column. The command window is one column of four rows that scrolls with the cursor,
  with a `cursor/count` line, beside the member choosing above the party's figures. Log, description and name plate
  over the stage right of the column; the result window low on the stage, above the party. Params fit the stage:
  members 84 pixels from x 196, 92 apart, feet at 492; enemies around x 392, up to four in a row (104 or 96 pixels),
  six in two interleaved ranks (72 in front at feet 228, 56 behind at feet 100).
* Battle, side: stage bottom 324; the party as one line per member (portrait, name, icons, HP and TP numbers and bars)
  in a panel at the bottom left, the commands at the bottom right; members 56 pixels in a diagonal at the right,
  enemies 72 to 56 pixels around x 210.

### The scripts

`make bindings` runs each with `--dev` on its own copy of the set (the front battle on the six-enemy data copy) and
checks window rectangles with `expect state windows` and `expect state window.NAME` at each moment, with a `shot`.

* `layout_field.txt`: title; the arrival's narration and a speaker's line with the banner; field; the guide's line with
  a portrait, the yes/no choice, the name entry; the shop's buy and sell tabs; the inn; pause; the reed path's banner.
* `layout_menus.txt`: status members, stats and abilities; items with the target list; equip removing and comparing; a
  manual save; order, macros, combos, quests, codex, log; options and the controls page; quest log and text log; the
  quit confirm; the Load screen loading the save; Bell Cove's save prompt.
* `layout_battle_front.txt` (front data): the cache battle's command, target and technique windows (`overlaps none`), a
  member hit, the sweep's name plate, the log, the result window; a party of five against six enemies in two ranks
  (`ranks 3+3`), auto, and the result with two level-ups.
* `layout_battle_side.txt`: a side battle's command, technique description, playback hints and result; the reed
  skimmer's battles in the front layout (its card, hit) and the side layout (its menu); a defeat's result.
