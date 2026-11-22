# Changes

One line per feature, newest first: name, kind, meaning. Each pass adds its lines at the top.

* `followers` on a town's `return` and on `ports[]`, area `arrival_followers`, script `warp <map> <marker> followers <placement>` (area keys, also in `tools/maps`, which now carries `return`; script syntax, `events::Action::followers`): where the followers stand after a return to the town, an arrival at the port, a landing on `arrival` (a return without `return`, boarding a vessel) or the warp, the six words and placement of an exit's `followers`; default `followers_arrive_other` as before; refusal `data.arrival_followers_unused` (R195).
* game.json actors' `require_flag` and `block_flag` (data keys): the actor is hidden, neither drawn nor solid (`ActorView::visible` false), unless the first is set and the second is not, whatever a script showed; before, only a talk point's `solid: "while_active"` or a script's `hide`/`remove` hid an actor (R194).
* `presentation.music.vehicles` entry `{"battle": TRACK, "field": TRACK}` (data key; a plain string is still the battle track): the `field` track plays on the field while the party rides that vehicle, over the area's, through `Session::areaMusic()` (the field music hosts read); a script's `music area` still wins (R193).
* area `return` (`{"area", "marker"}` or `{"area", "at", "facing"}`; data key, towns only): a return to the town lands there, in another area, the list still naming the town (C `area_name` and game_cli's `destinations` give the town's name); the entry counts as coming from the town; refusals `data.return_*`, data check `check.return_not_reached` (R192).
* script choice `header` (`choice [<text>] header <text> [into <variable>]`; script syntax, also `ChoiceView::header`/`headerId`, C `psiv_choice.header`/`header_id`, C# `Choice.Header`/`HeaderId`, ABI 20, the sample's dialogue field `choice_header`): text shown with the options, inside the list window in a layout that draws it; `game_cli` prints it after the prompt (R191).
* script commands `register town <map>` and `unregister town <map>` (script syntax): the town joins or leaves the towns the party may return to by story; saved with the visited areas; a map that is no `town` with an `arrival` and a `region` is `data.town_not_registrable` (R190).
* game.json `start_towns` (list of area ids, default none; data key): towns the party may return to from the new game on, as if visited; each a `town` with an `arrival` marker and a `region` (`data.town_not_registrable`) (R190).
* game.json `followers_on_exits` (default false; data key): the arrival line of an exit's `followers` (or `followers_arrive`, `followers_arrive_other`) goes on over exit cells, pits and `always` exits included, and places followers there; they fire nothing and block nothing, and the trail while walking already crossed exit cells (R189).
* exit `followers` (`stacked`, `behind`, `up`, `down`, `left`, `right`; area key, also in `tools/maps`) and game.json `followers_arrive` taking the same six (data key): where the followers stand when the party arrives through that exit, read from the exit taken; default game.json's `followers_arrive` (R185).
* game.json `followers_arrive_other` (same six, default `stacked`; data key): where the followers stand after any area entry that is no walk through an exit or off a gangway (a script's or developer's warp, travel, boarding a ship, the new game); before, `followers_arrive` placed those too, so data that set it to `behind` sets this as well to keep warps as they were; a load restores the saved trail and a battle's end keeps it (R185).
* game.json and interact hook `talk_at` (`point` default, `actor`; data keys, also in `tools/maps`): an actor's talk point answers interact on the cell the actor stands on, its own cell turning to floor, and an actor with a game.json home (a wanderer, a patrol, a path) may then have its talk point in its home area (before, `data.actor_home_twice`); refusals `data.bad_talk_at`, `data.talk_at_interact_hooks_only` (R186).
* hook `turns` (data key, default false, also in `tools/maps`): the actor of an interact or touch hook turns toward the leader as the hook's event starts; refusal `data.turns_actor_hooks_only` (R186).
* touch hook `touch_from` (`any` default, `keys`; data key, also in `tools/maps`): `keys` fires only on the player's own step into the actor and on interact, not on a path walk's step (game_cli `walk`, the C and C# walks, click-to-walk) or the actor's own walk into the leader; refusals `data.bad_touch_from`, `data.touch_from_touch_hooks_only` (R187).
* return to a town from a town's interior (checked, documented): the list is the visited towns with an `arrival` of the party's area's `region`, the area itself left out; an interior with the towns' `region` offers them, nothing looks for towns registered from the interior (test `aTownsInteriorOffersTheReturnToTownsOfItsRegion`) (R188).

* `useAbilityOutsideBattle` and an ability's `attack_stat` (fix): a field ability heals with its `attack_stat` as in battle, through the one attacker the battle builds for an ability; before, it healed with the user's mental. Rules version 15 unchanged (field uses make no battle record).

* `useItemOutsideBattle` with a `consumed: false` item (fix): the field use keeps what its effects did to the party, so a revive and a heal stand a fallen member at the heal as in battle; before, it reported the effects and changed nothing. Rules version 15 unchanged (field uses make no battle record).

* `Session::discard`, `psiv_discard`, `GameSession.Discard`, game_cli `discard ID [N]`, items.json `discardable` (session, C API, C#, tool, data key, default true): throw items or unworn pieces away from the field menu, never an item marked `discardable: false`; notice `game.discarded`, refusals `game.refuse.discard_menu_only`, `discard_one`, `discard_not_held`, `not_discardable` in `en` and `ja`; `psiv_inventory_entry.discardable` (R177).
* `Session::alliesOutOfReach`, `rules::Battle::alliesOutOfReach`, `rules::fieldTargetsOutOfReach`, `psiv_allies_out_of_reach`, `GameSession.AlliesOutOfReach` (session, core, C API, C#): the lineup members a one-ally ability or item would be refused on because nothing in it reaches them, in battle and in the field (R178).
* `SaveSummary::newest`, `psiv_save_summary.newest`, `SaveSummary.Newest` (save, C API, C#): the save Continue loads, beside the write time (R179).
* `Session::partyTalkLines`, `TalkLineView`, `psiv_party_talk_lines`, `psiv_talk_line`, `GameSession.PartyTalkLines` (session, C API, C#): party talk lines with speaker ids from any mode, nothing shown or marked (R180).
* `dev combo RECIPE`, `Session::devCombo`, `rules::Battle::setLeadOrder`, record `lead_order` line, `psiv_dev_combo`, `GameSession.DevCombo` (tool, session, core, C API, C#; dev): a combination's parts as commands leading the round, so it fires; marks the game (R181).
* rules.json `item_no_reach` (`refuse` default, `consume`; data key) and `Database::spentOnNoReach`: an item used up when it changes nothing is spent with its no-effect event on an ally it cannot reach instead of refused, in battle and in the field (R182).
* area `name` as a plain text key (area key, also in `tools/maps`): one variant with neither `from` nor `flag` (R183).
* text keys `ui.battle.stat.attack`, `ui.battle.stat.magic_defense`, `ui.action.extras`, `ui.action.system`, `ui.menu.tab.abilities`, `extras`, `party_talk`, `skills`, `system`, `techniques`, `ui.menu.party_talk`, `ui.menu.open_extras`, `ui.extras.title`, `dev`, `photosensitivity` (data, `en` and `ja`; `glyphs_ja.txt` gains 捨): the words the MonoGame sample kept as its own (R184).
* `PSIV_ABI_VERSION` 19, `Native.AbiVersion` 19, library 0.18.0 (versions).

* exits' rule (behavior, default changed): an exit takes the leader only on a step from a cell that is no exit, so a party that arrives on a multi-cell exit walks across it and leaves by stepping off and back on; pits still drop the party from any cell (R161).
* game.json `exit_fires` (`from_floor` default, `always`) and exit `fires` (data keys): today's rule back for the game, or one exit that fires on any step (map-edge exits) (R161).
* path walking and the data check by the exits' rule (session, check): `walkStep` and game_cli `walk` cross exit cells a step would not leave by and take one that would only as the goal; the check's reach walk does the same and counts a multi-cell exit used from any of its cells (R161).
* area `arrive_notice` as a list of `{from, notice}` entries (area key): the notice, and `Session::arrivalAnnounced`, by the area the party came from (R162).
* area `arrival_from`, `arrival_not_from` (area keys): a town registers as a place to return to only when entered from a listed area, or from one not listed (R162).
* area `field_poison`, `field_paralysis` (area keys) and rules.json `field_paralysis.everywhere` (data key): field poison and paralysis apart, `field_status` both; paralysis wearing off in every area (R163).
* game_cli `walk` refusing a goal an actor stands on (documented): no path, as before (R164).
* shop and rest point `name` (area key), `psiv_shop_info.name_key`/`name`, `psiv_inn_info.name_key`/`name` (data, C fields): a display name for hosts (R165).
* `Session::followers`, `FollowerView`, game.json `followers_arrive` (`stacked` default, `behind`), world chunk `trail`, `psiv_followers`, `GameSession.Followers`, game_cli `followers` (session, data key, save, C API, C#, tool): the followers' cells on the leader's trail, blocking nothing (R166).
* `events::Interpreter::next_output`, `Session::nextEventOutput`, `psiv_event_next_output`, `GameSession.NextEventOutput` (interpreter, session, C API, C#): whether the event shows a line, a choice, its end or something else next, without advancing it (R167).
* field use and battle-only effects (load rule): `modify_stat`, `reset_stat`, `strip_modifiers` and a battle's `grant_affinity` load on field items and abilities beside an effect that acts, and the field use skips them; only such effects on a field use stay a load error (R168).
* revive `hp: 0` (data key): the target stands at 0 for the rest of the action, so a heal after it lands exactly its amount; with nothing raising it, the floor of 1 (R169).
* game_cli `walk to ACTOR`, `talk ACTOR` (tool, C playthrough): walk up to an actor where it stands now, choosing again each step, face it and, for `talk`, interact; `data/scripts/approach.txt` (golden `tests/expected/approach.txt`) on the Wren Yard copy (R170).
* rules.json and item `consume_when_no_effect` (data keys, default false): a consumable item that changed nothing is used up anyway, in battle and in the field, with its no-effect event; a refused target still spends nothing (R171).
* macro `policy` (`power`, `expected`; data macros, saves, `Session::setMacroPolicy`, `psiv_macro_set_policy`, `macro option NAME policy`) and the caller's policy (`Battle::setAutoPolicy`, `Session::setAutoPlayMacro(name, trouble, policy)`, `startAuto(trouble, policy)`, `psiv_battle_set_auto_play_macro_policy`, `autobattle ... [power|expected]`, `auto [MODE] [POLICY]`): the automatic choice's policy, the call over the macro over rules.json `auto_policy`; macros text version 4 (R173).
* game.json `bag_slots`, `bag_stacking` (`stacks` default, `single`) (data keys), `Session::bagSlotsUsed`, `bagRoom`, `psiv_game_info.bag_slots`/`bag_single`, `psiv_world_info.bag_used`: a bag of limited slots; shops refuse, chests stay shut, drops are left, story gives always land (R174).
* game.json `autosave` (data key, default true) and `autosave_slots: 0`, `Session::autosaveOn`, `psiv_game_info.autosave`: no autosaves at all, departing from FLOW-050 (R175).
* docs/data-import.md "Values a converter must set deliberately" (docs, test `everyPlayShapingKeyIsInTheConverterList`): every play-shaping rules.json and game.json key with its default, kept complete by a test over the key tables (R176).
* `dev rest`, `Session::devRest`, `psiv_dev_rest`, `GameSession.DevRest` (tool, session, C API, C#): a free rest for testing, marking the game (R172).
* `psiv_exit.fires`, `PSIV_EXIT_FIRES_*`, `psiv_follower_cell`, `PSIV_NEXT_*`, `psiv_dev_rest`, `psiv_macro_set_policy`, `psiv_battle_set_auto_play_macro_policy`, `psiv_macro_policy`, `psiv_macro.policy`, `PSIV_NAME_MACRO_POLICY` (C API).
* save schema 15, `PSIV_ABI_VERSION` 18, library 0.17.0 (versions).

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

* battle window timing (layout): battle file param `command_open_delay` (the first command window waits after the opening playback) and top-level `result_sequence` (`victory`, `result`, `level_ups` steps with `ms`, fields `result_phase`, `result_phase_ms`, `level_lines`; level-ups then leave the notice steps and `result_lines`).
* vehicle battles (layout): optional screen files `battle_front_vehicle` and `battle_side_vehicle` draw a vehicle battle in place of the stage layout's file when given.
* title intro (layout): `title.json` top-level `intro` (named steps with `ms`, played on boot before the start state; fields `intro`, `intro_step`, `intro_index`, `intro_ms`, `intro_step_ms`) and param `after_defeat` (`menu`, `start`, `intro`).
* inn flow (layout): `inn.json` top-level `flow` from `welcome`, `price`, `choice`, `result`, `farewell` (fields `step`, `rested`; the result and farewell stay over the field after a rest); default the choice alone, as before.
* shop flow (layout): `shop.json` top-level `flow` from `welcome`, `mode`, `list`, `quantity`, `confirm`, `result`, `farewell` (fields `flow_step`, `confirm_options`, `confirm_cursor`; keeper line `farewell`); without it `steps` as before.
* travel flow (layout): travel params `one_destination` (`list`, `go`), `cancel_row`, `leave_key`, `arrive_key`, `message_ms`; fields `destinations` (`name`, `region`, `area`), `phase`, `message`, `message_ms`.
* item rows (layout): row fields `name`, `count`, `count_text` on the Items page (`item_rows`, the target's and Discard's counts), shop rows and battle item rows; params `count_format`, `count_format_one`, and menu_items `stack_rows` (`stacked`, `single`). An item's own `count` wins over the rows' count inside its row.
* field menu list mode (layout): menu param `navigation` (`tabs`, `list`; fields `navigation`, `list_cursor`, `page_open`, `entry_label`, `entry_page`): the entries as a list, the page shown only once confirmed.

* interaction prompt on data with `talk_at` "actor" (behavior): the faced-rule prompt asks the session's `InteractAt` for each cell beside the leader, so it points at the cell a wandering actor stands on now; other data as before.
* click on an actor (behavior) and field layout param `click_actor` (`walk` default, `interact`): the click walks up to the actor where it stands now, choosing again each step, and faces it, and with `interact` talks to it; before, a click on a solid actor's cell found no way.
* `make_library_data.py --moving`, `moving_talk.txt` (scripts): follower placement by the exit taken (`behind`, `up`) and a wanderer's talk point under `talk_at` "actor"; the library copies also set `followers_arrive_other` "behind", which keeps `library_followers.txt` as it was.
* `party_talk` "speakers" and `talk_hide_menu` (menu_options params), window `talk_speaker`, fields `talk_speaker_name`, `talk_speaker_id`, `talk_portrait`, `talk_text`, `talk_shown`, `talk_page`, `talk_pages`, `talk_index`, `talk_count`, `talk_more`, `page_arrow`, probe `talk`, `make_talk_data.py`, `make_talk_assets.py`, `talk_speakers.txt`, `small_talk.txt` (scripts): the party talk page one speaker at a time, portrait and expression by the line's tags, pages and the window's rows revealed at the text speed, with the menu hidden or kept; an `immediate` entry acts once when Confirm comes in the frame it opens.
* `make_host_data.py`, `make_host_assets.py`, `host_menus.txt`, `host_tags.txt`, `host_reach.txt` (scripts), `expect notext TEXT` (script expectation): the items below on generated data copies, and no `{` on any screen showing tagged text.
* control tags outside the dialogue (behavior): notices, the party talk page, keeper lines, shop and Items results, descriptions, the quest log, the codex, credits, choices and the battle's event lines act on tags as the text log does.
* `ui.action.extras` ... `ui.menu.tab.techniques`, `ui.battle.stat.attack`, `ui.battle.stat.magic_defense` (sample words): dropped, the text tables carry them.
* `combo RECIPE` (dev console command): `DevCombo`, the combination leading the round; the script verb `combo` goes through it too.
* `exit_markers` (manifest `field` key) and probe `exitmarks`: an open exit's marker by `FieldExit.Fires` (`always`, `from_floor`).
* `spritesteps` (probe) and palette tracks in battle (behavior): sprites step on the battle's clock instead of keeping the field's last step.
* `when`, `enabled` (menu child keys), `child_rows`, `item_count`, `macro_count` (fields): hidden and disabled children; `immediate` opens a tab's one usable child.
* `talk` items `speaker_id`, `portrait`, `pages` (menu_options fields): the party talk page from `PartyTalkLines()`, the menu kept open, each page a row.
* `newest`, `write_time` (save_slots item fields), `select_newest` (load_game param): the save Continue loads marked and preselected in the Continue chooser, and in the Load list with the param.
* `member_rows`, `target_rows` with `reachable` (menu_items, menu_abilities, menu_status fields): allies out of reach greyed before the pick (`AlliesOutOfReach`), in battle too, in place of learning them from a refusal.
* `discard` (menu_items param), `discarding`, `discard_step`, `discard_count`, `discard_max`, `discard_text` (fields): Discard among an item's actions for `Discardable` entries, with a count picker, a confirm and the outcome on the result line.
* `make_small_more_assets.py`, `small_more.txt`, `make_motion_more_assets.py`, `motion_more.txt` (scripts): the slots below at 320x224, and the motion and previewer additions.
* the animation previewer's enemy dummies (behavior): they attack with the data's `basic_attack`, which the log names.
* `encounter:ENC` keys (animation previewer): a key that names an encounter plays in it, on its enemies, with its placements.
* `bob`, `pulse` (figure motion kinds) and move `scale`: a hop and a scale, as idle states, states and move tracks.
* `at` (maps.areas[] layer key): `[x, y]` in picture pixels, placing a layer that does not repeat.
* `palettes` on a `tilesets` entry without `image` (manifest): recolors the generated tiles of its kind or area.
* `sprites` entries as `{"image", "palettes"}` (manifest): palette tracks on listed sprites; `fieldmotion` reads their steps.
* `immediate` (menu entry and child key): a party talk or extras page acts as it opens; an entry's single child opens in place of its list.
* `list_w`, `row_h`, `rows`, `pad`, `text_size` (menu_children params): the children list's sizes for small resolutions.
* `talk_size`, `talk_name_size`, `talk_row_h`, `talk_pad`, `talk_rows` (menu_options params) and the text styles `talk`, `talk_name`: the party talk box sized by the layout instead of fixed code sizes.
* `make_library_data.py`, `make_library_assets.py`, `field_status_cli.txt` and the runs `library_followers`, `exit_block`, `library_shop`, `library_autosave`, `field_status`, `library_macros` (scripts): the library state below on generated data copies, the 2x2 exit block in both field inputs and the field-status notices included.
* `rest` (dev console command): `DevRest`, a free rest marking the game.
* `ui.battle.stat.attack`, `ui.battle.stat.magic_defense` (sample words): the attack and magic defense stat-change words, from the tables when they have the keys.
* `auto_trouble`, `auto_policy` (settings rows, controls.json `battle` keys, probes): the trouble mode and policy auto play goes under (`SetAutoPlayMacro(name, mode, policy)`), with the round's trouble in the log.
* `macro_check`, `macro_failed`, `macro_flow`, `auto_choose` (battle params) and `macro_steps`, `macro_trouble`, `macro_trouble_text` (fields), probe `macrocheck`: each step marked by `CheckMacro` before Confirm, the failed step's result (`automatic`, `attack`, `lose_turn`), the menus back on Macro and the last macro (`remember`, or `reset` as before), and the Auto row asking the trouble mode and policy.
* `options` (menu_macros param) and `on_trouble`, `recover_who`, `unnamed`, `policy`, `option_rows` (fields): a macro's options edited through `SetMacroOptions` and `SetMacroPolicy`.
* `name`, `title` (inn fields), `name`, `bag`, `bag_used`, `bag_slots` (shop fields), `bag`, `bag_used`, `bag_slots` (menu_items fields): shop and inn display names (the shop's also the keeper's `{0}`) and the bag's slots; no autosave marks and no retry when game.json's `autosave` is false.
* narration `auto` (behavior): a narration line goes on by itself only while `NextEventOutput` says a line comes next, so the window waits for a press before the event's end, a choice or anything else.
* followers (behavior, sample follows the library): drawn from the session's `Followers()` trail (a load puts them back where the save kept them; past the trail's end they stand where the last one does); the field param `followers_arrive` only when game.json does not set the key.
* `actions`, `acting`, `action_ids`, `action_list`, `looking`, `look_text`, `result_text`, `result_ok` (menu_items param and fields): an item's actions, use and look (its description), and the uses' outcomes on the page instead of the notices.
* `macro_auto_first` (battle param): the macro list opens with the auto-battle macro first, marked `(auto)`.
* `item_order` (battle param): `"items_then_equipment"` (as before) or `"equipment_then_items"`, the worn pieces first in the Item list.
* `continue` (title param): `"newest"` (as before) or `"choose"`, Continue opening the Load list.
* Area changes (fix): a warp, travel or event that changes the area during a real-time step ends that step, and the new area's first frame shows the leader on its own cell (it showed the old step's target cell until the step ran out).
* Scripted leader walks (behavior): a script's `move` of the leader (also `nowait`) leads the line; the members not in the scene trail along its cells at its pace, as in walking (they stood still).
* `battle_anim_side.txt` (headless run), the `equip` and `combo` script verbs: the example animation set in the side layout, and the two-weapon variants and Twin Arc's script played live in both layouts.
* `make_anim_assets.py --leftovers`, `battle_anim_leftovers.txt` (headless run): the last animation additions checked, reloaded and played in both layouts, with broken cases in `make_anim_assets.py --broken`.
* Battle animation `travel.trail`, `directions` and `flip: auto` (default `none`), `shake.pattern: script`, the `mosaic` transition (`block`, `steps`), the `/layout:` qualifier on every key, and the problem for a figure animation nothing plays; probes `animsprites`, `battlejolt`.
* The animation previewer: `count` and `targets` on `preview anim`, an enemy acting for `enemy:` keys, a member using the item for `item:` keys, `overlay anim [on|off]`, and the layout's windows settled after a seek.
* A backdrop `swap` keeps its picture to its script's end and a later `restore` crossfades back in the same action; `placements` take `count:6` to `count:8` and a rank per place; probe `enemyplaces`.
* `idle` and `idle:flying` figure states as data (`pose`, `motion`, `color`, `lift`, `stagger`) on the controller's clock in script units, replacing the built-in idle and hover.
* `palette` on figure and effect entries of `assets.json` (a color list or a PNG strip), and palette sources by index in color transforms, on figures and sprites; `figure.LABEL` ends in `palette N`.
* `solo` (settings and turn scripts: `command`, `action`, `result`): only the member choosing or acting drawn, in both layouts.
* Number windows: `row` per side (`{"party", "enemy"}`), `pad` as `[x, y]`, `frame` (a styles.json frame, `battle`) and `close`.
* Area layers (`maps.areas[].layers`): pictures behind the area or in front of everything on it, each with its scroll factor against the camera, an automatic drift, the axes it repeats on, row bands at their own rates and a sine over its lines or columns, in a documented order around the ground and above pictures; checked, reloaded, in the broken copy and `world_motion.txt`.
* Frame runs on an area's picture (`maps.areas[].animated`): a sheet's frames on a rectangle in cells or picture pixels, on the ground or over the above picture, timed in ms or 60 Hz ticks on the global or the area-entry clock, with flag conditions; asset keys `animated:AREA:N`, the `fieldmotion` probe.
* Palette tracks (`palettes` on `maps.areas[]` entries and on tilesets): named source colors replaced exactly in looping steps (one step a still recolor) on the area's pictures and frame runs or the tileset's tiles, by the battle animations' recolor, timed in ms or ticks on the global or entry clock, with `flag`/`unless_flag`; a maps entry with only motion keeps the area's tiles.
* `make_shapes_data.py`, `shapes.txt` (headless run): every newer data shape the library accepts (object `flags` with `reset`, `presentation.pictures`, `music.fade_ms` and area music lists, `field_status`, area `name` lists, `counters`, exit `sound`/`block_flag`/`closed_tile`, wanderers' `stand_percent`/`axis`, `patrol`, `path`, `platforms`) read with no `data problem`; the sample's data reading never throws on an entry it cannot read, and `make_belt_data.py` sorts object flags by id.
* `make_menus_more_assets.py`, `menus_more.txt` (headless run): the menu follow-ups played, the children's problems checked on a copy.
* `tab_h`, `tab_pad`, `tab_gap`, `tab_text_y` (menu and menu_status params) and `unusable` (menu_abilities param): the tabs' sizes, and abilities not usable in the field dimmed or hidden.
* `party_talk` (menu_options param), `talking`, `talk` (fields) and `talk_box` (slot): the party talk's lines on its page with the menu kept open.
* `System` (game action) and `system_action` (menu_system param): S opens the system page alone over the field.
* `children` (menu entry field) and `menu_children` (screen key): a tab that lists pages under it and opens the one chosen.
* `make_adopt_assets.py`, `make_adopt_data.py`, `motion.txt`, `adopt.txt`, `scene_overlay.txt`, `narration_nowait.txt` (headless runs), `make_narration_data.py --nowait`: the library adoption items played.
* `platforms`, `actorpos.ID`, `battleunusable` (state probes): the platforms and people as drawn in the data's pixels, and the greyed rows of the battle menu.
* Camera overlay (behavior): names and marks a script's scene camera (`scene at X Y`, `scene, back to the leader`).
* `result_confirm` (battle field): false after a game over under `game_over_confirm` 0, which also empties the continue hint.
* Title `load` and field `can_load` (behavior and title field): Load shows by `CanLoad`.
* Notices `game.poison_fell`, `game.paralysis_worn_off`, `game.machine_stood_up` (behavior): toasts with the cues `hit`, `status`, `heal`.
* `step:notice.KEY` (animation key check): the notice must be one of `BattleKeys().Notices`.
* `max_uses` (battle item field) and a skill's battle row as `x2/3` (behavior): `BattleOption.MaxUses`.
* `attack`, `base_hp`, `base_tp`, `base_strength`, `base_mental`, `base_agility`, `base_dexterity`, `base_defense`, `experience_for_level`, `total_experience`, `kind` (`menu_status` fields): the member detail the session gives.
* Refused ally (behavior): Mend or any one-ally action the session refuses with `command.no_effect_on_target` keeps the target list open with that ally greyed out.
* `damage_numbers` (battle param: `applied`, `rolled`): floating numbers as the change made or as `BattleEvent.Rolled`.
* Patrolling actors (behavior): slide between cells at their `patrol.speed_percent` like wanderers; path actors drawn at home plus their pixel offset (`GameInfo.CellPixels`).
* `sprites.platform` (asset entry) and moving platforms (behavior): `Platforms()` drawn at their pixel places, the party drawn on the one carrying it with no glide, input gated on `StepMask` where there are platforms.
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
