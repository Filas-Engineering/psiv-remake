# Developer tools

The MonoGame sample (`bindings/monogame/Psiv.Sample`) has a set of tools for people working on data, scripts and presentation. They are off unless the sample starts with `--dev`:

```bash
cd bindings
dotnet run --project monogame/Psiv.Sample -- --dev                                  # the window with the tools
dotnet run --project monogame/Psiv.Sample -- --dev --script FILE --frames N          # headless, the tools reachable from the script
```

Without `--dev` none of their keys does anything and every `dev` script line is refused, so ordinary runs, transcripts and goldens are the same as before (`scripts/dev_off.txt` checks it). The game library and the C API carry the commands the tools need (below, "Session API"); any front end can offer the same. The tools' own screens (console, picker, previewer, badges, overlays) are drawn in code at fixed places, not by the layout files ([`ui-layout.md`](ui-layout.md)).

## Keys

The keys are the window's own, like F5 (asset reload) and F11 (fullscreen): they are not game actions and the Controls page does not list them. A game action rebound to one of them still works; with `--dev` the tool's key does its own thing as well.

| Key | Tool |
| --- | --- |
| `` ` `` (backquote, `OemTilde`) | Opens and closes the console |
| F1 | Layout windows overlay |
| F2 | Collision and terrain overlay |
| F3 | Hooks, exits, ports and markers overlay |
| F4 | Encounter zones overlay |
| F6 | Data hot reload |
| F7 | Actor ids overlay |
| F8 | Camera target overlay |
| F9 | Frames per second and frame time overlay |
| F10 | Running event and script line overlay |
| F12 | The previewer's picker |
| `[` and `]` | Slower and faster (0.25x, 0.5x, 1x, 2x, 4x, 8x) |
| `\` | Pause and resume |
| `.` | Pause, then step one frame (16 ms) at each press |
| Page Down, Page Up, End | While previewing: next panel (or play the battle entry again), previous panel, stop |

While the console or the picker is open the game gets no input and no frame time.

## The dev mark

Every developer command that changes the game marks it (`World::devMarked` in `game/`). Lists, overlays, speed, pausing and the previewer change nothing in the game being played and do not mark it; a refused command does not mark it either. A data reload marks it, since the game then runs on data it did not start with. What the mark does, exactly:

* The first time, the game says so with the notice `game.dev_marked` ("Developer tools changed this game. It and its saves are marked.").
* It is saved: every save written from a marked game has a `dev` chunk with the line `marked 1` and a `dev 1` line in its header (save schema 6; `game/README.md`, "Saves"). Loading such a save marks the session again; saves written before the mark, and saves of schemas 1 to 5, are not marked. The mark is never cleared, also not by an event restarting from its event-start world.
* A marked game writes no clear-data record: when the clear flag is first set it says `game.clear_not_recorded` instead, so a record made with developer commands never replaces one from real play.
* A marked game adds nothing more to the pacing numbers (`PacingRecorder`); what was counted before the mark stays.
* It is reported by the API (`psiv_world_info.dev`, `psiv_save_summary.dev`; C# `WorldInfo.Dev`, `SaveSummary.Dev`).
* The sample shows it while `--dev`: the badge at the top of the screen reads `DEV marked` in red (just `DEV` for an unmarked game), and save slot lists (the field menu's Save page, the save prompt, the title's Load screen) add `[dev]` to a marked slot.
* It is not part of the content hash, nothing else in the game reads it, and it changes no rule.

## Console

Opened with the backquote key. Type a command and press Enter; Up and Down recall earlier ones, Backspace erases, Escape or the backquote key closes it. Every command's outcome goes to the console and to the log (`dev: ...`). A refusal shows the game's own message and key, in the profile's language, for example `refused: 0, 0 is not a walkable cell of Reed Path. (game.refuse.dev_bad_cell)`. The console's own words, the overlays' labels, the badge and the previewer come from the text tables' `ui.dev.` keys in that language too; the commands and the `help` lines are the command language and stay as they are. At the title only the lists, the overlays, the speed and the previewer work; the game commands say `Start or load a game first.`

| Command | What it does |
| --- | --- |
| `help` | Lists the commands |
| `help overlays` | Lists every overlay with its key, the layout windows overlay (F1) first, and what `overlay all` and `overlay none` cover |
| `list KIND` | The ids of `areas`, `markers` (as `area:marker`), `flags`, `items`, `equipment`, `abilities`, `enemies`, `encounters`, `events`, `vehicles` (those with field rules) or `characters` |
| `warp AREA MARKER` | From the field, to a marker of any area (`warp reed_path west_gate`). A vehicle ridden stays where it was. The arrival works as a warp does: load hooks, the area-entry autosave, ambient talk |
| `warp AREA X Y` | The same to a walkable cell (`warp brine_flats 20 4`) |
| `flag NAME on\|off` | Sets or clears a declared flag (`flag travel.isles_open on`); the area's flag hooks run as when a script sets it |
| `give ID [COUNT]` | Adds items or equipment pieces, 1 by default, capped like any gain; a negative count takes them away (`give homing_shell 3`, `give tonic -1`). Not during a battle |
| `money AMOUNT` | Adds money, or takes it away when negative, kept between 0 and the cap (`money 500`) |
| `level MEMBER LEVEL` | Sets a roster member's level, 1 to 99, with no experience into it, and refills it as a rest does, reviving it (`level fighter 12`) |
| `rest` | A free rest for testing (`DevRest`): every roster member refilled as a rest does, vehicles' skill uses back, at no cost and anywhere a game is running, marking the game |
| `combo RECIPE` | In a battle while the members choose, the combination's parts become this round's leading commands of the members who know them (`DevCombo`), the members left without a command defend, and the round resolves, so the combination fires unless a part cannot be paid for (`combo twin_arc`); refused outside a battle or for a recipe not found yet. The script verb `combo RECIPE` does the same. Marks the game |
| `battle ENCOUNTER` | From the field, a battle with that encounter as a random battle; while riding, a vehicle battle (`battle warden`) |
| `win`, `lose` | Ends the live battle: every enemy, or every party combatant, falls, then a round resolves, so the battle ends as that outcome leads (rewards, an event's `win` or `lose` branch, a game over). The round plays through the battle presentation, with a step before its outcome that brings the stage to the session's numbers and falls, since the command sets them outside the events, so the playback still matches the session (`battlesync`) |
| `vehicle ID AREA MARKER` | From the field, gives the party a vehicle with field rules, or moves one it has, to a marker of a field area (`vehicle reed_skimmer reed_path skimmer_front`) |
| `encounters on\|off` | Random battles, battles aboard and visible enemies' battles off or on for this session (not saved; a data reload keeps the switch) |
| `event ID` | From the field, runs an event as a hook would (`event tidewater_chart`) |
| `overlay NAME [on\|off]` | Toggles an overlay (`collision`, `hooks`, `zones`, `actors`, `camera`, `fps`, `event`), or sets it; `overlay all` and `overlay none` |
| `speed X` | 0.25, 0.5, 1, 2, 4 or 8 |
| `pause`, `resume` | Holds and releases the frame time |
| `step [N]` | Pauses, then plays N frames (1 by default), one per frame |
| `reload` | The data hot reload (below) |
| `preview` | Opens the previewer's picker |
| `preview event ID` | Plays an event in the previewer (`preview event reed_path_lookout`) |
| `preview battle ENTRY` | Plays a battle presentation entry in the previewer (`preview battle technique.damage.shock`) |
| `preview next`, `preview prev`, `preview stop` | As Page Down, Page Up and End |
| `clear` | Empties the console |

## Data hot reload

F6, `reload` in the console, or `dev reload` in a script loads `game.json`, the areas, the text tables, the journal, the event scripts and core's battle data again from the directories the session was created with, and checks them as a new session would.

The game session cannot be rebuilt in place from changed data (it holds the data it was created with), so the reload is the honest version, done inside the library by `psiv_session_reload`: the game is written to memory as save text (`psiv_save_text`, with the running event's interpreter state), a new game session is built on the new data with the same options, profile and encounter switch, and the text is loaded into it. The handle stays the same, so a front end keeps every reference it has; the sample then rereads the ids the data defines and drops its scene state, which the new session rebuilds.

* When the data still loads and the game fits it, play goes on where it was: the same area, cell, facing, party, items and flags. The game is marked.
* When the data has problems, nothing changes and the game goes on with the data it had; the problems show on screen in a red box (the first eight, the first of them usually the cause of the rest) and go to the log. F6 again tries again.
* When the data loads but the game no longer fits it (its area gone), the game goes on with the old data and the box says why.

Limits, all from what save text holds:

* An event in progress resumes at the command it waited on: a line or a choice is shown again, an action in flight plays again from its start, and the presentation (panels, camera, music) is rebuilt as instant actions. If the event scripts changed, the event instead starts again from its first command, with the world as it was when the event started (`events/README.md`, the save contract).
* A battle in progress is not in save text. A field battle is dropped: the party stands where the battle started, with the encounter stream as before the battle. An event battle is set up again from its start (the event resumes at its `battle` command), which writes the usual pre-battle autosave.
* An ended battle's result screen, a game over, the text log and the pacing numbers start over. A menu, shop or travel screen the player opened closes; one an event opened opens again.
* Asset files are not reloaded by F6; F5 does that ([`assets.md`](assets.md)).

## Overlays

Each is toggled by its key or `overlay NAME`. The field overlays draw over the map and under comic panels, in the field and while an event's dialogue is up; their labels keep clear of each other and of the screen's edges.

* `collision` (F2): walls tinted red, water blue, floor that something solid stands on (an interact hook, a chest, a shop, a rest point, a port) orange, and a faint grid over every cell, from `psiv_field_grid`.
* `hooks` (F3): exits outlined in cyan with their destination (`> area:marker`), talk and step points, chests, shops, rest points and ports outlined in green while active (grey otherwise) with their kind, id and event, and markers as yellow dots.
* `zones` (F4): each encounter zone of the area as a tinted rectangle with its id and table (`zone east_marsh: marsh x2, training x1`) and its visible spawn cells, from `psiv_field_zones`.
* `actors` (F7): the id over every person drawn (party members and script actors) and `encounter#id` over visible enemies.
* `camera` (F8): a cross on what the field's camera follows, the leader or the scene's camera, with its name; while a script's scene camera (`camera to`, or `camera reset` on its way back) frames the field, the cross goes where it centers, named `scene at X Y` or `scene, back to the leader` (probe `overlay.camera`: `scene held X Y` or `scene leader` with the milliseconds left).
* `fps` (F9): frames per second and frame time, averaged over the last 30 frames of the time the window gives, the speed and whether paused (headless runs read `62.5 fps 16.0 ms x1`).
* `windows` (F1): every window the screen's layout drew this frame outlined, with its screen and name (`title/box`), on every screen including the battle and the menus ([`ui-layout.md`](ui-layout.md)). `overlay windows [on|off]` toggles it; `overlay all` and `overlay none` leave it as it is. `help overlays` lists it with its key; `help` keeps its six lines without it, because `scripts/dev_console.txt` prints them into its transcript and the console draws each line, so another line would change that transcript and its draw counts. `overlay.windows` reads how many windows it outlined.
* `event` (F10): the running event and the source line of the instruction its interpreter is at, `event reed_path_lookout events/reed_path.ev:62` (`psiv_event_position_get`: the innermost event a `call` runs, and while it waits on a `together` or `race` block, the first live branch's line).

## At small logical resolutions

The tools are laid out for 1280x720. A layout set at another resolution can shrink them with the styles' `dev_scale` (10 to 100 percent, default 100): every tool (the badge, the paused and speed banners, the console, the picker, the overlays' plates, the frame ruler, the windows overlay's labels) is drawn in a frame of the logical size over that percent and scaled down to it; with a bitmap font for the menu role the text keeps the font's own size, so a 320x224 set at `"dev_scale": 60` draws the console in its own 10-pixel letters. `dev_scale_bitmap` 1 shrinks a bitmap font's text too, to its nearest whole multiple. Off the battle, `dev_badge_x` (-1: centered) and `dev_badge_y` (12) place the badge, in the tools' units. `dev_draw` 0 draws none of them while every key and command still works. The probe `devdraw` reads `100%`, `60%` or `off`. `scripts/small_dev.txt` shows both on the small example set ([`ui-layout.md`](ui-layout.md), "The developer tools at small resolutions").

## Previewer

F12 (or `preview`) opens a picker with two lists: every event of the data, and every battle presentation entry, the table's actions and then its fallbacks. Left and Right switch lists, Confirm plays, Back closes.

**Events** play from a clean state, in a scratch session that is not the game being played: the same data, no save directory (so nothing is written), seed 1 and the sample's waiting presentation. A new game starts in it; its opening event is skipped where it can be, otherwise played through with every line confirmed and the first answer to every choice (a battle played by the session, a name entry given the character's own name); then the party is warped to the area of the hook that starts the event and onto a walkable cell beside it (an event no hook starts runs where the new game stands), and the event starts at once. It plays as in the game, with input, the speed and the frame step. Page Down plays on, confirming lines and answering choices (a battle on the way is played out by the session), until the next panel show has slid in, then pauses; Page Up starts the event again from the clean state and stops one panel show earlier; End stops. The game being played gets no frame time meanwhile and is not marked. The preview is silent.

**Battle presentation entries** play on dummy targets with no session battle: two party members (the lineup's first, or the data's first characters), three dummies of the data's first enemy, and one turn of synthetic typed events (the action's start, its cost, its results on the dummies as its shape asks: damage, healing, a revival, a status, a cure, a stat change, or no effect) built into staged steps by `BattleTimeline.Build`, as a round's events are, so the entry's animation, effect, cue, step lengths, shake and flash play as they would in a battle. Page Down plays it again, End stops. A fallback key (`technique.damage.shock`) plays as an action of that shape.

**Animation scripts** play with `preview anim KEY|SCRIPT [layout front|side] [enemy ID] [count N] [targets N]` on the same dummies (`count`: 1 to 8 of them, default 3; `targets`: how many a damaging action hits), the script forced for the kind of step it is (a turn's, a step's, a change's on a change after the results, a reaction's on the dummies' damage; a combination key on the first two members; an `enemy:ID/ACTION` key with the first dummy, of that enemy, acting on the members, its basic attack named in the log by its data's attack; a key of an encounter (`encounter:ENC/...`, or ending in `/encounter:ENC`) in that encounter, on dummies of the key's enemy or of one the encounter lists; an `item:ID` key with the first member using the item, a worn piece its use, on the member when it heals), with the `anim` overlay's frame ruler, which `overlay anim [on|off]` turns off and on (it stays as set through seeks). `.` steps a frame forward and `,` one back; `anim seek N` and `anim step N` jump to a script frame (the preview plays again quietly up to it, which draws the same as playing there, the battle layout's windows too: none is still opening or closing after a seek); Page Down replays, End stops. `anim why KEY` prints the script a key chooses and the current step's lookup; `anim expand KEY` prints the script flattened with the key's look and timings filled in, as JSON to paste into a file (also logged as `anim: JSON`). See [`battle-animation.md`](battle-animation.md), "Probes, script verbs and the previewer".

## Frame step and speed

The speed scales the frame time the controller passes on, to the session, the animations, the dialogue's reveal and the battle playback alike: at 0.25x a 16 ms frame passes on 4 ms, at 8x 128 ms. Paused, nothing advances; each step passes on one 16 ms frame. Headless frames stay 16 ms and every speed is a whole number of milliseconds per frame, so scripted runs stay deterministic. The badge's line under it says `speed x4` or `PAUSED`.

## Script verbs

Headless and window scripts (see `bindings/README.md`, "The sample") take:

* `dev COMMAND`: runs a console command; the script fails when it is refused or not understood. Its output goes to the transcript as `dev:` lines.
* `devrefused COMMAND`: the same, failing when the command is accepted. Without `--dev` every command is refused.
* `datafile swap FILE WITH`: copies WITH over FILE in the data directory, as a writer saving a file would; a reload then reads it.
* Key presses reach the tools as they reach the window (`press OemTilde`, `press F6`, `press PageDown`, `press OemPeriod`).
* `expect state` probes: `dev` (`on`, `off`), `devmark` (the game is marked: `yes`, `no`), `hudmark` (the last frame drew the marked badge), `console` (`open`, `closed`), `consolelast` (the console's last line), `overlays` (those on, or `none`), `overlay.NAME` (what that overlay drew in the last frame: `collision` its tinted cells, `hooks` its counts, `zones` its zone ids, `actors` the ids labeled, `camera` the target, `fps` its text, `event` the event and line; `-` when not drawn), `speed`, `paused`, `clock` (the controller's animation clock in ms), `playtime` (the game's play time in ms), `encounters`, `leader` (area and cell), `reload` (`ok`, `error`, `refused`, `-`), `deverror` (the reload problems shown, `-`), `preview` (`picker`, `event ID panels N`, `battle ENTRY step STEP`, `-`), and `preview.KEY` (any probe of the previewed event's screen, such as `preview.panels` or `preview.line`).

`make bindings` runs `scripts/dev_console.txt`, `dev_overlays.txt`, `dev_preview.txt` and `dev_speed.txt` with `--dev`, `dev_reload.txt` with `--dev` on a copy of the data in `bindings/build/dev_reload` (game and core data side by side, plus a renamed and a broken English text table), and `dev_off.txt` without `--dev`. With `--window` their `shot` lines save `bindings/build/shots/dev_*.png`.

## Session API

The tools stand on calls any front end can make (`game/README.md` and `bindings/README.md` have the details):

| `game::Session` | C | C# `GameSession` |
| --- | --- | --- |
| `devList` | `psiv_dev_ids` | `DevIds` |
| `devWarp`, `devWarpCell` | `psiv_dev_warp`, `psiv_dev_warp_cell` | `DevWarp`, `DevWarpCell` |
| `devSetFlag`, `devGive`, `devMoney`, `devSetLevel` | `psiv_dev_set_flag`, `psiv_dev_give`, `psiv_dev_money`, `psiv_dev_set_level` | `DevSetFlag`, `DevGive`, `DevMoney`, `DevSetLevel` |
| `devBattle`, `devEndBattle` | `psiv_dev_battle`, `psiv_dev_end_battle` | `DevBattle`, `DevEndBattle` |
| `devRest` | `psiv_dev_rest` | `DevRest` |
| `devCombo` | `psiv_dev_combo` | `DevCombo` |
| `devVehicle`, `devEncounters`, `devRunEvent`, `devMark` | `psiv_dev_vehicle`, `psiv_dev_encounters`, `psiv_dev_run_event`, `psiv_dev_mark` | `DevVehicle`, `DevEncounters`, `DevRunEvent`, `DevMark` |
| `devMarked`, `encountersOn` | `psiv_world_info.dev`, `.encounters` | `WorldInfo.Dev`, `.Encounters` |
| `eventPosition` | `psiv_event_position_get` | `EventPosition` |
| `area()->zones` | `psiv_field_zones` | `FieldZones` |
| `saveText`, `loadSaveText` | `psiv_save_text`, `psiv_load_text` | `SaveText`, `LoadText` |
| (the binding) | `psiv_session_reload` | `Reload` |

Refusals are text keys in both text tables (`game.refuse.no_game`, `game.refuse.dev_*`).
