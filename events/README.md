# events

An event scripting format and interpreter for cutscenes and field events. It is engine-neutral: plain C++17 and the standard library only, no exceptions across the public API, and no engine types. Writers author scenes as data (see [FORMAT.md](FORMAT.md)). An engine runs them by implementing one interface, `events::Host`.

## Layout

```
include/events/   public headers
	compiler.h        compile(sources) -> Program + diagnostics
	diagnostics.h     Diagnostic (file, line, severity, message)
	host.h            Host interface and the values passed to it
	interpreter.h     Interpreter: start, step, skip, save, restore
	program.h         the compiled program (flat instruction list)
	recording_host.h  fake Host for tests and the CLI
	save_state.h      SaveState and its text form
src/              lexer, parser, validator, codegen, interpreter, hosts
tests/            one test executable with a minimal check helper
tools/event_cli.cpp
samples/          sample scenes; between them they use every command
```

## Build and test

Needs `clang++` and `make`. Nothing else.

```
make          # build/event_cli and build/event_tests
make test     # runs the tests, then checks samples/*.ev
make clean
```

All code builds with `-std=c++17 -Wall -Wextra -Werror`.

The CLI:

```
build/event_cli check samples/*.ev
build/event_cli run samples/harbor.ev harbor_arrival --choose 0,2
build/event_cli run samples/lighthouse.ev lighthouse_ambush --battle lose --skip-at 3
```

`run` steps the event at 0.1 s (`--dt`) against the recording host and prints each host call with its step number, then the final flags, variables, items, party and player control. `--choose` and `--battle` script the answers in order. `--skip-at N` asks for a skip before step N.

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

Handles. `start_action`, `show_line` and `present_choice` return a non-zero `Handle`. The interpreter polls `is_done(handle)` every step. After `is_done` returns true it may read `result(handle)`, then calls `release(handle)`. If a skip or an error abandons the handle, it calls `cancel(handle)` instead, and never calls release after that. Each handle is released or cancelled exactly once, so the host can free its bookkeeping then. An instant action can return a handle that is already done.

- `show_line(Line)`: show the line. It is done when the player confirms it. An empty `speaker` means narration, and `portrait` is the expression tag. When `text.is_id` is true, `text.value` is a localization string ID.
- `present_choice(ChoiceRequest)`: done when the player picks. `result()` is the option index. For `yes_no` requests the options list is empty, and the host shows its own yes and no, where yes = 0 and no = 1.
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
| battle | encounter | | | battle over; `result()` is a `BattleResult` |

- `get_flag`, `set_flag`, `get_var`, `set_var`, `give_item`, `take_item`, `add_party_member`, `remove_party_member` and `set_player_control` are synchronous and go to the game's own state. Unknown flags must read as false and unknown variables as 0.

The game's own state (flags, variables, items, party) lives in the host and is saved by the game's normal save system, not by the interpreter.

## Save-state contract

`Interpreter::save()` returns a `SaveState`. This is plain data (ints, doubles, vectors) that an engine can write to its own archive. `save_state_to_text` and `save_state_from_text` give a portable one-line text form. Save it next to the host's world state.

To resume, create an interpreter over the same `Program` with the loaded host and call `restore(state, error)`. The rules:

1. Same scripts only. A `SaveState` stores the program's fingerprint, a hash of every source file's name and text. If any script changed, even a comment, `restore` returns false with an error. Have the game fall back, for example by applying the event's `on skip` result or restarting the event.
2. Timers resume exactly. A save taken during `wait` keeps the remaining time, so the rest of the event plays identically.
3. Host handles are not saved. Whatever was in flight starts again on the first `step()` after restore:
   - A thread that was waiting on a line, choice, action or battle runs that command again. The player sees the line again, and the actor walks to the marker again from wherever the loaded game placed it.
   - `nowait` actions that were still running are started again.
   - Actions must therefore reach a well-defined end state when repeated. Moves go to a target, and fades end at a level. A restarted battle starts over, so save inside a battle with the battle system's own save, not this one.
4. The world is restored by the game, not the interpreter. After loading, the host must already report the flags and variables as they were at save time.
5. Skip state is kept. A save taken while an `on skip` block runs restores into that block, and skip stays unavailable until it finishes.

## Tests

`make test` runs `build/event_tests`. It covers parse errors and their line numbers, each validation rule, branching, loops and goto, choices, waits spanning several steps, `together` branches finishing in either order, `nowait`/`sync`, nested calls, battle results, runtime errors, and handle discipline (no leaks, no use after release). The skip tests check that a skip at every step leaves the same world as playing through, including a skippable event called from an unskippable one. The save/restore tests cover an identical remaining trace after restoring mid-wait, a replayed line after restoring mid-dialogue, a restore at every step, and rejection of saves from other scripts.
