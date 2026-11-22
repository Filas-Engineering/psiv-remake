# Changes

One line per feature, newest first: name, kind, meaning. Each pass adds its lines at the top.

* `game_cli --capabilities`, `psiv_capabilities`, `GameSession.Capabilities` (tool, C API, C#): what the build supports as JSON, read from the loaders' and the compiler's own tables (game/README.md, "Capabilities").
* `rules::DataKeySet`, `game::gameDataKeys`, `events::script_commands` (library): the accepted data keys and script commands as tables both the loaders and the report read.
* `picture show|hide|clear` (script command): a saved stack of story pictures; `presentation.pictures` (game.json key) lists the names when declared (R117).
* `camera to <marker>|<x> <y> [seconds]`, `camera reset [seconds]` (script command): moves the scene camera to a cell and back to the leader, saved mid-move (R118).
* `psiv_pictures`, `psiv_scene_camera_get`, `PSIV_ACTION_CAMERA_TO`, `PSIV_ACTION_CAMERA_RESET` (C API, ABI 15): the picture stack and the scene camera for hosts.
* exit `sound` (area key): a cue played when the party leaves by that exit, `psiv_field_info.exit_sound` (R119).
* `presentation.music.fade_ms` (game.json key) and `music play <track> fade <ms>|cut` (script option): music fades as data, `psiv_action.fade_ms` (R120).
* `psiv_game_info.party_talk_ambient` (C API field): a host can hide the ambient talk option (R121).
* visible enemies avoid wanderers (rule): no visible enemy steps onto a blocking wanderer's cell; no golden changed, so no option (R121).
* exact steps for visible enemies (rule, `step_timing: "exact"`): their cell cost keeps the step time's fraction; they still step on ticks (R122).
* `FieldMotion::nextTickUs`, `stepLandsInUs`, `runStepLandsInUs` (session, C fields): when the next tick and a step's landing come, for a glide with no wait (R122).
* save schema 12 (save format): world chunk `picture` and `scene_camera` lines; schemas 1 to 11 load.
* `events-save 5` (interpreter save text): the last `camera to` or `camera reset`, written only when used.
* battles aboard from a host's menus (checked): macros with vehicle steps, named auto macros and auto-battle work through the existing C calls (R123).
* quantity buying and selling (checked): `buy`/`sell` with a count already make one price, money and carry check; keepers' `.confirm` documented (R124).

## Sample

Newest first. The MonoGame sample (`bindings/monogame/Psiv.Sample`) only.

* `--capabilities` (sample option) and `check_capabilities.py` (make check): every asset, layout, dialogue, animation, presentation, action and script name this build accepts, as JSON with `schema_version`; the script player refuses verbs and `expect state` keys it does not list.
* `shop_steps.txt`, `make_shop_assets.py` (headless run): the shop in steps and on the tabs.
* `result_text`, `result_ok` (shop fields): a trade's outcome; a layout that reads `result_text` takes the shop's notices there instead of the toasts.
* `steps` (shop param) with `step`, `choices`, `choice_cursor`, `quantity`, `quantity_max`, `unit_price`, `total` (shop fields): Buy or Sell, the list, a quantity with its total and the keeper's confirm line, then the result.
* `stacked_screens.txt`, `small_screens.txt`, `narration_route.txt`, `make_screens_assets.py`, `make_narration_data.py --routing`, `make_narration_assets.py --route` (headless runs): the items below played.
* Extras menu pages (behavior): a field menu page the menu's `entries` leave out opens the field menu with that page as its only tab, until it closes; Extras lists every page of its entries off the title.
* `options` objects with `when`, `has_save`, `save_count`, `has_clear` (title layout entry field and fields): a title entry shows only while its condition holds.
* `list_extras` (controls param): default 1, so the Controls page lists `Extras`; 0 leaves it off.
* `music_fade_ms` (timing, default 800): the music's cross-fade on area changes and script music; 0 cuts.
* `dev_badge_x`, `dev_badge_y`, `dev_scale_bitmap` (styles params): the developer badge's place off the battle, and `dev_scale` shrinking bitmap-font text.
* Built-in pause (layout): sized by the styles' `size_body` and `size_small`, unchanged at the built-in sizes.
* `page_arrow` (UI skin picture and dialogue and battle image field): a next-page mark for the dialogue and `result_more`.
* `result_step`, `transition_playing` (battle fields and animation conditions): the result screen and a transition script.
* `narration_when` (dialogue param: `all`, `tagged` or an expression), `{narration}`, `{narration:on}`, `{narration:off}` (built-in tags), `starts_with` (expression function): which lines the narration window takes.
* `shown_ms`, `line_key`, `picture_top`, `picture_names` (dialogue fields); a `picture_hide` without an id takes the top picture only (picture stack rule).
* `pad_box_h`, `pad_gap`, `pad_cell_w`, `pad_cell_h`, `pad_entry_size`, `pad_label_size`, `pad_text_x`, `pad_text_y`, `pad_label_y` (name_entry and menu_macros params): the name pad's sizes.
* Notices drawn once (fix): the topmost screen with a `toasts` slot draws them; the screen under the Extras menu leaves them out.
* `command_rows` (battle layout field, `top` and `member` lists): with `command_top`, which commands the top-level step and the members' rows show.
* `list_extras` (controls param): the Controls page lists the `Extras` action.
* `Extras` (game action, default C): opens the Extras menu from the title, the field and a battle's command menus.
* `extras` (screen key, `ui/extras.json`, with `entries`): the Extras menu: settings rows, options, controls, the photosensitivity notice, the developer console, field menu pages.
* `rows` (menu_system field): the system page's rows: save, quit and settings rows.
* `menu_abilities`, `menu_system` (screen keys): the technique, skill and ability pages and the system page.
* `entries` (menu field): the field menu's pages in order with optional labels, from a set that adds techniques, skills, abilities, system, party talk and extras.
* `page_ids` (menu field): each tab's page id.
* `PSIV_DIGEST_DIR` (environment variable): every headless run writes its frame digests there, for a before-and-after parity check.
* `make_menu_assets.py`, `menus_layout.txt`, `menus_battle.txt` (headless runs): the menu items played.
* `narration` window (layout, `ui/dialogue.json`): narration lines gather in a window without a box, `narration_lines` at once, over the picture stack.
* `narration_lines`, `narration_cps`, `narration_delay_ms`, `narration_width` (dialogue params): lines held, reveal speed at Normal scaled by the text speed, wait before each line, wrap width.
* `narration_visible`, `narration_rows`, `narration_all_shown` (dialogue fields): the window's state and rows for the layout.
* `narration` (bitmap font role and text style): the narration's font, falling back to `dialogue`.
* `narration`, `narrationreveal` (state probes): the narration rows shown and the line's reveal.
* Window content rule (layout behavior): a closing fade or slide carries its content with the frame; grow kinds show content once full size.
* `loglines` (state probe): the text log's lines as shown.
* Text log without control tags (behavior): the log shows lines as the dialogue showed them, wrapped at the small text size to `wrap_width`.
* Log layouts by frame size (built-in layouts): `quest_log`, `text_log`, `menu_log`, `menu_quests` place by `W`/`H`, unchanged at 1280x720.
* `alt/styles_tiny.json` (small example set): 192x144 styles for the logs check.
* `field_trail.txt`, `small_logs.txt`, `narration.txt`, `narration_ja.txt` (headless runs): followers after load and warp, logs at small sizes, narration in two languages.
