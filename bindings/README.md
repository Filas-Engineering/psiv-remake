# bindings

A flat C interface to the headless game session in `../game` (with `../core` and `../events` behind it), a .NET wrapper over that interface, and a small MonoGame DesktopGL sample that plays the placeholder adventure through it. Nothing here changes the C++ libraries; they are built with their own Makefiles and linked in.

## Layout

| Path | Contents |
| --- | --- |
| `c/psiv_session.h` | The C interface: opaque `psiv_session`, plain structs and enums, `psiv_` functions |
| `c/psiv_session.cpp` | Its implementation over `game::Session`, built into the shared library |
| `c/exports_macos.txt`, `c/exports_linux.map` | Export lists, so only `psiv_*` symbols leave the library |
| `c/tests/playthrough.c` | `game_cli`'s command language rewritten against the C interface; prints the same transcript |
| `c/tests/test_api.c` | Focused tests: errors and data problems, string ownership, saves, a battle through the event stream, presentation, field and menus, and the adventure's newer paths (line data with portraits, localized yes and no, name entry, a lost event battle kept readable, replayed from its record and retried, path walking, pacing numbers), script actors (talk points, the arrival's walk under both presentations, saves and loads, another area, every actor id), and the menu operations (party order saved and loaded, the macro editor and its refusals, member detail, skill uses, and the combo log filled by an edited macro in the hold fight) |
| `csharp/Psiv.Session/` | .NET 10 class library: `LibraryImport` declarations (`Native.cs`), C# enums and records (`Types.cs`), `SessionHandle` and `GameSession` (`GameSession.cs`), `BattleText` and `ScriptRunner` (the command language again, over C#) |
| `csharp/Psiv.Session.Tests/` | xUnit tests: the scripted playthrough against the golden transcript and a fresh `game_cli` run, struct layouts, errors and data problems, saves, a battle, presentation, field and menus, the guide's line data and name entry, a lost event battle with its result, record, replay and retry, script actors in the field view, every actor id and during a waited move, and the menu operations (party order, the macro editor, member detail, skill uses, the combo log) |
| `monogame/Psiv.Sample/` | MonoGame DesktopGL sample with a headless mode: `SampleController` (screens), `FieldRenderer` (camera, tiles, sprites), `Art` (pictures generated in code), `Surface` (the drawing interface, its MonoGame and headless implementations, the shared panel style); `scripts/smoke.txt`, `scripts/large_areas.txt` and `scripts/actors.txt` are the headless runs `make test` does (`actors.txt` also plays the tide chart scene: party members walked and turned, poses, a resume and a skip) |
| `Makefile` | `make`, `make test`, `make clean` |

## Build

Needs `clang++`/`clang` and GNU Make 3.81 or later for the native layers, and the .NET 10 SDK for the rest. The Makefile looks for `dotnet` at `~/.dotnet/dotnet`; pass `DOTNET=/path/to/dotnet` to use another. When it is missing, `make` and `make test` do the native layers only, and `make test` says so.

```
make          # ../core, ../events, ../game through their Makefiles, then build/libpsiv_session.*, the C tests, and the .NET projects
make test     # C tests; the C playthrough compared with game_cli; then the C# tests and the sample's headless run
make clean    # build/ and the .NET bin/ and obj/ folders (the sibling libraries keep their builds)
```

`make test` runs, in order:

1. `build/c_tests`: the focused C tests.
2. `game_cli --seed 6 --auto-battle --script ../game/data/scripts/playthrough.txt` and `build/c_playthrough` with the same arguments, each with an empty save directory. The two transcripts must be identical byte for byte, must match `../game/tests/expected/playthrough.txt`, and every file the two runs wrote (autosaves, the manual save, the profile and the clear-data record) must be identical.
3. `dotnet test` on `csharp/Psiv.Session.Tests` (the C# playthrough is compared with both the golden transcript and a fresh `game_cli` run, saves included).
4. The sample in headless mode on `monogame/Psiv.Sample/scripts/smoke.txt` for 2000 frames, then on `scripts/large_areas.txt` (a walk through the 48x36 and 40x40 areas) for 1600 frames, then on `scripts/actors.txt` (the arrival's scripted walk, checked where the sample draws the actor, then the tide chart scene: party members walked and turned, each pose and the nod's and shake's motion marks, the scene resumed from its pre-battle autosave and skipped, the apprentices walking back into line after it without ever being drawn over the leader and the trail taking them over when the leader walks off first, and no action notice among the toasts) for 1300 frames, then on `scripts/party.txt` (the board scene: two members outside the lineup shown with their own sprites and no label, one walked in from the gate and one turned before any move and so put next to the leader, then added, walking back to the last two places in the line without ever being drawn over the leader, and a full party trailing four followers, while the peddler's wave from the reed path draws nothing) for 1100 frames; each fails on any failed expectation or unfinished script.

The first .NET build restores xUnit, `Microsoft.NET.Test.Sdk`, `MonoGame.Framework.DesktopGL` 3.8.2 and `FontStashSharp.MonoGame` 1.3.10 from the NuGet cache or nuget.org.

### macOS

As above. The library is `build/libpsiv_session.dylib` with install name `@rpath/libpsiv_session.dylib`; the C tests find it with `-rpath @loader_path`, and the .NET projects copy it next to their assemblies.

### Linux

`make` picks `build/libpsiv_session.so`, links it with `-shared` and the version script `c/exports_linux.map`, and builds the C tests with `-rpath '$ORIGIN'`. The C++ libraries end up inside a shared object; their own Makefiles build them with `-fPIC`, so nothing extra is needed. MonoGame DesktopGL needs SDL2 and OpenAL (shipped in its package for linux-x64 only; on aarch64 install `libsdl2-2.0-0` and `libopenal1`), the X11 libraries SDL2 loads and an OpenGL driver; the sample's font list covers DejaVu, Liberation and Noto. `../tools/linux-check.sh` runs the window under Xvfb with Mesa's llvmpipe software renderer on `scripts/smoke.txt` (see "Platforms" in `../README.md`); its package list is what a bare Ubuntu 24.04 needs.

### Windows

With clang and GNU Make from MSYS2 or similar (`OS=Windows_NT` selects the branch): `build/psiv_session.dll` plus the import library `build/libpsiv_session.dll.a`, exported through `__declspec(dllexport)` (only the `PSIV_API` functions). The tests link the import library and need the DLL next to them, which it is. The sibling Makefiles name their tools without `.exe`, so the build of `game_cli` may need their help on that platform. The .NET projects copy `psiv_session.dll` next to the assemblies. This Makefile branch has not been run; `../tools/windows-check.sh` builds the same DLL, the C tests and the C playthrough with MinGW-w64 directly and runs them under Wine. Paths given to the library (`data_dir`, `core_data_dir`, `save_dir`) are UTF-8 there as everywhere.

To use another build of the library from .NET, set the MSBuild property `PsivNativeDir` to the folder holding it.

## The C interface

### Groups

| Group | Functions |
| --- | --- |
| Version | `psiv_abi_version`, `psiv_version`, `PSIV_ABI_VERSION`, `psiv_sizeof` (struct sizes, for foreign bindings to check their layouts) |
| Errors | `psiv_last_error`, `psiv_status`, `psiv_data_problems` (every data load problem with its text key) |
| Names | `psiv_name_of(kind, value)`: the stable name of any enum value, as the C++ libraries spell it (lowercase with underscores); `psiv_parse_name(kind, text, &value)`, which also takes the older spellings of reward multipliers and event types |
| Session | `psiv_session_options_init`, `psiv_session_create`, `psiv_session_destroy`, `psiv_game_info_get` |
| Profile and settings | `psiv_profile_get`, `psiv_profile_set`, `psiv_profile_set_value`, `psiv_settings_get`, `psiv_settings_set`, `psiv_settings_set_auto_macro`, `psiv_macros` |
| Macro editor | `psiv_macro_list` (every macro with its steps: member, command, ability or item, target rule, names, and why a step cannot run now), `psiv_macro_create`, `psiv_macro_rename`, `psiv_macro_delete`, `psiv_macro_move`, `psiv_macro_set_step`, `psiv_macro_remove_step`, `psiv_macro_move_step` |
| Title and saves | `psiv_new_game`, `psiv_continue_game`, `psiv_load`, `psiv_retry_battle`, `psiv_quit_to_title`, `psiv_save_manual`, `psiv_saves` |
| State | `psiv_mode_get`, `psiv_menu_kind_get`, `psiv_world_info_get`, `psiv_flag` |
| Frame and field | `psiv_update(dt_ms, direction)`, `psiv_walk`, `psiv_face`, `psiv_walk_to` and `psiv_walk_step` (the game's path walk), `psiv_interact`, `psiv_party_talk`, `psiv_field_info_get`, `psiv_field_grid`, `psiv_field_rows`, `psiv_field_enemies`, `psiv_field_markers`, `psiv_field_exits`, `psiv_field_points`, `psiv_field_actors`, `psiv_actor_ids` |
| Events | `psiv_line_pending`, `psiv_line_get` (speaker id and name, text ID, rendered text, portrait), `psiv_line_confirm`, `psiv_choice_pending`, `psiv_choice_get` (an ask's answers from the dialogue tables), `psiv_choose`, `psiv_can_skip`, `psiv_skip` |
| Presentation | `psiv_actions_poll`, `psiv_action_finish`, optional `psiv_actions_set_callback` |
| Notices | `psiv_notices_drain` |
| Battle | `psiv_battle_info_get`, `psiv_battle_combatants`, `psiv_battle_options`, `psiv_battle_set_command`, `psiv_battle_apply_macro`, `psiv_battle_start_auto`, `psiv_battle_cancel_auto`, `psiv_battle_fill_from_auto`, `psiv_battle_fill_from_policy`, `psiv_battle_fill_auto_round`, `psiv_battle_resolve_round`, `psiv_battle_last_events`, `psiv_battle_play` (a whole battle the way the terminal plays it), `psiv_battle_result_get` and `psiv_battle_dismiss` (the ended battle), `psiv_battle_record` and `psiv_battle_replay` |
| Party, menus, shops | `psiv_party`, `psiv_party_swap`, `psiv_member_detail_get`, `psiv_member_abilities`, `psiv_combo_log`, `psiv_inventory`, `psiv_shop_get`, `psiv_inn_get`, `psiv_open_menu`, `psiv_close_menu`, `psiv_buy`, `psiv_sell`, `psiv_rest`, `psiv_use_item`, `psiv_use_ability`, `psiv_equip`, `psiv_unequip`, `psiv_name_entry_member`, `psiv_enter_name` |
| Journal and text | `psiv_quest_log_get`, `psiv_encyclopedia`, `psiv_glossary`, `psiv_text_log`, `psiv_pacing_table`, `psiv_pacing_areas`, `psiv_pacing_bosses`, `psiv_render`, `psiv_format_text`, `psiv_display_name`, `psiv_string_text` |

### Errors

Every function returns a `psiv_status`, or a plain value where it cannot fail. On anything but `PSIV_OK`, `psiv_last_error(session, &error)` gives the status, a message key, its parameters and the text rendered in the profile's language:

* `PSIV_REFUSED`: the game said no. The key is the game's (`game.refuse.*`) or core's (`equip.*`, `field.*`, battle command keys), exactly what `game::Session` returned.
* `PSIV_INVALID_ARGUMENT`, `PSIV_NOT_FOUND`: the binding's own checks, keys `binding.*` with English text (`binding.null_argument` names the argument, `binding.out_of_range` the argument and the value). `PSIV_NOT_FOUND` also covers reads of state that is not there now: no area, no line, no battle, no shop.
* `PSIV_DATA_ERROR`: `psiv_session_create` could not load the data; the text is the loader's list of problems, and `psiv_data_problems` gives each with its file, line, entry, text key and parameters. Also `psiv_battle_replay` on a record that does not parse or replay (key `binding.bad_record`).
* `PSIV_INTERNAL_ERROR`: an exception inside the library (out of memory and the like). No C++ exception crosses the interface.

`psiv_last_error(NULL, ...)` reports the last failed `psiv_session_create` on the calling thread.

### Ownership

* **Handles**: `psiv_session_create` returns a session the caller frees with `psiv_session_destroy`. The session owns its loaded data. One thread at a time.
* **Returned pointers** (strings, arrays, the contents of structs the library fills): owned by the session, valid until the next call to a function marked `MUTATES` in the header, or until the session is destroyed. Reads never invalidate earlier reads, so a frame calls `psiv_update` once and then reads everything it draws. String fields are never NULL; `""` means none.
* **Arguments**: copied before anything else happens, so a pointer the library returned can be passed back in, even to a `MUTATES` function. The library keeps no pointer of the caller's.
* **Errors**: the record `psiv_last_error` fills has its own storage, valid until the next failing call.
* **Callbacks**: the optional action callback gets a record valid only during the call, and may call `psiv_action_finish` and nothing else.

All strings are UTF-8. Booleans and enums inside structs are `int32_t`, so layouts do not depend on the compiler's enum size; `psiv_sizeof("psiv_battle_event")` and friends let a foreign binding check its mirrors (the C# tests do).

### How the frame loop maps to the session

Each frame:

1. `psiv_update(session, dt_ms, held_direction)`. In the field it runs fixed ticks (`tick_ms`) and walks the leader one cell per `step_ms` of held input; a direction held for less than a tick still counts at the next tick, so a tap turns the leader. In an event it steps the interpreter. Battle and title ignore it.
2. `psiv_mode_get` says what to present:
   * Field: `psiv_field_info_get`, `psiv_field_grid`, `psiv_field_enemies`, `psiv_field_points`, `psiv_field_exits`, `psiv_field_actors` (the script actors in the area: where each stands, its facing, whether it is visible; one no script touched stands at the talk point named after it, see `../game/README.md`, "Actors"). `psiv_actor_ids` lists every actor wherever it stands, so a script subject in it but not in `psiv_field_actors` is an actor in another area, and any other subject a character. Act with `psiv_interact`, `psiv_open_menu`, `psiv_party_talk`; `psiv_face` turns in place. For click-to-walk, `psiv_walk_to(x, y)` and then `psiv_walk_step` each frame (or in a loop, running events in between) until it reports anything but `PSIV_WALK_WALKING`.
   * Event: `psiv_line_get` and `psiv_line_confirm`; `psiv_choice_get` and `psiv_choose`; `psiv_can_skip` and `psiv_skip`. With neither a line nor a choice, keep calling `psiv_update`.
   * Battle: `psiv_battle_combatants` and `psiv_battle_options` for the menus, `psiv_battle_set_command` per member (or `psiv_battle_apply_macro`, or `psiv_battle_start_auto` then `psiv_battle_fill_from_auto` each round, or `psiv_battle_fill_auto_round`), then `psiv_battle_resolve_round`, whose typed events the front end plays back. `psiv_battle_play` runs a whole battle at once. The battle can end inside those calls; the mode then says where control went, and `psiv_battle_info_get`, `psiv_battle_combatants`, `psiv_battle_result_get` (rewards paid out, drops, level-ups) and `psiv_battle_record` describe the ended battle, which the game keeps until the next battle starts, `psiv_battle_dismiss`, a new game, a load or the title.
   * Menu (`psiv_menu_kind_get`: field menu, inn, save prompt, name entry) and Shop: the menu and shop functions, then `psiv_close_menu`. From the field menu: party order (`psiv_party_swap`), the macro editor (`psiv_macro_*`), status screens (`psiv_party`, `psiv_member_detail_get`, `psiv_member_abilities`) and the combo log (`psiv_combo_log`, with the recipe count for locked entries).
   * Title: `psiv_new_game`, `psiv_continue_game`, `psiv_load`, `psiv_retry_battle`.
3. `psiv_notices_drain`: arrivals, rewards, autosaves, party talk lines, warnings, each with key, parameters and text.
4. `psiv_actions_poll`: presentation actions (moves, fades, cameras, music), from the game's own action queue. With `PSIV_PRESENTATION_INSTANT` (the default) they are already done and only tell the engine what happened (the queue keeps the last 256). With `PSIV_PRESENTATION_WAIT` the event waits on each until the engine calls `psiv_action_finish(handle)` when its animation ends (a script actor's move: `psiv_field_actors` already reports where it ends, so walk the sprite there from where it was drawn and finish the action on arrival); `instant` records are done at once (the rebuild after a skip or a restore), and `PSIV_ACTION_CANCEL` records name actions a skip abandoned.

## The C# wrapper

```csharp
using GameSession s = GameSession.Create(new SessionOptions { DataDirectory = "../game/data", SaveDirectory = "saves", Seed = 7 });
s.NewGame().ThrowIfFailed();
while (running)
{
	s.Update(dtMs, heldDirection);
	switch (s.Mode)
	{
	case Mode.Field: Draw(s.Field()!); break;
	case Mode.Event: if (s.Line() is { } line) ShowLine(line); else if (s.Choice() is { } c) ShowChoice(c); break;
	case Mode.Battle: /* s.Combatants(), s.BattleOptions(i), s.SetCommand(...), s.ResolveRound() */ break;
	}
	foreach (Notice n in s.DrainNotices()) Show(n.Text);
	foreach (PresentationAction a in s.PollActions()) Start(a);
}
```

* `GameSession` owns a `SessionHandle : SafeHandle` and is `IDisposable`. A disposed session throws `ObjectDisposedException`.
* Every value it returns is a managed copy (records such as `FieldView`, `Line`, `Combatant`, `BattleEvent`, `Member`, `ShopInfo`), so the C library's pointer lifetime rule never reaches C# code.
* Game operations return `PsivResult`: `Ok`, or `Error` (a `PsivError` with status, key, parameters and text) when the game refused; `ThrowIfFailed()` turns it into an exception. Bad arguments and internal errors throw `PsivException`; data that does not load throws `PsivDataException`, whose `Problems` list every load problem with its text key. Reads of state that is not there return `null` (`FieldInfo()`, `Line()`, `Shop()`, `BattleInfo()`).
* Enums mirror the C enums; `GameSession.NameOf(kind, value)` gives the library's names.
* `SetActionCallback` is the optional push delivery, through an `UnmanagedCallersOnly` function pointer; the polled `PollActions` is the main path.
* `Field()` returns the whole field view, script actors included (`FieldView.Actors`, records of `FieldActor`); `ActorIds()` lists every actor id wherever the actor stands.
* The game's walk and auto play are `WalkTo`/`WalkStep`, `FillAutoRound` and `PlayBattle`; the ended battle is `BattleInfo()`, `Combatants()`, `BattleResult()` and `DismissBattle()`; records are `BattleRecord()` and `ReplayBattle(record)`; pacing is `PacingTable()`, `PacingAreas()` and `PacingBosses()`. `SessionOptions.CoreDataDirectory` gives core's data directory.
* The field menu's screens: `SwapLineup`, `MacroList` (records of `Macro` and `MacroStep`), `CreateMacro`, `RenameMacro`, `DeleteMacro`, `MoveMacro`, `SetMacroStep`, `RemoveMacroStep`, `MoveMacroStep`, `MemberDetail(member)`, `MemberAbilities(member)` (with skill uses) and `ComboLog()`.
* `Create` refuses a library whose ABI version differs from the wrapper's.

## The sample

```
dotnet run --project monogame/Psiv.Sample -- [--data DIR] [--saves DIR] [--seed N] [--font FILE] [--exit-after-ms N] [--screenshot FILE.png]
dotnet run --project monogame/Psiv.Sample -- --script FILE --frames N [--saves DIR]      # headless: no window
dotnet run --project monogame/Psiv.Sample -- --window --script FILE [--screenshot FILE.png]   # the window, keys from the script
```

The data directory is found above the binary or the working directory when `--data` is left out.

Keys: arrows walk (a tap turns), Enter/Z/Space confirm and talk, Escape/X back, M menu, Q quest log, T party talk, L text log, Tab skip. In battle, Up/Down and Enter pick commands and targets, Enter hurries the event playback and Escape stops auto-battle; in the shop, Left/Right switch between buying and selling; in the field menu, Left/Right change tabs.

What is drawn:

* Field: the whole window is the map, drawn in 48 px cells (16 px pictures at 3x), so 1280x720 shows about 26 by 15 cells. The camera follows the party leader, stops at the area's edges and centers an area smaller than the window. The game moves everyone a whole cell per step; the sample glides each sprite between cells over the area's `step_ms`, starting when the session reports the new cell, so the view scrolls smoothly and game timing is untouched. Tiles depend on the area kind (town: cobbles, slate roofs, plaster walls with windows; field: grass, hedges; dungeon: flagstones, rock, brick). Floors pick one of four variants and an occasional decoration (flowers, pebbles, cracks, moss, a grate) from a hash of the cell's coordinates. A wall with a non-wall cell below shows its front face, others their top, with dark edges toward open cells and a shadow on the floor under them. Water has four frames, a bank on its top edge and foam on the others. Exits on the area's edge are pulsing arrows pointing out, others a doorway; chests are closed or open (`active`), shops a stall, rests a bed, and active step hooks a faint rune. Script actors (`psiv_field_actors`) are people with four facings and a two-frame walk; hidden ones are not drawn. When a script moves one, the session reports its end cell at once and the sample walks the sprite there a cell at a time, along x and then y as the game does, one area step time per cell (divided by the move's speed), and finishes the move action when it arrives, so the event waits on the walk; anything else that changes an actor's cell (a load, a skip) puts it there at once. A script's `move` or `face` of a party member plays the same way, but the session reports nothing for it: the game keeps only the leader's cell, which scripts do not move, so where a scene puts party members is presentation (see `../game/README.md`, "Actors"). The sample follows each such action as it starts: a move walks the member from where it is drawn to the marker (or the position's x and z, rounded) and leaves it facing its last leg, a turn faces it toward a marker, an actor or another member along the longer axis, as the game turns actors. The script actors in the party's area are all in `psiv_field_actors` and every actor is in `psiv_actor_ids`, so an action whose subject is an actor outside the party's area is finished at once with nothing drawn, not even a label, and any other subject of a move, turn or `anim` is a party member. A member outside the lineup has no cell until a script's first move puts it on the marker at once (as the game puts an actor without a talk point), or a turn or `anim` that comes first puts it next to the leader: on the nearest walkable cell that no drawn person, actor or member stands on or walks to, trying the cell behind the leader (against its facing), then its right and its left, then the cell in front, and further out in the same order when those are all taken. From then on it is placed like any other member. The member stays there, out of the walking line, until the event ends. Then the leader is drawn at its own cell at once, since the player has it back; each other lineup member the scene moved walks back to its place in the line, a cell per area step, and turns the way the leader faces when it arrives; members outside the lineup are no longer drawn. The walk back takes the shortest path over walkable cells that avoids the leader, the cells other drawn members stand on or are going to and the visible actors (trying north, east, south and west from each cell, and at most six cells longer than the straight walk); only when there is none does it go along x and then y as scripted moves do. Player control is not held back: if the leader takes a step before they arrive, the trail takes each of them over from where it is drawn, gliding a step time per cell of the gap. After a skip or a load the session's instant actions (the interpreter's rebuild: each subject's last move, its later facing, its looping pose) put the members where watching left them. An `anim` plays a pose on an actor or a party member: nod (the head dips two pixels twice, tipping forward at the bottom, with a stroke beside each side of the crown), shake (the head turns to show its profile one way and the other, or toward the viewer from the side, with an arc on each side of it), jump (a hop), startle (arms up, a hop and a `!`), bow (head and shoulders lowered and held), wave (an arm up, the hand swinging), and for any other name two small bounces. `Art.PoseFor` maps script animation names to poses in one place. A pose keeps the person's facing; the event waits for one play of it (480 to 900 ms), and a looping `anim` repeats until another `anim` for that person or the end of the event and is finished at once, since the script does not wait for loops. A speech bubble waits while its actor plays a pose. A talk point named after an actor shows that actor, with a speech bubble while the point has something to say and the actor stands on it; any other talk point is a noticeboard with the bubble. A bubble is left out while the leader stands on the cell it covers. Visible enemies are creatures colored by encounter, bobbing, with a `!` while they chase. The leader is the first member of the lineup the session reports, with four facings and a two-frame walk; the other active members follow in lineup order (all four followers of a full party) in the leader's last cells. The session does not report follower positions, so follower cells are presentation only: the trail is the sample's own and starts over when the leader is moved more than one cell at once. Sprites are drawn top row first, party, actors and enemies together. A banner names the area for a moment on arrival; the area name, money and play time sit at the top, notices stack at the bottom left, the controls at the bottom.
* Dialogue: while a line or choice is up, a bordered box at the bottom (at the top when the leader stands where it would cover them), the speaker's name on a plate, a portrait from the speaker's look, wrapped text, choices with a cursor bar, skip.
* Battle: a backdrop for the area kind (sky, hills, rooftops or a cave, and the ground tiles), the enemies standing on it with names, statuses and HP bars, the event log across the top (newest line brightest), a panel per party member (portrait, statuses, HP and TP bars, the command already chosen) with the acting member highlighted and a pointer or a pulsing frame on the target, and the command menu (attack, technique, skill, item, defend, escape, macro and auto). The event stream plays back one line every 350 ms.
* Field menu (party and field techniques, items, equipment, manual save, quests, encyclopedia, text log), shop (buy and sell), inn, save prompt, name entry, quest log and text log: one window style over the dimmed field, with tabs and a cursor bar.
* Title: a field scene behind the menu.

Every picture is generated at startup by `Art` from code: palettes, a few 16x16 character templates (people, creatures) and procedural tiles from a seeded generator. There are no asset files. Every person, party member or actor, looks the same in the field, the dialogue portrait and the battle panel: hair, skin, coat and scarf colors, headwear (none, a cap, a hood, long hair or a headband) and clothes (a coat, an apron or a belt) all come from a hash of its id. Creature colors and horns come from a hash of the encounter or enemy id.

The sample plays script actions with the asynchronous presenter (`Presentation.Wait`): each action becomes a short timed animation (a move, turn or `anim` of a script actor or a party member plays on its sprite as above; otherwise its own length held between 150 and 600 ms: a fade darkens or clears the screen, anything else slides a label with its subject across the top), and the controller calls `FinishAction` when it ends, so events wait on them. Since every action shows on screen, the session's action notices (`(face dockhand dock_front)` and the like) do not become toasts; they stay in the headless transcript as `notice:` lines. Other notices (arrivals, autosaves, items, shop results, refusals) still show as toasts. Animations advance by the frame time the session gets, so headless runs, at a fixed 16 ms per frame, stay deterministic. A direction tap goes to the session as held input for one frame; the game turns the leader at its next tick.

Text is drawn with FontStashSharp from the first system font found in a fallback list (Arial, Verdana, Geneva on macOS; DejaVu, Liberation, Noto on Linux; Segoe UI, Arial, Tahoma on Windows; CJK faces after them as fallbacks for missing glyphs). `--font FILE` or `PSIV_FONT` puts a font first.

Headless scripts are lines of `press KEY [COUNT]`, `hold KEY FRAMES`, `wait FRAMES`, `pressuntil KEY SCREEN MAXFRAMES`, `cmd <game_cli command>` (to walk to a marker, say), `expect screen NAME`, `expect mode NAME`, `expect actor ID X Y [FACING]` (the last frame drew the person, a script actor or a party member, standing on that cell, facing north, east, south or west if given), `expect actor ID walking` (drew it between cells), `expect actor ID hidden` (did not draw it), `expect pose ID POSE [marked]` (drew it in that pose: `none`, `nod`, `shake`, `jump`, `startle`, `bow`, `wave` or `other`; with `marked`, with the pose's motion marks), `expect label none` (no action shows as a sliding label), `expect apart ID FRAMES` (the last frame and the next FRAMES frames, played with no keys, draw nobody else less than a cell from the person on both axes), `expect toast TEXT` and `expect toast none` (a toast holding that text is up, or none is), `expect notoast TEXT` (no toast holds that text), `dump` (the text of the last frame) and `shot FILE.png` (takes one frame with no keys; with `--window` that frame is saved as a PNG, headless runs write nothing). Frames are 16 ms. The run fails on an unmet expectation, a failed command or a script that does not finish in the frame budget. A `cmd` runs to completion inside one frame, so it must not start a script action, which would wait for animations no frame plays.

### Replacing the drawing

The sample keeps the game out of its drawing code: `SampleController` reads the session after each update and draws through `IDrawSurface` (`Fill` a rectangle, draw `Text`, `Measure` text, `CreateImage` from a pixel array and draw it scaled with `Image`). `MonoGameSurface` implements it with a `SpriteBatch`, one texture per image and FontStashSharp; `RecordingSurface` is the headless one, which checks image handles and sizes, counts draws and needs no graphics device. Animation runs on a clock that advances only by the frame times the controller gets, so headless runs stay deterministic. To put real art behind the same screens, change what `Art` hands the surface (load sprite sheets instead of generating pixels), implement `IDrawSurface` over another renderer, or replace a screen's `Draw*` method and keep its `Update*` method, which holds all the session calls. For a real game, keep the frame loop of the section above and write the screens from scratch: everything they need comes from the records `GameSession` returns, and animations hook in through `Presentation.Wait` and `FinishAction`.

## Changes a host must make

This round: `PSIV_ABI_VERSION` 6, library version 0.5.0. One struct layout changed; everything else is an addition.

* Struct layout: `psiv_ability_info` adds `uses` and `max_uses` at the end (a skill's uses left and after a rest, -1 for other kinds). Rebuild foreign mirrors of it.
* New party order: `psiv_party_swap(a, b)` swaps two lineup positions from the field menu (refusals `game.refuse.party_menu_only`, `game.refuse.party_no_position`). Saves already kept the lineup's order. There is no swap with members outside the lineup: the party changes only through events (SAV-034).
* New macro editor: `psiv_macro`, `psiv_macro_step`, `psiv_macro_target_kind` (`PSIV_NAME_MACRO_TARGET_KIND` for its names), `psiv_macro_list`, `psiv_macro_create`, `psiv_macro_rename`, `psiv_macro_delete`, `psiv_macro_move`, `psiv_macro_set_step`, `psiv_macro_remove_step`, `psiv_macro_move_step`. Changes are refused outside the field menu (`game.refuse.macro_menu_only`); the header lists every refusal key. Deleting the auto macro turns auto-battle's macro off and queues the notice `game.auto_macro_cleared`; renaming it keeps it as the auto macro.
* New status screen data: `psiv_member_detail_get` with `psiv_member_detail`, `psiv_stat_bonus`, `psiv_affinity` and `psiv_status_resist` (experience to the next level, critical and evasion, the weapon's element, each slot's bonus, element affinities and status resistances).
* New combo log: `psiv_combo_log` with `psiv_combo_entry` (recipe, combination name, play time, participants and required abilities with names) and the number of recipes.
* `psiv_name_kind` adds `PSIV_NAME_MACRO_TARGET_KIND` at the end; the game's text tables add the keys above.
* C#: `Native.AbiVersion` is 6; `AbilityInfo` adds `Uses` and `MaxUses`; new `MacroTargetKind`, `NameKind.MacroTargetKind`, the records `Macro`, `MacroStep`, `MemberDetail`, `StatBonus`, `StatusResist`, `ComboEntry` and `ComboLog`, and the `GameSession` methods `SwapLineup`, `MacroList`, `CreateMacro`, `RenameMacro`, `DeleteMacro`, `MoveMacro`, `SetMacroStep`, `RemoveMacroStep`, `MoveMacroStep`, `MemberDetail` and `ComboLog`.

The round before: `PSIV_ABI_VERSION` 5, library version 0.4.0. Only an addition: the version moved so that a wrapper refuses an older library without the new function.

* New `psiv_actor_ids`: every actor id in `game.json` order, wherever the actor stands, at the title too. A front end that draws script subjects tells an actor in another area (in this list, not in `psiv_field_actors`) from a party member (in neither) and draws nothing for the first.
* C#: `Native.AbiVersion` is 5; new `Native.ActorIds` and `GameSession.ActorIds()`.
* The placeholder board scene starts with `anim peddler wave`, an `anim` of an actor that stands on the reed path while the party is in Tidewater. The game changes nothing for it (it never tracks `anim`).
* On Windows, `data_dir`, `core_data_dir` and `save_dir` with characters outside ASCII now reach the files they name: the libraries took them in the ANSI code page, not as UTF-8.

Two rounds before: nothing. The C interface, `PSIV_ABI_VERSION` 4, the C# wrapper and the saves were unchanged; the sample and the placeholder data changed. The tidewater area has seven more markers and two more talk points (the tide chart and the board by the inn), which the C tests count. The board's scene fills the party to five, so a front end that drew a fixed number of followers should draw them all. In it the engineer walks in from the east gate, while the scout is turned toward the leader and nods before any move, so a front end that draws party members has to give such a member a place of its own (the `board_beside` marker is no longer used).

Three rounds before: `PSIV_ABI_VERSION` 4, library version 0.3.0. Only additions: the version moved so that a wrapper refuses an older library without the new function.

* New `psiv_actor` and `psiv_field_actors`: the script actors in the party's area, with id, speaker name, cell, facing (a `psiv_direction`), `visible` and `scripted`. `psiv_sizeof("psiv_actor")` gives its size.
* Saves are written as schema 3 with an `actors` chunk; schema 2 saves load (see `../game/README.md`, "Saves").
* With `PSIV_PRESENTATION_WAIT`, the placeholder arrival now waits on three actions (two moves and a turn of the dockhand) between its narration and the dockhand's line, and three more after it.
* C#: `Native.AbiVersion` is 4; new `PsivActor`, `Native.FieldActors` and the `FieldActor` record; `FieldView` takes `Actors` as its last parameter.

Four rounds before: `PSIV_ABI_VERSION` 3. Core's rules version 9 removed two battle event types.

* `psiv_event_type`: `PSIV_EVENT_STATUS_TICKED` and `PSIV_EVENT_STAT_MODIFIER_EXPIRED` are gone, and every later value moved down (`PSIV_EVENT_STAT_MODIFIED` is now right after `PSIV_EVENT_STATUS_REMOVED`). A `PSIV_EVENT_STAT_MODIFIED` `amount` is the stat's change from its base, and there is no expiry. C#: `BattleEventType.StatusTicked` and `StatModifierExpired` are gone; `Native.AbiVersion` is 3.
* `psiv_battle_set_command` refuses Defend and items for a vehicle (key `command.vehicle_cannot`).
* The session tests now use seed 285 (`bindings/c/tests/test_api.c`, C# `SessionTests.NewSession`).

Five rounds before: `PSIV_ABI_VERSION` 2. The binding now calls the game for everything it used to work around (line data, turning, the auto macro, the ended battle, yes and no, presentation, walking, auto play).

* Struct layouts: `psiv_session_options` adds `core_data_dir` after `data_dir`; `psiv_line` adds `text_id` after `speaker_name`. Call `psiv_session_options_init` and rebuild foreign mirrors (the C# wrapper's are updated).
* Names: `psiv_name_of` returns event types as `battle_started` ... `battle_ended` and reward multipliers as `x1`, `x1_5`, `x2`. Use `psiv_parse_name` to read a name back; it also takes `BattleEnded` and `1.5`.
* `psiv_choice_get`: an ask's options are now the dialogue table's `choice.yes` and `choice.no` in the profile's language (`Yes`, `No` in English) instead of `yes`, `no`.
* `psiv_settings_set_auto_macro`: an unknown macro is `PSIV_REFUSED` (key `game.refuse.no_macro`), no longer `PSIV_NOT_FOUND`.
* `psiv_face`: also refused while a script holds player control.
* `psiv_battle_info_get` and `psiv_battle_combatants` after a battle: the game's final state of the ended battle (its round, defending flags and statuses as the battle left them; commands empty), kept until the next battle, `psiv_battle_dismiss`, a new game, a load or the title.
* Party combatants are named with the name chosen at name entry, in `name` and `label`.
* New: `psiv_parse_name`, `psiv_data_problems` and `psiv_problem`, `psiv_walk_to`, `psiv_walk_step` and `psiv_walk_status`, `psiv_battle_fill_auto_round`, `psiv_battle_play` with `psiv_auto_battle` and `psiv_auto_round`, `psiv_battle_result_get` with `psiv_battle_result` and `psiv_level_up`, `psiv_battle_dismiss`, `psiv_battle_record`, `psiv_battle_replay`, `psiv_pacing_areas` and `psiv_pacing_bosses` with `psiv_pacing_area` and `psiv_pacing_boss`.
* C#: `Line` adds `TextId`; `SessionOptions` adds `CoreDataDirectory`; a data error throws `PsivDataException`; new `ParseName`, `WalkTo`, `WalkStep`, `FillAutoRound`, `PlayBattle`, `BattleResult`, `DismissBattle`, `BattleRecord`, `ReplayBattle`, `PacingAreas`, `PacingBosses` and their records.
* Build: the sibling libraries are always position-independent; the Linux branch no longer passes `CXX="clang++ -fPIC"`.

## Not covered

* Windows: the C library and its C tests pass cross-compiled with MinGW-w64 under Wine; MSVC, this Makefile's Windows branch, the C# wrapper, the sample and its font list have not run there. Linux passes `make test` with g++ and clang++ and runs the sample's window under Xvfb with software rendering (see "Platforms" in `../README.md`), but not in a real desktop session.
* The pause overlay, making a macro from a combo log entry (BAT-065) and anything else `game` itself does not build (see `../game/README.md`, "Not built").
* The pacing bot and the events tools have no C entry points.
* One session per data load: two sessions load the data twice.
* Mouse or gamepad input, resizing, audio: the sample is keyboard only, at 1280x720. Script actions other than fades, `anim` and moves and turns of actors and party members (and shows and hides of actors) show as a label sliding across the top; poses are procedural, and every animation name the sample does not know plays the same bounce. After a skip or a load, a party member's facing that only a move's last leg set follows from where it stood when the rebuild ran, as for actors in the game.
