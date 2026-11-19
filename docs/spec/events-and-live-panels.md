# Events and live panels

How cutscenes and field events are authored in the `events/` script format and run by its interpreter, how live panels work, skip rules, mid-event saves, and the pacing budget per scene. Dialogue text rules are in [dialogue-and-text.md](dialogue-and-text.md). When events autosave is [FLOW-051](game-flow.md).

## Authoring

| ID | Requirement | Label | Source |
|---|---|---|---|
| EVT-001 | Every event is authored by hand in the `events/` script format. Nothing is converted from the original, whose events are hand-written code. The 128 field event hooks are the minimum count, not the full count | Proposed (scope) | S-ENGINE, S-COUNTS |
| EVT-002 | The script format supports: dialogue, choices, flag reads and writes, actor movement and animation, parallel blocks, live panels bound to cameras, audio, battles with result branches, and warps | Proposed (spec) | L-EVENTS |
| EVT-003 | Scripts compile ahead of time. Compilation fails on: an unknown actor, camera, panel layout, string ID, audio cue or enemy group; an undeclared flag; a battle with no Victory branch | Proposed (spec) | L-EVENTS |
| EVT-004 | Flags are declared in a registry with a type (boolean or integer), a scope (global or event-local) and a default. Global flags are saved | Proposed (spec) | L-EVENTS |
| EVT-005 | No event changes plot or outcomes. No branch changes who lives or dies. Player choices exist only where the original has them (EVT-007) | Proposed (scope) | P-STORY, S-NONGOALS |
| EVT-006 | A choice's result is written to a flag before its branch runs | Proposed (spec) | L-EVENTS |
| EVT-007 | To measure: every player choice in the original, its options, and what each option changes. Method: rights holder script and data; cross-check by taking each option in the reference build. Feeds EVT-005 | To measure | P-STORY |
| EVT-008 | Parallel blocks support two joins: wait for all, and wait for any. When a block ends by "wait for any", the other branches stop at their next yield point and apply nothing further | Proposed (spec) | L-EVENTS |
| EVT-009 | A battle command takes an enemy group, an escape-allowed flag, and branches for Victory, Defeat and Escaped. Victory is required. If Defeat has no branch, defeat goes to the game-over flow ([FLOW-040](game-flow.md)). If Escaped has no branch, escape is disabled for that battle | Proposed (spec) | L-EVENTS |
| EVT-010 | A warp moves the party to an area and spawn point and loads the area. The event continues after the load if it has more commands | Proposed (spec) | L-EVENTS |
| EVT-011 | Audio commands set music, ambient sound and one-shot effects. Music and ambient state are part of game state, so skip and load restore them | Proposed (spec) | L-EVENTS |

## Interpreter and Host

| ID | Requirement | Label | Source |
|---|---|---|---|
| EVT-020 | The interpreter talks to the engine only through the Host interface. Each Host call that takes time (show text, move actor, play animation, camera or panel change, battle, warp, wait) returns a handle the interpreter waits on | Proposed (spec) | L-EVENTS |
| EVT-021 | Given the same script, starting state and Host responses, the interpreter issues the same sequence of Host calls and state changes. It can run against a mock Host with no engine | Proposed (spec) | L-EVENTS |
| EVT-022 | The interpreter can suspend at any yield point and serialize its full state: position in each running block, local values, pending waits, and which presentation is active (panel page, music, actor poses) | Proposed (spec) | L-EVENTS |
| EVT-023 | Each Host call is tagged as state-changing or presentation-only. The tag drives skip (EVT-041) | Proposed (spec) | L-EVENTS |

## Live panels

| ID | Requirement | Label | Source |
|---|---|---|---|
| EVT-030 | Story scenes play as manga panels. Each panel is a live viewport into a 3D scene with its own camera, lighting and animation. Story scenes are not replaced by ordinary full-screen cutscenes | Proposed (scope) | S-PILLAR-2, S-ANTIGOALS |
| EVT-031 | No story scene uses illustrated stills | Decided | D-STATIC |
| EVT-032 | No story scene uses pre-rendered video. This is revisited after the vertical slice only for a scene that cannot be done in real time | Decided | D-VIDEO |
| EVT-033 | A panel page is data: a list of panels, each with a normalized screen rectangle, border style, z-order, scene or set binding, camera binding, lighting rig, and enter and exit transitions | Proposed (spec) | S-SYS-PANEL, L-EVENTS |
| EVT-034 | Panel pages are made in the live panel tool, an editor that lays out panels, binds each to a camera in a live scene, previews them live, and saves page data the `events/` scripts reference by ID | Proposed (scope) | S-SYS-PANEL |
| EVT-035 | Scripts control panels with commands to show a page, show or hide a panel, move a panel's camera, and turn the page. A panel renders only while it is on screen | Proposed (spec) | L-EVENTS |
| EVT-036 | Panels on one page may show different scenes, or the same scene through different cameras, at the same time | Proposed (spec) | S-PILLAR-2 |
| EVT-037 | At most four panels on a page animate at once. Other visible panels hold: still rendered live, but with camera and actors not moving, and redrawn only when lighting, framing or content changes. Holding a panel is how the budget is met; a hold is never replaced by a saved image | Proposed (spec) | S-PILLAR-2, D-STATIC |
| EVT-038 | Transitions are cut, slide, wipe, fade and page turn, each with a data-set duration. No transition is a full-screen flash ([UI-015](menus-and-ui.md)) | Proposed (spec) | S-FAN-ACCESS |
| EVT-027 | Panels run at 60 frames per second. A page may be marked to run at 30 only if it misses 60 on a target in [FLOW-001](game-flow.md). Text boxes and input stay at 60 on such pages. The list of 30 pages is tracked and reviewed (S-OPEN 3) | Proposed (scope) | P-FPS, S-OPEN |
| EVT-028 | The dialogue box for a line is anchored to the panel that shows its speaker, found through the line's portrait code | Proposed (spec) | S-SCRIPT, S-PILLAR-2 |
| EVT-029 | Panel camera moves and panel reveals can be timed to text pages and to delivery data ([TXT-025](dialogue-and-text.md)) | Proposed (spec) | S-SCRIPT |
| EVT-039 | Panel sets and character rigs are shared across panels and scenes; a panel page references sets by ID rather than owning a copy | Proposed (scope) | S-RISKS |

## Skip

| ID | Requirement | Label | Source |
|---|---|---|---|
| EVT-040 | Every scene can be skipped, including the first time it plays. Every event that has at least one presentation command is a scene for this rule | Proposed (scope) | S-SYS-QOL, S-FAN-ACCESS |
| EVT-041 | On skip, the interpreter runs the rest of the event in skip mode: state-changing calls apply in order; presentation-only calls complete at once with their end state (final actor positions, final music); waits take zero time. Skip mode stops at the next choice, the next battle, or the end of the event | Proposed (spec) | L-EVENTS |
| EVT-042 | The game state after a skip is identical to the state after watching the same scene to the end with the same choices. Play time and the text log are excluded from the comparison | Proposed (scope) | S-SYS-QOL |
| EVT-043 | Skipping takes a hold of 1 second on the skip button, with a visible fill, or a press followed by a confirm. A single accidental press never skips | Proposed (spec) | S-SYS-QOL |
| EVT-044 | After a choice or battle that stopped skip mode, the player can skip again | Proposed (spec) | S-SYS-QOL |

## Mid-event saves

| ID | Requirement | Label | Source |
|---|---|---|---|
| EVT-050 | The autosave before an event battle stores the interpreter state (EVT-022) and the game state at the moment the event started | Proposed (spec) | L-EVENTS |
| EVT-051 | Loading a mid-event save resumes at the same command with the presentation rebuilt: panel page, music, ambient sound and actor poses | Proposed (spec) | L-EVENTS |
| EVT-052 | Each compiled script has a content hash. If a mid-event save's script hash does not match the installed script, the game restores the stored event-start state and restarts the event from its first command | Proposed (spec) | L-EVENTS |
| EVT-053 | The player cannot make a manual save while an event runs | Proposed (spec) | L-EVENTS |

## Scene pacing

The scope asks for a pacing budget per scene, measured against the original (S-RISKS). It does not set the budget. EVT-060 proposes it.

| ID | Requirement | Label | Source |
|---|---|---|---|
| EVT-060 | Each scene's presentation overhead (total time minus time spent waiting for the player to advance text) is no more than 110% of the original scene's overhead, at normal text speed with text advanced as soon as each page completes | Proposed (spec) | S-RISKS, S-ANTIGOALS |
| EVT-061 | To measure: for every scene, the original's total time from trigger to control returning, and its presentation overhead, with text advanced as soon as each page completes. Method: frame counting on reference build captures, using the text reveal speed from TXT-026. Feeds EVT-060 | To measure | S-RISKS |
| EVT-062 | Scene durations are computed without rendering: the interpreter runs each scene against a timing Host that returns the authored duration of each presentation call. The result is compared with the EVT-061 baseline in CI | Proposed (spec) | L-EVENTS, S-RISKS |
