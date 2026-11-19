# Event script format

Event scripts are plain text files, by convention with the `.ev` extension. One file can hold any number of events. All the files you check together form one program, so an event can `call` an event in another file.

Check your scripts with:

```
build/event_cli check samples/*.ev
```

## The basics

- One command per line. Indentation does not matter to the parser, but use tabs to show nesting.
- Blocks (`if`, `while`, `choice`, `ask`, `battle`, `together`, `sequence` and `event` itself) close with `end`.
- `#` starts a comment that runs to the end of the line. A `#` inside a quoted string is text.
- Text is either a quoted string, `"Like this."`, or a string ID, `@harbor.greeting`. A string ID is looked up in the game's localization table, so the same script works in every language. Inside quotes, `\"` is a quote, `\\` a backslash and `\n` a line break.
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

When the player skips, the game cancels everything the scene was doing and runs the `on skip` block. That block must leave the world exactly as playing the scene to the end would: the same flags, variables, items, party and player control. It must also work when the player skips halfway, after some of those changes have already happened. So guard any change that adds up:

```
on skip
	if not harbor.met_guide
		give harbor_map
		set harbor.met_guide
	end
	unlock player
end
```

`set`, `clear`, `party add`, `party remove`, `var x = n`, `lock player` and `unlock player` are safe to repeat. `give`, `take` and `var x += n` are not, so put them behind a flag as shown above.

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

All three branches are optional. When the result has no branch, the script carries on after `end`. The game decides what losing means (for example, a game over screen) before the script sees the result.

## Error messages

The checker reports every problem it finds as `file:line: error: message`. It catches:

- unknown commands, and missing, extra or wrong-type arguments
- blocks without an `end`, and an `end`, `else`, `option`, `yes`, `no`, `win`, `lose` or `flee` in the wrong place
- unknown or duplicate labels, unknown or duplicate events
- branches that can never run
- commands that are not allowed inside `together` or `on skip`
- a choice with fewer than two options, and duplicate `yes`, `no`, `win`, `lose` or `flee` sections
