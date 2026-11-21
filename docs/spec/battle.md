# Battle

The battle rules `core/` implements, the contract between `core/` and the renderer, difficulty, and determinism. How a battle starts in the field is in [encounters.md](encounters.md). Escape is [ENC-030](encounters.md). Battle frame rate is [FLOW-004](game-flow.md). Leveling after victory is in [save-and-progression.md](save-and-progression.md).

The scope says battle is the largest systems task and the original's battle logic spans over 40 fighter states (S-ENGINE). Many rules below therefore wait on To measure rows. Until a To measure row is answered, `core/` implements the Proposed (spec) default and keeps the rule in data so the answer is a data change.

## Ownership

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-001 | Combat is turn-based. There is no real-time or action input that affects results | Proposed (scope) | P-COMBAT, S-NONGOALS |
| BAT-002 | All battle rules live in `core/`: headless C++17, no engine dependency, no wall-clock time, no file or network access during resolution | Proposed (spec) | L-CORE |
| BAT-003 | The party has up to five active members. The enemy side is one enemy group | Proposed (scope) | S-SYS-BATTLE |
| BAT-004 | A battle is set up from one input record: party combatants (stats, equipment-derived values, abilities, skill uses left, carried statuses), the enemy group, the opening (Normal, Party first, Enemy first), flags (escape allowed, defeat has a branch, vehicle battle), the difficulty overlay ([BAT-124](#difficulty)), and the battle seed | Proposed (spec) | L-CORE |
| BAT-005 | To measure: the original's maximum enemy group size, formation positions, and whether enemies are arranged in rows that affect targeting. Method: rights holder data; cross-check by inspecting the largest groups in the reference build. Feeds BAT-004 and BAT-070 | To measure | S-ENGINE |

## Round lifecycle

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-010 | Each round runs these phases in order: (1) round start effects; (2) command selection for every living combatant that can act; (3) initiative and order; (4) execution of each action in order; (5) round end effects and status duration updates. End conditions are checked after every action and after phase 5 | Proposed (spec) | L-CORE |
| BAT-011 | An enemy's target is picked when the round's order is resolved, and its action is picked when its turn comes, from battle state and history only. Enemy AI never reads the party's commands for the current round. This follows the original ([original-battle-rules.md](../research/original-battle-rules.md)) | Proposed (spec) | L-CORE |
| BAT-012 | A combatant whose statuses prevent acting gets no command for the round. A status that forces a command (for example a random target) supplies it in phase 2 | Proposed (spec) | L-CORE |
| BAT-013 | The battle ends with exactly one result: Victory (every enemy is defeated), Defeat (every party member is defeated or held by a status that counts toward Defeat), Escaped ([ENC-030](encounters.md)), or Scripted end (a condition set by the event, such as surviving a number of rounds). In the original paralysis counts toward Defeat and sleep does not; a party that is all asleep keeps playing rounds in which only enemies act ([original-battle-rules.md](../research/original-battle-rules.md)) | Proposed (spec) | L-CORE |
| BAT-014 | To measure: which party states count toward Defeat in the original (HP zero only, or also statuses that prevent acting), and whether enemy group defeat ignores enemies that cannot act. Method: rights holder data; cross-check by applying each disabling status to the whole party in the reference build. Feeds BAT-013 | To measure | S-ENGINE |
| BAT-015 | Each action is validated when it executes, not when chosen: the actor can still act, its cost can still be paid, and its target is still valid. An invalid target is retargeted per BAT-071. If no valid target remains or the cost cannot be paid, the action is cancelled with an event stating the reason | Proposed (spec) | L-CORE |
| BAT-016 | To measure: when status effects tick and durations count down in the original (round end, or the affected combatant's own action), and what happens to an action whose target fell earlier in the round. Method: rights holder data; cross-check in the reference build by targeting an enemy that a faster ally defeats first. Feeds BAT-010, BAT-015 and BAT-071 | To measure | S-ENGINE |

## Command selection

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-020 | The command set per party member is the original's set (BAT-023), plus top-level entries for Macro and Auto. The menu layout is in [UI-006](menus-and-ui.md). Published sources say the original has no Auto entry and that Run is one party-level entry in the top menu, so Auto is an addition; see [published-mechanics.md](../research/published-mechanics.md) | Proposed (spec) | S-PILLAR-1 |
| BAT-021 | The player can step back to a previous party member's command until the round is locked | Proposed (spec) | S-GOALS |
| BAT-022 | Command selection has no time limit | Proposed (spec) | P-COMBAT |
| BAT-023 | To measure: the original's full battle command set per party member, what each does, and whether item use and equipment-granted abilities are separate commands. Method: rights holder data and a reference build session. Feeds BAT-020 | To measure | S-ENGINE |

## Order

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-030 | After commands are locked, each combatant that can act gets an initiative: its battle agility plus a random whole number from 0 to just under half the highest battle agility among them, drawn from the battle seed. A combatant with 0 agility gets no random part. Actions execute in descending initiative. Ties go to the party side, then to the lower combatant index. Read from the community disassembly ([published-mechanics.md](../research/published-mechanics.md)); BAT-033 confirms it against the reference build | Proposed (spec) | L-CORE |
| BAT-031 | Initiative is computed once per round. Agility changes during a round take effect next round | Proposed (spec) | L-CORE |
| BAT-032 | The execution order is not shown to the player before execution | Proposed (spec) | S-GOALS |
| BAT-033 | Measured from the community disassembly ([published-mechanics.md](../research/published-mechanics.md)): the original's initiative is battle agility plus a random whole number from 0 to just under half the highest battle agility, with no roll at 0 agility, ties to the party and then the lower slot, and fallen, paralyzed and sleeping combatants left out. No order display was found. To be confirmed against the rights holder's data when it arrives. Feeds BAT-030 to BAT-032 | Proposed (spec) | S-ENGINE |

## Abilities

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-040 | Abilities are data. Kinds: basic attack; technique (costs technique points); skill (limited uses per character, refilled on rest per [FLD-034](field.md)); item use (consumes inventory); enemy skill. Each has: ID, kind, cost, targeting mode, effect list, power, type tags, presentation key, and a flag for field use | Proposed (spec) | L-CORE |
| BAT-041 | The content set covers the counts in the scope: 31 techniques and 103 enemy skills, plus the original's player skills, subject to the phase 0 audit and the cut-content decision in S-OPEN 8 | Proposed (scope) | S-COUNTS |
| BAT-042 | At Original difficulty, damage, healing, hit, critical and resistance formulas match the original's (BAT-043). Read from the community disassembly ([published-mechanics.md](../research/published-mechanics.md)) and implemented in `core/`: a chance score is twice (a roll of 0 to 63, plus the actor's stat, less the target's stat). A basic attack scores dexterity against agility; it misses at 8 or below and is critical above 116, unless it targets a group. No other ability rolls to hit. Damage is the attacking stat times a spread (8 plus sixteen rolls of 0 to 7) over 64, plus the attacking stat, plus twice a bonus, scaled by the target's element factor, less the defending stat, from 1 to 999. The bonus is a quarter of the attacking stat on a critical and the ability's power otherwise. Healing is half of (mental times the spread over 64, plus mental, plus twice the power). A status succeeds when (a roll of 0 to 63, plus the actor's stat, less the target's stat) times the target's element factor is above the ability's threshold. Defend quarters a physical hit before defense is subtracted and does nothing else. A sleeper has an even chance to wake at each round end; paralysis on an enemy ends with the round. Still stand-ins: the stat pairs per ability (mental against mental) and resistance percent as the element factor | Proposed (spec) | S-FAN-BALANCE |
| BAT-043 | To measure: every battle formula (damage, healing, hit, evasion, critical, resistance, status success), per-ability numbers, and per-enemy stats. Method: rights holder data. Cross-check 20 samples per formula against the reference build by recording damage numbers. Feeds BAT-042 | To measure | S-SOURCE |
| BAT-044 | Field-usable techniques, skills and items resolve through the same `core/` functions as in battle | Proposed (spec) | L-CORE |
| BAT-045 | The late-game technique the scope reports as misbehaving behaves as its data intends | Proposed (scope) | S-FAN-DEFECTS |
| BAT-046 | To measure: which technique misbehaves in the original, how, and its intended behavior. Method: get the community bug fix change list in phase 0 (S-FAN-DEFECTS), then confirm intent against the rights holder's Japanese data. Feeds BAT-045 | To measure | S-FAN-DEFECTS |

## Targeting

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-070 | Targeting modes are data: one enemy, all enemies, one ally, all allies, self, one fallen ally, and random enemy. The original's set (BAT-072) is the minimum | Proposed (spec) | L-CORE |
| BAT-071 | Retarget rule when a single target is no longer valid at execution, as in the original ([original-battle-rules.md](../research/original-battle-rules.md)): a single-enemy action moves to the living enemy with the most hit points missing when its first effect is plain damage, otherwise to the one with the least missing, ties settled by coin flips in formation order. A single-ally action is never retargeted: it does nothing and its cost is still paid. An enemy re-picks by its weighted roll | Proposed (spec) | L-CORE |
| BAT-072 | To measure: the original's targeting modes per ability and its retarget behavior. Method: rights holder data; cross-check in the reference build. Feeds BAT-070 and BAT-071 | To measure | S-ENGINE |
| BAT-073 | The target cursor starts on that party member's last target if still valid, else on the first valid target | Proposed (spec) | S-PILLAR-1 |

## Effects and statuses

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-080 | Effects are data. Kinds: damage, heal, resource change, apply status, remove status, stat change, revive, instant defeat. A stat change sets the battle value to the base plus or minus the user's stat, replaces any earlier change to that stat and lasts until the battle ends. Each effect has its own success roll where the formula calls for one | Proposed (spec) | L-CORE |
| BAT-081 | Statuses are data. Each has: ID, duration rule (rounds, a separate duration on enemies, until battle end, until cured, persists outside battle), action rule (normal, cannot act, forced command), stats it holds at 1, statuses it removes, cure conditions, a round-end recovery chance, stacking rule, whether it survives defeat, and whether it counts toward Defeat (BAT-014). No status deals damage in battle | Proposed (spec) | L-CORE |
| BAT-082 | To measure: the original's full status list and each status's duration, persistence after battle, tick effect, cures and action rule. Method: rights holder data; cross-check by inflicting each status in the reference build. Feeds BAT-081 | To measure | S-SOURCE |
| BAT-083 | Every status application, failure and removal produces an event with a message key, so the battle shows a text message for it. This restores the status messages the Western release dropped. The message text is [TXT-012](dialogue-and-text.md) | Proposed (scope) | S-FAN-TEXT |
| BAT-084 | No battle presentation leaves an interface element on screen after the action that created it ends. No status visual hides any part of the affected combatant's model | Proposed (scope) | S-FAN-DEFECTS |

## Enemy AI

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-090 | Each enemy has an AI profile in data, in the original's shape ([original-battle-rules.md](../research/original-battle-rules.md)): 8 action slots, each the basic attack or an ability, and up to 4 overrides, each a condition and an ability. At its turn the enemy draws one slot at random, never the slot index drawn last by any enemy, and the first override whose condition holds replaces the draw | Proposed (spec) | L-CORE |
| BAT-091 | Override conditions available: own HP at or below half; own HP at or below a quarter (once per battle); any living enemy at or below half; last enemy standing; hit by a basic attack, by any ability, by a technique or by an attack on a whole group since last consumed; own techniques blocked; its first turn after a Party first opening (the ambush reaction); another enemy kind present or absent; a free formation position beside it; its whole starting group still standing. The last five follow the condition kinds read from the original ([original-battle-rules.md](../research/original-battle-rules.md)); which kinds and how each one tests are this spec's reading where the disassembly was not read in full. The default target is a weighted roll over party members who are not defeated, heavier toward the front of the party order. An override may name another target rule: random, lowest HP, highest HP, has or lacks a status, last attacker | Proposed (spec) | L-CORE, S-FAN-BALANCE |
| BAT-092 | At Original difficulty, AI profiles reproduce the original's enemy behavior (BAT-093) | Proposed (spec) | S-FAN-BALANCE |
| BAT-093 | To measure: each enemy's action selection in the original: which abilities, how often, under what conditions, and how it picks targets. Method: rights holder data; cross-check by observing at least 50 rounds for each of 15 sampled enemies including every boss in the vertical slice. Feeds BAT-092 | To measure | S-SOURCE |

## Macros

The scope makes macros a first-class feature (S-PILLAR-1) but does not define how a macro interacts with turn order. `core/` defines a macro as filling the round and forcing relative order. BAT-052 is this spec's exact rule for "relative order".

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-050 | A macro is a named, ordered list of commands, at most one per party member. Macros are saved per save file | Proposed (scope) | S-SYS-BATTLE, L-CORE |
| BAT-051 | Using a macro fills the round: every party member's command comes from the macro and command entry for the round ends | Proposed (spec) | L-CORE |
| BAT-052 | A macro forces the party's relative order and nothing else. Order is first resolved as usual (BAT-030). Then the macro's members are taken in pairs, in macro order: a member who would act before the member listed ahead of it is moved to act immediately after that member; a member already acting later is not moved, and whoever is between the two stays between them. So an enemy can act between two macro entries and break a combination ([BAT-061](battle.md)), and listing the slowest member first keeps the rest directly behind it. Macros do not reorder vehicle battles. Read from the community disassembly ([published-mechanics.md](../research/published-mechanics.md)); BAT-057 confirms it against the reference build | Proposed (spec) | L-CORE |
| BAT-053 | An entry for a party member who is absent or cannot act is skipped. An entry whose cost cannot be paid is cancelled at execution per BAT-015. At selection time the menu marks a macro that has such entries ([UI-005](menus-and-ui.md)) | Proposed (spec) | L-CORE |
| BAT-054 | Macros store no single-target choice. Each round a single-enemy action is retargeted by BAT-071; a heal goes to the member missing the most hit points, a cure to the first member it would cure (else the actor), and a revive to the first defeated member, as in the original ([original-battle-rules.md](../research/original-battle-rules.md)) | Proposed (spec) | L-CORE |
| BAT-055 | The number of macro slots is at least the original's (BAT-057) and at least 8 | Proposed (spec) | S-PILLAR-1 |
| BAT-056 | After any round, the player can save that round's commands as a new macro from the battle menu | Proposed (spec) | S-PILLAR-1 |
| BAT-057 | Measured from published sources and the community disassembly ([published-mechanics.md](../research/published-mechanics.md)): the original has eight macros, set up outside battle, each a fixed set of actions for the whole party. A macro does not guarantee back-to-back party actions: it only delays a member who would act before the member listed ahead of it, and there is no reordering in vehicle battles. To be confirmed against the rights holder's data when it arrives. Feeds BAT-052 and BAT-055 | Proposed (spec) | S-PILLAR-1 |

## Combination attacks and the combo log

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-060 | Combination attacks are data. A definition has: ID, required abilities (each optionally tied to a character), whether their order matters, the resulting effect list, and a presentation key. The content set covers the 15 combinations counted in the scope | Proposed (scope) | S-COUNTS, L-CORE |
| BAT-061 | A combination triggers at the start of a party member's turn when that member's action and the next ones in the order (2 to 4 in all) match a recipe, first recipe in data order. The run ends at an enemy, at a member who cannot act, at a command that is not a technique or skill, or at one whose cost cannot be paid. A recipe says whether order matters; 4-member recipes always need the exact order ([original-battle-rules.md](../research/original-battle-rules.md)) | Proposed (spec) | L-CORE |
| BAT-062 | When a combination triggers, its effect list replaces the effect lists of its component abilities, the other participants' turns are used up, and every participant pays its own component's cost. Its damage uses the damage formula with the participants' stats summed as the attack and the components' powers summed as the bonus. A combination always hits | Proposed (spec) | L-CORE |
| BAT-063 | To measure: the original's combination trigger rules: whether order matters, whether enemy actions or cancelled actions break it, whether components' own effects also apply, whether it can span rounds, and how costs are paid. Method: rights holder data; cross-check by attempting each of the 15 combinations with and without an enemy acting between components in the reference build. Feeds BAT-061 and BAT-062 | To measure | S-SYS-BATTLE |
| BAT-064 | The combo log records each combination the first time it triggers, by any means including auto-battle: combination ID, play time, and the characters involved. It is saved. The first trigger emits a ComboDiscovered event | Proposed (scope) | S-SYS-BATTLE |
| BAT-065 | From a combo log entry, the player can create a macro pre-filled with the required abilities in a valid order. Discoverability screens are in [UI-011](menus-and-ui.md) | Proposed (spec) | S-CONTENT |

## Battle speed and auto-battle

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-100 | Battle speed settings are 1x, 1.5x, 2x and 3x. Speed scales presentation playback only. The event stream and the battle outcome are identical at every speed | Proposed (scope) | S-SYS-BATTLE |
| BAT-101 | At 1x, the presentation time of a basic attack, a technique, an enemy action and a combination is no longer than the original's measured time for the same kind of action (BAT-102). Battles resolve in seconds | Proposed (scope) | S-PILLAR-1, S-ANTIGOALS |
| BAT-102 | To measure: per-action presentation time in the reference build for basic attacks, techniques, enemy actions and each combination, plus whole-battle time for normal encounters in five areas. Method: frame counting on captures, at least 20 samples per kind. Feeds BAT-101 | To measure | S-PILLAR-1 |
| BAT-103 | Auto-battle: the player picks one macro as the auto macro and turns Auto on. Each round the auto macro supplies the commands with no input. Published sources say the original has no auto-battle, so this is an addition (BAT-105); see [published-mechanics.md](../research/published-mechanics.md) | Proposed (scope) | S-SYS-BATTLE |
| BAT-104 | Auto-battle stops at the next command phase when: the player presses cancel, a party member falls, or an auto macro entry cannot be paid. It stops at once when the battle ends. It is allowed in every battle | Proposed (spec) | S-SYS-BATTLE |
| BAT-105 | To measure: whether the original has any repeat or automatic battle command, and its rules. Method: rights holder data and a reference build session. Feeds BAT-103 and BAT-104 | To measure | S-SYS-BATTLE |

## Event stream contract

`core/` resolves a whole round as soon as commands are submitted. The renderer animates the result. The renderer never decides an outcome.

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-110 | Submitting a round's commands returns the ordered list of typed events for that round. The renderer plays them back. Nothing the renderer does changes `core/` state | Proposed (spec) | L-CORE |
| BAT-111 | Every change to battle state appears in exactly one event. Applying the events in order to the battle's start snapshot reproduces the end state exactly | Proposed (spec) | L-CORE |
| BAT-112 | Events carry only IDs, numbers, enums and message keys. They carry no localized text and no engine types | Proposed (spec) | L-CORE |
| BAT-113 | The renderer may overlap the animations of events that belong to the same action. It never reorders events across actions | Proposed (spec) | S-PILLAR-1 |
| BAT-114 | The event schema has a version number. A change to any event type or field changes the version | Proposed (spec) | L-CORE |
| BAT-115 | The minimum event set is the table below | Proposed (spec) | L-CORE |

| Event | Payload |
|---|---|
| BattleStarted | combatants, formation, opening, flags |
| RoundStarted | round number |
| CommandsLocked | each combatant's command (ability, target rule), macro ID if used |
| OrderResolved | ordered list of combatant IDs |
| ActionStarted | actor, ability, resolved targets |
| ActionCancelled | actor, reason (actor unable, cost, no target) |
| Retargeted | actor, old target, new target |
| ResourceSpent | combatant, resource (TP, skill uses, item), amount |
| Hit / Missed / Critical | actor, target, ability |
| Damaged / Healed | target, amount, resulting HP |
| StatusApplied / StatusResisted / StatusRemoved | target, status, message key |
| StatusTicked | target, status, effect amounts |
| StatChanged | target, stat, delta, duration |
| Fell / Revived | combatant |
| ComboTriggered | combination ID, component actions |
| ComboDiscovered | combination ID |
| EscapeAttempted | actor, success |
| Message | message key, parameters |
| RoundEnded | round number |
| BattleEnded | result, rewards (experience, money, items) |

## Difficulty

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-120 | There are two difficulty presets, Original and Hard. Original is the default and uses the original's balance | Proposed (scope) | S-SYS-DIFF, S-FAN-BALANCE |
| BAT-121 | Hard is built on smarter enemies, not bigger numbers: enemies react to the party's earlier actions, grow more aggressive when hurt, resist instant-defeat effects, and bosses gain abilities. Healing techniques are weaker, so items matter. Party members get clearer roles through per-character ability tuning | Proposed (scope) | S-FAN-BALANCE |
| BAT-122 | On Hard, no enemy's HP, attack or defense differs from Original by more than 20% | Proposed (spec) | S-FAN-BALANCE |
| BAT-123 | Hard keeps the leveling pace: a player on Hard who fights the same battles meets ENC-033's M5 tolerance for the level Hard is tuned for, without extra battles. Hard may scale rewards to achieve this | Proposed (scope) | S-FAN-BALANCE |
| BAT-124 | Difficulty is a data overlay (AI profiles, ability numbers, resistances, reward scale) passed in battle setup. `core/` has no code branch on difficulty | Proposed (spec) | L-CORE |
| BAT-125 | The reward multiplier is a separate option, off by default, with values 1x, 1.5x and 2x. It applies per [SAV-023](save-and-progression.md) | Proposed (scope) | S-SYS-DIFF, S-FAN-BALANCE |
| BAT-126 | Difficulty and reward multiplier can change at any time outside battle. A change applies from the next battle. Menu labels are [UI-014](menus-and-ui.md) | Proposed (spec) | S-FAN-BALANCE |

## Determinism

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-130 | `core/` draws randomness only from the injected generator. It has no global mutable state and its results do not depend on threads or time | Proposed (spec) | L-CORE |
| BAT-131 | The same setup record, seed and commands produce a byte-identical serialized event stream on every platform and compiler. Rules arithmetic is integer or fixed-point only | Proposed (spec) | L-CORE |
| BAT-132 | A battle record (setup, seed, each round's commands, schema and rules version) replays to the identical event stream. Bug reports attach the battle record | Proposed (spec) | L-CORE |
| BAT-133 | The order of random draws is part of the rules. A change that alters it changes the rules version | Proposed (spec) | L-CORE |
| BAT-134 | The battle seed is drawn from the saved encounter stream ([ENC-006](encounters.md)) at battle setup. "Retry battle" ([FLOW-042](game-flow.md)) mixes a retry counter into it, so a retry does not replay the same rolls | Proposed (spec) | S-ENGINE |

## Vehicle battles

| ID | Requirement | Label | Source |
|---|---|---|---|
| BAT-140 | Vehicle battles run on `core/` with the vehicle as data: its combatant stats, its abilities, and how damage to it affects the party | Proposed (scope) | S-CONTENT |
| BAT-141 | To measure: per vehicle, the original's vehicle battle rules: who issues commands, which commands exist, how vehicle damage and defeat work, and which enemies appear. Method: rights holder data and a reference build session per vehicle. Feeds BAT-140 | To measure | S-CONTENT |
