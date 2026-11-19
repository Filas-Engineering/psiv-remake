# events

An event scripting format and interpreter for cutscenes and field events. It is engine-neutral: plain C++17 and the standard library only, no exceptions across the public API, and no engine types. Writers author scenes as data (see [FORMAT.md](FORMAT.md)). An engine runs them by implementing one interface, `events::Host`.

## Layout

```
include/events/   public headers
	compiler.h        compile(sources, options) -> Program + diagnostics
	diagnostics.h     Diagnostic (file, line, severity, message)
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

All code builds with `-std=c++17 -Wall -Wextra -Werror`.

The CLI:

```
build/event_cli check samples/*.ev
build/event_cli check --strict --registry sample_text/registry.txt --glossary sample_text/glossary.txt --table sample_text/en.strings --table sample_text/ja.strings samples/*.ev
build/event_cli run samples/harbor.ev harbor_arrival --choose 0,2
build/event_cli run samples/market.ev samples/harbor.ev market_haggle
build/event_cli run samples/lighthouse.ev lighthouse_ambush --battle lose --skip-at 3
build/event_cli lint --entries sample_text/entries.txt samples/*.ev
build/event_cli time --config sample_text/timing.cfg --table sample_text/en.strings samples/harbor.ev harbor_arrival
build/event_cli strings check --table sample_text/en.strings --table sample_text/ja.strings samples/*.ev
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

Runtime problems put the interpreter in `Status::error`, with `error()` giving `file:line: message`. Examples are a host returning an out-of-range choice index, a host that returns no handle for a menu or rest it does not support, calls nested more than 32 deep, or 100,000 commands in one step without a wait. Everything in flight is cancelled when this happens.

`Program` is immutable after compiling, and one program can drive many interpreters.

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
| move | actor | marker, or `position` if `has_position` | `speed` (0 = default) | actor arrives |
| face | actor | actor or marker | | turn finished |
| animate | actor | animation | `loop` | animation ends (loops: when started) |
| show_actor, hide_actor | actor | | | visible or hidden |
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

- `Action::instant`: put the end state on screen at once, with no transition, and return a handle that is already done. If the end state is already there (that music is already playing, the actor already stands on the marker), do nothing. The interpreter sets it when it rebuilds the screen after a skip or a restore. A warp issued while skipping is also instant: skip the transition, but load the map; its handle may take as long as loading does. `presentation_only(type)` is true for every type but warp (EVT-023).
- `get_flag`, `set_flag`, `get_var`, `set_var`, `give_item`, `take_item`, `add_party_member`, `remove_party_member` and `set_player_control` are synchronous and go to the game's own state. Unknown flags must read as false and unknown variables as 0.
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

The host forwards the request to whatever battle rules it uses: look up the encounter, set up the fight with escape on or off, run it, and when it ends map the outcome to `BattleResult::win`, `lose` or `flee`. The handle is done at that point. The interpreter has no other dependency on the rules. If the host reports `lose` when `defeat_handled` is false, or `flee` when `escape_allowed` is false, the interpreter stops with a runtime error, because the script has no branch for it. Rewards (experience, money, drops) are the rules' business and happen outside the script.

## Save-state contract

`Interpreter::save()` returns a `SaveState`. This is plain data (ints, doubles, strings, vectors) that an engine can write to its own archive. `save_state_to_text` and `save_state_from_text` give a portable one-line text form. Save it next to the host's world state.

To resume, create an interpreter over the installed `Program` with the loaded host and call `restore(state, error)`. The rules:

1. A `SaveState` stores the program's fingerprint: a hash of every source file's name and tokens, the compiler's code layout version and the registry's local declarations. Comments, blank lines and indentation are not part of it, so editing them keeps old saves resumable.
2. It also stores the outermost event's name and `start_world`, the host's `snapshot_world()` from when that event started (EVT-050). When the fingerprint differs, the saved positions mean nothing in the new program, so `restore` calls `host.restore_world(start_world)`, then starts that event again from its first command, and `restarted()` returns true (EVT-052). `restore` returns false with an error only if the event no longer exists, the host cannot restore the snapshot, or the save is corrupt. A save of an interpreter that was not running (idle or finished) restores as such, whatever the fingerprint.
3. Timers resume exactly. A save taken during `wait` keeps the remaining time, so the rest of the event plays identically.
4. It stores what the event put on screen (`SaveState::presentation`, EVT-022): the panels on screen with their cameras and layouts, the current music and ambient sound, whether the screen is faded out, the last camera, and for each actor its visibility, last move target, facing and looping animation. The first `step()` after `restore` rebuilds it with instant host calls before anything else (EVT-051), in this order: music, ambient, camera, actors, panels, fade. Only what the event itself set is rebuilt; anything else stays as the loaded game has it. A warp forgets the actors and camera, since the new map places its own.
5. Host handles are not saved. Whatever was in flight starts again on the first `step()` after the rebuild:
   - A thread that was waiting on a line, choice, menu, rest, action or battle runs that command again. The player sees the line again.
   - `nowait` warps that were still running are started again. Other `nowait` actions are not; the rebuild already put their end state on screen.
   - Actions must therefore reach a well-defined end state when repeated. Moves go to a target, and fades end at a level. A restarted battle starts over, so save inside a battle with the battle system's own save, not this one.
6. When the scripts match, the world is restored by the game, not the interpreter. After loading, the host must already report the flags and variables as they were at save time. Local flags and variables come back from the save.
7. Skip state is kept. A save taken while skipping (which can only last across steps while a warp or rest finishes) resumes skipping.
8. Only save while an event runs for the autosave before an event battle (EVT-050). The game must not offer a manual save during an event (EVT-053). The host may call `save()` from inside `start_battle`, or from inside any other host call that returns a handle: the state is the one from just before that command, so a restore issues it again. Inside `start_battle`, any skip has already stopped and the screen has been rebuilt. Do not call it from inside the synchronous calls (`set_flag`, `give_item` and the like): a restore does not repeat those, but the world saved with it would already include them.

The text form starts with `events-save 3` and ends with ` sum ` and a 64-bit FNV-1a checksum, in 16 hex digits, of everything before it. `save_state_from_text` rejects a text whose checksum does not match, so a truncated or damaged save is reported instead of loaded (SAV-014). It also rejects other versions, including version 2 (before presentation state and the checksum); a game should treat such a save's event state as damaged.

## Tools as a library

Every CLI command is a library call, so a build pipeline can run them without the CLI:

- `registry.h`: `load_registry`, `Registry`, passed to `compile` through `CompileOptions`.
- `strings.h`: `load_string_table`, `resolve_text`, `collect_texts`, `find_control_tags`, `check_strings`, `check_inline_text`, `extract_strings`.
- `glossary.h`: `load_glossary`, `find_name_tokens`, `name_set_for`, `name_case_word`, `shown_name`, `render_names`, `find_name_mentions`, `check_names`.
- `timing.h`: `TimingConfig`, `load_timing_config`, `TimingHost`, `time_event`. Feed `TimedPath::time.total` to the CI comparison with the measured baseline (EVT-062).
- `lint.h`: `lint(sources, LintOptions)`.

## Tests

`make test` runs `build/event_tests`, then every CLI command on the samples, including the strict check with the registry, glossary and both string tables. The tests cover parse errors and their line numbers, each validation rule, the registry (loading, every kind of unknown name, flag and variable types, local values), branching, loops and goto, choices and the answer they record before their branch, waits spanning several steps, `together` branches finishing in either order, `race` stopping the slower branches, `nowait`/`sync`, nested calls, menus and rests, battle requests and results (including results the script did not allow, and the opening), runtime errors, and handle discipline (no leaks, no use after release). The skip tests check that skipping at every step of every choice and battle path, and again after each stop, leaves the same world and the same presentation as watching; that a skip stops at choices, battles and menus and is offered again after them; that it fast-forwards running branches to their end state, passes through warps and rests, and keeps the unskippable caller's own actions. The save/restore tests cover an identical remaining trace after restoring mid-wait, a replayed line after restoring mid-dialogue, a restore at every step, the presentation rebuild and its order, a save written from inside `start_battle`, a save while skipping, the fingerprint ignoring comments, blank lines and indentation, a restart from the event-start world after a script change, and corrupt saves: every single-byte flip and every truncation is rejected. The tool tests cover string table loading and checks (including dropped tokens, reordered control tags, the `tools/text` control codes, and name tokens that change case form between languages), control tags and name tokens reaching the host as written, inline text, extraction with option lines, glossary loading, name rendering in all three case forms, the mention report and the name check, scene timing across branches, and each lint rule.

## Changes a host must make

This round changed the public API as follows. Old saves of interpreter state (`events-save 2`) no longer load, and every program's fingerprint changed.

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
- EVT-022 lists "panel page" and "actor poses". The format has no page, so the panels on screen are the page. A pose is the last looping animation; a move clears the facing; a warp clears actors and camera.
- The presentation state is what the event last commanded. A move cut short by a skip or a race is recorded at its target.
