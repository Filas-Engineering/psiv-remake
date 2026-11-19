# bindings

A flat C interface to the headless game session in `../game` (with `../core` and `../events` behind it), a .NET wrapper over that interface, and a small MonoGame DesktopGL sample that plays the placeholder adventure through it. Nothing here changes the C++ libraries; they are built with their own Makefiles and linked in.

## Layout

| Path | Contents |
| --- | --- |
| `c/psiv_session.h` | The C interface: opaque `psiv_session`, plain structs and enums, `psiv_` functions |
| `c/psiv_session.cpp` | Its implementation over `game::Session`, built into the shared library |
| `c/exports_macos.txt`, `c/exports_linux.map` | Export lists, so only `psiv_*` symbols leave the library |
| `c/tests/playthrough.c` | `game_cli`'s command language rewritten against the C interface; prints the same transcript |
| `c/tests/test_api.c` | Focused tests: errors, string ownership, saves, a battle through the event stream, presentation, field and menus |
| `csharp/Psiv.Session/` | .NET 10 class library: `LibraryImport` declarations (`Native.cs`), C# enums and records (`Types.cs`), `SessionHandle` and `GameSession` (`GameSession.cs`), `BattleText` and `ScriptRunner` (the command language again, over C#) |
| `csharp/Psiv.Session.Tests/` | xUnit tests: the scripted playthrough against the golden transcript and a fresh `game_cli` run, struct layouts, errors, saves, a battle, presentation, field and menus |
| `monogame/Psiv.Sample/` | MonoGame DesktopGL sample with a headless mode; `scripts/smoke.txt` is the headless run `make test` does |
| `Makefile` | `make`, `make test`, `make clean` |

## Build

Needs `clang++`/`clang` and GNU Make 3.81 or later for the native layers, and the .NET 10 SDK for the rest. The Makefile looks for `dotnet` at `~/.dotnet/dotnet`; pass `DOTNET=/path/to/dotnet` to use another. When it is missing, `make` and `make test` do the native layers only.

```
make          # ../core, ../events, ../game through their Makefiles, then build/libpsiv_session.*, the C tests, and the .NET projects
make test     # C tests; the C playthrough compared with game_cli; then the C# tests and the sample's headless run
make clean    # build/ and the .NET bin/ and obj/ folders (the sibling libraries keep their builds)
```

`make test` runs, in order:

1. `build/c_tests`: the focused C tests.
2. `game_cli --seed 7 --auto-battle --script ../game/data/scripts/playthrough.txt` and `build/c_playthrough` with the same arguments, each with an empty save directory. The two transcripts must be identical byte for byte, must match `../game/tests/expected/playthrough.txt`, and every file the two runs wrote (autosaves, the manual save, the profile and the clear-data record) must be identical.
3. `dotnet test` on `csharp/Psiv.Session.Tests` (the C# playthrough is compared with both the golden transcript and a fresh `game_cli` run, saves included).
4. The sample in headless mode on `monogame/Psiv.Sample/scripts/smoke.txt` for 2000 frames; it fails on any failed expectation or unfinished script.

The first .NET build restores xUnit, `Microsoft.NET.Test.Sdk`, `MonoGame.Framework.DesktopGL` 3.8.2 and `FontStashSharp.MonoGame` 1.3.10 from the NuGet cache or nuget.org.

### macOS

As above. The library is `build/libpsiv_session.dylib` with install name `@rpath/libpsiv_session.dylib`; the C tests find it with `-rpath @loader_path`, and the .NET projects copy it next to their assemblies.

### Linux

`make` picks `build/libpsiv_session.so`, links it with `-shared` and the version script `c/exports_linux.map`, and builds the C tests with `-rpath '$ORIGIN'`. The C++ libraries end up inside a shared object, so they need position-independent code: the Makefile passes `CXX="clang++ -fPIC"` to the sibling Makefiles. Those only rebuild what is out of date, so if `../core`, `../events` or `../game` were built before without `-fPIC`, run `make clean` in each of them first. MonoGame DesktopGL needs SDL2 (shipped in its package for linux-x64) and an OpenGL driver; the sample's font list covers DejaVu, Liberation and Noto.

### Windows

With clang and GNU Make from MSYS2 or similar (`OS=Windows_NT` selects the branch): `build/psiv_session.dll` plus the import library `build/libpsiv_session.dll.a`, exported through `__declspec(dllexport)` (only the `PSIV_API` functions). The tests link the import library and need the DLL next to them, which it is. The sibling Makefiles name their tools without `.exe`, so the build of `game_cli` may need their help on that platform. The .NET projects copy `psiv_session.dll` next to the assemblies.

To use another build of the library from .NET, set the MSBuild property `PsivNativeDir` to the folder holding it.

## The C interface

### Groups

| Group | Functions |
| --- | --- |
| Version | `psiv_abi_version`, `psiv_version`, `PSIV_ABI_VERSION`, `psiv_sizeof` (struct sizes, for foreign bindings to check their layouts) |
| Errors | `psiv_last_error`, `psiv_status` |
| Names | `psiv_name_of(kind, value)`: the stable name of any enum value, as the C++ libraries spell it |
| Session | `psiv_session_options_init`, `psiv_session_create`, `psiv_session_destroy`, `psiv_game_info_get` |
| Profile and settings | `psiv_profile_get`, `psiv_profile_set`, `psiv_profile_set_value`, `psiv_settings_get`, `psiv_settings_set`, `psiv_settings_set_auto_macro`, `psiv_macros` |
| Title and saves | `psiv_new_game`, `psiv_continue_game`, `psiv_load`, `psiv_retry_battle`, `psiv_quit_to_title`, `psiv_save_manual`, `psiv_saves` |
| State | `psiv_mode_get`, `psiv_menu_kind_get`, `psiv_world_info_get`, `psiv_flag` |
| Frame and field | `psiv_update(dt_ms, direction)`, `psiv_walk`, `psiv_face`, `psiv_interact`, `psiv_party_talk`, `psiv_field_info_get`, `psiv_field_grid`, `psiv_field_rows`, `psiv_field_enemies`, `psiv_field_markers`, `psiv_field_exits`, `psiv_field_points` |
| Events | `psiv_line_pending`, `psiv_line_get` (speaker id and name, text, portrait), `psiv_line_confirm`, `psiv_choice_pending`, `psiv_choice_get`, `psiv_choose`, `psiv_can_skip`, `psiv_skip` |
| Presentation | `psiv_actions_poll`, `psiv_action_finish`, optional `psiv_actions_set_callback` |
| Notices | `psiv_notices_drain` |
| Battle | `psiv_battle_info_get`, `psiv_battle_combatants`, `psiv_battle_options`, `psiv_battle_set_command`, `psiv_battle_apply_macro`, `psiv_battle_start_auto`, `psiv_battle_cancel_auto`, `psiv_battle_fill_from_auto`, `psiv_battle_fill_from_policy`, `psiv_battle_resolve_round`, `psiv_battle_last_events` |
| Party, menus, shops | `psiv_party`, `psiv_member_abilities`, `psiv_inventory`, `psiv_shop_get`, `psiv_inn_get`, `psiv_open_menu`, `psiv_close_menu`, `psiv_buy`, `psiv_sell`, `psiv_rest`, `psiv_use_item`, `psiv_use_ability`, `psiv_equip`, `psiv_unequip`, `psiv_name_entry_member`, `psiv_enter_name` |
| Journal and text | `psiv_quest_log_get`, `psiv_encyclopedia`, `psiv_glossary`, `psiv_text_log`, `psiv_pacing_table`, `psiv_render`, `psiv_format_text`, `psiv_display_name`, `psiv_string_text` |

### Errors

Every function returns a `psiv_status`, or a plain value where it cannot fail. On anything but `PSIV_OK`, `psiv_last_error(session, &error)` gives the status, a message key, its parameters and the text rendered in the profile's language:

* `PSIV_REFUSED`: the game said no. The key is the game's (`game.refuse.*`) or core's (`equip.*`, `field.*`, battle command keys), exactly what `game::Session` returned.
* `PSIV_INVALID_ARGUMENT`, `PSIV_NOT_FOUND`: the binding's own checks, keys `binding.*` with English text (`binding.null_argument` names the argument, `binding.out_of_range` the argument and the value). `PSIV_NOT_FOUND` also covers reads of state that is not there now: no area, no line, no battle, no shop.
* `PSIV_DATA_ERROR`: `psiv_session_create` could not load the data; the text is the loader's list of problems.
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

1. `psiv_update(session, dt_ms, held_direction)`. In the field it runs fixed ticks (`tick_ms`) and walks the leader one cell per `step_ms` of held input; in an event it steps the interpreter. Battle and title ignore it.
2. `psiv_mode_get` says what to present:
   * Field: `psiv_field_info_get`, `psiv_field_grid`, `psiv_field_enemies`, `psiv_field_points`, `psiv_field_exits`. Act with `psiv_interact`, `psiv_open_menu`, `psiv_party_talk`; `psiv_face` turns on a tap.
   * Event: `psiv_line_get` and `psiv_line_confirm`; `psiv_choice_get` and `psiv_choose`; `psiv_can_skip` and `psiv_skip`. With neither a line nor a choice, keep calling `psiv_update`.
   * Battle: `psiv_battle_combatants` and `psiv_battle_options` for the menus, `psiv_battle_set_command` per member (or `psiv_battle_apply_macro`, or `psiv_battle_start_auto` then `psiv_battle_fill_from_auto` each round), then `psiv_battle_resolve_round`, whose typed events the front end plays back. The battle can end inside that call; the mode then says where control went, and `psiv_battle_info_get`/`psiv_battle_combatants` still describe the battle that just ended until the next one starts.
   * Menu (`psiv_menu_kind_get`: field menu, inn, save prompt, name entry) and Shop: the menu and shop functions, then `psiv_close_menu`.
   * Title: `psiv_new_game`, `psiv_continue_game`, `psiv_load`, `psiv_retry_battle`.
3. `psiv_notices_drain`: arrivals, rewards, autosaves, party talk lines, warnings, each with key, parameters and text.
4. `psiv_actions_poll`: presentation actions (moves, fades, cameras, music). With `PSIV_PRESENTATION_INSTANT` (the default) they are already done and only tell the engine what happened. With `PSIV_PRESENTATION_WAIT` the event waits on each until the engine calls `psiv_action_finish(handle)` when its animation ends; `instant` records are done at once (the rebuild after a skip or a restore), and `PSIV_ACTION_CANCEL` records name actions a skip abandoned.

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
* Game operations return `PsivResult`: `Ok`, or `Error` (a `PsivError` with status, key, parameters and text) when the game refused; `ThrowIfFailed()` turns it into an exception. Bad arguments, data errors and internal errors throw `PsivException`. Reads of state that is not there return `null` (`FieldInfo()`, `Line()`, `Shop()`, `BattleInfo()`).
* Enums mirror the C enums; `GameSession.NameOf(kind, value)` gives the library's names.
* `SetActionCallback` is the optional push delivery, through an `UnmanagedCallersOnly` function pointer; the polled `PollActions` is the main path.
* `Create` refuses a library whose ABI version differs from the wrapper's.

## The sample

```
dotnet run --project monogame/Psiv.Sample -- [--data DIR] [--saves DIR] [--seed N] [--font FILE] [--exit-after-ms N] [--screenshot FILE.png]
dotnet run --project monogame/Psiv.Sample -- --script FILE --frames N [--saves DIR]      # headless: no window
dotnet run --project monogame/Psiv.Sample -- --window --script FILE [--screenshot FILE.png]   # the window, keys from the script
```

The data directory is found above the binary or the working directory when `--data` is left out. Keys: arrows walk (a tap turns), Enter/Z/Space confirm and talk, Escape/X back, M menu, Q quest log, T party talk, L text log, Tab skip. Screens: title, field (colored grid: walls, floor, water, exits in green, talk points magenta, chests gold, shop cyan, rest purple, enemies red or orange while chasing, the leader white with a facing mark), dialogue box with speaker, wrapped text, choices and skip, battle (party and enemy lists with HP bars and statuses, a command menu with attack, technique, skill, item, defend, escape, macro and auto, target selection, and the event stream played back one line every 350 ms; Enter hurries it, Escape stops auto-battle), field menu (party and field techniques, items, equipment, manual save, quests, encyclopedia, text log), shop (buy and sell), inn, save prompt, name entry, quest log and text log overlays.

Text is drawn with FontStashSharp from the first system font found in a fallback list (Arial, Verdana, Geneva on macOS; DejaVu, Liberation, Noto on Linux; Segoe UI, Arial, Tahoma on Windows; CJK faces after them as fallbacks for missing glyphs). `--font FILE` or `PSIV_FONT` puts a font first.

Headless scripts are lines of `press KEY [COUNT]`, `hold KEY FRAMES`, `wait FRAMES`, `pressuntil KEY SCREEN MAXFRAMES`, `cmd <game_cli command>` (to walk to a marker, say), `expect screen NAME`, `expect mode NAME` and `dump` (the text of the last frame). Frames are 16 ms. The run fails on an unmet expectation, a failed command or a script that does not finish in the frame budget.

### Replacing the drawing

The sample keeps the game out of its drawing code: `SampleController` reads the session after each update and draws through `IDrawSurface` (`Fill` a rectangle, draw `Text`, `Measure` text). `MonoGameSurface` implements it with a `SpriteBatch` and FontStashSharp; `RecordingSurface` is the headless one. To put real rendering behind the same screens, implement `IDrawSurface` over sprites and a proper font, or replace a screen's `Draw*` method and keep its `Update*` method, which holds all the session calls. For a real game, keep the frame loop of the section above and write the screens from scratch: everything they need comes from the records `GameSession` returns, and animations hook in through `Presentation.Wait` and `FinishAction`.

## Not covered

* Builds on Linux and Windows: the Makefile branches and the font list are written, not run.
* The sample uses instant presentation; `Presentation.Wait` is exercised by the C and C# tests only.
* The placeholder adventure has no name entry, no save prompt and no portraits, so those paths are covered by the interface but not reached by any test. Retry after a game over is only reachable through an event battle the party loses.
* Editing macros, the combo log, the pause overlay and anything else `game` itself does not build (see `../game/README.md`, "Not built").
* Battle records and replays (`rules::serializeRecord`, `replayBattle`), the pacing bot and the events tools have no C entry points.
* One session per data load: two sessions load the data twice.
* Mouse or gamepad input, resizing, audio: the sample is keyboard only, at 1280x720.
