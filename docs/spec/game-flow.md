# Game flow

Top-level modes, how the game moves between them, loading, autosave timing, and the budgets that apply to every mode. What a save contains is in [save-and-progression.md](save-and-progression.md).

## Architecture and platform

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLOW-001 | The game targets current consoles and PC. There is no handheld-class target. Every performance and loading budget in these specs is measured on the lowest-spec supported console and the minimum-spec PC | Decided | D-PLATFORMS |
| FLOW-002 | The game is built in Unreal Engine 5 | Proposed (scope) | P-ENGINE |
| FLOW-003 | Battle rules live in `core/` and event logic lives in `events/`. Neither library depends on the engine. The engine reaches them through thin adapters, so both libraries can be reused by a follow-up and tested without rendering | Proposed (spec) | S-GOALS, L-CORE, L-EVENTS |
| FLOW-004 | The field and battle run at 60 frames per second on every target in FLOW-001. Story panels follow [EVT-027](events-and-live-panels.md) | Proposed (scope) | P-FPS, S-FAN-DEFECTS |
| FLOW-005 | A first playthrough of the main story takes close to the original's length (roughly 20 hours per the scope). Content is not padded to add hours | Proposed (scope) | P-LENGTH, S-ANTIGOALS |
| FLOW-006 | To measure: median time to finish the main story in the reference build. Method: at least three full critical-path playthroughs by players new to the game and three by experienced players, timed from new game to the final scene, with the guild jobs done and not done as two separate numbers. Feeds FLOW-005 and [ACC-FLOW-05](acceptance.md) | To measure | P-LENGTH |

## Modes

| Mode | Purpose | Owned by |
|---|---|---|
| Title | New game, continue, load, settings, quit | This file |
| Field | Player-controlled movement and interaction | [field.md](field.md) |
| Battle | Turn-based combat | [battle.md](battle.md) |
| Cutscene | An event that takes control from the player, including live panels | [events-and-live-panels.md](events-and-live-panels.md) |
| Menu | Field menu, shops, save and load screens, settings | [menus-and-ui.md](menus-and-ui.md) |

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLOW-010 | Exactly one top-level mode is active at a time: Title, Field, Battle, Cutscene or Menu. A system pause overlay can sit on top of any mode and does not change the mode | Proposed (spec) | S-ENGINE |
| FLOW-011 | The Field mode has explicit sub-states that cover at least: free control, field menu, interaction, event, shop. Only one sub-state is active at a time | Proposed (scope) | S-ENGINE |
| FLOW-012 | To measure: the full list of the original's ten field sub-states and what each allows. Method: from the rights holder's engine documentation if it exists; otherwise enumerate every distinct field interface state in the reference build (what input does, what is on screen) and map each to a remake sub-state. Feeds FLOW-011 | To measure | S-ENGINE |
| FLOW-013 | Allowed transitions are only those in the table below. Any other transition is a bug and is logged | Proposed (spec) | S-ENGINE |
| FLOW-014 | During a transition, player input is discarded. No input pressed before a transition is applied after it | Proposed (spec) | S-PILLAR-1 |
| FLOW-015 | The system pause overlay is available in every mode except during a save write. Pausing in Battle or Cutscene freezes the presentation and the interpreter; it never changes rules state | Proposed (spec) | S-GOALS |
| FLOW-016 | When a console suspends and resumes the game, the game resumes in the same mode and sub-state with no loss of progress | Proposed (spec) | D-PLATFORMS |

### Transition table

| From | To | Trigger |
|---|---|---|
| Title | Field | New game (via the opening event) or a load |
| Title | Menu | Settings, load screen |
| Field | Battle | Contact with a visible enemy or a random-battle trigger ([encounters.md](encounters.md)) |
| Field | Cutscene | An event starts ([FLD-023](field.md)) |
| Field | Menu | Field menu, shop, inn, save screen |
| Battle | Field | Victory, escape, or a scripted end that returns to the field |
| Battle | Cutscene | The battle was started by an event; control returns to the event's result branch |
| Battle | Title | Defeat with no defeat branch, after the game-over screen |
| Cutscene | Field | Event ends |
| Cutscene | Battle | Event starts a battle ([EVT-009](events-and-live-panels.md)) |
| Cutscene | Menu | Event opens a shop, a name entry, or a save prompt |
| Menu | Field, Title, Cutscene | Menu closes back to the mode that opened it |

## Title

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLOW-020 | The title screen offers: Continue, New Game, Load, Settings, and Quit on PC only | Proposed (spec) | S-SYS-QOL |
| FLOW-021 | Continue loads the most recent save by write time, manual or autosave. It is hidden when no save exists | Proposed (spec) | S-SYS-QOL |
| FLOW-022 | Quit to Title from the pause overlay asks for confirmation and states the play time since the last save | Proposed (spec) | S-SYS-QOL |

## Loading and transitions

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLOW-030 | From field contact or a random trigger to the first party command input, a battle start takes at most 1.5 seconds at default battle speed, and no longer than the reference build's measured time from FLOW-033 | Proposed (spec) | S-PILLAR-1, S-ANTIGOALS |
| FLOW-031 | From the last result screen input to field control, a battle end takes at most 1.0 second. The party is at the position it had when the battle started, under the same field camera | Proposed (spec) | S-PILLAR-1 |
| FLOW-032 | Moving between areas takes at most 3 seconds from fade-out to control on the targets in FLOW-001. There is no loading screen inside one town, one dungeon floor or one world-map zone | Proposed (spec) | S-PILLAR-1 |
| FLOW-033 | To measure: in the reference build, the time from encounter trigger to first command input, from the last battle input to field control, and from a door or exit to control in the next map. Method: frame counting on captures, 20 samples each across at least five areas. Feeds FLOW-030 to FLOW-032 | To measure | S-ENGINE |
| FLOW-034 | Every enemy, effect and battle stage an area can use is resident or streamed before the player can trigger a battle in that area, so FLOW-030 never waits on disk | Proposed (spec) | S-PILLAR-1 |

## Defeat

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLOW-040 | When the party is defeated in a battle that has no defeat branch ([EVT-009](events-and-live-panels.md)), the game shows a game-over screen and applies the original's defeat outcome from FLOW-041 | Proposed (spec) | S-GOALS |
| FLOW-041 | To measure: what the original does after party defeat (return to title, return to a town, any penalty to money or items, what state is kept). Method: lose battles on purpose at three points in the reference build (early field, mid-game dungeon, a boss) and record the result. Feeds FLOW-040 | To measure | S-GOALS |
| FLOW-042 | The game-over screen also offers "Retry battle". It reloads the autosave taken before that battle (FLOW-050) and starts the same battle with a new seed per [BAT-134](battle.md). This is a quality-of-life addition the scope does not list | Proposed (spec) | S-SYS-QOL |

## Autosave points

What is written is in [SAV-001](save-and-progression.md). This section says when.

| ID | Requirement | Label | Source |
|---|---|---|---|
| FLOW-050 | The game autosaves at each point in the table below and at no other time | Proposed (scope) | S-SYS-QOL |
| FLOW-051 | No autosave happens during a battle, during a choice, or while text is on screen. The only mid-event autosave is the one before an event battle, which stores interpreter state per [EVT-050](events-and-live-panels.md) | Proposed (spec) | S-SYS-QOL, L-EVENTS |
| FLOW-052 | An autosave never blocks input for more than 100 ms. A small non-flashing indicator shows while it writes | Proposed (spec) | S-PILLAR-1, S-SYS-ACCESS |
| FLOW-053 | If an autosave write fails, the game shows a non-modal warning and tries again at the next autosave point. It never stops play | Proposed (spec) | S-SYS-QOL |
| FLOW-054 | Autosaves rotate through three slots, separate from manual slots ([SAV-002](save-and-progression.md)) | Proposed (spec) | S-SYS-QOL |

| Autosave point | When exactly |
|---|---|
| Area entry | After loading into an area the party has not been in since the last autosave, once control returns |
| Event end | After an event that changed at least one story flag, once control returns |
| Before a scripted or boss battle | After the battle is set up and before the first command input |
| After rest | After an inn or equivalent rest completes |
| Vehicle | After a vehicle is acquired, once control returns |
