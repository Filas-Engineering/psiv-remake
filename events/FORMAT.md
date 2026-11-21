# Event script format

Event scripts are plain text files, by convention with the `.ev` extension. One file can hold any number of events. All the files you check together form one program, so an event can `call` an event in another file.

Check your scripts with:

```
build/event_cli check samples/*.ev
build/event_cli check --strict --registry sample_text/registry.txt --glossary sample_text/glossary.txt --table sample_text/en.strings --table sample_text/ja.strings samples/*.ev
build/event_cli lint --entries sample_text/entries.txt samples/*.ev
```

`check` reports errors that stop a script from compiling. With `--registry` it also rejects names the game does not have (see [Registry](#registry)). `--table` adds the string table checks and `--glossary` the name checks. `--strict` is the gate for shipped scripts (see [Strict check](#strict-check)). `lint` reports likely mistakes in scripts that do compile (see [Lint](#lint)).

## The basics

- One command per line. Indentation does not matter to the parser, but use tabs to show nesting.
- Blocks (`if`, `while`, `choice`, `ask`, `battle`, `together`, `sequence` and `event` itself) close with `end`.
- `#` starts a comment that runs to the end of the line. A `#` inside a quoted string is text.
- Text is either a quoted string, `"Like this."`, or a string ID, `@harbor.greeting`. A string ID is looked up in the game's string tables (see [String tables](#string-tables)), so the same script works in every language. Inside quotes, `\"` is a quote, `\\` a backslash and `\n` a line break.
- A renamed name in text is written as a name token, `{name:tide_lamp}`, never typed out. `{name_upper:tide_lamp}` shows it in capitals and `{name_lower:tide_lamp}` in lowercase. The game shows the shipped or the restored name depending on the "show shipped names" setting (see [Name glossary](#name-glossary)).
- Other `{...}` codes in text are control tags for the game, for example `{page}` to wait for the player before the next page of a line. They are written `{word}` or `{word:value}`. A translation must keep the same control tags in the same order (see [String tables](#string-tables) and [Control tags](#control-tags)).
- Names (actors, flags, markers, cameras and so on) are letters, digits, `_` and `.`, and do not start with a digit. Use dots to group names: `harbor.met_guide`.
- Numbers are written `3`, `-2` or `1.5`.

## Events

```
event harbor_arrival skippable
	say guide @harbor.welcome
	set harbor.met_guide
end
```

Every event is marked `skippable` or `unskippable`.

### Skipping

When the player skips a skippable event, the rest of the scene plays at once:

- Every command that changes the game applies, in order, exactly as when the scene is watched: flags, variables, items, party, player control, warps and rests.
- Lines are not shown. The game's text log still gets them, marked as skipped.
- Waits take no time.
- Moves, camera, panels, fades, music and ambient sound complete at once: the game puts everything at its end state without playing it. Shakes and one-shot sounds are dropped, because they leave nothing behind.
- The skip stops at the next `choice`, `ask`, `battle`, `shop`, `save prompt` or `name entry`. The player answers or fights as usual and can skip again afterwards.

So the game after a skip is the same as after watching the same choices and battles, whenever the player skips. Write the scene once; there is nothing to add for skipping.

Older scripts end skippable events with an `on skip` block that repeats the scene's effects. The checker still accepts one, warns that it is ignored, and never runs it. Delete it.

A loop that only waits for the game to change a flag never ends while skipping, because its waits take no time. The event then stops with a runtime error.

### Skipping inside other events

A skippable event can be called from an unskippable one. If the player skips while it runs, only that event is skipped, then the calling event carries on normally from the line after the `call`. When several skippable events call each other, the skip covers all of them, up to the outermost one in that chain. Unskippable events they call are skipped with them.

## Dialogue

| Command | Example |
| --- | --- |
| `say <speaker> <text> [as <portrait>]` | `say guide "Mind the gap." as smiling` |
| `narrate <text>` | `narrate "The tide is coming in."` |

```
say traveler @harbor.traveler_thanks
narrate @harbor.fog_rolls_in
```

Each line waits until the player confirms it.

## Choices

A multi-option choice. The prompt is optional. Each `option` starts a branch, and the choice needs at least two options:

```
choice @harbor.first_stop_prompt into harbor.first_stop
option @harbor.option_market
	say guide @harbor.market_hint
option @harbor.option_rest
	say guide @harbor.inn_hint
end
```

A yes/no question. Both branches are optional:

```
ask @harbor.satchel_question into harbor.satchel_carried
yes
	say guide @harbor.satchel_light
no
	say guide @harbor.satchel_refused
end
```

`into` is optional and records the answer before its branch runs: a `choice` sets the variable to the option's number, 1 for the first; an `ask` sets the flag for yes and clears it for no.

## Flags and variables

Flags are true or false and start false. Variables are whole numbers and start at 0. A registry can declare other starting values, and flags and variables that belong to one event (see [Registry](#registry)).

| Command | Example |
| --- | --- |
| `set <flag>` | `set harbor.met_guide` |
| `clear <flag>` | `clear lighthouse.cleared` |
| `var <name> = <integer>` | `var market.offer = 30` |
| `var <name> += <integer>` | `var market.rounds += 1` |
| `var <name> -= <integer>` | `var market.offer -= 5` |

## Conditions

```
if harbor.met_guide and not market.bought_lantern
	call market_buy_lantern
else if market.offer < 25
	say vendor "Final price."
else
	say vendor "Prices are prices."
end
```

A condition is one or more tests:

- `flag` is true when the flag is set
- `not flag` is true when it is not
- `variable <op> <integer>`, where `<op>` is `==`, `!=`, `<`, `<=`, `>` or `>=`. `not` works here too.
- `vehicle <vehicle>` is true when the party has that vehicle, and `aboard <vehicle>` when it rides it.
- `party <member>` is true when the member is in the active party, and `item <item>` when the party holds at least one of the item or equipment piece.
- `money <op> <integer>` compares the party's money, with the operators of a variable test: `if money >= 20`.

`not` works on each of these. A flag may still be named `vehicle`, `aboard`, `party` or `item`: the word starts one of these tests only when a name follows it. `money` followed by a comparison is always the party's money, so a variable named `money` cannot be compared.

Join tests with `and` or with `or`, but not with both in one condition. Nest ifs instead.

The checker rejects any branch that can never run: an `else` or `else if` after an `else`, or an `else if` that repeats a condition already tested in the same chain.

## Loops and jumps

| Command | Example |
| --- | --- |
| `while <condition>` ... `end` | `while market.offer > 18` |
| `label <name>` | `label farewell` |
| `goto <name>` | `goto farewell` |

Labels belong to their event. A loop that never waits is stopped with a runtime error, so a loop should always contain a line, a wait or an action.

## Actors

| Command | Example |
| --- | --- |
| `move <actor> to <marker> [speed <n>] [nowait]` | `move traveler to pier_end speed 1.5` |
| `move <actor> to <x> <y> <z> [speed <n>] [nowait]` | `move vendor to 4 0 -2.5 nowait` |
| `face <actor> <actor or marker> [nowait]` | `face guide traveler` |
| `anim <actor> <animation> [loop] [nowait]` | `anim keeper bow loop` |
| `show <actor> [nowait]` | `show traveler` |
| `hide <actor> [nowait]` | `hide vendor` |

## Camera, panels and the screen

Live panels are comic-style frames, each showing what a named camera sees in the live scene.

| Command | Example |
| --- | --- |
| `panel show <id> camera <camera> layout <slot> [nowait]` | `panel show p1 camera ferry_ramp layout top_left` |
| `panel hide <id> [nowait]` | `panel hide p1` |
| `panel clear [nowait]` | `panel clear` |
| `camera cut <camera> [nowait]` | `camera cut pier_wide` |
| `camera move <camera> <seconds> [nowait]` | `camera move pier_follow 2.0` |
| `fade out [seconds] [nowait]` | `fade out 1.0` (default 0.5) |
| `fade in [seconds] [nowait]` | `fade in 1.5` |
| `shake <seconds> [strength <n>] [nowait]` | `shake 0.8 strength 0.4` (default strength 1) |

## Audio

| Command | Example |
| --- | --- |
| `music play <track> [nowait]` | `music play tide_theme` |
| `music stop [nowait]` | `music stop` |
| `music fade <seconds> [nowait]` | `music fade 1.0` (fades out, then stops) |
| `music area <track>` | `music area harbor_storm` (the area the party is in plays this track from now on, saved with the game, until the party leaves or a script changes it) |
| `music area default` | goes back to the area's own track |
| `ambient play <sound> [nowait]` | `ambient play harbor_gulls` (a background loop such as wind or surf) |
| `ambient stop [nowait]` | `ambient stop` |
| `sound <cue> [nowait]` | `sound bell_chime` |

Music and ambient sound are part of what a save rebuilds; a one-shot `sound` is not. `music area` is game state, not presentation: it changes the game at once, applies while skipping, and is not allowed inside `together` or `race`.

## Waiting and running things in parallel

By default every actor, camera, panel, screen and audio command waits until the game reports it is done. For `music play` and `sound`, the game reports done as soon as playback starts. A looping `anim` never waits.

- `nowait` starts a command and goes straight to the next line.
- `sync` waits for every `nowait` command still running.
- An event does not end until its `nowait` commands have finished.
- `wait <seconds>` pauses the script: `wait 0.5`.

`together` runs each command inside it at the same time, and continues once all of them are done. `race` runs them the same way but continues as soon as the first one is done; the others stop where they are and do nothing more. To chain several steps in one parallel branch, wrap them in `sequence`:

```
together
	move traveler to pier_end
	sequence
		anim guide wave
		face guide traveler
	end
	camera move pier_follow 2.0
end
```

```
race
	camera move ferry_departing 4.0
	wait 2.5
end
```

Inside `together` and `race` you can use only actor, camera, panel, screen and audio commands, `wait` and `sequence`. You cannot use `nowait` there, because every branch already runs in parallel. A `sequence` cannot contain another `sequence`.

## Flow

| Command | Example |
| --- | --- |
| `call <event>` | `call market_counter_offer` |
| `return` | ends the current event early and goes back to its caller |

## Game state

| Command | Example |
| --- | --- |
| `give <item> [count]` | `give lantern_oil 2` |
| `take <item> [count]` | `take coin_pouch` |
| `party add <member>` | `party add guide` |
| `party remove <member>` | `party remove guide` |
| `warp <map> <marker>` | `warp lighthouse_base door_inside` |
| `vehicle give <vehicle> <map> <marker>` | `vehicle give skiff harbor pier_end` (the party now has it, standing there) |
| `vehicle place <vehicle> <map> <marker>` | `vehicle place skiff harbor pier_end` (moves a vehicle the party has) |
| `vehicle remove <vehicle>` | `vehicle remove skiff` (the party no longer has it) |
| `learn <member> <ability>` | `learn guide ward_song` (the member learns a technique or skill for good) |
| `follower <actor>` | `follower peddler` (walks with the party without joining it); `follower none` ends it |
| `money give <amount>` | `money give 30` (at least 1) |
| `money take <amount>` | `money take 20` (the game keeps money at 0 or more) |
| `equip <member> <piece>` | `equip guide brass_dagger` (puts the piece on from the bag, by the game's equipment rules) |
| `unequip <member> <piece>` | `unequip guide brass_dagger` (takes the worn piece off, back to the bag) |
| `lock player` | stops player input during the scene |
| `unlock player` | gives control back |

The `vehicle`, `learn`, `follower`, `money`, `equip` and `unequip` commands change the game at once, like `give`, and apply while skipping. With a registry, `learn` names a listed `member` and `ability`, `follower` a listed `actor`, `equip` and `unequip` a listed `member` and `item`, and `music area` a listed `audio` track. A piece that does not fit, or is not held or worn, is the game's to refuse (`../game` warns and carries on). With a registry the vehicle must be listed on a `vehicle` line and the marker must be on the map, as for `warp`; they do not change the map the party is on. Where a vehicle may stand, and what happens to a party riding one that a script moves or removes, is the game's business (`../game/README.md`).

## Menus and rests

Each of these waits until the game is done with it.

| Command | Example |
| --- | --- |
| `shop <shop>` | `shop vendor_stall` (until the player leaves the shop) |
| `rest [<rest point>]` | `rest harbor_inn` (an inn's rules, such as its price, apply); `rest` alone heals the party for free |
| `save prompt` | lets the player save; the save is an ordinary one, so loading it does not come back into this event |
| `name entry <member>` | `name entry traveler` (until the player confirms a name) |

A skip stops at `shop`, `save prompt` and `name entry`, because the player decides something there. A `rest` happens while skipping, without its usual presentation.

## Battles

```
battle lamp_room_moths opening enemy_first
win
	give brass_key
lose
	warp harbor pier_end
flee
	say guide @lighthouse.more_oil
end
```

`aboard <vehicle>` is optional and comes right after the encounter: the party fights riding that vehicle, which takes the commands while the members ride along (`battle reef_crabs aboard skiff`). With a registry, the vehicle must be listed on a `vehicle` line.

`opening` is optional: `normal` (the default), `party_first` or `enemy_first`.

`until` is optional and gives the battle a scripted end, after `opening` if both are written:

```
battle tide_wyrm until survive 3
battle tide_wyrm opening enemy_first until defeat wyrm_head
```

- `until survive <rounds>`: the battle also ends once the party has lasted that many rounds (1 to 999).
- `until defeat <enemy>`: the battle also ends when that enemy of the group falls. With a registry, the enemy must be listed on an `enemy` line.

A scripted end runs the `win` section. A victory or a defeat that happens at the same moment takes precedence.

The sections decide how the battle is set up:

- `win` is required. It may be empty.
- `lose` is optional. Without it, a defeat goes to the game-over screen and the script never sees it. With it, the script carries on in that section after a defeat.
- `flee` is optional. Without it, the player cannot escape this battle. To allow escape and simply carry on after `end`, write an empty `flee` section.

After the section that ran, the script carries on after `end`.

## String tables

A string table holds the text for one language. Keep one file per language, for example `en.strings` and `ja.strings`.

```
# Comments start with '#' at the start of a line.
language en

harbor.option_rest = The inn, I need a rest
market.closing_narration = The shutters rattle down.\nThe square empties.
```

- The first line that is not blank or a comment is `language <code>`.
- Every other line is `<id> = <text>`. IDs follow the rules for names. Each ID appears once per file.
- Spaces around the `=` and at the end of the line are dropped. In the text, `\n` is a line break and `\\` a backslash. A `#` inside the text is text.

Check the tables against the scripts:

```
build/event_cli strings check --table sample_text/en.strings --table sample_text/ja.strings samples/*.ev
```

It reports, as errors, an ID a script uses that a table lacks, and a translation that drops, adds or changes a name token or a control tag. Name tokens may come in any order, since word order differs between languages, and may change their case form (`{name_upper:moth_swarm}` in English, `{name:moth_swarm}` in Japanese), since capitalization differs too; the IDs must match. Control tags must come in the same order, each written the same: `{wait:30}` and `{wait:15}` are different tags. As warnings it reports an ID no script uses and an ID one language has and another lacks. Without script files it only compares the tables with each other. The command fails when there are errors, and with `--strict` on warnings too.

Some IDs are shown by the game itself, not by a script: speaker names, quest log and menu text. List them in a file and pass it with `--used`, so they are not reported as unused:

```
# One string ID per line, or a prefix ending in '*'.
speaker.guide
codex.*
```

An ID listed in full must be in every table, as if a script used it; an ID missing from one is an error at its line in the list. A prefix only marks the IDs that start with it as used. `check` takes `--used` too.

To move a scene from quoted text to IDs, print a proposed table:

```
build/event_cli strings extract samples/harbor.ev > harbor_proposed.strings
```

It lists every quoted text in the scripts with an ID of the form `<event>.<n>`, and the file and line it came from. It changes no file. Rename the IDs you want, add the lines to your tables, and replace each quoted text in the script with its `@id`.

## Name glossary

The glossary lists every renamed name: what the shipped release called it and its restored name.

```
# id | shipped name | restored name | reason
tide_lamp | Tidlamp | Tide Lantern | cut by the item name length limit
moth_swarm | MothSwm | Moth Swarm | cut by the enemy name length limit
```

One entry per line, fields separated by `|`. The reason is optional. In text, write a name token instead of the name. The game shows the restored name, or the shipped name when the player turns on "show shipped names". That setting only applies to English text; other languages always show the restored name.

A name token has three forms, the same ones `tools/text` writes:

| Token | Shows |
| --- | --- |
| `{name:tide_lamp}` | the name as the glossary writes it: `Tide Lantern`, or `Tidlamp` |
| `{name_upper:tide_lamp}` | in capitals: `TIDE LANTERN`, or `TIDLAMP` |
| `{name_lower:tide_lamp}` | in lowercase: `tide lantern`, or `tidlamp` |

Only the letters A to Z change case; other characters show as the glossary writes them.

List every line that mentions a glossary name:

```
build/event_cli names sample_text/glossary.txt --table sample_text/en.strings samples/*.ev
```

It reports each token, in any of the three forms, with the name it shows (add `--show-shipped` to see the shipped set), each name typed out instead of using a token, and each token that is broken or not in the glossary. Lines that use a string ID are checked in every table given. Typed-out names, broken tokens and unknown tokens make the command fail.

## Control tags

The interpreter does not interpret any control tag or name token. It gives the host each line (`Host::show_line`, or `Host::log_skipped_line` while skipping) with its text as written: the quoted text, or the string ID for the host to look up. The host resolves the ID, renders name tokens, and acts on the tags. These come from `tools/text` string table exports:

| Tag | What the host does |
| --- | --- |
| `{page}` | Ends a page of the line: the host waits for the player to confirm, clears the box and shows the rest. The line is done after its last page. |
| `{wait:N}` | Pauses the text reveal at that point. `N` is a whole number; this format does not fix its unit. Shows nothing. |
| `{portrait:ID}` | Switches the speaker's portrait to `ID` from that point. The `as <portrait>` of `say` is the portrait the line starts with. Shows nothing. |
| `{var:ID}` | Shows a value the game holds at runtime, such as a party member's name. `ID` is the game's name for it, not a script variable. |

Any other `{word}` or `{word:value}` is a control tag too, and also goes to the host as written. The checks never reject a tag by its name: `strings check` and the strict check compare tags between languages, and the registry does not list them. `time` measures the text as written, so tags and name tokens count as characters.

## Timing a scene

`time` plays one event without rendering and reports how long it takes, once for every combination of choice options and allowed battle results:

```
build/event_cli time --config sample_text/timing.cfg --table sample_text/en.strings samples/harbor.ev harbor_arrival
```

For each path it prints the choices taken and the total time, split into dialogue (a line or choice is on screen), action (an action or battle is running and no line is shown) and wait (only `wait` is running). The parts add up to the total. Each run starts with every flag false and every variable 0; set others with `--set flag,flag` and `--var name=3`. A line's time is its character count divided by the text speed, plus the line pause. Pass `--table` so lines that use string IDs have text to measure.

### Timing configuration

```
# Comments start with '#'.
text_speed 30          # characters per second; 0 means instant
line_pause 0.25        # added to every line
choice_time 0          # how long each choice stays up
battle_time 0          # how long each battle lasts
duration move 1.5      # every move
duration move traveler 2.5   # moves of one actor
```

`duration` takes an action name, an optional subject (actor, camera, panel or track) and seconds. The action names are `move`, `face`, `anim`, `show`, `hide`, `panel_show`, `panel_hide`, `panel_clear`, `camera_cut`, `camera_move`, `fade_in`, `fade_out`, `shake`, `music_play`, `music_stop`, `music_fade`, `ambient_play`, `ambient_stop`, `sound` and `warp`. Menus and rests take no time. An action with no `duration` lasts the seconds written in the script (fades, camera moves, shakes, music fades), or no time at all. The game's data carries the same table (`presentation.durations` in `game.json`, see `../game/README.md`), and the game reports each action's seconds by this rule (`events::action_seconds`), so an engine and this tool time a scene alike.

## Lint

`lint` looks for likely mistakes in scripts that compile. Every finding is a warning; `--strict` makes the command fail when there are any.

- A flag set or cleared but never tested, or tested but never set or cleared, by any script. The same for variables. The game may read or write some of these itself, so treat them as questions.
- An event that no other event calls and that is not an entry event. Entry events are the ones the game starts itself; list them with `--entry harbor_arrival,market_haggle` or in a file with one name per line, given with `--entries`.
- Two options of a `choice` with identical branches, or an `ask` whose `yes` and `no` do the same thing, unless it records the answer with `into`.
- A command right after `return` or `goto` in the same block, which nothing can reach unless a `label` comes first.

## Registry

The registry lists the game's content, so `check --registry` can reject a name the game does not have, such as a misspelled actor or a flag nobody declared. Without a registry any name compiles.

```
# Comments start with '#'.
actor guide innkeeper keeper traveler vendor
marker door_inside pier_end stall_counter
camera pier_wide stall_front
layout full top_left top_right
audio tide_theme harbor_gulls bell_chime
animation bow shrug wave
portrait cheerful strained
item brass_key harbor_map
member guide traveler
encounter lamp_room_moths
map harbor lighthouse_base
map_markers harbor pier_end stall_counter
map_markers lighthouse_base door_inside
event_map harbor harbor_arrival
enemy lamp_moth
vehicle skiff
shop vendor_stall
rest harbor_inn

flag harbor.met_guide
flag scene.door_open local default true
var harbor.first_stop
var market.offer local default 30
```

- A kind and one or more names per line. The kinds are `actor` (also every `say` speaker and every `show`, `move`, `face` and `anim` subject), `marker` (`move`, `face` and `warp` targets), `camera`, `layout` (the `layout` of `panel show`), `audio` (music, ambient sounds and sound cues), `animation`, `portrait` (the `as` of `say`), `item` (also the `item` test), `member` (`party`, `name entry` and the `party` test), `encounter`, `enemy` (`until defeat`), `vehicle` (`battle ... aboard`, the `vehicle` commands and the `vehicle` and `aboard` tests), `ability` (`learn`), `map`, `shop` and `rest`. Panel IDs are not listed; each scene names its own.
- `map_markers <map> <marker>...` lists the markers on one map; they count as listed markers. A marker on several maps is listed for each. `warp <map> <marker>` to a marker that is not on that map is an error. The checker also follows the map the party is on through each event, from its last `warp` (and from the start, see `event_map`), so a `move` to a marker on another map is an error too. Where the map cannot be known (after a branch that warps on one side only, a `call`, a `label`, or in a loop that warps), nothing more is checked until the next `warp`.
- `event_map <map> <event>...` says the game starts those events on that map, for example from a hook in that area. An event listed for one map starts the move check on it; one listed for several, or none, starts with the map unknown.
- `enemy <name>...` lists the enemies a `battle ... until defeat` may name.
- Every map named on a `map_markers` or `event_map` line must be on a `map` line.
- `flag <name>` and `var <name>` declare one flag or variable each, with `global` (the default) or `local`, and `default <value>` (`true` or `false` for a flag, an integer for a variable). Using a flag as a variable, or the other way round, is an error.
- A `local` flag or variable belongs to one run of an event: it starts at its default each time the game starts an event, is shared with the events that event calls, is kept in mid-event saves, and is never part of the game's own state.
- A global default is for the game to apply when it starts a new game; the scripts do not apply it.

## Strict check

`check --strict` is the gate for shipped scripts. On top of the normal checks it fails on:

- quoted text anywhere a player sees it (`say`, `narrate`, prompts and options): every such text must be a string ID, so it can be translated;
- any warning, including an ignored `on skip` block.

Give `--glossary` to fail on glossary names typed out instead of written as name tokens, and on broken or unknown tokens in any of the three forms, in the scripts and in every table given. Give `--table` to run the string table checks; their errors fail the check, and with `--strict` their warnings do too. Give `--used` with the IDs the game shows itself (see [String tables](#string-tables)).

## Error messages

The checker reports every problem it finds as `file:line: error: message`. Each diagnostic also carries a text key and its parameters (`Diagnostic::key` and `params`), so a tool can show it in another language; `diagnostic_text()` has the English template of every key. It catches:

- unknown commands, and missing, extra or wrong-type arguments
- blocks without an `end`, and an `end`, `else`, `option`, `yes`, `no`, `win`, `lose` or `flee` in the wrong place
- unknown or duplicate labels, unknown or duplicate events
- branches that can never run
- commands that are not allowed inside `together` or `race`
- a choice with fewer than two options, and duplicate `yes`, `no`, `win`, `lose` or `flee` sections
- a battle without a `win` section
- with a registry: names it does not list, and flags and variables that are undeclared or used as the other type
