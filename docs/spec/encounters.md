# Encounters

How battles start in the field: visible enemies, the random-battle fallback, battle openings, escape, and how encounter pacing is measured against the reference build. Scripted battles started by events are in [EVT-009](events-and-live-panels.md). Battle rules are in [battle.md](battle.md).

## Mode and data

| ID | Requirement | Label | Source |
|---|---|---|---|
| ENC-001 | Field battles start from visible enemies, tuned to match the original's encounter pacing | Proposed (scope) | S-SYS-ENC |
| ENC-002 | Random battles stay available as a fallback. Each area has a build-time encounter mode, Visible or Random. It is content data, not a player setting. Switching an area's mode needs no other content change | Proposed (scope) | S-RISKS |
| ENC-003 | Visible mode is committed for production only after the encounter prototype meets the tolerances in ENC-033 in at least one dungeon and one world-map zone | Proposed (scope) | S-SYS-ENC, S-RISKS |
| ENC-004 | Both modes draw enemy groups from the same per-area encounter table, built from the original's tables. No area contains a group that cannot appear there in the original, subject to the roster decision in S-OPEN 7 | Proposed (spec) | S-SOURCE, S-FAN-CUT |
| ENC-005 | To measure: per area, the original's encounter table (groups and weights), encounter rate (distribution of steps between battles), which areas have no encounters, and the enemy group size range. Method: rights holder data; cross-check by recording at least 100 encounters in each of five sampled areas of the reference build. Feeds ENC-004, ENC-040 and ENC-031 | To measure | S-SOURCE |
| ENC-006 | Encounter randomness uses its own seeded stream, separate from battle seeds. Its state is saved. The remake matches the original's distributions, not its exact random sequence | Proposed (spec) | S-ENGINE |
| ENC-007 | Battles while in a vehicle are vehicle battles ([BAT-140](battle.md)) and use the vehicle's own encounter table | Proposed (spec) | S-CONTENT |

## Visible enemies

| ID | Requirement | Label | Source |
|---|---|---|---|
| ENC-010 | Each visible area has authored spawn points and a cap on simultaneous visible groups. Each visible enemy stands for one group, drawn from the encounter table when it spawns. Its model is the group's lead enemy | Proposed (spec) | S-SYS-ENC |
| ENC-011 | A battle starts when a visible enemy's collision capsule overlaps the leader's. The battle uses the group drawn at spawn | Proposed (spec) | S-SYS-ENC |
| ENC-012 | A visible enemy idles or wanders inside a leash radius. It notices the leader inside a detection radius with line of sight, then chases for a limited time and returns to its leash. Detection radius, chase speed and chase time are per-enemy data | Proposed (spec) | S-SYS-ENC |
| ENC-013 | Default chase speed is 1.1 times the leader's walking speed and default chase time is 6 seconds, so a player can sometimes outrun a group but not always | Proposed (spec) | S-SYS-ENC |
| ENC-014 | A defeated group does not come back until the party leaves the area and re-enters it | Proposed (spec) | S-SYS-ENC |
| ENC-015 | For 3 seconds after any battle ends, no visible enemy can start a battle or chase. A group the party escaped from stays in the field and obeys the same rule | Proposed (spec) | S-PILLAR-1 |
| ENC-016 | Spawn count, spawn points, respawn rule and enemy movement values are per-area data, tuned until the area meets ENC-033 | Proposed (spec) | S-SYS-ENC |
| ENC-017 | In visible mode, spawn points are placed on the critical path so a player who does not try to avoid enemies fights about as many battles as in the original. A player who avoids enemies on purpose must still reach each boss within ENC-034's level gap | Proposed (spec) | S-RISKS |

## Openings (ambush and advantage)

`core/` takes the opening as a battle setup input ([BAT-004](battle.md)). This section decides which opening a battle gets.

| ID | Requirement | Label | Source |
|---|---|---|---|
| ENC-020 | Visible mode: if the leader touches an enemy from inside the rear 90-degree arc of that enemy's facing while it is not chasing, the opening is Party first | Proposed (spec) | S-SYS-ENC |
| ENC-021 | Visible mode: if a chasing enemy touches the leader from inside the rear 90-degree arc of the leader's facing, the opening is Enemy first. Every other contact is Normal | Proposed (spec) | S-SYS-ENC |
| ENC-022 | Random mode: the opening is drawn with the original's probabilities from ENC-024. The community disassembly shows the original's rule ([published-mechanics.md](../research/published-mechanics.md)): a chance score of the party's highest agility against a number stored with each enemy formation, Enemy first at 12 or below and Party first above 116. Until the formation numbers are known, `game/` draws from per-zone percentages in data | Proposed (spec) | S-GOALS |
| ENC-023 | Across a full playthrough, the share of Party first and Enemy first openings in visible mode is within 5 percentage points of the original's shares | Proposed (spec) | S-GOALS |
| ENC-024 | To measure: whether the original has preemptive or ambush openings, their probabilities, what affects them, and their exact effect on the first round. Method: rights holder data; cross-check over the encounters recorded for ENC-005. Feeds ENC-020 to ENC-023 and BAT-004 | To measure | S-GOALS |

## Escape

| ID | Requirement | Label | Source |
|---|---|---|---|
| ENC-030 | Escape is a party command. It resolves at its place in the execution order. On success the battle ends at once with the result Escaped, actions not yet executed are dropped, and no rewards are given. On failure the round continues. Published sources say Run is one party-level entry in the original's top battle menu; whether it takes a place in the order is unknown (ENC-035); see [published-mechanics.md](../research/published-mechanics.md) | Proposed (spec) | L-CORE |
| ENC-031 | Escape success uses the original's rule from ENC-035. Read from the community disassembly ([published-mechanics.md](../research/published-mechanics.md)): escape succeeds when no enemy can act; otherwise it fails when twice (a roll of 0 to 63, plus the highest agility among living party members, less the formation's escape number) is 40 or below. `core/` follows this and uses the highest living enemy agility as the formation's number until the real numbers are known. A battle setup flag disables escape; bosses and battles whose event says so set it ([EVT-009](events-and-live-panels.md)) | Proposed (spec) | S-GOALS |
| ENC-032 | Escape is always shown in the command menu. When disabled for the battle it is shown greyed with the reason | Proposed (spec) | S-FAN-TEXT |
| ENC-035 | To measure: the original's escape rule: who can choose it, success chance and its inputs (agility, level, group), what happens to the rest of the party's commands on failure, and which battles forbid it. Method: rights holder formulas; cross-check with at least 200 attempts across three areas in the reference build. Feeds ENC-030 and ENC-031 | To measure | S-GOALS |

## Pacing measurement

| ID | Requirement | Label | Source |
|---|---|---|---|
| ENC-033 | The remake passes encounter pacing when, per area on the critical path at Original difficulty: battles per traversal (M1) and median field seconds between battles (M2) are each within 20% of the reference build; average party level at each boss start (M5) is within 1 level; and dungeon completion time (M6) is within 15%. Experience and money per hour (M4) are reported but have no tolerance | Proposed (spec) | S-SYS-ENC, S-RISKS |
| ENC-034 | Players who avoid visible enemies on purpose reach each boss no more than 2 levels below the reference build's M5. If they fall lower, spawn placement changes, not rewards | Proposed (spec) | S-RISKS |
| ENC-036 | To measure: the reference build baselines for M1 to M6 on the critical path of every area. Method: at least three players play the reference build along the critical path without deliberate grinding or avoidance; a logging tool or video review records each battle's start, end, area, rewards and party levels. Feeds ENC-033 | To measure | S-SYS-ENC |
| ENC-037 | The remake logs the same metrics in playtest builds through telemetry, with the same area and landmark IDs as the baseline, so the comparison is per area and automatic | Proposed (spec) | S-SYS-ENC |

| Metric | Definition |
|---|---|
| M1 | Battles fought between entering an area and reaching its exit landmark on the critical path |
| M2 | Median field time (excluding menus and events) between the end of one battle and the start of the next |
| M3 | Share of total play time spent in battle |
| M4 | Experience and money earned per hour, per area |
| M5 | Average level of the active party at the first command input of each boss battle |
| M6 | Time from entering a dungeon to the end of its boss battle |

## Random-battle fallback

| ID | Requirement | Label | Source |
|---|---|---|---|
| ENC-040 | In random mode, a distance counter replaces the original's step counter. One original step equals the distance from ENC-041. A battle triggers when the counter passes a threshold drawn from the original's distribution for that area | Proposed (spec) | S-RISKS |
| ENC-041 | To measure: the world distance one original step represents, as the ratio between the original's tile size and the remake's scale for each area kit. Method: derive from the area's measured walk time in FLD-022 divided by its step count over the same route. Feeds ENC-040 | To measure | S-RISKS |
| ENC-042 | The random counter does not advance for 3 seconds after a battle, in towns, or in areas with no encounters in the original | Proposed (spec) | S-PILLAR-1 |
