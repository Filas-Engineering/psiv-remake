# events

An event scripting format and interpreter for cutscenes and field events. It is engine-neutral: plain C++17 and the standard library only, no exceptions across the public API, and no engine types. Writers author scenes as data (see [FORMAT.md](FORMAT.md)). An engine runs them by implementing one interface, `events::Host`.

## Layout

```
include/events/   public headers
	compiler.h        compile(sources, options) -> Program + diagnostics
	diagnostics.h     Diagnostic (file, line, severity, message, text key and parameters), diagnostic_text()
	glossary.h        name glossary, name tokens, "show shipped names" rendering
	host.h            Host interface and the values passed to it
	interpreter.h     Interpreter: start, step, skip, save, restore
	lint.h            static checks on compiled scripts
	program.h         the compiled program (flat instruction list)
	recording_host.h  fake Host for tests and the CLI
	registry.h        the game's content names, flags and variables, for compile checks
	save_state.h      SaveState, the presentation state and the text form
	strings.h         string tables: load, resolve, check, extract
	timing.h          TimingHost and scene timing (pacing budget)
src/              lexer, parser, validator, codegen, interpreter, hosts, tools
tests/            one test executable with a minimal check helper
tools/event_cli.cpp
samples/          sample scenes (scripts only); between them they use every command
sample_text/      string tables, glossary, registry, timing config and entry list for the samples
```

## Build and test

Needs `clang++` and `make`. Nothing else.

```
make          # build/event_cli and build/event_tests
make test     # runs the tests, then every CLI command on the samples
make clean
```

All code builds with `-std=c++17 -Wall -Wextra -Werror -fPIC`, so `build/libevents.a` can go into a shared library as it is.

The CLI:

```
build/event_cli check samples/*.ev
build/event_cli check --strict --registry sample_text/registry.txt --glossary sample_text/glossary.txt --table sample_text/en.strings --table sample_text/ja.strings samples/*.ev
build/event_cli run samples/harbor.ev harbor_arrival --choose 0,2
build/event_cli run samples/market.ev samples/harbor.ev market_haggle
build/event_cli run samples/lighthouse.ev lighthouse_ambush --battle lose --skip-at 3
build/event_cli lint --entries sample_text/entries.txt samples/*.ev
build/event_cli time --config sample_text/timing.cfg --table sample_text/en.strings samples/harbor.ev harbor_arrival
build/event_cli strings check [--strict] [--used FILE] --table sample_text/en.strings --table sample_text/ja.strings samples/*.ev
build/event_cli strings extract samples/*.ev
build/event_cli names sample_text/glossary.txt --table sample_text/en.strings samples/*.ev
```

`run` takes one or more script files and then the event name, so `call` reaches events in any of the files, as with `check`. It steps the event at 0.1 s (`--dt`) against the recording host and prints each host call with its step number, then the final flags, variables, items, party and player control. `--choose` and `--battle` script the answers in order. `--skip-at N` asks for a skip before step N.

`check`, `lint`, `time`, `strings` and `names` are described for writers in [FORMAT.md](FORMAT.md). `time` also takes several files before the event name. Every command exits 1 when it finds an error (for `check --strict` and `lint --strict`, also a warning) and 2 on bad arguments.

## Embedding

```cpp
events::Registry registry;            // optional: load_registry() or add() / declare_*()
events::CompileOptions options;
options.registry = &registry;
events::CompileResult result = events::compile({{"harbor.ev", text}}, options);
if (!result.ok()) {
	for (const events::Diagnostic& d : result.diagnostics) {
		log(events::format_diagnostic(d));
	}
	return;
}
MyHost host;
events::Interpreter interpreter(result.program, host);
interpreter.start("harbor_arrival");
// every frame:
interpreter.step(delta_seconds);   // returns Status::running until done
```

`step()` runs until every thread is blocked, then returns. A thread blocks on a timed `wait`, on a host handle that is not done yet (an action, a dialogue line, a choice, a battle, a menu or a rest), on a `together` block whose branches are still running or a `race` none of whose branches has finished, or on `sync` or the end of an event while `nowait` actions are still running. Time passed to `step()` only counts against waits that were already in progress when the step began. A wait that starts during a step begins counting at the next step.

`next_output()` says what the innermost running event shows next without running anything (`NextOutput`: `line`, `choice`, `end`, `other`, `none` while nothing runs): it looks past the line its main thread shows now, through `goto`s and the jumps that close blocks, returns from `call`s and flag and variable changes; a branch on a condition, a choice's answer, a wait, a block, an action and every other command give `other`. A host holds a narration window that goes on by itself while the answer is `line`.

`position()` says where a running event is, for tools such as a script line overlay: the innermost event (one a `call` runs, inside its caller) and the file and source line of the instruction its main thread is at, which is the command it waits on; while the main thread waits on a `together` or `race` block, the first live branch's. Every `Instruction` keeps its `file` and `source_line`, so nothing extra is compiled for it. It is empty while nothing runs.

Runtime problems put the interpreter in `Status::error`, with `error()` giving `file:line: message`. Examples are a host returning an out-of-range choice index, a host that returns no handle for a menu or rest it does not support, calls nested more than 32 deep, or 100,000 commands in one step without a wait. Everything in flight is cancelled when this happens.

`Program` is immutable after compiling, and one program can drive many interpreters.

Background events (`event <name> background`, [FORMAT.md](FORMAT.md), "Background events") run in interpreters of their own beside the field: `EventInfo::background` marks them, `Host::spawn_event(name)` is called for `spawn`, and `cancel()` stops a running interpreter where it stands, cancelling every handle still in flight and leaving `Status::idle`. The compiler guarantees that a background event calls no host method that takes control (`show_line`, `present_choice`, `start_battle`, the menus, rests and warps, the party, item, money, vehicle and player-control calls), so a game can give each its own small host. Each one saves and restores like any interpreter; a host that returns an empty `snapshot_world()` for them and accepts it in `restore_world` makes a changed script restart the event from its first command over the loaded world.

## Implementing Host

Each engine implements `events::Host` once. In Unreal, for example, this is a `UObject` or subsystem that owns the handle table.

Handles. `start_action`, `show_line`, `present_choice`, `start_battle`, `open_shop`, `rest_party`, `open_save_prompt` and `open_name_entry` return a non-zero `Handle`. The interpreter polls `is_done(handle)` every step. After `is_done` returns true it may read `result(handle)`, then calls `release(handle)`. If a skip or an error abandons the handle, it calls `cancel(handle)` instead, and never calls release after that. Each handle is released or cancelled exactly once, so the host can free its bookkeeping then. An instant action can return a handle that is already done.

- `show_line(Line)`: show the line. It is done when the player confirms it. An empty `speaker` means narration, and `portrait` is the expression tag. When `text.is_id` is true, `text.value` is a localization string ID.
- `present_choice(ChoiceRequest)`: done when the player picks. `result()` is the option index. For `yes_no` requests the options list is empty, and the host shows its own yes and no, where yes = 0 and no = 1.
- `start_battle(BattleRequest)`: done when the battle is over. `result()` is a `BattleResult` (see [Battles](#battles)). The host may write its pre-battle autosave from inside this call (see the save contract).
- `open_shop(shop)`, `open_save_prompt()`, `open_name_entry(member)`: done when the menu closes. The save prompt writes an ordinary save without `SaveState` (EVT-053), so loading it returns to the field.
- `rest_party(RestRequest)`: done when the rest is over. An empty `point` is a free full heal; otherwise the rest point's own rules (its price) apply. `instant` is set while skipping: heal without the fade or jingle.
- These four have default implementations that return 0. The interpreter treats 0 as "not supported" and stops the event with a runtime error, so a host only overrides the ones its scripts use.
- `log_skipped_line(Line)`: a line a skip passed over, for the text log (TXT-028). The default does nothing.
- `start_action(Action)`: `Action::type` says what to do. The fields used by each type:

| type | subject | target | other fields | done when |
| --- | --- | --- | --- | --- |
| move | actor | marker, or `position` if `has_position` | `speed` (0 = default), `waypoints` (`via` markers, walked in order first), `pathfind` (find a way around what stands in it) | actor arrives |
| face | actor | actor, marker, or a direction (`north`, `east`, `south`, `west`; `is_direction`) | | turn finished |
| animate | actor | animation | `loop`, `seconds` (a standard animation's length, else 0) | animation ends (loops: when started) |
| show_actor, hide_actor | actor | a transition (`with <animation>`), or empty | `seconds` (a standard animation's length, else 0) | visible or hidden, after the transition |
| panel_show | panel id | camera | `slot` (layout slot) | panel transition finished |
| panel_hide | panel id | | | transition finished |
| panel_clear | | | | all panels gone |
| camera_cut | camera | | | immediately |
| camera_move | camera | | `seconds` | blend finished |
| fade_in, fade_out | | | `seconds` | fade finished |
| shake | | | `seconds`, `strength` | shake finished |
| music_play | track | | | playback started |
| music_stop | | | | stopped |
| music_fade | | | `seconds` | faded out and stopped |
| ambient_play | sound | | | playback started |
| ambient_stop | | | | stopped |
| sound | cue | | | playback started |
| warp | map | marker | | map loaded, party placed |
| place_actor | actor | marker, or `position` if `has_position` | `facing` (a direction, or empty to keep it) | actor stands there (at once) |

- `Action::instant`: put the end state on screen at once, with no transition, and return a handle that is already done. If the end state is already there (that music is already playing, the actor already stands on the marker), do nothing. The interpreter sets it when it rebuilds the screen after a skip or a restore. A warp issued while skipping is also instant: skip the transition, but load the map; its handle may take as long as loading does. `presentation_only(type)` is true for every type but warp (EVT-023).
- `get_flag`, `set_flag`, `get_var`, `set_var`, `give_item`, `take_item`, `add_party_member`, `remove_party_member` and `set_player_control` are synchronous and go to the game's own state. Unknown flags must read as false and unknown variables as 0.
- `give_vehicle(vehicle, map, marker)`, `place_vehicle(...)` and `remove_vehicle(vehicle)` (the `vehicle` commands) and `has_vehicle(vehicle)` and `aboard_vehicle(vehicle)` (the `vehicle` and `aboard` tests) are synchronous too. They have defaults that do nothing and report no vehicle, so a host without vehicles need not override them; a game that keeps vehicles in its state saves them with the rest of it and includes them in `snapshot_world`.
- `remove_actor(actor, until_reload)` (`remove`) is synchronous and game state: the actor is gone for good, or until the party next enters an area. The default does nothing.
- `spawn_event(event)` (`spawn`) asks the game to start that background event beside the field; the game decides when it runs and what stops it. `set_actor_look(actor, enemy, look)` (`look`) is synchronous game state: draw the actor with that enemy's field figure, a look the game names, or (both empty) its own figure again. Both default to nothing.
- `save_party(snapshot)`, `restore_party(snapshot)` and `swap_party_member(PartySwap)` (`party save`, `party restore`, `party swap`) are synchronous and game state. `restore_party` returns false when no snapshot has that name, and the interpreter then stops the event with a runtime error at the command's file and line, while skipping too. The defaults keep no snapshot (so every restore fails) and swap nobody. A game that keeps snapshots saves them with the rest of its state and includes them in `snapshot_world`.
- The standard animations (`standard_animations()`, `standard_animation(name)`: `fade_in`, `fade_out`, `dissolve`, `blink_out`, `hop`, `shake`, each with its default seconds and `AnimationEnd`) reach `start_action` as an `animate` like any other; draw them. The interpreter treats `fade_in` as a show and `fade_out`, `dissolve` and `blink_out` as a hide for what it rebuilds, and the rebuild plays a hiding one with `instant` set in place of a `hide`.
- `money()` and `change_money(delta)` (the `money` test and `money give|take`), `equip_piece(member, piece)` and `unequip_piece(member, piece)`, and `set_area_music(track)` (`music area`; empty for `default`) are synchronous too, with defaults that hold no money and do nothing. A game that keeps them saves them with the rest of its state.
- `snapshot_world()` returns the game state (flags, variables, items, party, player control and anything else the game saves) as an opaque string. The interpreter calls it when `start()` begins an event and keeps the result in `SaveState::start_world`. `restore_world(snapshot)` puts such a string back and returns false if it cannot. The interpreter calls it only when restoring a save made with different scripts (see the save contract).
- Line text may contain name tokens, `{name:<id>}`, `{name_upper:<id>}` and `{name_lower:<id>}`. Render them with `render_names(text, glossary, name_set_for(show_shipped_setting, text_language))` from `glossary.h` when the line is shown, so the "show shipped names" setting applies at once (TXT-016) and only to English (TXT-017). Resolve `@string.id` text with `resolve_text` from `strings.h` first.
- Line text may also contain control tags: `{page}`, `{wait:N}`, `{portrait:ID}`, `{var:ID}` and any other `{word}` or `{word:value}`. The interpreter passes them through untouched; the host acts on them as `FORMAT.md` ("Control tags") describes. `find_control_tags` lists them.

The game's own state (flags, variables, items, party) lives in the host and is saved by the game's normal save system, not by the interpreter. The exception is a registry's `local` flags and variables, which live in the interpreter for one run of an event and never reach the host. The initial values of the registry's global ones are in `Program::declarations` for the host to apply when it starts a new game.

### Battles

A `battle` block calls `start_battle` with a `BattleRequest`:

- `encounter`: the encounter (enemy group) ID from the script.
- `escape_allowed`: true when the script has a `flee` section. When false, the battle must not offer escape.
- `defeat_handled`: true when the script has a `lose` section. When false, a defeat must go to the game's game-over flow, and the host never reports it to the interpreter. Typically the game-over flow loads a save, which throws this interpreter away.
- `opening`: `normal`, `party_first` or `enemy_first`, from the script's `opening` option.
- `end`, `end_rounds`, `end_enemy`: the scripted end from the script's `until` option. `BattleEndKind::survive_rounds` ends the battle after `end_rounds` rounds; `enemy_defeated` ends it when the enemy `end_enemy` (its game ID) falls. Report a scripted end as `BattleResult::win`.
- `vehicle`: empty, or the vehicle the party fights aboard, from the script's `aboard` option.

The host forwards the request to whatever battle rules it uses: look up the encounter, set up the fight with escape on or off and with the scripted end, run it, and when it ends map the outcome to `BattleResult::win`, `lose` or `flee`. The handle is done at that point. The interpreter has no other dependency on the rules. If the host reports `lose` when `defeat_handled` is false, or `flee` when `escape_allowed` is false, the interpreter stops with a runtime error, because the script has no branch for it. Rewards (experience, money, drops) are the rules' business and happen outside the script.

## Save-state contract

`Interpreter::save()` returns a `SaveState`. This is plain data (ints, doubles, strings, vectors) that an engine can write to its own archive. `save_state_to_text` and `save_state_from_text` give a portable one-line text form. Save it next to the host's world state.

To resume, create an interpreter over the installed `Program` with the loaded host and call `restore(state, error)`. The rules:

1. A `SaveState` stores the program's fingerprint: a hash of every source file's name and tokens, the compiler's code layout version and the registry's local declarations. Comments, blank lines and indentation are not part of it, so editing them keeps old saves resumable.
2. It also stores the outermost event's name and `start_world`, the host's `snapshot_world()` from when that event started (EVT-050). When the fingerprint differs, the saved positions mean nothing in the new program, so `restore` calls `host.restore_world(start_world)`, then starts that event again from its first command, and `restarted()` returns true (EVT-052). `restore` returns false with an error only if the event no longer exists, the host cannot restore the snapshot, or the save is corrupt. A save of an interpreter that was not running (idle or finished) restores as such, whatever the fingerprint.
3. Timers resume exactly. A save taken during `wait` keeps the remaining time, so the rest of the event plays identically.
4. It stores what the event put on screen (`SaveState::presentation`, EVT-022): the panels on screen with their cameras and layouts, the current music and ambient sound, whether the screen is faded out, the last camera, and for each actor its visibility, last move or place target (with the move's `via` markers and `pathfind`, and whether it was a place), facing and looping animation, or the standard animation that hid it. The first `step()` after `restore` rebuilds it with instant host calls before anything else (EVT-051), in this order: music, ambient, camera, actors, panels, fade. Only what the event itself set is rebuilt; anything else stays as the loaded game has it. A warp forgets the actors and camera, since the new map places its own.
5. Host handles are not saved. Whatever was in flight starts again on the first `step()` after the rebuild:
   - A thread that was waiting on a line, choice, menu, rest, action or battle runs that command again. The player sees the line again.
   - `nowait` warps that were still running are started again. Other `nowait` actions are not; the rebuild already put their end state on screen.
   - Actions must therefore reach a well-defined end state when repeated. Moves go to a target, and fades end at a level. A restarted battle starts over, so save inside a battle with the battle system's own save, not this one.
6. When the scripts match, the world is restored by the game, not the interpreter. After loading, the host must already report the flags and variables as they were at save time. Local flags and variables come back from the save.
7. Skip state is kept. A save taken while skipping (which can only last across steps while a warp or rest finishes) resumes skipping.
8. Only save while an event runs for the autosave before an event battle (EVT-050). The game must not offer a manual save during an event (EVT-053). The host may call `save()` from inside `start_battle`, or from inside any other host call that returns a handle: the state is the one from just before that command, so a restore issues it again. Inside `start_battle`, any skip has already stopped and the screen has been rebuilt. Do not call it from inside the synchronous calls (`set_flag`, `give_item` and the like): a restore does not repeat those, but the world saved with it would already include them.
9. A `camera cut` or `camera move` never changes which camera a panel shows. A panel's camera comes from its `panel show` (EVT-033 makes the binding the panel's own), and `PresentationState` keeps the last camera cut or moved to (`camera`) apart from each panel's (`PanelState::camera`). The camera a cut or move names becomes the scene's camera; what that does to a panel showing it (the MonoGame sample pushes such a panel in) is the host's framing. The rebuild issues that camera's instant `camera cut` before the panels' instant `panel show` calls, so a host that frames panels by the scene's camera keeps the camera from the cut and applies it to the panels shown after it in the same rebuild: the panel actions carry only their own camera.

The text form starts with `events-save 6` when the presentation holds a kept choice (`choice ... keep`), else `events-save 5` when it holds a `camera to` or `camera reset`, else `events-save 4`, or `events-save 3` when no actor's state has a route or a place (each the oldest version that holds what the state uses, so older builds read it), and ends with ` sum ` and a 64-bit FNV-1a checksum, in 16 hex digits, of everything before it. `save_state_from_text` rejects a text whose checksum does not match, so a truncated or damaged save is reported instead of loaded (SAV-014). It reads versions 3 to 6 and rejects others, including version 2 (before presentation state and the checksum); a game should treat such a save's event state as damaged.

## Tools as a library

Every CLI command is a library call, so a build pipeline can run them without the CLI:

- `registry.h`: `load_registry`, `Registry` (with `add_marker`, `add_event_map` and `marker_on` for marker maps), passed to `compile` through `CompileOptions`.
- `strings.h`: `load_string_table`, `resolve_text`, `collect_texts`, `find_control_tags`, `load_game_strings`, `GameStrings`, `check_strings`, `check_inline_text`, `extract_strings`.
- `glossary.h`: `load_glossary`, `find_name_tokens`, `name_set_for`, `name_case_word`, `shown_name`, `render_names`, `find_name_mentions`, `check_names`.
- `timing.h`: `TimingConfig`, `load_timing_config`, `TimingHost`, `time_event`. Feed `TimedPath::time.total` to the CI comparison with the measured baseline (EVT-062).
- `lint.h`: `lint(sources, LintOptions)`.

## Tests

`make test` runs `build/event_tests`, then every CLI command on the samples, including the strict check with the registry, glossary and both string tables. The tests cover parse errors and their line numbers, each validation rule, the registry (loading, every kind of unknown name, flag and variable types, local values, marker maps with warps and moves to another map's marker, event start maps, enemies named by a scripted end, vehicles and their markers), branching, loops and goto, choices and the answer they record before their branch, waits spanning several steps, `together` branches finishing in either order, `race` stopping the slower branches, `nowait`/`sync`, nested calls, menus and rests, the vehicle commands and tests (also applied while skipping), `learn` and `follower` (applied while skipping, names checked against the registry), the actor commands (`place`, `move ... via ... pathfind`, a direction for `face`, `remove` applied while skipping, standard animation lengths, their syntax errors and registry checks, `show`/`hide ... with` and its rebuild, `look`, a skip at every step putting animations at their end state, a save mid-animation restoring the route and replaying the animation, and version 3 written when no route is used), background events (`spawn` and `look` reaching the host also while skipping, the commands a background event may not hold, `call` and `spawn` targets, a loop run to its end, `cancel` giving back every handle), battle requests and results (including results the script did not allow, the opening and the scripted end), runtime errors, and handle discipline (no leaks, no use after release). The skip tests check that skipping at every step of every choice and battle path, and again after each stop, leaves the same world and the same presentation as watching; that a skip stops at choices, battles and menus and is offered again after them; that it fast-forwards running branches to their end state, passes through warps and rests, and keeps the unskippable caller's own actions. The save/restore tests cover an identical remaining trace after restoring mid-wait, a replayed line after restoring mid-dialogue, a restore at every step, the presentation rebuild and its order, a save written from inside `start_battle`, a save while skipping, the fingerprint ignoring comments, blank lines and indentation, a restart from the event-start world after a script change, and corrupt saves: every single-byte flip and every truncation is rejected. The tool tests cover string table loading and checks (including IDs the game shows itself, including dropped tokens, reordered control tags, the `tools/text` control codes, and name tokens that change case form between languages), control tags and name tokens reaching the host as written, inline text, extraction with option lines, glossary loading, name rendering in all three case forms, the mention report and the name check, scene timing across branches, and each lint rule.

## Changes a host must make

Newest: a choice that stays on screen and option states ([FORMAT.md](FORMAT.md), "Choices").

- `program.h`: `Op::choice_clear` (appended, name `choice clear`). A `switch` over `Op` needs the new case. `OptionState` (`word`, `has_condition`, `condition`) and `Instruction::option_states`, one per option or empty when no option has a state.
- `host.h`: `ChoiceRequest::keep` and `ChoiceRequest::states` (each option's state word, "" for none, evaluated as the choice is presented; empty when no option has one). `describe(ChoiceRequest)` adds ` keep` after the header and ` state <word>` after an option that has one.
- `interpreter.h`: `Interpreter::kept_choice(picked, states)`: the last answered `choice ... keep` of the running event, nullptr when none or after `choice clear` or the event's end.
- `save_state.h`: `PresentationState::kept_choice` (the instruction, -1 for none), `kept_picked`, `kept_states`; `kSaveTextVersion` 6, the text form version 6 only when a state holds a kept choice, so every other state is written as before; versions 3 to 5 still load.
- Scripts that use none of the new syntax keep their fingerprint and code.

Before that: a warp's follower placement ([FORMAT.md](FORMAT.md), "Game state").

- `host.h`: `Action::followers`, a warp's `followers <placement>` (`stacked`, `behind`, `up`, `down`, `left` or `right`), empty without one. `describe(Action)` adds ` followers <placement>` to a warp that has one.
- Scripts that use none of the new syntax keep their fingerprint and code.

Before that: towns registered by story and choice headers ([FORMAT.md](FORMAT.md), "Game state" and "Choices").

- `program.h`: `Op::register_town` (appended, name `register town`): `Instruction::name` the area, `number` 1 for `unregister town`. A `switch` over `Op` needs the new case.
- `host.h`: `ChoiceRequest::header` (empty without one) and the virtual `Host::register_town(area, registered)`, whose default does nothing. `describe(ChoiceRequest)` adds ` header <text>` after the prompt; `collect_texts` gives the header the role `choice header`.
- `RecordingHost` logs `register town <area>` and `unregister town <area>`.
- Scripts that use none of the new syntax keep their fingerprint and code.

Latest: story pictures, the scene camera and music fades ([FORMAT.md](FORMAT.md), "Story pictures", "Camera, panels and the screen" and "Audio").

- `program.h`: `Op::picture` (appended, name `picture`): `Instruction::name` the picture (empty for a hide of the top one), `number` 0 show, 1 hide, 2 clear. A `switch` over `Op` needs the new case.
- `host.h`: `ActionType::camera_to` and `ActionType::camera_reset` (appended, names `camera to`, `camera reset`, timing names `camera_to`, `camera_reset`): a `camera to` carries a marker in `target`, or a cell in `position.x` (column) and `position.y` (row) with `has_position`, and `seconds` (0 cuts); a reset carries `seconds`. `Action::fade_ms` on `music_play`: the script's `fade <ms>`, 0 for `cut`, -1 for neither. The virtual `Host::show_picture(picture)`, `hide_picture(picture)` (empty: the top one) and `clear_pictures()`, synchronous game state like `set_area_music`, which do nothing by default; they apply while skipping. `describe(Action)` writes `camera to <marker>` or `camera to <x> <y>`, `camera reset`, each with the seconds when above 0, and `music play <track> fade <ms>` or `cut`.
- `registry.h`: `NameKind::picture` (appended, file keyword `picture`); `picture show` and a named `picture hide` must name a listed one only when the registry lists at least one.
- `save_state.h`: `PresentationState::camera_to`, `camera_reset`, `camera_marker`, `camera_has_position`, `camera_position`: the last `camera to` or `camera reset` since the last cut or move (which clears `camera`), rebuilt as one instant action; a warp clears them as it clears `camera`. The text form is version 5 only when a state holds one of them.
- `RecordingHost` logs `picture show <name>`, `picture hide [<name>]` and `picture clear`.
- Scripts that use none of the new syntax keep their fingerprint and code.
- `language.h`: `script_commands()` (each command word, its forms, and whether it compiles in a background event and runs in `together`/`race`) and `condition_tests()`, the tables the parser now reads; a new command needs its entry there. `save_state.h`: `kSaveTextVersion` (5).

This round, last: `credits <credits>` and `end game` ([FORMAT.md](FORMAT.md), "Menus and rests" and "Flow").

- `program.h`: `Op::credits` (name `credits`, `Instruction::name` the credits id) and `Op::end_game` (name `end game`), appended. A `switch` over `Op` needs the new cases.
- `registry.h`: `NameKind::credits` (appended, file keyword `credits`); with a registry, `credits` names a listed one.
- `host.h`: the virtual `Host::play_credits(credits)`, a blocking call like `open_name_entry` whose default returns 0 (a runtime error at the line), and `Host::end_game()`, which does nothing by default. A skip stops at `credits`. After `end_game` returns, the interpreter cancels every branch and handle and reports `Status::finished`; it applies while skipping.
- `RecordingHost` logs `credits <id>` (done at once) and `end game`.
- The parser reads a line starting `end game` as the command, never as a block's `end`; both are errors in background events and parallel blocks. Scripts that use neither keep their fingerprint and code; the save text is unchanged.

Latest pass: `Interpreter::next_output()` and `Interpreter::NextOutput` (above). No save, format, fingerprint or host call changed.

This round, in a later pass: `party order` and `silent` on `party add`, `party remove` and `party swap` ([FORMAT.md](FORMAT.md), "Game state").

- `program.h`: `Op::party_order` (appended, name `party order`) with `Instruction::names`; `party_add` and `party_remove` carry `number` 1 for `silent`.
- `host.h`: `PartySwap::silent` (written by `describe(PartySwap)` as `silent`); the virtual `Host::add_party_member_silently` and `remove_party_member_silently`, which call `add_party_member` and `remove_party_member` by default, and `Host::order_party(members)`, whose default changes nothing and accepts only names `in_party` knows. A `false` from `order_party` stops the event with a runtime error.
- `RecordingHost` logs `party add <member> silent`, `party remove <member> silent` and `party order <members> -> true|false`, reordering its `party`.
- Diagnostic key `script.party_order_twice`. Scripts that use none of the new words keep their fingerprint and code; the save text is unchanged.

This round, in a later pass: background events and the newer actor commands ([FORMAT.md](FORMAT.md), "Background events" and "Actors").

- `program.h`: `EventInfo::background`; `Op::spawn` and `Op::actor_look` (appended; `Instruction::name` the event or the actor, `action.subject` `enemy` and `action.target` the enemy, or `action.target` alone the look, both empty for `default`).
- `host.h`: the virtual `Host::spawn_event(event)` and `Host::set_actor_look(actor, enemy, look)`, which do nothing by default; `Action::target` of `show_actor` and `hide_actor` may name a transition, `describe(Action)` writes it as `with <animation>`.
- `interpreter.h`: `Interpreter::cancel()`.
- `RecordingHost` logs `spawn <event>` (and keeps `spawned`) and `look <actor> ...`.
- Diagnostic keys `script.background_calls_foreground`, `script.background_not_allowed`, `script.spawn_needs_background`, `script.spawn_unknown_event`, `script.transition_wrong_way`; `script.event_marked_skippable_unskippable` now names `background` too. `lint` counts a `spawn` as a use of the event. Scripts that use none of the new syntax keep their fingerprint and code; the save text is unchanged (`events-save 3` or `4`).

This round: the actor commands `place`, `move ... via ... [pathfind]`, `face <actor> <direction>` and `remove <actor> [until reload]`, and the standard animations ([FORMAT.md](FORMAT.md), "Actors"); and the party commands `party save [NAME]`, `party restore [NAME]` and `party swap <slot|member> <member> [equipment keep|bag]` ([FORMAT.md](FORMAT.md), "Game state").

- Party commands: `host.h` gains `enum class SwapEquipment` (`rule`, `keep`, `bag`), `struct PartySwap`, `describe(PartySwap)` and the virtual `save_party`, `restore_party` and `swap_party_member`, with defaults; `program.h` gains `Op::party_save`, `Op::party_restore` and `Op::party_swap` (appended) and `Instruction::swap`; `party save` and `party restore` without a name use `default`. `RecordingHost` keeps `party_snapshots`, logs `party save <name>`, `party restore <name> -> true|false` and `describe(PartySwap)`, and includes the snapshots in `snapshot_world` (`snapshot` lines, written only when there are some), `restore_world` and `same_world`. Diagnostic key `script.party_slot_at_least_1`. With a registry, `party swap` names listed members. Scripts that use none of them keep their fingerprint.

- `host.h`: `ActionType::place_actor` (appended, name `place`, also a timing action name); `Action::waypoints`, `pathfind` and `facing`; `enum class AnimationEnd`, `struct StandardAnimation`, `standard_animations()`, `standard_animation()`, `is_direction()`; the virtual `Host::remove_actor(actor, until_reload)`, which does nothing by default. A `switch` over `ActionType` needs the new case. `describe(Action)` writes `via`, `pathfind` and `facing`.
- `program.h`: `Op::remove_actor` (appended; `Instruction::name` the actor, `number` 1 for `until reload`).
- `save_state.h`: `ActorState::via`, `pathfind`, `placed`; `pose` also names the standard animation that hid an actor. The text form is version 4 when an actor's state uses them, else 3; both load.
- `RecordingHost` keeps `removed` (actor to until-reload), logs `remove <actor> [until reload]` and includes it in `snapshot_world`, `restore_world` and `same_world`.
- An `anim` of a standard animation carries its default length in `Action::seconds`, which the timing rule uses when no `duration` applies; a registry need not list the standard animations. Scripts that use none of the new commands compile to the same code and keep their fingerprint; the code layout version is unchanged.

The round before: `host.h` gains the virtual methods `money`, `change_money`, `equip_piece`, `unequip_piece` and `set_area_music`, with defaults; `Op` gains `money`, `equip`, `unequip` and `area_music` (appended) and `TermTest` gains `money`. Scripts that do not use the new commands compile to the same code and keep their fingerprint, so `events-save 3` saves resume as before. Diagnostic key `script.money_at_least_1`.

Two rounds before: the script commands `learn <member> <ability>` and `follower <actor>|none` (`Op::learn`, `Op::follower`, appended), the registry kind `ability` (`NameKind::ability`, appended), and the host calls `Host::teach_ability` and `Host::set_follower`, which do nothing by default, so a host that does not override them still compiles and runs. Scripts that do not use the commands keep their fingerprint, and `events-save 3` saves still load.

Three rounds before: `Interpreter::position()` and `Interpreter::Position` (event, file, line), above. Nothing else changed: no save, format, fingerprint or host call.

Four rounds before. No save changes: `events-save 3` saves still load, and scripts that do not use the new syntax keep their fingerprint (the compiler's code layout version is unchanged; the new ops are appended).

- `host.h`: new virtual methods with defaults, `give_vehicle`, `place_vehicle`, `remove_vehicle`, `has_vehicle` and `aboard_vehicle` (see [Implementing Host](#implementing-host)). Override them to keep vehicles in the game's state.
- `program.h`: `Op::vehicle_give`, `vehicle_place` and `vehicle_remove` (appended; `Instruction::name` is the vehicle, `action.subject` and `action.target` the map and marker); `enum class VehicleTest { none, owns, aboard }` and `Term::vehicle`, set for a `vehicle` or `aboard` test, whose `name` is then the vehicle. `describe(Condition)` writes them as `vehicle X` and `aboard X`. A `switch` over `Op` needs the new cases.
- `RecordingHost` keeps `vehicles` (id to `"map marker"`), logs `vehicle give|place|remove ...` and `has_vehicle X -> true|false`, includes them in `snapshot_world`, `restore_world` and `same_world`, and never rides one. `event_cli run` prints the vehicles at the end.
- Format: the `vehicle give|place|remove` commands and the `vehicle` and `aboard` tests ([FORMAT.md](FORMAT.md)). With a registry, the vehicle must be listed and the marker on the map.

Five rounds before. No save, format or fingerprint changes: `events-save 3` saves still load, and the interpreter issues the same host calls as before.

- `host.h`: new `parse_action_type(name, type)` (the action names timing configurations use: `move`, `panel_show`, `camera_move`, ...) and `action_seconds(action, by_type, by_subject)`, the timing rule (the entry for the action's type and subject, else its type's, else the action's own `seconds`). `RecordingHost`, and so `TimingHost` and `event_cli time`, use them unchanged; the game uses them for the durations its data carries (`../game/README.md`, "game.json").
- The save-state contract gains rule 9 on cameras and panels (below). Nothing changed in the interpreter; the rule says what a host that frames panels by the scene's camera must keep.

Six rounds before. No save changes: `events-save 3` saves still load. Scripts that do not use `aboard` keep their fingerprint.

- `diagnostics.h`: `Diagnostic` adds `key` and `params`, the text key and parameters of the problem; `message` is still the English text, rendered from them (a parser diagnostic's message also starts with its command, as before). New `struct DiagnosticMessage { key, params, empty() }`, `diagnostic_text()` (the English template of every key: `script.*`, `strings.*`, `names.*`, `registry.*`, `lint.*`, `timing.*`), `format_message(table, key, params)`, and `diagnostic(severity, file, line, key, params)` to build one. A host that showed `message` keeps working; one that shows its own language looks `key` up in its table. Aggregate initialization of `Diagnostic` with four fields still compiles and leaves the key empty.
- `host.h`: `BattleRequest::vehicle`, from `aboard <vehicle>` on `battle`: set the battle up as a vehicle battle with it. `describe(BattleRequest)` writes ` aboard <vehicle>` after the encounter.
- `registry.h`: `NameKind::vehicle` (appended, keyword `vehicle`); with a registry, an unlisted vehicle is a compile error.
- Build: the library is compiled with `-fPIC`.
- Format: `aboard <vehicle>` on `battle`; `vehicle` lines in the registry.

Seven rounds before. No save or fingerprint changes: `events-save 3` saves still load, and scripts that do not use the new syntax keep their fingerprint.

- `host.h`: new `enum class BattleEndKind { none, survive_rounds, enemy_defeated }` and `BattleRequest::end`, `end_rounds` and `end_enemy`, from the script's `until`. Set the battle's scripted end from them and report it as `win`. `describe(BattleRequest)` appends ` until survive <n>` or ` until defeat <enemy>`.
- `registry.h`: `NameKind::enemy` (appended, keyword `enemy`); `Registry::marker_maps`, `Registry::event_maps`, `add_marker(map, marker)`, `add_event_map(map, event)` and `marker_on(marker, map)`; registry files take `map_markers`, `event_map` and `enemy` lines. A host that builds its registry in code should call `add_marker` for every marker (and `add_event_map` for every event it starts on a known map) so a warp or move to another map's marker fails to compile. A warp to a marker on another map, and a move to one on the map the party is known to be on, are now compile errors.
- `strings.h`: new `struct GameStrings { file, ids, prefixes, covers() }` and `load_game_strings(file, text, out, diagnostics)`. `check_strings` takes an optional third argument, `const GameStrings&`: its IDs must be in every table and, with its prefixes, are not reported as unused. The unused warning now reads "not used by any script or by the game".
- CLI: `check` and `strings check` take `--used FILE`; `strings check` takes `--strict`.
- Format: `until survive <rounds>` and `until defeat <enemy>` on `battle`; the game strings file.

Eight rounds before, the public API changed as follows. Old saves of interpreter state (`events-save 2`) no longer load, and every program's fingerprint changed.

Host (`host.h`):

- New virtual methods, each with a default, so existing hosts still compile: `Handle open_shop(const std::string& shop)`, `Handle rest_party(const RestRequest& request)`, `Handle open_save_prompt()`, `Handle open_name_entry(const std::string& member)` (defaults return 0, which stops a script that uses the command with a runtime error), and `void log_skipped_line(const Line& line)` (default does nothing). Override the menus and the rest your scripts use; override `log_skipped_line` to put skipped lines in the text log.
- `Action::instant` (new): honor it for every action type. After a skip or a restore the interpreter rebuilds the screen with instant calls instead of replaying the scene.
- `ActionType` gained `ambient_play` and `ambient_stop`, inserted before `sound`, so `sound` and `warp` have new numeric values. Handle them in any `switch`.
- `BattleRequest::opening` (new, `BattleOpening::normal`, `party_first` or `enemy_first`): set the battle up with it instead of always normal. `describe(BattleRequest)` appends ` opening <name>` when it is not normal.
- New types and functions: `struct RestRequest { std::string point; bool instant; }`, `enum class BattleOpening`, `bool presentation_only(ActionType)`, `std::string describe(const RestRequest&)`, `const char* battle_opening_name(BattleOpening)`.
- `start_battle` may now call `Interpreter::save()` directly to write the pre-battle autosave; there is no need to defer it until `step()` returns.
- The skip contract changed: the interpreter no longer runs an `on skip` block or keeps a change count. It fast-forwards the scene, calls the host for every state change in order, never calls `show_line` while skipping (it calls `log_skipped_line`), and stops at the next choice, battle, shop, save prompt or name entry. `can_skip()` is now false while the event waits on one of those.

Interpreter (`interpreter.h`):

- `skip()`: new semantics as above. New `bool skipping() const` and `const PresentationState& presentation() const`.
- `restore()`: the first `step()` afterwards issues instant calls to rebuild the presentation, and `nowait` presentation actions are no longer started again.
- `save()`: documented as safe inside any host call that returns a handle.

Compiler (`compiler.h`, `registry.h`, `program.h`):

- `compile(const std::vector<Source>&)` became `compile(const std::vector<Source>&, const CompileOptions& = CompileOptions())`. `CompileOptions::registry` (a `const Registry*`) turns on the registry checks. Existing calls compile unchanged.
- New `registry.h`: `enum class NameKind`, `struct Registry` (`file`, `names`, `declarations`, `add`, `has`, `declare_flag`, `declare_var`, `find`), `load_registry(file, text, registry, diagnostics)`, `name_kind_name(NameKind)`. A game that checks script references itself (encounters, items, members, markers) can build a `Registry` from its data instead.
- `program.h`: new `struct Declaration` and `Program::declarations`; new `Op` values `shop`, `rest`, `save_prompt`, `name_entry`; new `Instruction::option_lines`; `Instruction::name` holds a choice's `into` target and the shop, rest point or member of the new commands; `Instruction::number` is 1 on the `fork` of a `race`. Removed: `EventInfo::skip_entry`.

Saves (`save_state.h`):

- Removed: `enum class ChangeKind`, `struct SavedChange`, `SaveState::applied`, `SaveState::skip_credit`.
- `SaveState::skip_depth` now means the outermost event being skipped.
- New: `struct ActorState`, `struct PanelState`, `struct PresentationState` (each with `operator==`), `SaveState::presentation`, `SaveState::locals`.
- The text form is `events-save 3` with a trailing checksum. Versions 1 and 2 and damaged texts are rejected.

Text tools (`strings.h`, `glossary.h`, `recording_host.h`, CLI):

- `check_strings` now reports name token differences between languages as errors (they were warnings) and adds control tag differences as errors. New `find_control_tags`, `check_inline_text`, and `check_names`.
- Name tokens have two more forms, `{name_upper:<id>}` and `{name_lower:<id>}`, matching `tools/text`. `find_name_tokens`, `render_names`, `find_name_mentions`, `check_names`, `check_strings` and the `names` command handle all three; `find_control_tags` no longer reports the two new forms as control tags. New `enum class NameCase`, `name_case_word`, `shown_name`, `NameToken::name_case` and `NameMention::name_case`. No signature changed.
- `TextUse::line` and `extract_strings` give an option's own line, not its choice's.
- `RecordingHost`: new `instant_calls`; instant actions take no time except warps; the new host methods are logged as `shop <id>`, `rest [<point>]`, `save prompt`, `name entry <member>` and `skipped <line>`, and done at once.
- CLI: `check` takes `--strict`, `--registry`, `--glossary` and `--table`. `names` now exits 1 on a typed-out glossary name.

Format (`FORMAT.md`): new `race`, `into` on `choice` and `ask`, `opening` on `battle`, `ambient play` and `ambient stop`, `shop`, `rest`, `save prompt`, `name entry`, control tags, and the registry file. `on skip` blocks are ignored with a warning.

## Choices made where the spec is silent or in conflict

- EVT-053 forbids manual saves during an event, while game-flow lists an event opening a save prompt. `save prompt` exists; the host writes an ordinary save without interpreter state, so loading it returns to the field.
- EVT-006 says a choice's result goes to a flag. A multi-option answer does not fit a flag, so `choice ... into` writes a variable (1 for the first option) and `ask ... into` a flag. `into` is optional, because existing scripts have none.
- EVT-041 names only choices and battles as skip stops. Shops, save prompts and name entry also stop a skip, because the player decides something there; a rest does not, since it only changes state.
- Presentation with no end state (shakes, one-shot sounds) is dropped while skipping.
- EVT-008 says stopped branches stop "at their next yield point". When the first branch of a race ends, every other branch is already waiting at a yield point, so they stop there and their command in flight is cancelled. When several finish in one step, the first listed wins. While skipping, every branch finishes at once, so the first branch wins; branches cannot change game state, so only the presentation can differ from watching.
- EVT-004 does not define "event-local". Local values last for one run of an event started by the game, including the events it calls.
- EVT-003 names marker errors but not maps. A marker's map is known from `map_markers`; the party's map is followed from `warp`s and from `event_map`, and where the flow makes it unknown nothing is checked rather than guessing.
- BAT-013 lets an event set a scripted end; the format had no syntax. `until defeat` names an enemy by ID, not a formation position, and the host uses the first position that enemy holds. A scripted end runs the `win` section, as the game already did.
- EVT-022 lists "panel page" and "actor poses". The format has no page, so the panels on screen are the page. A pose is the last looping animation; a move clears the facing; a warp clears actors and camera.
- The presentation state is what the event last commanded. A move cut short by a skip or a race is recorded at its target.
- EVT-035 lists "move a panel's camera" among the script's panel commands, but the format's `camera move` names a camera, not a panel, and EVT-033 binds a panel to its camera in its page data. So a camera move moves the camera every panel showing it sees through; it does not rebind panels, and a panel keeps reporting the camera it was shown with (rule 9 of the save contract).
