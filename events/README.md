# events

An event scripting format and interpreter for cutscenes and field events. It is engine-neutral: plain C++17 and the standard library only, no exceptions across the public API, and no engine types. Writers author scenes as data (see [FORMAT.md](FORMAT.md)). An engine runs them by implementing one interface, `events::Host`.

## Layout

```
include/events/   public headers
	compiler.h        compile(sources) -> Program + diagnostics
	diagnostics.h     Diagnostic (file, line, severity, message)
	glossary.h        name glossary, name tokens, "show shipped names" rendering
	host.h            Host interface and the values passed to it
	interpreter.h     Interpreter: start, step, skip, save, restore
	lint.h            static checks on compiled scripts
	program.h         the compiled program (flat instruction list)
	recording_host.h  fake Host for tests and the CLI
	save_state.h      SaveState and its text form
	strings.h         string tables: load, resolve, check, extract
	timing.h          TimingHost and scene timing (pacing budget)
src/              lexer, parser, validator, codegen, interpreter, hosts, tools
tests/            one test executable with a minimal check helper
tools/event_cli.cpp
samples/          sample scenes (scripts only); between them they use every command
sample_text/      string tables, glossary, timing config and entry list for the samples
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

`lint`, `time`, `strings` and `names` are described for writers in [FORMAT.md](FORMAT.md). `time` also takes several files before the event name.

## Embedding

```cpp
events::CompileResult result = events::compile({{"harbor.ev", text}});
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

`step()` runs until every thread is blocked, then returns. A thread blocks on a timed `wait`, on a host handle that is not done yet (an action, a dialogue line or a choice), on a `together` block whose branches are still running, or on `sync` or the end of an event while `nowait` actions are still running. Time passed to `step()` only counts against waits that were already in progress when the step began. A wait that starts during a step begins counting at the next step.

Runtime problems put the interpreter in `Status::error`, with `error()` giving `file:line: message`. Examples are a host returning an out-of-range choice index, calls nested more than 32 deep, or 100,000 commands in one step without a wait. Everything in flight is cancelled when this happens.

`Program` is immutable after compiling, and one program can drive many interpreters.

## Implementing Host

Each engine implements `events::Host` once. In Unreal, for example, this is a `UObject` or subsystem that owns the handle table.

Handles. `start_action`, `show_line`, `present_choice` and `start_battle` return a non-zero `Handle`. The interpreter polls `is_done(handle)` every step. After `is_done` returns true it may read `result(handle)`, then calls `release(handle)`. If a skip or an error abandons the handle, it calls `cancel(handle)` instead, and never calls release after that. Each handle is released or cancelled exactly once, so the host can free its bookkeeping then. An instant action can return a handle that is already done.

- `show_line(Line)`: show the line. It is done when the player confirms it. An empty `speaker` means narration, and `portrait` is the expression tag. When `text.is_id` is true, `text.value` is a localization string ID.
- `present_choice(ChoiceRequest)`: done when the player picks. `result()` is the option index. For `yes_no` requests the options list is empty, and the host shows its own yes and no, where yes = 0 and no = 1.
- `start_battle(BattleRequest)`: done when the battle is over. `result()` is a `BattleResult` (see [Battles](#battles)).
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
| sound | cue | | | playback started |
| warp | map | marker | | map loaded, party placed |

- `get_flag`, `set_flag`, `get_var`, `set_var`, `give_item`, `take_item`, `add_party_member`, `remove_party_member` and `set_player_control` are synchronous and go to the game's own state. Unknown flags must read as false and unknown variables as 0.
- `snapshot_world()` returns the game state (flags, variables, items, party, player control and anything else the game saves) as an opaque string. The interpreter calls it when `start()` begins an event and keeps the result in `SaveState::start_world`. `restore_world(snapshot)` puts such a string back and returns false if it cannot. The interpreter calls it only when restoring a save made with different scripts (see the save contract).
- Line text may contain name tokens, `{name:<id>}`. Render them with `render_names(text, glossary, name_set_for(show_shipped_setting, text_language))` from `glossary.h` when the line is shown, so the "show shipped names" setting applies at once (TXT-016) and only to English (TXT-017). Resolve `@string.id` text with `resolve_text` from `strings.h` first.

The game's own state (flags, variables, items, party) lives in the host and is saved by the game's normal save system, not by the interpreter.

### Battles

A `battle` block calls `start_battle` with a `BattleRequest`:

- `encounter`: the encounter (enemy group) ID from the script.
- `escape_allowed`: true when the script has a `flee` section. When false, the battle must not offer escape.
- `defeat_handled`: true when the script has a `lose` section. When false, a defeat must go to the game's game-over flow, and the host never reports it to the interpreter. Typically the game-over flow loads a save, which throws this interpreter away.

The host forwards the request to whatever battle rules it uses: look up the encounter, set up the fight with escape on or off, run it, and when it ends map the outcome to `BattleResult::win`, `lose` or `flee`. The handle is done at that point. The interpreter has no other dependency on the rules. If the host reports `lose` when `defeat_handled` is false, or `flee` when `escape_allowed` is false, the interpreter stops with a runtime error, because the script has no branch for it. Rewards (experience, money, drops) are the rules' business and happen outside the script.

## Save-state contract

`Interpreter::save()` returns a `SaveState`. This is plain data (ints, doubles, vectors) that an engine can write to its own archive. `save_state_to_text` and `save_state_from_text` give a portable one-line text form. Save it next to the host's world state.

To resume, create an interpreter over the installed `Program` with the loaded host and call `restore(state, error)`. The rules:

1. A `SaveState` stores the program's fingerprint: a hash of every source file's name and tokens. Comments, blank lines and indentation are not part of it, so editing them keeps old saves resumable.
2. It also stores the outermost event's name and `start_world`, the host's `snapshot_world()` from when that event started (EVT-050). When the fingerprint differs, the saved positions mean nothing in the new program, so `restore` calls `host.restore_world(start_world)`, then starts that event again from its first command, and `restarted()` returns true (EVT-052). `restore` returns false with an error only if the event no longer exists, the host cannot restore the snapshot, or the save is corrupt. A save of an interpreter that was not running (idle or finished) restores as such, whatever the fingerprint.
3. Timers resume exactly. A save taken during `wait` keeps the remaining time, so the rest of the event plays identically.
4. Host handles are not saved. Whatever was in flight starts again on the first `step()` after restore:
   - A thread that was waiting on a line, choice, action or battle runs that command again. The player sees the line again, and the actor walks to the marker again from wherever the loaded game placed it.
   - `nowait` actions that were still running are started again.
   - Actions must therefore reach a well-defined end state when repeated. Moves go to a target, and fades end at a level. A restarted battle starts over, so save inside a battle with the battle system's own save, not this one.
5. When the scripts match, the world is restored by the game, not the interpreter. After loading, the host must already report the flags and variables as they were at save time.
6. Skip state is kept. A save taken while an `on skip` block runs restores into that block, and skip stays unavailable until it finishes. The running count of what the scene already gave, took and added (see "Events" in FORMAT.md) is saved too, so `on skip` still applies each change once.
7. Only save while an event runs for the autosave before an event battle (EVT-050). The game must not offer a manual save during an event (EVT-053).

The text form starts with `events-save 2`. Version 1 saves (before the fallback and the change count) are rejected by `save_state_from_text`.

### Skipping and the change count

`on skip` describes the scene's total effect. The interpreter keeps, per call depth, a running total of each item given or taken and each variable changed with `+=`/`-=`. A skip merges the totals of every event it unwinds, and each give, take or add inside `on skip` first uses up the matching total in the same direction, then applies only the rest. So a skip at any moment never applies a change twice, whether or not the writer guarded it with a flag. Flags, `var x = n`, party changes and player control are idempotent and are not counted.

This was chosen over a check-time rule that rejects unguarded gives in `on skip`. A guard flag only protects the change if the played part sets the flag in the same step it gives the item; a scene that gives first and sets the flag after a line still double-applies when skipped in between, and a checker cannot see that. Counting at run time covers that case, keeps old guarded scripts working, and asks nothing of the writer.

## Tools as a library

Every CLI command is a library call, so a build pipeline can run them without the CLI:

- `strings.h`: `load_string_table`, `resolve_text`, `collect_texts`, `check_strings`, `extract_strings`.
- `glossary.h`: `load_glossary`, `find_name_tokens`, `name_set_for`, `render_names`, `find_name_mentions`.
- `timing.h`: `TimingConfig`, `load_timing_config`, `TimingHost`, `time_event`. Feed `TimedPath::time.total` to the CI comparison with the measured baseline (EVT-062).
- `lint.h`: `lint(sources, LintOptions)`.

## Tests

`make test` runs `build/event_tests`, then every CLI command on the samples. The tests cover parse errors and their line numbers, each validation rule, branching, loops and goto, choices, waits spanning several steps, `together` branches finishing in either order, `nowait`/`sync`, nested calls, battle requests and results (including results the script did not allow), runtime errors, and handle discipline (no leaks, no use after release). The skip tests check that a skip at every step leaves the same world as playing through, with guarded and with unguarded `on skip` blocks, nested skips, and a save inside `on skip`. The save/restore tests cover an identical remaining trace after restoring mid-wait, a replayed line after restoring mid-dialogue, a restore at every step, the fingerprint ignoring comments, blank lines and indentation, a restart from the event-start world after a script change, and corrupt saves. The tool tests cover string table loading and checks, extraction, glossary loading, name rendering and the mention report, scene timing across branches, and each lint rule.
