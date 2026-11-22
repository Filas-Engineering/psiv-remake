# Changes

One line per feature, newest first: name, kind, meaning. Each pass adds its lines at the top.

* field ticks with many actors (performance): actors are indexed by id and by home area once per session, so a tick builds each actor's view a few times (the leader's step, wanderers and patrols, visible enemies) instead of once per wanderer, per path actor and per id lookup; results and the encounter stream unchanged.
* `walk` path search (performance): a grid over the area, with the solid actors' cells taken once per step instead of once per cell searched and objects looked up in a per-area cell index; same paths, same ends (no path, 4000 steps).
* `Session::area()` with gated exits or solid-while-active hooks (performance): the view's key reads only what can change, so it no longer walks every hook on each call.
* `SessionStats`, `Session::stats()`, `game_cli --profile` (session, tool): counts and wall time of field ticks, wanderers, patrols, visible enemies, background events, path searches, settles, autosaves and loads; the profile prints them and the time per script command to stderr.
* `make perf`, `make_stress_data`, `tests/stress_data.h`, test `stressWorldStaysWithinWorkBounds` (tool, test): a synthetic world (300 areas, 700 actors, 1900 hooks, 3500 events) and a script timed in both step timings; the test bounds actor views and path cells on a small copy.
* macro `on_trouble` (`continue`, `recover`, `stop`; data macros, saves, `Session::setMacroOptions`, `psiv_macro_set_options`, `macro option`): what repeating the macro does in a round where trouble holds (R151).
* rules.json `auto_macro_on_trouble` (default `stop`) and `named_macro_on_trouble` (default `continue`) (data keys): the trouble mode of the save's auto macro and of a named auto play macro (R151).
* `Session::setAutoPlayMacro(name, trouble)`, `psiv_battle_set_auto_play_macro_trouble`, `autobattle on|random [NAME] [continue|recover|stop]`, `auto [MODE]` (session, C API, tool): the caller's trouble mode, over the macro's and the rules' (R151).
* macro `recover_who`, rules.json `macro_recover_who` (`all` default, `uncovered`, `troubled`): who takes core's choice in a recover round (`Battle::applyMacroRound`) (R151).
* `Battle::startAuto(macro, mode)` (core): auto-battle under `continue` or `recover` stops only on a cancel or the battle's end (R151).
* `rules::stepTroubled`, `Battle::macroTrouble`, `MacroTroubleReport`, `Session::lastMacroTrouble`, `Session::macroTrouble`, `AutoRound::trouble`, `psiv_battle_macro_trouble`, `psiv_auto_round.trouble`, `trouble on|off` (core, session, C API, tool): the one definition of trouble and each round's report (R152).
* macro `unnamed`, rules.json `macro_unnamed` (`attack` default, `none`, `auto`): what a member the macro has no step for does; `Battle::passing`, records' `pass` line (R153).
* `MacroFallback::Attack`, `LoseTurn`, `MacroStepStatus::LostTurn`, `PSIV_MACRO_FALLBACK_ATTACK`, `_LOSE_TURN`, `PSIV_MACRO_LOST_TURN`, `macro NAME MODE`, `fight NAME MODE` (core, C API, tool): a failed step attacks or loses the turn at no cost; defaults unchanged (R154).
* `Battle::planMacro`, `Battle::checkMacro`, `Battle::checkCommand`, `Session::checkMacro`, `psiv_battle_macro_check`, `macro check NAME` (core, session, C API, tool): each step's state this round (R155).
* one macro round per selection (checked): `applyMacro` already plays exactly one round; the host keeps the chosen macro and cursor (R156).
* effect `reset_stat` (data key): takes one stat's battle change away (R157).
* `modify_stat` `stat: "attack"` (data key): moves only the attack value; strength changes still move attack (R158).
* `revive` `power: 0` (data key): stands the target up at the floor of 1; `hp: N` checked exact (R159).
* game.json `machine_step_heal_fallen` `off`|`heal`|`stand` (data key, booleans still read): `heal` banks the step heal on a fallen machine until it stands; party chunk `machine_bank` (R160).
* `PSIV_STAT_ATTACK`, `PSIV_NAME_MACRO_FALLBACK`, `_TROUBLE`, `_RECOVER_WHO`, `_UNNAMED`, `psiv_macro.on_trouble`, `.recover_who`, `.unnamed` (C API).
* save schema 14, macros block 3 (written only with options), `PSIV_ABI_VERSION` 17, library 0.16.0 (versions).
* `data/scripts/trouble.txt` (script, golden `tests/expected/trouble.txt`): continue, recover and stop on the same seeded battle in Wren Loft on the `make_actor_data` copy.

* ways in on exit cells (data check fix): the reach walk leaves from an arrival marker standing on an exit cell, so rooms behind such doors count as reached (R150).
* doors (area rule): an `interact` hook on a gated exit's cell answers while the exit is shut; once open the exit takes the cell (R145).
* `flags` entries `{"id", "reset": "area"}` (game.json key): flags that clear whenever the party enters an area (R146).
* `leader <member>`, `music <track>`, `area <map>`, `tile <terrain>` (script conditions): the leader, the track playing, the area, the terrain under the leader; `Host::game_state` (R147).
* area `name` (area key), `Session::areaNameKey`, `psiv_field_info.area_name_key` (data, session, C field): an area's shown name by where the party came from or by flag (R148).
* opposite belts on one cell, fractional belt steps (checked): only the active belt moves the party; exact timing keeps the fraction (R149).
* actors' `wander` `stand_percent`, `axis` (game.json keys): a turn's chance of standing; steps along x or y only (R126).
* actors' `patrol` (game.json key): a looping route of walks, walks to markers or cells, facings and pauses; waits at a taken cell; saved (R127).
* actors' `path`, `cell_pixels` (game.json keys): pixel offsets from the home per field tick, a footprint that blocks the cells it overlaps; saved (R128).
* area `platforms` (area key): moving platforms (`loop`, `one_way`, `triggered`) that carry the party between docks, no battle rolls aboard, saved mid-ride, joined by the data check (R129).
* `Session::platformsHere`, `ridingPlatform`, `psiv_platforms`, `psiv_field_info.riding_platform`, `game_cli platforms` (session, C API, tool): platforms for hosts (R129).
* `Session::canLoad`, `psiv_can_load`, `GameSession.CanLoad` (session, C API, C#): whether Load can run now (R130).
* `Session::battleKeys`, `psiv_battle_keys_get`, `GameSession.BattleKeys` (session, C API, C#): the message keys, notices and cancel reasons a battle can emit (R131).
* `MemberDetail` `attack`, `baseStats`, `experienceForLevel`, `totalExperience`, `kind`, `vulnerable`; `psiv_battle_option.max_uses` (session, C fields): member detail a layout binds (R132).
* `Event::rolled`, `psiv_battle_event.rolled` (core, C field): the rolled damage or heal beside the amount that changed; not serialized (R133).
* enemies' and vehicles' `stats.attack` (data key): an attack apart from strength; absent, attack is strength (R134).
* any piece's `stats.attack` (data key): counts into the attack stat, not only weapons (R135).
* characters' `skill_uses` (data key): a skill's maximum uses by level, stepwise; `rules::maxSkillUses` (R136).
* revive `living_target` and `hp`, effect `set_hp_percent` (data keys): revives aimed at the living, flat revives, hit points set to a percent (R137).
* combos' `orders` (data key): further orders that fire the same recipe (R138).
* `strip_modifiers` `equipment_affinities` (data key): also takes away what pieces did to element affinities (R139).
* effect `grant_affinity` (data key): an element affinity for the battle or, `permanent`, for good (saved with the member) (R140).
* area `field_status`, rules.json `field_poison_every`, `field_poison_status`, `field_paralysis` (data keys): poison and paralysis by field steps (R141).
* single-ally targets nothing can affect refused (rule fix, rules version 15): `command.no_effect_on_target`; default picks pass them by (R142).
* revives first on fallen targets (rule fix, rules version 15): the other effects then reach a target the revive stood up (R143).
* `machine_step_heal_fallen` (game.json key): the step heal stands fallen machines up (R144).
* save schema 13, `PSIV_ABI_VERSION` 16, library 0.15.0, rules version 15 (versions).
* `data/scripts/motion.txt` (script, golden `tests/expected/motion.txt`): Wren Mill on the `make_actor_data ... motion` copy.

* saves with a vehicle's macro step (fix): a save, autosave or event-start world holding a macro with a vehicle's step loads again, saves already written included; the load check takes a vehicle as a step's member, counts only member steps toward five and refuses an unknown vehicle or a vehicle step with an item, piece, defend or member target; save schema 12 unchanged.
* auto play macro across a battle (checked): `Session::setAutoPlayMacro`, `autobattle on|random MACRO` and `psiv_battle_set_auto_play_macro` try the macro in full every round, a fallen member's step passed by and a stuck step taking core's choice that round only, in party and vehicle battles, as `fight MACRO` does; the save's auto macro without a named one still stops by core's stop rules.
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

* `make_scene_data.py`, `make_scene_assets.py`, `scene.txt`, `shop_voice.txt`, `vehicle_commands.txt`, `game_over.txt`, `arrive_behind.txt`, `above_cover.txt`, `glide.txt`, `narration_auto.txt` (headless runs): this pass's items played.
* `narration_wait` (`press`, `auto`) and `narration_hold_ms` (dialogue params), `{narration:nowait}` (built-in tag value): narration lines that go on by themselves until the window is full; the window waits for a press before a speaker's line or a choice.
* `party_figures`, `enemy_figures` (battle params, expressions, default 1): a side's figures left out of the `stage` slot, the backdrop staying.
* Keeper line parameters (shop `voice_line`): `{0}` kind word, `{1}` item, `{2}` quantity, `{3}` total, `{4}` price of one; `VOICE.confirm_buy`, `.confirm_sell`, `.thanks_buy`, `.thanks_sell` (and `ui.shop.*` alike) when the tables have them.
* `followers_arrive` (field param: `stacked`, `behind`): followers placed behind the leader after an exit, warp, travel or load.
* `game_over_return_ms` (timing, default 0) and `game_over_confirm` (battle param, default 1): a lost battle's result screen returns to the title by itself; Confirm can be turned off.
* `abovecover`, `pixel.X.Y`, `cellpixel.X.Y` (state probes): the area's above picture checked pixel by pixel over everything on the map (it covers all of it; bubbles and marks go over it by design).
* `command_rows.vehicle` (battle layout field: `attack`, `skills`, `macro`, `auto`, `escape`): Macro (`ApplyMacro`) and Auto (`StartAuto`) in a vehicle battle's rows.
* Real-time glide by the library's landing (behavior, `glide` probe): a step the session lands later than its step time (`StepLandsInUs`, `RunStepLandsInUs`) glides on to it instead of stopping at the cell's end.
* Exit sounds, music fades and the ambient talk option (behavior): `FieldInfo.ExitSound` played on arrival, `PresentationAction.FadeMs` and `GameInfo.MusicFadeMs` over the layout's `music_fade_ms`, no ambient talk row when `GameInfo.PartyTalkAmbient` is false.
* Scene camera (behavior, `scenecamera` probe): the field centered on `SceneCamera()`, easing to its cell and back to the leader, jumping on instant actions, held on the field after the event.
* Story pictures from scripts (behavior): `Pictures()` and the dialogue tags' pictures as one stack, the script's at the bottom, drawn over the field after the event until hidden and kept by loads.
* Data read as the library accepts it (fix): fractional step times, belts sharing cells (the running one drawn and carrying, so a belt reverses with its flag), names or objects with an `id`; an unreadable entry logs `data problem:` and is skipped instead of stopping the sample; `make_belt_data.py`, `belts.txt` (headless run).
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
