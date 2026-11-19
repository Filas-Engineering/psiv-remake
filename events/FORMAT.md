# Event script format

Event scripts are plain text files, by convention with the `.ev` extension. One file can hold any number of events. All the files you check together form one program, so an event can `call` an event in another file.

Check your scripts with:

```
build/event_cli check samples/*.ev
build/event_cli lint --entries sample_text/entries.txt samples/*.ev
```

`check` reports errors that stop a script from compiling. `lint` reports likely mistakes in scripts that do compile (see [Lint](#lint)).

## The basics

- One command per line. Indentation does not matter to the parser, but use tabs to show nesting.
- Blocks (`if`, `while`, `choice`, `ask`, `battle`, `together`, `sequence` and `event` itself) close with `end`.
- `#` starts a comment that runs to the end of the line. A `#` inside a quoted string is text.
- Text is either a quoted string, `"Like this."`, or a string ID, `@harbor.greeting`. A string ID is looked up in the game's string tables (see [String tables](#string-tables)), so the same script works in every language. Inside quotes, `\"` is a quote, `\\` a backslash and `\n` a line break.
- A renamed name in text is written as a name token, `{name:tide_lamp}`, never typed out. The game shows the shipped or the restored name depending on the "show shipped names" setting (see [Name glossary](#name-glossary)).
- Names (actors, flags, markers, cameras and so on) are letters, digits, `_` and `.`, and do not start with a digit. Use dots to group names: `harbor.met_guide`.
- Numbers are written `3`, `-2` or `1.5`.

## Events

```
event harbor_arrival skippable
	say guide "Welcome."
	set harbor.met_guide
on skip
	set harbor.met_guide
end
```

Every event is marked `skippable` or `unskippable`. A skippable event can have an `on skip` block before its `end`.

When the player skips, the game cancels everything the scene was doing and runs the `on skip` block. That block must leave the world exactly as playing the scene to the end would: the same flags, variables, items, party and player control. Write it as the scene's total effect, as if nothing had happened yet:

```
on skip
	give harbor_map
	var harbor.visits += 1
	set harbor.met_guide
	unlock player
end
```

The player may skip halfway, after the scene has already given some items. The interpreter keeps count of what the played part of the scene gave, took and added with `+=`/`-=`, including inside events it called. When `on skip` gives, takes or adds, it only applies the part that is still missing. If the scene already gave the map, `give harbor_map` in `on skip` gives nothing; if it gave one of two shells, `give shell 2` gives one more. So a skip at any moment applies each change once.

`set`, `clear`, `party add`, `party remove`, `var x = n`, `lock player` and `unlock player` are safe to repeat and need nothing special.

Guarding with a flag, as older scripts do, still works:

```
on skip
	if not harbor.met_guide
		give harbor_map
		set harbor.met_guide
	end
end
```

The count is per item and per variable, and per direction: a `give` in `on skip` is only reduced by gives in the played part, a `take` only by takes. It covers changes made by the script, not ones the game makes itself (battle rewards, for example). It is kept in mid-event saves.

`on skip` cannot contain `say`, `narrate`, `choice`, `ask`, `battle`, `together`, `call`, `goto` or `label`. It can use `if`, `while`, `wait` and any world command (warp, music, camera, fade and so on).

If a skippable event changes game state but has no `on skip` block, the checker warns you.

### Skipping inside other events

A skippable event can be called from an unskippable one. If the player skips while it runs, only that event is skipped. Its `on skip` block runs, then the calling event carries on from the line after the `call`. When several skippable events are nested, the skip goes back to the outermost one in that chain and runs its `on skip` block.

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
choice "Where to first?"
option "The market"
	var harbor.first_stop = 1
option @harbor.option_rest
	say guide "The inn it is."
end
```

A yes/no question. Both branches are optional:

```
ask "Carry the satchel?"
yes
	set harbor.satchel_carried
no
	say guide "Suit yourself."
end
```

## Flags and variables

Flags are true or false and start false. Variables are whole numbers and start at 0.

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
| `sound <cue> [nowait]` | `sound bell_chime` |

## Waiting and running things in parallel

By default every actor, camera, panel, screen and audio command waits until the game reports it is done. For `music play` and `sound`, the game reports done as soon as playback starts. A looping `anim` never waits.

- `nowait` starts a command and goes straight to the next line.
- `sync` waits for every `nowait` command still running.
- An event does not end until its `nowait` commands have finished.
- `wait <seconds>` pauses the script: `wait 0.5`.

`together` runs each command inside it at the same time, and continues once all of them are done. To chain several steps in one parallel branch, wrap them in `sequence`:

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

Inside `together` you can use only actor, camera, panel, screen and audio commands, `wait` and `sequence`. You cannot use `nowait` there, because every branch already runs in parallel. A `sequence` cannot contain another `sequence`.

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
| `lock player` | stops player input during the scene |
| `unlock player` | gives control back |

## Battles

```
battle lamp_room_moths
win
	give brass_key
lose
	warp harbor pier_end
flee
	say guide "We can come back later."
end
```

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

It reports, as errors, an ID a script uses that a table lacks. As warnings it reports an ID no script uses, an ID one language has and another lacks, and an ID whose name tokens differ between languages. Without script files it only compares the tables with each other.

To move a scene from quoted text to IDs, print a proposed table:

```
build/event_cli strings extract samples/harbor.ev > harbor_proposed.strings
```

It lists every quoted text in the scripts with an ID of the form `<event>.<n>`, and the file and line it came from (options show the line of their `choice`). It changes no file. Rename the IDs you want, add the lines to your tables, and replace each quoted text in the script with its `@id`.

## Name glossary

The glossary lists every renamed name: what the shipped release called it and its restored name.

```
# id | shipped name | restored name | reason
tide_lamp | Tidlamp | Tide Lantern | cut by the item name length limit
moth_swarm | MothSwm | Moth Swarm | cut by the enemy name length limit
```

One entry per line, fields separated by `|`. The reason is optional. In text, write `{name:tide_lamp}` instead of the name. The game shows the restored name, or the shipped name when the player turns on "show shipped names". That setting only applies to English text; other languages always show the restored name.

List every line that mentions a glossary name:

```
build/event_cli names sample_text/glossary.txt --table sample_text/en.strings samples/*.ev
```

It reports each token with the name it shows (add `--show-shipped` to see the shipped set), each name typed out instead of using a token, and each token that is broken or not in the glossary. Lines that use a string ID are checked in every table given. Broken and unknown tokens make the command fail.

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

`duration` takes an action name, an optional subject (actor, camera, panel or track) and seconds. The action names are `move`, `face`, `anim`, `show`, `hide`, `panel_show`, `panel_hide`, `panel_clear`, `camera_cut`, `camera_move`, `fade_in`, `fade_out`, `shake`, `music_play`, `music_stop`, `music_fade`, `sound` and `warp`. An action with no `duration` lasts the seconds written in the script (fades, camera moves, shakes, music fades), or no time at all.

## Lint

`lint` looks for likely mistakes in scripts that compile. Every finding is a warning; `--strict` makes the command fail when there are any.

- A flag set or cleared but never tested, or tested but never set or cleared, by any script. The same for variables. The game may read or write some of these itself, so treat them as questions.
- An event that no other event calls and that is not an entry event. Entry events are the ones the game starts itself; list them with `--entry harbor_arrival,market_haggle` or in a file with one name per line, given with `--entries`.
- Two options of a `choice` with identical branches, or an `ask` whose `yes` and `no` do the same thing.
- A command right after `return` or `goto` in the same block, which nothing can reach unless a `label` comes first.

## Error messages

The checker reports every problem it finds as `file:line: error: message`. It catches:

- unknown commands, and missing, extra or wrong-type arguments
- blocks without an `end`, and an `end`, `else`, `option`, `yes`, `no`, `win`, `lose` or `flee` in the wrong place
- unknown or duplicate labels, unknown or duplicate events
- branches that can never run
- commands that are not allowed inside `together` or `on skip`
- a choice with fewer than two options, and duplicate `yes`, `no`, `win`, `lose` or `flee` sections
- a battle without a `win` section
