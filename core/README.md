# Battle rules core

A headless battle rules library for a turn-based party RPG. It is plain C++17 and uses only the standard library, so it can go into an Unreal Engine module unchanged. The rules never print, never read globals and never throw across the public API. All names, numbers and text in `data/` are original placeholders.

## Layout

| Path | Contents |
| --- | --- |
| `include/rules/` | Public headers (namespace `rules`) |
| `src/` | Library sources, built into `build/librules.a` |
| `cli/battle_cli.cpp` | Terminal front end |
| `tests/` | One test executable with a minimal assert helper (`tests/test.h`) |
| `data/` | Placeholder JSON data: rules, statuses, abilities, items, equipment, party (with vehicles), enemies (with AI slots and overrides) and encounters (with drops), combos, difficulties, and the English text table `text_en.json` |

## Build and test

Needs `clang++` and GNU Make 3.81 or later. The compile flags are `-std=c++17 -Wall -Wextra -Werror -fPIC`; position-independent code lets `build/librules.a` go into a shared library as it is.

```
make          # library, CLI and tests in build/
make test     # runs build/rules_tests, a full auto battle, then a record run and its replay, compared byte for byte
make clean
```

Run one test group by name: `./build/rules_tests combo`. Run tests from `core/`, because they load `data/`.

The CLI:

```
./build/battle_cli --help
./build/battle_cli --seed 1 --auto                                  # plays the "training" encounter, prints the event log
./build/battle_cli --seed 1 --auto --encounter warden --difficulty hard --reward x1_5
./build/battle_cli --seed 1 --auto --macro tempest                  # auto-battle with that macro until a stop rule fires
./build/battle_cli --seed 1 --auto --opening enemy_first
./build/battle_cli --seed 1 --auto --record build/run.record        # also writes the battle record
./build/battle_cli --replay build/run.record                        # prints exactly what the recorded run printed
./build/battle_cli --seed 4 --encounter marsh                       # interactive; "save NAME" keeps the last round as a macro
```

Other options: `--data DIR`, `--level N` (party level, default 1) and `--max-rounds N` (default 50). `--difficulty` defaults to `original`. Exit codes: 0 when the battle ended (any outcome) or the player quit, 1 for a data or record error, 2 for bad arguments, 3 when the round limit was hit.

## How a host drives a battle

1. Load data once: `Result<Database> db = loadDatabaseFromDirectory("data")`, or `loadDatabaseFromTexts` with file contents the engine already read (`dataFileNames()` lists them). On failure, `problems` holds every problem as a `Problem` (`file`, `line`, `entry`, and `message`, a text key with its parameters), and `error` the same in English, one per line, as `file:line: entry: text`. `problemText()` has the English template of every key (`load.*`, `json.*`, `text.*`); `formatProblem(problem, table)` renders one with any table. Load the string table with `loadTextTable("data/text_en.json")` (or `parseTextTable`); it is not part of the `Database` or its content hash.
2. Keep the party as `std::vector<PartyMember>` between battles, with an `EquipmentBag` of unworn pieces (`db.startingEquipmentBag` to begin). `makePartyMember(db, character, level)` creates one in its starting equipment. `rest(member, db)` restores hit points, technique points and skill uses and cures statuses per data. Change equipment with `equip(member, db, bag, equipmentId)` (at the piece's default position), `equip(member, db, bag, equipmentId, position)` (a hand for weapons and shields, see [equipment.json](#equipmentjson)) and `unequip(member, db, bag, position)`, which move pieces between the bag and the member; `canEquip` checks the category first, `fitsPosition` the place. `memberStats` and `knownAbilities` give what the member has now, and `memberCombatant(db, member)` the whole combatant a battle would set up from it (run it on a copy wearing another piece to compare equipment). `useItemOutsideBattle` and `useAbilityOutsideBattle` apply field-usable items and techniques to the party through the battle's own effect code. An item used up to its last one leaves the inventory, so no entry with a count of 0 is left behind.
3. Fill a `BattleSetup`: `party`, `inventory`, the enemy group (`useEncounter(setup, db, encounterId)` fills `enemyGroup`, `enemies`, `escapeRating` and `flags.escapeAllowed`), `opening` (`openingFromContact` turns a visible-mode contact into one, `openingFromRoll` a random-mode roll), `flags`, `scriptedEnd`, `difficulty`, `rewardMultiplier` and `seed`. For a vehicle battle set `flags.vehicleBattle` and `vehicle` (see [Vehicle battles](#vehicle-battles)).
4. Start: `Result<Battle> battle = Battle::create(db, setup, &comboLog)`. Combatant indexes are stable: party members first, in lineup order, then enemies in formation order. The battle keeps its own copy of the data with the difficulty overlay applied. The `ComboLog` must outlive the battle.
5. Each round:
   * Optionally `setPlayTime(ms)`: the combo log stores it when a combination is first discovered.
   * Set party commands with `setCommand(index, Command::ability("spark", target))`. It returns an empty `Message`, or a message key with parameters saying why the command was rejected (for example escape in a battle that forbids it, a side caught off guard by the opening, or a member whose status forces its command; `sideActsNextRound` and `escapeAllowed` tell the menu in advance). Or call `applyMacro(macro)`, which fills the commands and returns a `MacroStepResult` for each step (applied, retargeted, fell back or skipped, with the issue).
   * Or run auto-battle: `startAuto(macro)` once, then call `fillFromAuto()` at each command phase. It fills the round from the macro and returns true, or turns auto-battle off and returns false, with `autoStop()` saying why. `cancelAuto()` is the player's cancel.
   * Call `resolveRound()`. Party members without a command use their basic attack. Enemies without a command are left to their AI, which picks the action when the enemy's turn comes. The call returns the round as a list of typed `Event`s for the renderer to play back. The host can also set enemy commands for scripted fights.
   * After a round, `lastRoundAsMacro(name)` turns that round's party commands into a macro.
6. Stop when `outcome()` is no longer `Ongoing`. `gameOver()` is true for a Defeat without a defeat branch. Then read `rewards()` (`experience` and `currency` after scaling, `memberExperience` per party member after the split, and `items`, the encounter's drop), `partyState()` (hit points, technique points, skill uses, equipment and the statuses that persist outside battle) and `inventory()`, and apply `grantExperience(member, db, rewards.memberExperience[i])` to each member. It returns a `LevelUp` with the levels gained and the abilities learned.
7. Save `serializeRecord(battle.record())` for bug reports. `parseRecord` reads it back and `replayBattle(db, record, comboLog, events)` rebuilds the battle and its full event stream. `serializeEvents(events)` gives a stable text form of an event stream. Macros and the combo log go into saves with `serializeMacros`/`parseMacros` and `serializeComboLog`/`parseComboLog`.

`autoCommand(battle, actor)` is a simple deterministic per-member policy. The CLI and the tests use it.

Player-facing results are `Message` values: a stable key and parameters (ids or numbers), never English. `formatText(table, key, params)` fills a template's `{0}`, `{1}` and so on, and `messageKeys(db)` lists every key core can return or emit with that data, so a build can check a string table covers them. Data loading problems carry a key and parameters too (`Result::problems`, step 1). Other developer-facing errors (record parsing, `Battle::create`, `useEncounter`) stay plain English strings.

### Round resolution

1. The first round's events open with `BattleStarted`, then a `Message` for a Party first or Enemy first opening.
2. The side that lost the opening (Party first: enemies; Enemy first: the party) gets no commands for the first `surprise_rounds` rounds.
3. Every other living combatant gets a command. A status whose action is `forced` replaces it with a basic attack on a random target from its pool (BAT-012), drawn in combatant index order. An enemy without a command gets a basic attack with no target: its AI decides when its turn comes. For the whole round a defender takes a hit without an element at `defend_affinity` (a quarter by default), before defense is subtracted. Defend does nothing against an elemental hit unless `defend_elemental` holds its affinity at most at that. `CommandsLocked` lists every command and the macro used.
4. Turn order: each combatant rolls an initiative of agility plus a random number from 0 to just under half the highest agility among those acting; a combatant with 0 agility gets no roll. Ties go to the party, then to the lower index. A macro then takes its members in pairs, in step order: a member who would act before the member listed ahead of it is moved to act right after that member, and one already acting later stays put. So an enemy can stay between two macro steps and break a combination, and listing the slowest member first keeps the others directly behind it. `OrderResolved` lists the order. Then every enemy its AI drives draws its foe, in index order (see [Enemy AI](#enemy-ai)).
5. At each queue position, an actor that cannot act (defeated, or a `cannot_act` status) loses the turn and pays nothing: one `ActionCancelled` with `actor_unable` and the cause (`defeated` or the status). Otherwise an enemy its AI drives picks its action, and a party member's turn checks for a combination: up to four queued turns from this actor, passing over an enemy that fell earlier in the round and stopping at a living enemy, a member who cannot act, a command that is not a technique or skill, or one that cannot be paid for (technique points, skill uses, blocked techniques). The recipes are compared in data order and the first whose components match the first collected turns fires (in any order, in the listed order for an `ordered` recipe or one with four components, with each tied component used by its character, and an ability of the same `family` filling a component's place). The other participants' turns are spent on it, and every participant pays its own cost. The combo aims at all enemies if its result does, otherwise at the first participant's target, replaced by that participant's own rule if it fell. Its damage uses the sum, over the participants, of mental for a technique component and strength for a skill component as the attack, and the sum of the components' damage powers as the bonus; it always lands. Its other effects read the participants' average strength, mental, dexterity and critical chance, and its stat changes move by the summed stat. The first time a recipe fires the log records it and the battle emits `ComboDiscovered`.
6. Otherwise the actor takes a normal turn. Every action is validated when it runs (BAT-015); a failed check is one `ActionCancelled` with a `cancel` reason:
   * Technique points, skill uses and item stock are checked: `not_enough_tp`, `no_uses_left`, `no_item`, at no cost. A technique whose user has a status that blocks techniques spends its points first, then cancels as `silenced`.
   * Targets are resolved, and a change emits `Retargeted` before `ActionStarted`:
     * A single foe target that is gone is replaced. A party member's action whose first effect is damage (basic attacks included) turns to the living enemy missing the most hit points, any other to the one missing the fewest; each later tied enemy takes the place on a coin flip. An enemy draws again by party order. A party member's target of -1 is the first enemy.
     * In a macro round a single-foe action ignores its target and picks by that rule every time, and a one-ally action picks for itself: a heal the member, not defeated and of a kind it reaches, missing the most hit points (ties to the later member), a one-ally raise the first member in line (the original's pick never moves off it), a cure the first living member with a status it cures (or the actor), a revive the first defeated member (or the heal pick).
     * A single ally target is never replaced: on a defeated ally a non-revive action does nothing, and its cost is still paid (an item is used up). A `one_fallen_ally` target that is back up cancels with `no_target`; with no chosen ally it takes the first fallen one. `random_enemy` draws one living foe at execution.
   * Group targeting (`enemy_group`) hits every living foe in the target's group. Enemies take their group from the encounter (by default their enemy id). The whole party is one group, so an enemy's group ability hits every living party member.
   * Effects apply in order. A physical hit emits `Hit`; a miss emits `Missed` and skips the rest of that ability's effects on that target.
   * An item whose effects changed nothing (a heal at full hit points, a cure for a status the target lacks, a resisted status, immune damage) is not used up, and the battle emits `ItemHadNoEffect`. The spec does not say what happens; this is the chosen rule. The one exception is a one-ally item on a defeated ally, which is used up.
7. Escape resolves at its place in the order. On success the battle ends at once with `Escaped`, later actions are dropped and there are no rewards. On failure the round continues.
8. The battle ends as soon as one side has nobody standing (in an encounter that replaces its fallen, a fallen enemy's replacement joins at once, so the enemies never run out), the party escapes, or the scripted end condition holds. A party member counts as down when defeated or held by a status that `counts_toward_defeat`; an enemy only when defeated. A party that is only asleep plays on, and only the enemies act. Victory and Defeat take precedence over a scripted end that happens at the same time.
9. At the end of the round statuses with a `recover_chance` roll to end, in combatant index order. Then status durations count down and expire; a status that runs out leaves the stats it held at 1 for the rest of the battle, with a `StatModified` for each. A `survive_rounds` scripted end is checked last.

The RNG (`Rng`, SplitMix64) belongs to the battle and is seeded from the setup. `state()` and `restore()` save and continue a generator. The same setup, data and commands always give the same event stream. The order of random draws is part of the rules version: forced-command targets, in combatant index order; initiative rolls, in combatant index order; enemy foes, one draw per enemy its AI drives, in index order. Then per turn: for an enemy its AI drives, a fresh foe if its foe fell, then its action slot (an enemy whose turn an enemy recipe spends draws neither); per action, a replacement for a gone foe (a party member's coin flips on ties, an enemy's party-order draw), then a `random_enemy` target; per effect: for a basic attack's physical damage the hit roll, then for any damage or in-battle heal the sixteen spread rolls; for a status or a lowered foe stat one roll, unless the target is immune; for an escape one roll, only when an enemy can act. At round end the recovery rolls. When the battle ends in Victory, one drop roll comes last, if the encounter has a drop.

### Enemy AI

Each enemy has eight action slots and up to four overrides in data (BAT-090, BAT-091):

1. When the round's order is resolved, every enemy its AI drives draws its foe from the party members who are not defeated, in party order, weighted count, count - 1, ..., 1: 2:1 with two members, 3:2:1 with three. Asleep and paralyzed members stay in the draw.
2. When its turn comes, an enemy that cannot act loses it. Otherwise, if its foe fell, it draws again the same way. Then it draws one of its eight slots, never the slot index any enemy in the battle drew last (the first draw of the battle can be any of the eight, every later one is one of seven). When the action it ends up with (after the overrides below) is its part of an enemy recipe and the next turns are the recipe's other enemies, they act together (see [combos.json](#combosjson)).
3. The overrides are checked in order; the first whose condition holds and whose ability finds a target replaces the slot. An action the enemy cannot pay for (technique points, skill uses), or that finds no target (a revive with nobody fallen), becomes its basic attack on its foe. A technique blocked by a status spends its points and fails, as for the party.
4. A single-foe action with the default target rule aims at the drawn foe. Other target rules pick when the turn comes (see enemies.json). Heals pick the ally with the lowest hit point share, cures an ally with the status, revives the first defeated ally.

## Data formats

Every file is a JSON object. Unknown fields are load errors, which catches typos. References are checked across files, in this order: rules, statuses, abilities, items, equipment, party, enemies, combos, difficulties. Each file may reference only the files before it.

### rules.json

Rules that wait on measurements of the original game. The first three fields are required; the others are optional, and leaving them out keeps the placeholder's rules.

```json
{
	"surprise_rounds": 1,
	"rest_revives": true,
	"machine_recover_hp": 1
}
```

* `surprise_rounds` (0 to 9): rounds the surprised side loses after a Party first or Enemy first opening.
* `rest_revives`: whether `rest` stands a fallen member up (FLD-035 is to measure; true keeps the earlier behavior).
* `affinity_divisor` (optional, 1 to 1000, default 100): what an affinity is divided by when it scales damage, so affinities are percents at 100 and the original's factors at 4 (normal 4). The divisor is also the normal affinity: an element a combatant's data leaves out takes it (`Combatant::neutralAffinity`), and so does a hit without an element unless `neutral_affinity` says otherwise; a defended hit without an element takes 25 scaled to the divisor (`25 * divisor / 100`: 50 at 200) unless `defend_affinity` says otherwise. Affinities in data are written on the same scale (up to `4 * divisor`, at least 400). Weak and resisted compare the affinity with the divisor. The damage formula multiplies by the affinity before it divides, so the affinity step rounds down once, after the spread step's own rounding and before the defense comes off. The research (original-battle-rules.md and published-mechanics.md, BAT-043) confirms the shape, factor over 4 with 4 normal and Defend's factor 1, but not a divisor of 200; it is data so either scale can be written.
* `neutral_affinity` (optional, 1 to `4 * divisor` or 400, whichever is higher, default the divisor): the affinity a hit without an element takes, from either side: a basic attack without a weapon or `attack_element` element, a `physical_damage` effect without `element`, a physical item or combination (`BattleRules::elementlessHitAffinity()`). So 100 at a divisor of 200 halves plain hits while an element the data leaves out stays normal. A hit without an element is never labeled weak, resisted or immune; only an elemental hit's affinity is compared with the divisor. The research does not make it half the divisor: the original's physical hits use a factor of their own whose normal value is 4, the divisor (original-battle-rules.md, sections 6 and 9; published-mechanics.md, BAT-043), so the default keeps the divisor.
* `defend_affinity` (optional, 0 to `4 * divisor` or 400, whichever is higher, default `25 * divisor / 100`): a defending combatant's affinity for a hit without an element, for the round (`BattleRules::defendedAffinity()`).
* `defend_elemental` (optional bool, default false): Defend also holds a defender's affinity for an elemental hit at most at `defend_affinity` (the lower of the two, so a weakness drops and an immunity stays). The research says Defend sets only the physical factor and does nothing against other elements (published-mechanics.md, BAT-043; original-battle-rules.md, section 9), which is the default.
* `magic_defense` (optional bool, default false): elemental damage subtracts the target's `magic_defense` stat (enemies' stats, characters' base and growth, equipment bonuses) in place of its mental. published-mechanics.md (BAT-043, a fan FAQ, low confidence) gives magic defense as base mental plus equipment, and the disassembly read says the data names each ability's defending stat; neither fixes this switch, so it is an option.
* `instant_defeat_kinds` (optional list of `living`, `spirit`, `machine`, default none): party members of these kinds can be felled by an instant-defeat effect, besides characters marked `vulnerable`. The disassembly read (original-battle-rules.md, section 6) shows the death effect working on party members, rolled with the target's element factor; the placeholder keeps the party immune except the Cogwright.
* `ambush_opening` (optional, `party_first` or `enemy_first`, default `party_first`): the opening that marks enemies for the `ambushed` override. With `party_first` (the placeholder's reading) the mark is spent at each enemy's first turn whether or not the override fires; with `enemy_first` enemies are marked by an Enemy first opening and keep the mark until an `ambushed` override fires. The disassembly read did not find what sets the original's bit (original-battle-rules.md, section 1) and one forum post calls enemy-first openings ambushes (published-mechanics.md, ENC-024), so neither reading is confirmed and it is a switch.
* `elements` (optional list of ids): elements beyond the built-in `heat`, `chill`, `shock`, `gale` and `holy`, usable wherever an element is (abilities, instant defeat, weapons, enemy basic attacks, affinities of enemies, characters, vehicles and equipment). An id is lowercase letters, digits and `_`, new and not `none`. `Database::elementIds()` lists the built-in ones, then these; the game names each in every language (`name.element.<id>`, `../game`). The placeholder declares `tide`.
* `machine_recover_hp` (0 to 9999): the hit points a fallen machine member (party.json `kind`) stands up with when a battle ends in anything but Defeat, after the experience is shared out, with a `Revived` event (message `battle.machine_recovered`). The original sets 1 (original-battle-rules.md section 5); 0 leaves it down.

### statuses.json

```json
{"statuses": [
	{"id": "sleep", "name": "Sleep", "action": "cannot_act", "stats_to_one": ["agility"], "recover_chance": 50},
	{"id": "paralysis", "name": "Paralysis", "action": "cannot_act", "stats_to_one": ["agility", "dexterity"], "removes": ["sleep"], "enemy_duration": 1, "counts_toward_defeat": true},
	{"id": "silence", "name": "Silence", "blocks_techniques": true, "stacking": "keep", "survives_defeat": true},
	{"id": "jumble", "name": "Jumble", "action": "forced", "forced_target": "random_any", "duration": 2, "stacking": "keep"},
	{"id": "defeated", "name": "Defeated"}
]}
```

Optional fields, which cover BAT-081:

* Action rule: `action` is `normal` (default), `cannot_act` (the turn is cancelled) or `forced` (the battle supplies a basic attack on a random target). `forced_target` (forced only): `random_any` (default, anyone else), `random_ally` or `random_foe`. `blocks_techniques` (bool) is a partial action rule: a technique spends its points and is cancelled as silenced.
* Duration rule: `duration` in rounds (0, the default, has no round limit: until cured, or until the battle ends). `enemy_duration` (0 to 99): when nonzero, the duration an enemy gets in place of `duration`. `persists_outside_battle` (bool): `partyState()` carries the status out of battle with its remaining rounds, and `PartyMember::statuses` carries it into the next one.
* `stats_to_one` (list of `agility` and `dexterity`): those stats are 1 while the status lasts. A cure or a recovery gives them back; a status that runs out by its duration leaves them at 1 for the rest of the battle, as the original does.
* `removes` (list of status ids): receiving this status removes those.
* Cure conditions: cure effects; `wakes_on_damage` (bool); `recover_chance` (percent, `cannot_act` only, rolled at the end of each round); `rest_cures` (bool, default true).
* Stacking rule: `stacking` is `refresh` (default; reapplying restarts the duration), `extend` (adds the duration to what is left, up to 99) or `keep` (a reapplication changes nothing and makes no roll; it emits `StatusResisted` with the `unchanged` key).
* `counts_toward_defeat` (bool): a party member with it counts as down when the battle checks for Defeat (BAT-014). It does not apply to enemies: an enemy is down only when defeated.
* `survives_defeat` (bool): defeat leaves it in place, so it is still there after a revive. Defeat clears every other status.
* `damage_cap` (0 to 999): when nonzero, no hit takes more than this from the holder, so a shield of 1 holds every attack to minimal damage until something cures it (the placeholder `aegis` and the Sunstone item).

A status with id `defeated` is required, with only an id and a name. The battle adds it when hit points reach 0. Effects cannot apply it and nothing can resist it.

The placeholder `poison` has no effect in battle and no round limit; it only persists outside battle. `silence` lasts until the battle ends and survives defeat.

### abilities.json

```json
{"abilities": [
	{"id": "spark", "name": "Spark", "kind": "technique", "cost": 3, "target": "one_enemy", "effects": [
		{"type": "elemental_damage", "power": 26, "element": "shock"}
	]}
]}
```

* `kind` is one of:
  * `attack`: basic attacks, including weapon attacks.
  * `technique`: needs `cost` in technique points.
  * `skill`: needs `uses`, which refill on rest.
  * `combo`: only reachable through a combo recipe.
* `target` is one of `one_enemy`, `enemy_group`, `all_enemies`, `one_ally`, `all_allies`, `self`, `one_fallen_ally` or `random_enemy`.
* `field` (optional bool): usable outside battle. Only techniques and skills with a party target and only heal, cure_status, revive, travel and (items) teach effects qualify.
* `attack_stat` (optional, a stat name): the user's stat the ability attacks, heals and rolls with and moves stats by, in place of mental for a technique and strength otherwise (the original names it per skill; published-mechanics.md, BAT-043).
* `family` (optional): a name shared by abilities that stand in for each other in combination recipes: an ability fills a component that names it, or one whose ability has the same family (B3).
* Every effect may carry `kinds` (a list of `living`, `spirit`, `machine`): it then works only on targets of those kinds, and `living` covers `spirit`. Leaving it out works on every kind. The placeholder heals and revives for the party name `living`, so they pass a machine member by; Gear Salve and Self Repair name `machine`.
* Effects:
  * `{"type": "physical_damage", "power": N, "element": E}`. `power` (0 to 9999) is the bonus in the damage formula; a plain attack has 0. With the optional `element` the hit takes the target's affinity for it and its weak, resisted or immune label, Defend leaves it alone (unless `defend_elemental`), and it wins over the weapon's or the enemy's attack element; without one it is a hit without an element (`neutral_affinity`).
  * `{"type": "elemental_damage", "power": N, "element": "heat|chill|shock|gale|holy"}`
  * A combo's damage effects have no `power`: the participants' components give it (see [Round resolution](#round-resolution)). The loader rejects one.
  * `{"type": "heal", "power": N}`
  * `{"type": "apply_status", "status": ID, "threshold": 0-255}`. `threshold` is optional (default 0): the score the status roll must beat.
  * `{"type": "cure_status", "status": ID}`
  * `{"type": "modify_stat", "stat": "strength|mental|agility|dexterity|defense|magic_defense", "direction": "up|down", "threshold": 0-255, "power": N}`. The stat's battle value becomes its base plus or minus the user's mental for a technique, or strength for a skill or basic attack (a combo's summed stat), or the optional `power` (1 to 999) when it is given. `magic_defense` is the stat elemental damage subtracts when `rules.json` sets `magic_defense`, accepted either way like `magic_defense` in stats. Each change is worked out from the base, so the latest one replaces the earlier. Lowering floors agility and dexterity at 1 and the others at 0. A change lasts until the battle ends. Lowering a foe's stat rolls like a status against the optional `threshold` (`down` only); raising always lands.
  * `{"type": "revive", "power": percent of max hit points}`. The target must be `one_fallen_ally` or `all_allies`, and `one_fallen_ally` needs a revive effect.
  * `{"type": "instant_defeat", "threshold": 0-255, "element": E}` (B7): rolls like a status (mental against mental); it works only on a `vulnerable` target: an enemy whose data says so (so bosses are immune), or a party member marked `vulnerable` or of a kind `instant_defeat_kinds` names. With the optional `element` the roll's factor is the target's affinity for it on the original's scale (`affinity * 4 / affinity_divisor`, normal 4), and an affinity of 0 is immunity. A landed one emits the `battle.instant_defeat` message (the mark a renderer shows) and defeats the target; a miss emits `battle.instant_defeat_resisted`, an immune target `battle.instant_defeat_immune` (with `effectiveness` Immune), both as `Message` events.
  * `{"type": "restore_tp", "power": N}` (B20): restores N technique points, emitted as `Healed` with `resource` tp. Not allowed on field abilities, so it works in battle only.
  * `{"type": "strip_modifiers"}` (B10): removes every stat change on the target, one `StatModified` with amount 0 per stat.
  * `{"type": "summon"}` (B13, an enemy's own ability with target `self`): calls up the enemy its data names in `summons` in a free formation position beside it (a coin flip when both sides are free), as a new combatant (`EnemyJoined`). Nothing happens without a free place.
  * `{"type": "merge"}` (B14, an enemy's own ability with target `self`): another living enemy of the user's id, or of the id its data names in `merges_with`, leaves the battle (`EnemyLeft`, no rewards) and the user takes the form its data names in `merges_into` at full hit points (`EnemyTransformed`).
  * `{"type": "transform"}`: each target that is an enemy with `transforms_into` takes that form at full hit points (`EnemyTransformed`); any other target is unchanged (an item on one is not used up). With target `self` on an enemy's own ability the user alone changes, and the enemy needs `transforms_into`; with the `after_turns` override it changes after a number of its turns, with any other override on that condition. Any other targeting reaches other combatants: an item or a party ability aimed at an enemy turns it into its next form, and an enemy's `one_ally` transform goes to an ally that has a form to take (the lowest hit point share among those, else as for other one-ally actions). Combinations and equipment `use` cannot transform.
  * `{"type": "split", "count": 2-8}` (an enemy's own ability with target `self`): the user becomes `count` enemies of the id its data names in `splits_into`: the first takes its place (`EnemyTransformed`), the others join in the free formation places nearest it, left before right (`EnemyJoined` with the user as `actor`); places run out at the formation's eight. The split enemies pay; the one that split does not.
  * `{"type": "self_destruct", "rewards": bool}` (enemies): after the ability's other effects, the user leaves the battle (a `battle.self_destruct` message, then `EnemyLeft` with `actor` -1). It pays its experience and currency only with `rewards: true`.
  * `{"type": "teach", "ability": ID}` (P8, items only): the target learns a technique or skill for good (`PartyMember::taught`). It must be the only effect of a field item aimed at `one_ally`; a battle refuses it like travel, and `useItemOutsideBattle` keeps the item when the member already knows the ability.
  * `{"type": "travel", "to": "town|dungeon_exit"}` (FLD-051): takes the party back to a town it has visited, or out of the dungeon it is in. It must be the only effect, on a `field` technique, skill or item that targets `all_allies`; enemies, vehicles and combinations cannot have one. Core moves nobody: `travelEffect(effects)` finds it for the host, which checks where the party may go and moves it (`../game`, "Rules this library chose"). In battle `setCommand` refuses it with `command.field_only`; outside battle `useAbilityOutsideBattle` and `useItemOutsideBattle` only take the cost (technique points, a skill use or the item) and report `ResourceSpent`.

### items.json

```json
{"items": [{"id": "tonic", "name": "Tonic", "target": "one_ally", "effects": [{"type": "heal", "power": 30}]}]}
```

Items use the same effects and targets (of the enemy-only effects, `transform`), but their power is flat: `consumed: false` (default true) makes an item that is never used up, in battle or in the field (the placeholder Dawn Bell). Their power is flat: user stats do not scale it, and physical item damage never misses and never lands a critical. An item's `modify_stat` needs a `power` (1 to 999): the amount the stat moves. `field` works as for abilities. An item may leave `effects` out: a key item, which does nothing in battle and is not usable through `useItemOutsideBattle`; the host gives it its use (the game's keys and the items that call a vehicle, `../game`).

### equipment.json

```json
{"equipment": [
	{"id": "ember_rod", "name": "Ember Rod", "slot": "weapon", "category": "rod", "element": "heat", "stats": {"mental": 3}},
	{"id": "volley_sling", "name": "Volley Sling", "slot": "weapon", "category": "sling", "attack": "scatter_shot"},
	{"id": "calm_charm", "name": "Calm Charm", "slot": "accessory", "category": "charm", "status_resist": {"sleep": 100}},
	{"id": "spark_ring", "name": "Spark Ring", "slot": "accessory", "category": "charm", "abilities": ["glint"]},
	{"id": "kelp_cleaver", "name": "Kelp Cleaver", "slot": "weapon", "category": "greatblade", "hands": 2, "stats": {"strength": 12}},
	{"id": "driftwood_buckler", "name": "Driftwood Buckler", "slot": "shield", "category": "buckler", "stats": {"defense": 4}}
]}
```

* `slot`: `weapon`, `shield`, `head`, `body`, `arms` or `accessory`. Weapons and shields are hand pieces (P10): a member has two hands, the right one (the `weapon` position, where a hand piece goes first) and the left (`left_hand`, a position only, never a piece's slot), and can hold two weapons, a weapon and a shield, or two shields. `equip` with no position puts a weapon in the right hand and a shield in the left; any other piece goes in its own slot, one per slot. Starting equipment fills the other hand when a piece's first one is taken.
* `hands` (weapons only, 1 or 2, default 1): a two-handed weapon goes in the right hand only and sends the left hand's piece back to the bag; a piece put in the left hand sends back a two-handed weapon held in the right.
* Every worn piece adds its `stats`, so two weapons add both attack bonuses and two shields both defense bonuses, as the published formulas read (attack is strength plus equipment, defense the stat plus equipment; published-mechanics.md, BAT-043). The basic attack stays one attack: the right hand's weapon gives its `attack` and `element`, the left hand's what the right lacks, and a weapon whose attack reaches every enemy or a group, in either hand, makes the attack reach them (original-battle-rules.md, section 2).
* `category`: a free label. A character can equip a piece when the category is in the character's `equip` list.
* `stats` (optional): bonuses from -999 to 999 for `hp`, `tp`, `strength`, `mental`, `agility`, `dexterity`, `defense`, `critical` and `evasion`. Totals are clamped (hit points at least 1, critical and evasion 0 to 100).
* Weapons only:
  * `element`: the basic attack's physical damage takes the target's affinity for this element.
  * `attack`: an ability of kind `attack` that replaces the wearer's basic attack.
* `abilities` (optional): techniques and skills the wearer knows while wearing it. A granted skill starts with full uses the first time and keeps its remaining uses when the piece comes off, so swapping never refills it; rest refills it.
* `status_resist` (optional): added to the wearer's own resistances, capped at 100.
* `use` (optional): a battle ability (not a combo, travel, summon, merge, transform, split or self-destruct) the wearer casts through the Item command with the piece's id (`Command::item("storm_rod", target)`), with the wearer's stats, at no cost and without using the piece up (P12). Only the member wearing it can, unless:
* `use_from_bag` (optional bool, needs `use`): any party member who can use items (not a vehicle, nobody aboard) casts the piece's `use` while it is in the bag, at the item rule's flat power (the effects' own `power`, user stats not counted), and it stays in the bag. The host passes such pieces in `BattleSetup::pieces` (the Squall Fan).
* `affinities` (optional, element to percent, 0 to 400): scales the wearer's affinity for that element by the percent, piece after piece, rounded down (the Ember Mail halves heat and takes chill at 150).

### party.json

* `characters`: each has these fields:
  * `id`, `name`
  * `base` and `growth` stats: `hp`, `tp`, `strength`, `mental`, `agility`, `dexterity`, `defense`, and optional `critical` and `evasion`: points added to the critical and dodge side of the hit roll, which the original does not have; 0 gives the original's roll. Growth is added per level above 1.
  * `level_stats` (optional, in place of `base`): the stats at each level, one object per level from level 1 (1 to 99 rows, the fields of `base` without `level`), as the original's per-character table sets absolute values each level (original-battle-rules.md, section 5). Past the last row the stats are that row plus `growth` per level (no growth when `growth` is left out), so a table that stops short of 99 still levels to the cap. `statsAtLevel` reads it.
  * `experience_table` (optional): the total experience each level from 2 needs, rising strictly (1 to 98 entries, 1 to 9999999). The member's experience still counts toward the next level, so the cost of a level is the difference between two totals; past the table the formula's curve goes on. `experienceToNextLevel(character, level)` reads it; level-ups, the game's member detail and rewards use it.
  * `basic_attack`: an ability of kind `attack`.
  * `abilities`: techniques and skills known from level 1.
  * `learns` (optional): `[{"level": 3, "ability": "cleave"}]`. Level 2 to 50. An ability can be known or learned only once.
  * `affinities` (optional): see enemies.json.
  * `status_resist` (optional): status id to a percent that scales down the status roll's factor; 100 is immunity.
  * `equip` (optional): the equipment categories the character can wear. Each must be used by some piece.
  * `equipment` (optional): starting equipment, which `makePartyMember` puts on.
  * `kind` (optional): `living` (default), `spirit` or `machine` (P6). See effect `kinds` and `machine_recover_hp`.
  * `vulnerable` (optional bool): enemy instant-defeat effects can fell the member (see `instant_defeat_kinds`).
  * `teachable` (optional): techniques and skills events can teach the character (`learn`, `../events`). Combination recipes and data macros count an ability a character can know by any route: from the start, by level, through equipment it can wear, `teachable`, or an item with a `teach` effect (which any character can use).
  * `base` and `growth` may give `magic_defense`, as may equipment `stats`.
* `vehicles` (optional): see [Vehicle battles](#vehicle-battles).
* `lineup`: 1 to 5 character ids.
* `inventory`: item id to count.
* `equipment_bag` (optional): equipment id to the number of spare pieces, which `db.startingEquipmentBag` holds.
* `macros`: each has a `name` and ordered `steps`. A step has these fields:
  * `member`
  * `command`: `attack`, `ability`, `item`, `defend` or `escape`.
  * `id`: for ability and item steps. The member must know the ability from the start, by level or through equipment it can wear.
  * `target` (optional, a rule, not a combatant index): left out means the ability's default; `"self"`; `"all"`; `{"enemy": N}` for formation position N from 0; `{"member": "fighter"}` for a party member by character. The rule must fit the command's targeting (`macroTargetFits` in `macro.h`, which a host editing macros calls too). When the step runs, a single-foe action and a heal, cure or revive on one ally pick their target afresh (see [Round resolution](#round-resolution)), as the original's macros do, so the rule sets only the command `applyMacro` returns.

  Each member can have only one step per macro.

When a macro step cannot run as written (BAT-053):

* The member is defeated or not in the party, the party is caught off guard, or a status stops the member (`cannot_act`) or forces its command: the step is skipped.
* Silenced, not enough technique points, no skill uses left, item stock used up (items are reserved across earlier steps), or no target yet (a revive with nobody down): the command is kept as written and reported as `Applied` with that issue. It is validated again when it runs, so it is cancelled then unless things changed. `isCostIssue` says which issues mean the cost cannot be paid.
* Unknown ability or escape not allowed, which `setCommand` would refuse: the member makes a basic attack on the intended enemy, or on the first living one.
* The target is gone (dead enemy, empty formation position, member not in the party): the step is retargeted.

### enemies.json

```json
{"enemies": [{
	"id": "rust_hound", "name": "Rust Hound",
	"stats": {"hp": 90, "tp": 8, "strength": 14, "mental": 4, "agility": 11, "dexterity": 10, "defense": 6, "level": 2, "critical": 5},
	"basic_attack": "bite", "abilities": ["venom_spit"],
	"affinities": {"shock": 150},
	"status_resist": {"sleep": 50},
	"slots": ["bite", "venom_spit", "bite", "bite", "venom_spit", "bite", "bite", "venom_spit"],
	"overrides": [{"when": "self_hp_half", "ability": "venom_spit"}],
	"experience": 10, "currency": 7
}],
"encounters": [
	{"id": "training", "enemies": ["gel_mite", "gel_mite", "rust_hound"], "drop": {"item": "tonic", "rate": 58}},
	{"id": "marsh", "enemies": [{"enemy": "bog_wisp", "group": "wisps"}, {"enemy": "bog_wisp", "group": "wisps"}, "gel_mite"]},
	{"id": "warden", "enemies": ["brass_warden", "bog_wisp"], "escape": false}
]}
```

* `affinities` is the percent of elemental damage taken. Elements left out take 100. Above 100 is a weakness, below 100 a resistance, and 0 is immunity.
* `status_resist`: as for characters.
* `slots` (required): exactly 8 entries, each the enemy's basic attack or one of its abilities. How often an enemy does something is how many slots hold it (see [Enemy AI](#enemy-ai)).
* `overrides` (optional, up to 4): `{"when", "ability"}`, checked in order; the first whose condition holds replaces the slot drawn. `ability` is the basic attack or one of the abilities. Conditions (`when`):
  * `self_hp_half`: its hit points are at or below 50 percent.
  * `self_hp_quarter_once`: at or below 25 percent; it fires once per battle, for whichever enemy first uses it.
  * `any_ally_hp_half`: a living enemy, itself included, is at or below 50 percent.
  * `last_standing`: it is the only enemy left.
  * `hit_by_attack`, `hit_by_ability`, `hit_by_technique`: a party basic attack, any party technique, skill, item or combo, or a party technique landed damage on it since an override last read these. Whichever fires clears all three.
  * `techniques_blocked`: it has a status that blocks techniques.
  * `hit_by_group`: a party ability aimed at a whole group or every enemy landed damage on it; read and cleared with the three above.
  * `ambushed`: set by the opening `rules.json` `ambush_opening` names. With `party_first` (default) it holds at the enemy's first turn after a Party first opening and is spent at that turn whether or not the override fires; with `enemy_first` it holds from an Enemy first opening until this override fires. The original's setter of this bit is unknown (original-battle-rules.md).
  * `after_turns` (with `turns`, 1 to 99): the enemy has taken at least that many turns before this one (a transform after N turns).
  * `ally_present`, `ally_absent` (with `enemy`): another living enemy of that id is, or is not, in the battle.
  * `free_slot_beside`: a formation position next to it is free (for a summon).
  * `group_intact`: every enemy that started the battle in its group still stands, so a group's joint ability stops once one falls (B4 as data).
* Override target rules (optional `target`, BAT-090/091): `default`, `random`, `lowest_hp`, `highest_hp` (current hit points, ties to the lower index), `has_status` and `lacks_status` (with `target_status`; a random match), and `last_attacker` (the foe that last damaged this enemy). They need a single-target ability, and `last_attacker` one aimed at foes. An override whose rule finds no target does not fire that turn.
* `flying` (optional bool): presentation only, for a renderer to show the enemy off the ground.
* `kind` (optional): `living` (default), `spirit` or `machine` (B6). `vulnerable` (bool): instant defeat can work on it. `statuses`: statuses it starts every battle with (a boss shield). `summons`, `merges_into` (enemy ids, needed by an ability with a summon or merge effect) and `next_form` (the enemy it becomes at full hit points when it falls, in place of being defeated, B16; a chain of forms must end). These may name any enemy of the file.
* `merges_with`, `transforms_into`, `splits_into` (enemy ids): the partner a merge takes in (default: its own id), and what a transform or a split makes of it; a transform or split effect needs its field.
* `attack_element` (optional, an element): its basic attack's physical damage takes the target's affinity for it, like a weapon's element, so Defend does not quarter it. Default none.
* `next_form_at` (optional, 1 to 99, needs `next_form`): the enemy takes its next form, at full hit points, as soon as a hit leaves it at or below that percent of its maximum hit points, not only when it falls (B16).
* `stats` may also give `magic_defense` (0 to 999, default 0), which counts when `rules.json` sets `magic_defense`.
* Combatants added during a battle (summons, replacements) take the next indexes after the starting ones, so indexes stay stable; their formation position is in `EnemyJoined`'s `amount`.
* An encounter is an enemy group: up to 8 formation places in order, at least one holding an enemy. An entry is an enemy id, `{"enemy", "group"}` to set the group label group targeting uses (default: the enemy id), or `null` for a place left free, which a summon or a split can fill. Formation positions count free places (`Combatant::position`, `Battle::formationSize()`, `Battle::enemyAt(position)`), so a macro's `{"enemy": N}` and a scripted end name places, not combatant indexes. `escape: false` forbids escape, for bosses. `awareness` and `escape_rating` (optional, 0 to 255) are the numbers the opening and escape rolls run against; each defaults to the highest agility among the encounter's enemies. An `escape_rating` of 240 or more forbids escape.
* `replace_fallen` (optional bool, B12): a fallen enemy is replaced at once by a fresh one of its kind in its place, so the battle is never won; escape or defeat ends it.
* `drop` (optional): `{"item", "rate"}`, the encounter's item drop (SAV-031), or `{"equipment", "rate"}` for a piece (an id that is not also an item). After a Victory one roll of 0 to 127 must come out below `rate` (0 to 127) for one to drop: items in `rewards().items`, pieces in `rewards().equipment`, both in `BattleEnded`'s `items`. The battle finds the encounter by `BattleSetup::enemyGroup`, which `useEncounter` sets.

### combos.json

```json
{"combos": [
	{"id": "storm_spark", "abilities": ["spark", "gust"], "result": "storm_spark"},
	{"id": "twin_arc", "abilities": [{"ability": "glint", "character": "medic"}, "spark"], "result": "twin_arc"},
	{"id": "cinder_gale", "abilities": ["gust", "ember"], "ordered": true, "result": "cinder_gale"}
]}
```

* A recipe has 2 to 4 technique or skill components and a `result` ability of kind `combo`. A component is an ability id, or `{"ability", "character"}` to tie it to a character who can use it (BAT-060). `ordered: true` requires the listed order; otherwise any order fires it. A recipe with 4 components always needs the listed order.
* Recipes are compared in data order and the first match wins, so a longer recipe must come before a shorter one that matches its first components (the placeholder data lists its two three-member recipes first).
* No two recipes can use the same set of abilities, and some set of different characters must be able to use each recipe's components together.
* The first time a recipe fires, `ComboLog::discover` records it with the play time the host set and the participants' character ids (BAT-064), and the battle emits `ComboDiscovered`.
* The placeholder data has 15 recipes, the count the scope gives.

Enemies have recipes of their own (B4), in `enemy_combos`:

```json
"enemy_combos": [
	{"id": "kite_pincer", "parts": [{"enemy": "squall_kite", "ability": "talon"}, {"enemy": "cinder_kite", "ability": "talon"}], "result": "pincer_dive"}
]
```

* A recipe has 2 to 4 `parts`, each an enemy id and its basic attack or one of its abilities, and a `result` of kind `combo`; ids are unique among both kinds of recipe. `ordered: true` requires the listed order.
* Right after an enemy its AI drives draws its action, the first recipe in data order with a part of that enemy and that action is tried: the next queued turns, passing over party members who fell earlier in the round, must be living enemies its AI drives that can act and pay for a part not yet taken, of that part's id. When every part is filled the joint attack runs on the first enemy's foe, worked out as a party combination is (summed stats and powers, always lands, every participant pays), and the others' turns are spent on it, so they draw no action. A participant that has fallen is not in the queue alive, so a recipe stops being possible once one of its enemies falls. It emits `ComboTriggered` with the participants and no `ComboDiscovered`; the combo log is the party's.
* The disassembly read shows no enemy recipes (original-battle-rules.md, section 4, where an enemy turn ends any combination); this is data for the remake's own encounters, and none of the original's need it.

### text_en.json

`{"text": {"key": "template"}}`. Templates take positional parameters `{0}`, `{1}`. Status keys are `status.<id>.applied`, `.resisted`, `.immune`, `.removed` and, for `keep` stacking, `.unchanged`; their templates take `{0}` the target and `{1}` the status. The CLI also keeps its own `cancel.*` and `auto.stopped.*` lines here. A test checks the table covers `messageKeys(db)`.

### difficulties.json

A difficulty is a data overlay. `applyDifficulty(db, difficulty)` returns the overlaid data and the battle reads only that, so no rule branches on the difficulty.

```json
{"difficulties": [
	{"id": "original", "name": "Original"},
	{"id": "hard", "name": "Hard", "reward_percent": 120,
		"abilities": [{"id": "mend", "power_percent": 80}],
		"enemies": [{"id": "brass_warden", "stats_percent": {"hp": 115, "strength": 110}, "add_abilities": ["numbing_sting"],
			"overrides": [{"when": "self_hp_half", "ability": "numbing_sting", "target": "highest_hp"}], "status_resist": {"sleep": 100}}]}
]}
```

* `reward_percent` (optional, 1 to 1000): scales battle experience and currency, before the reward multiplier.
* `abilities`: `power_percent` scales damage and heal powers; `cost` (techniques) and `uses` (skills) replace the base values.
* `enemies`: `stats_percent` scales stats. Hit points, strength and defense must stay within 80 to 120 percent (the spec's 20% limit for Hard); the others within 50 to 200. `add_abilities` adds techniques or skills. `slots` and `overrides`, when present, replace the enemy's and may use added abilities. `status_resist` replaces the listed resistances.

## Vehicle battles

A vehicle is a combatant defined in `party.json` (BAT-140); BAT-141's rules follow the original:

```json
"vehicles": [{"id": "reed_skimmer", "name": "Reed Skimmer", "stats": {"hp": 220, "tp": 20, "strength": 20, "mental": 8, "agility": 6, "dexterity": 10, "defense": 14},
	"basic_attack": "ram", "abilities": ["hull_vent", "hull_patch"], "affinities": {"shock": 150}}]
```

* `stats`, `basic_attack`, `abilities`, `affinities` and `status_resist` work as for enemies. The abilities must be skills.
* A vehicle id may not also be a character id.

With `flags.vehicleBattle` set, `BattleSetup::vehicle` names the vehicle, and `Battle::create` refuses an unknown one, or a vehicle without the flag. The party side is then the members of `setup.party`, in lineup order and marked `Combatant::aboard`, followed by the vehicle; enemies follow as usual, so `partyCount()` is one more than the party. Members aboard take no command (`setCommand` refuses with `command.aboard`), are skipped by macros, cannot be targeted by either side, and do not count for Victory or Defeat. The vehicle attacks, uses its skills or escapes; `setCommand` refuses Defend and items with `command.vehicle_cannot`. It starts each battle at full hit points, and when it falls every member aboard is defeated with it, so the battle is lost. `rewards()` halves the experience, and `memberExperience` has one entry per member aboard, split among those not defeated; the vehicle gets none. `partyState()` returns the members aboard. The vehicle's skill uses carry from battle to battle as the original's do (B19): `BattleSetup::vehicleSkillUses` gives what is left (a skill left out starts full) and `Battle::vehicleSkillUses()` returns it after the battle; its hit points are full at every start.

## Battle records

`serializeRecord` writes a line-based text record (format 8, which adds `piece` lines for the bag pieces a member may cast and allows a free formation place as an `enemy -` line; `parseRecord` still reads 3 to 7): format version, rules version, event schema version, content hash of the loaded data, the full setup (seed, difficulty, reward multiplier, opening, flags, escape rating, vehicle, scripted end, enemy group and formation, inventory, the vehicle's skill uses left, each member's level, experience, hit points, technique points, skill uses, equipment, carried statuses, chosen name and taught abilities), the combo log entries already known, and per round the play time, the macro name, the commands the host set and the macro block order. Commands the battle filled in itself are not recorded, because it fills them the same way on replay. Auto-battle commands are set before `resolveRound`, so they are recorded like the host's. Ids are written with letters, digits and `_` kept and every other byte as `%XX`; an empty value is `-`.

The last line is `checksum <FNV-1a 64 of everything before it>`. `parseRecord` refuses a record whose checksum is missing or does not match (SAV-014), so a damaged record is never replayed. It is a guard against accidental damage, not tampering.

`replayBattle` refuses a record from another rules version (`kRulesVersion`), event schema (`kEventSchemaVersion`) or data (content hash). Bump `kRulesVersion` whenever a rule, a formula or the order of random draws changes.

`serializeEvents` writes one line per event: the round, the type name (`damage_dealt`), then every field that differs from its default, in declaration order, as `name=value`.

## Events

`Event` has a `type` and only the fields that type uses. `actor` and `target` are combatant indexes. Status events carry a `message` key for the string table (BAT-083). The types, in the BAT-115 set (spec names in brackets where core's differ):

* `BattleStarted` (round 0, first in round 1's list): `ids` and `groups` per combatant index, `partyCount`, `opening`, `flags`.
* `RoundStarted`, `RoundEnded`: `round`.
* `CommandsLocked`: `commands` (every combatant's command for the round, by index; a target of -1 means the battle picks; an enemy its AI drives shows a basic attack with -1, since it picks when its turn comes), `macro`.
* `OrderResolved`: `order`.
* `Retargeted`: `actor`, `previousTarget`, `target`. Also an enemy drawing a new foe because its foe fell.
* `ActionStarted`: `command` says attack, ability, item or defend. `ability` holds the ability or item id; `target` is the single resolved target or -1.
* `ActionCancelled`: `cancel` gives the reason; `actor_unable` names the cause in `status`. A `silenced` cancel follows the `ResourceSpent` for the points the technique cost.
* `ComboTriggered`: `combo` (recipe id), `ability` (result), `participants`.
* `ComboDiscovered`: `combo`.
* `ResourceSpent`: `resource` (technique points, a skill use or an item), `amount`, `ability`.
* `ItemHadNoEffect`: the item was not used up.
* `Hit`, `Missed`, `Critical`: `actor`, `target`, `ability`. `Critical` comes right before the critical hit's `DamageDealt`.
* `DamageDealt` (Damaged): `actor`, `amount`, `hp` (after), `element`, `effectiveness`.
* `Healed`: `amount`, `hp`. `Revived`: `amount` and `hp` (the hit points it stands up with).
* `StatusApplied` (`duration`), `StatusResisted` (`effectiveness` is `Immune` when the target is immune and no roll was made), `StatusRemoved` (`statusEnd`: cured, duration, recovered, woke on damage or defeated; the text form omits `cured`, the default). A lowered stat that the target resists is a `StatusResisted` with `stat` set, no `status`, and the `battle.stat_resisted` key.
* `StatModified` (StatChanged): `stat`, `amount` (the battle value less the base, after floors). With no `actor` it comes at round end, from a status that ran out and left the stat at 1.
* `CombatantDefeated` (Fell): hit points 0 and the `defeated` status. Each status it clears gets a `StatusRemoved` first; statuses that survive defeat and stat changes stay.
* `EscapeAttempted`: `succeeded`.
* `Message`: `message`, `params`.
* `BattleEnded`: `outcome`, `experience`, `currency`, `items` (the drops, item id to count), `gameOver`.
* `EnemyJoined`: `target` (the new index), `actor` (the summoner, or -1 for a replacement), `ids` and `groups` (its id and group), `amount` (its formation position), `hp`.
* `EnemyLeft`: `target` leaves without being defeated (merged into `actor`); it no longer counts or pays.
* `EnemyTransformed`: `target` takes the form in `ids` at full `hp`, after a `StatusRemoved` for each status it had.

## Formulas

Every row follows the original as read from the community disassembly except revive, the experience curve, stats at a level and a combo's heal (average mental), which are **placeholders to be tuned**, and the `critical` and `evasion` additions (0 gives the original). The math is in `src/formulas.cpp`; the inputs marked (data) come from the data files.

| What | Formula |
| --- | --- |
| Initiative | `agility + roll`, where roll is 0 to `highest agility / 2 - 1`; 0 when agility is 0 |
| Chance score | `(roll + actor stat - target stat) * 2`, where roll is 0 to 63 |
| Hit (basic attacks) | the chance score of attacker dexterity against target agility plus evasion (data). The attack misses at 8 or below. Techniques, skills, combos and items always land |
| Critical (basic attacks) | the same roll, with the attacker's critical (data) added to dexterity. Critical above 116, and only against a single target |
| Spread | 8 plus sixteen rolls of 0 to 7: 8 to 120, 64 on average |
| Damage | `(attack * spread / 64 + attack + bonus * 2) * affinity / divisor - defense`, from 1 to 999, or 0 when the target is immune; the divisor is `affinity_divisor`, 100 by default |
| Physical damage | attack is strength, defense is the target's defense, bonus is the effect's `power`, plus `attack / 4` on a critical. Affinity is the target's for the effect's `element`, else the weapon's or enemy's attack element; without one, `neutral_affinity` (the divisor by default). Items: attack is `power`, no bonus |
| Elemental damage | attack is mental, defense is the target's mental (its magic defense when `rules.json` sets `magic_defense`), bonus is the effect's `power`. Items: attack is `power`, no bonus |
| Combo damage | attack is the sum of each participant's stat for its component: the component's `attack_stat`, else mental (technique) or strength (skill); bonus the sum of the components' first damage `power` (0 without one); defense and element as above for the recipe's effect |
| Opening (random mode) | `openingFromRoll`: the chance score of the highest living party agility against the encounter's `awareness`. Enemy first at 12 or below, Party first above 116 |
| Defending | a hit without an element uses `defend_affinity`, by default `25 * divisor / 100` (25); with `defend_elemental` an elemental hit uses the lower of its affinity and that |
| Heal | `(mental * spread / 64 + mental + power * 2) / 2`. On the field the spread is 64. Items: `power`. Combos: the participants' average mental |
| Revive | `max(1, max hit points * power / 100)` |
| Status roll | `(roll + actor mental - target mental) * ((100 - resistance) * 4 / 100)`, where roll is 0 to 63 and items have 0 mental. Resisted at `threshold` or below. Resistance 100 is immunity, with no roll. A lowered foe stat rolls the same with resistance 0 |
| Stat change | `base + amount` or `base - amount`, floored at 1 for agility and dexterity and 0 for the others. Amount: the effect's `power` when given (always for items), else the user's mental (technique), strength (skill or basic attack), `attack_stat`, or the summed stat (combo). The latest change to a stat replaces the earlier |
| Enemy foe | one roll over the party members not defeated, in party order, weighted `count - position` |
| Enemy action | one of 8 slots, never the index any enemy drew last (1 in 8 for the battle's first draw, then 1 in 7) |
| Escape | succeeds when no living enemy can act. Otherwise the chance score of the highest living party agility against the battle's escape rating must be above 40 |
| Experience to next level | the character's `experience_table` (the difference of two totals), else `15 * level^2 + 10`. Carried-over experience counts toward the next level. Maximum level 99, the published cap. A level-up raises the maximums and leaves current hit points and technique points alone |
| Stats at a level | the character's `level_stats` row, past its end the last row plus `growth` per level; without a table `base + growth * (level - 1)`; plus equipment bonuses |
| Rewards | Victory: every enemy's experience and currency; scripted end: the fallen enemies' only. Times `reward_percent * multiplier / 10000` (multiplier 100, 150 or 200), rounded down once. A vehicle battle halves the experience. Each party member not defeated gets the experience divided by their number, rounded down; currency is paid in full |
| Drops | Victory only: one roll of 0 to 127 below the encounter's `rate` drops one of its item. Neither the reward multiplier nor `reward_percent` scales it |

## Not built yet

* Vehicles in the field and their encounter tables (ENC-007) are the host's business (`../game`).
* Presentation keys and type tags on abilities and combinations (BAT-040, BAT-060).
* Per-character ability tuning in difficulty overlays; ability overlays apply to every user of the ability.
* `BattleStarted` reaches the host with the first round's events; before that the host reads the battle state.

## Changes a host must make

This round: rules version 14 (a combination component's `attack_stat` is the stat its participant adds to the combination's attack, which before was always mental or strength), event schema 7 and record format 8 unchanged; `replayBattle` refuses records of rules version 13. Battles of the placeholder data play as before (none of its components has an `attack_stat`); every other option of the round is data with today's behavior as the default.

* `battle_rules.h`: `elementlessAffinity`, `defendAffinity` (both `std::optional<int>`), `defendElemental`, `elementlessHitAffinity()`, `defendedAffinity()` (`rules.json` `neutral_affinity`, `defend_affinity`, `defend_elemental`). `battle.h`: private `Battle::elementAffinity`.
* `physical_damage` takes `element`. `transform` may target other combatants and items may carry it; an enemy's `transforms_into` is needed only for a `self` transform among its abilities.
* `character.h`: `CharacterDef::levelExperience` (per-level costs from `experience_table` totals) and `levelStats` (`level_stats`). `progression.h`: `experienceToNextLevel(character, level)`; `statsAtLevel` reads the tables. `formulas::experienceToNextLevel(level)` stays the curve.
* Loader problems `load.base_or_level_stats`, `load.experience_table_rises`, `load.level_stats_have_no_level`, `load.table_levels`.

The round before: rules version 13 (a combatant's normal affinity is the divisor, so `affinity_divisor` other than 100 no longer halves or resists ordinary hits), event schema 7 unchanged (the event's `element` field is now an id; its text form is the same), record format 8 (`parseRecord` still reads 3 to 7; `replayBattle` refuses records of rules version 12). Battles of the earlier placeholder data play as before; the content hash changes with the data.

* `types.h`: `Element` is a struct holding an id (`Element::Heat` and the others stay as constants, `Element::None` is the empty id), `kBuiltInElements`; `Stat::MagicDefense` and new `EffectKind` values `Transform`, `Split`, `SelfDestruct` (appended). `names.h`: `toString(const Element&)` returns `std::string`; `parseElement` reads the built-in names only, `Database::hasElement`, `elementIds`, `elementIndex` know the declared ones.
* `battle_rules.h`: `ambushOpening`, `elements`, `neutralAffinity()`. `combatant.h`: `neutralAffinity`, `paysWhenGone`, `turnsTaken`. `ability.h`: `Effect::count`, `Effect::rewards`, `AbilityDef::attackStat`, `ItemDef::consumed`. `equipment.h`: `useFromBag`, `affinities`. `enemy.h`: `AiConditionKind::AfterTurns`, `AiOverride::turns`, `ItemDrop::equipment`, `EnemyDef::attackElement`, `mergesWith`, `transformsInto`, `splitsInto`; an empty `FormationSlot::enemy` is a free place. `character.h`: `CharacterDef::teachable`. `setup.h`: `BattleSetup::pieces`. `battle.h`: `Rewards::equipment`, `Battle::formationSize`, `enemyAt`.
* Message key `battle.self_destruct`; loader problems `load.bad_element_id`, `load.drop_equipment_is_item`, `load.drop_item_or_equipment`, `load.drop_missing_equipment`, `load.only_after_turns_has_turns`, `load.split_needs_splits_into`, `load.transform_needs_transforms_into`, `load.unknown_ambush_opening`, `load.use_from_bag_needs_use`.
* Data: the Ember, Shale and Kiln sprites (`sprites`), the Moss Pupa and Moth (`pupa`), the Brine Jelly and Droplets (`jelly`, a formation with free places), the Fuse Beetle (`beetles`, dropping the Ember Mail), the techniques Fuse, Molt, Divide, Cinder Burst and Dust Veil, the Dawn Bell, the Squall Fan, the Ember Mail, the element `tide`, and the fighter's `teachable` Hallow. No zone draws the new encounters.

The round before: rules version 12 (two hands, enemy recipes), event schema 7 unchanged, record format 7 (a member may wear a piece in the `left_hand` position; `parseRecord` still reads 3 to 6, `replayBattle` refuses records of rules version 11). Battles of the earlier placeholder data play as before; the content hash changes with the data.

* `types.h`: `EquipSlot::Shield` and `EquipSlot::LeftHand` (appended; names `shield` and `left_hand`). `equipment.h`: `EquipmentDef::hands`. A loop over equipment slots or a fixed-size table of them needs the two new values.
* `progression.h`: `isHandPiece`, `fitsPosition`, `defaultPosition`, `startingPosition` and `equip(member, db, bag, id, position)`; `equip(member, db, bag, id)` puts the piece at its default position. Message key `equip.wrong_position` (in `messageKeys` and `text_en.json`).
* `combo.h`: `EnemyComboPart`, `EnemyComboRecipe`; `database.h`: `Database::enemyCombos`. `ComboTriggered` can now have enemy participants.
* `enemy.h`: `EnemyDef::flying`, `nextFormAt`. Items may leave `effects` out (key items).
* Rule options, all optional with today's behavior as the default, so no battle of default data plays differently and no record replays differently (the rules version moved for two hands and enemy recipes, not for these): `battle_rules.h` `affinityDivisor`, `magicDefense`, `instantDefeatKinds` (`rules.json` `affinity_divisor`, `magic_defense`, `instant_defeat_kinds`); `stats.h` `Stats::magicDefense` (`magic_defense` in stats and equipment); `character.h` `CharacterDef::vulnerable`; `instant_defeat` takes `element`; `formulas::damage` takes the divisor and `formulas::elementScore` is new. Loader problem `load.next_form_at_needs_next_form`. Placeholder: the Gloam Reaper (Last Toll, encounter `reaper`), the Shellback and its bare form (`next_form_at` 50, encounter `shellback`), the Ward Mantle (magic defense 8), the Cogwright marked `vulnerable`.
* Loader problems `load.enemy_cannot_use`, `load.enemy_combo_part_shape`, `load.more_than_two_hands`, `load.needs_2_parts` and `load.only_weapons_have_hands`; `left_hand` is no piece slot (`load.unknown_slot`).
* Data: the weapons Gill Knife and Kelp Cleaver (two-handed), the shields Driftwood Buckler and Reef Targe (the fighter wears `greatblade` and `buckler`, the engineer `buckler`), the characters Test Lancer and Test Harpooner, the key items Skimmer Reed, Dune Horn, Rime Bell, Vault Key and Lantern Charm, the vehicles Dune Crawler and Rime Borer with the skills Sand Spray and Rime Drill, the enemies Squall Kite and Cinder Kite (flying, encounter `kites`, recipe `kite_pincer` into Pincer Dive) and Cinder Bulwark (encounter `bulwark`); the Bog Wisp and the Hive Lantern fly.

The round before: rules version 11, event schema 7, record format 6 (`parseRecord` still reads 3 to 5; `replayBattle` refuses records of rules version 10 and event schema 6). Battles of the earlier placeholder data play as before except vehicle battles, whose skill uses now carry over, and combinations whose parts had a fallen enemy between them.

* `types.h`: `Element::Holy`, new `EffectKind` values `InstantDefeat`, `RestoreTp`, `StripModifiers`, `Summon`, `Merge`, `Teach` (appended), new `CreatureKind` (`names.h`: `toString`, `parseCreatureKind`). A `switch` over `EffectKind` or `Element` needs the new cases.
* `ability.h`: `Effect::kinds`, `Effect::ability`, `Effect::reaches(kind)`, `AbilityDef::family` (after `field`), `teachEffect(effects)`.
* `enemy.h`: `AiConditionKind` adds `HitByGroup`, `Ambushed`, `AllyPresent`, `AllyAbsent`, `FreeSlotBeside`, `GroupIntact`; `AiOverride::enemy`; `EnemyDef::kind`, `vulnerable`, `statuses`, `summons`, `mergesInto`, `nextForm`; `Encounter::replaceFallen`.
* `character.h`: `CharacterDef::kind`, `PartyMember::taught` (known abilities include them; records and the game's saves keep them). `status.h`: `StatusDef::damageCap`. `battle_rules.h`: `machineRecoverHp` (`rules.json` requires `machine_recover_hp`). `equipment.h`: `EquipmentDef::use`. `setup.h`: `BattleSetup::vehicleSkillUses`. `combatant.h`: `kind`, `vulnerable`, `position`, `gone`, `hitByGroup`, `ambushed`.
* `battle.h`: `Battle::vehicleSkillUses()`; `rewards()` is fixed when the battle ends (before fallen machines stand up). Combatants can be added during a battle, after the starting ones: size per-combatant arrays from `combatants()` each round. `formulas.h`: `kMaxLevel` 99.
* `event.h`: `EventType::EnemyJoined`, `EnemyLeft`, `EnemyTransformed` (appended). `Healed` with `resource` tp restores technique points.
* `text.h`: `messageKeys` adds `battle.instant_defeat`, `battle.instant_defeat_immune`, `battle.instant_defeat_resisted`, `battle.machine_recovered` and `field.learned`; `text_en.json` has them and the `aegis` and `daze` status keys.
* Data: the effects, fields and conditions above; placeholder content to exercise them (the Cogwright, Gear Salve, Kindle Salts, Null Dust, Sunstone, Hallow Scroll, Arc Disc, Storm Rod, the techniques Bright Spark, Hallow, Unmake, Renewal and skills Calm Tide and Self Repair, the statuses Daze and Aegis, and the enemies Hive Lantern, Twin and Grand Ooze, Mirror Crab, Tide Colossus and its core, Aegis Sentinel and Echo Twin in the encounters `hive`, `oozes`, `crabs`, `colossus`, `sentinel`, `endless` and `echoes`). The mystic learns Bright Spark at 11 and Unmake at 15, the medic Renewal at 14. The content hash changes with the data.

The round before: rules version 10 (a battle now refuses a command whose ability or item travels), event schema 6 and record format 5 unchanged. No battle of the existing data plays differently and no record of it replays differently, but `replayBattle` refuses records of rules version 9, since the version changed.

* `types.h`: `EffectKind::Travel` (appended; name `travel`) and `enum class TravelTarget { Town, DungeonExit }` (names `town`, `dungeon_exit`; `names.h`: `toString(TravelTarget)`, `parseTravelTarget`). `ability.h`: `Effect::travel` and `travelEffect(effects)`. A `switch` over `EffectKind` needs the new case.
* `Battle::setCommand` refuses an ability or item with a travel effect (`command.field_only`, in `messageKeys` and `text_en.json`). `useItemOutsideBattle` spends a travel item without applying anything; `useAbilityOutsideBattle` pays a travel technique's cost as before.
* Loader problems `load.unknown_travel_target`, `load.travel_alone`, `load.travel_not_in_combo` and `load.travel_party_only`; `load.field_use_allows_only_heal` now lists travel too.
* Data: `abilities.json` adds the techniques `homeward` (8 TP, to a town) and `outpath` (4 TP, out of a dungeon); `items.json` adds `homing_shell` (to a town) and `waymark_chalk` (out of a dungeon); in `party.json` the mystic learns Homeward at level 9 and the scout Outpath at level 8, levels no seeded run reaches. The content hash changes with the data.

The round before: rules version 9, event schema 6 and record format 5 are unchanged, no rule or data format changed, and battle outcomes and records are the same.

* `battle.h`: new `memberCombatant(db, member)`, the party combatant `Battle::create` builds for a member (level stats plus equipment, the basic attack a weapon replaces, the weapon's element, known abilities, affinities and status resistances), before any difficulty overlay. `Battle::create` and `Battle::partyOnly` call it, so a status screen or an equipment comparison built on it shows the numbers a battle uses. The character must exist in the data.

The round before: rules version 9, event schema 6, record format 5 (unchanged: the AI's state is rebuilt on replay). Enemy targeting and action choice, retargeting, combinations, rewards, drops, stat changes, statuses, defeat and vehicle battles follow the original as read from the community disassembly. `parseRecord` still reads formats 3 and 4; `replayBattle` refuses records of rules version 8 and earlier, and of event schema 5.

* `enemy.h`: `AiRule`, `AiCondition` and `EnemyDef::ai` are replaced by `EnemyDef::slots` (8 ability ids) and `EnemyDef::overrides` (`AiOverride { when, ability, target }`); `AiConditionKind` now lists `SelfHpHalf`, `SelfHpQuarterOnce`, `AnyAllyHpHalf`, `LastStanding`, `HitByAttack`, `HitByAbility`, `HitByTechnique`, `TechniquesBlocked`. New `kAiSlots` and `kMaxAiOverrides`. `EnemyDef::drops` is gone; `ItemDrop` is now `{ item, rate }` and `Encounter::drop` holds it. `difficulty.h`: `EnemyOverlay::ai` is replaced by `slots` and `overrides`.
* `status.h`: `StatusDef::tickDamagePercent` is gone; new `statsToOne`, `removes`, `survivesDefeat`. `countsTowardDefeat` applies to party members only.
* `ability.h`: `Effect::percent` and `Effect::duration` are gone; new `Effect::raises`. An item's stat change takes its amount from `power`.
* `combatant.h`: `ActiveModifier` is `{ stat, amount }` (amount from the base, no duration). New `Combatant::pinned`, `hitByAttack`, `hitByAbility`, `hitByTechnique`, `base()`, `setModifier()`.
* `event.h`: `EventType::StatusTicked` and `StatModifierExpired` are gone (later enumerators moved); `StatModified::amount` is the change from the base, with no `duration`. A resisted stat change is a `StatusResisted` with `stat` and the `battle.stat_resisted` key.
* `types.h`, `battle_rules.h`, `names.h`: `ExperienceSplit`, `BattleRules::experienceSplit`, `toString(ExperienceSplit)` and `parseExperienceSplit` are gone. `formulas.h`: `statusTick` and `modifiedStat` are gone. `vehicle.h`: `VehicleDef::crewDamagePercent` is gone. `combo.h`: new `kMaxComboParticipants` (4).
* `Battle`: an AI enemy's action is decided at its turn, so `CommandsLocked` shows it as a basic attack with target -1. `resolveTargets` keeps a single ally target even when it has fallen, and a gone foe resolves to the first living foe; the battle replaces gone foes only when the action runs. `setCommand` refuses Defend and items for a vehicle with `command.vehicle_cannot`. `rewards()` splits the experience among members not defeated and halves it in a vehicle battle; `items` is the encounter's drop. `grantExperience` no longer raises current hit points and technique points.
* `text.h`: `messageKeys` adds `battle.stat_resisted` and `command.vehicle_cannot`; `text_en.json` has them and `status.silence.unchanged`.
* Data: `rules.json` lost `experience_split`. `statuses.json` lost `tick_damage_percent` and accepts `stats_to_one`, `removes` and `survives_defeat`. `modify_stat` takes `direction` and an optional `threshold` (items also `power`) in place of `percent` and `duration`. A combo's damage effects have no `power`. Enemies take `slots` and `overrides` in place of `ai`, and no `drops`; encounters accept `drop`. Vehicles lost `crew_damage_percent` and their abilities must be skills. Recipes have 2 to 4 components. Difficulty overlays take `slots` and `overrides` in place of `ai`.

Before that: rules version 8, record format 5. Defend, the status roll, round-end recovery, and per-encounter opening and escape numbers follow the original. Statuses gained `enemy_duration`; `apply_status` takes `threshold` in place of `chance`; encounters gained `awareness` and `escape_rating`; the record gained the escape rating.

Before that: rules version 7. Hit, critical, damage, healing and escape follow the original as read from the community disassembly. `rules.json` lost `critical_damage_percent` and `escape`, physical damage effects lost `critical`, and their `power` is now a bonus, not a percent.

Before that: rules version 6. Turn order follows the original: the roll is 0 to just under half the highest agility, and a macro only delays a member who would act before the member listed ahead of it.

The round before: rules version 4, event schema 5, record format 4. `parseRecord` still reads format 3, but `replayBattle` refuses records of event schema 4, since event type names changed. The data's content hash is unchanged.

* `names.h`: every `toString` name is lowercase with underscores. `EventType` names changed from `DamageDealt` to `damage_dealt` and so on, which changes the text form of `serializeEvents`; `RewardMultiplier` names changed from `1`, `1.5`, `2` to `x1`, `x1_5`, `x2`, which changes records and the battle CLI's printout. `parseEventType` and `parseRewardMultiplier` also accept the old spellings, so older event text, records, saves and `--reward 1.5` still read. Compare names through `toString`, not literals.
* `result.h`: new `struct Problem { file, line, entry, message }` and `Result::problems`, filled by `loadDatabaseFromTexts`, `loadDatabaseFromDirectory`, `parseJson`, `parseTextTable` and `loadTextTable`; `error` keeps the English lines (a JSON syntax error now reads `file:line: text` instead of `file: line N: text`). New `Result::failure(problems, text)`.
* `text.h`: new `problemText()`, `formatProblem(problem, table)` and `formatProblems(problems, table)`.
* `character.h`: `PartyMember::name`, the name the player gave the member; empty keeps the character's data name. `Battle::create` gives the party combatant that name, `partyState()` returns it, and records keep it (`member_name` line).
* `version.h`: `kEventSchemaVersion` 5, `kRecordFormatVersion` 4.
* Build: the library is compiled with `-fPIC`.

The previous round: rules version 4, event schema 4, record format 3; older records are refused, and the data's content hash changed.

* `battle.h`: `Rewards` adds `items` (`Inventory`, the drops). `rewards().memberExperience` now has one entry per member of `setup.party` (the same as `partyCount()` except in a vehicle battle). Add `rewards().items` to the party's inventory.
* `event.h`: `Event` adds `items` (`Inventory`), set on `BattleEnded`; the text form writes it as `items=id:count,...`.
* `enemy.h`: new `struct ItemDrop { item, chance, count }` and `EnemyDef::drops`.
* New `vehicle.h`: `struct VehicleDef { id, name, stats, basicAttack, abilities, affinities, statusResist, crewDamagePercent }`. `database.h`: `Database::vehicles` and `Database::vehicle(id)`.
* `setup.h`: `BattleSetup::vehicle`. `combatant.h`: `Combatant::aboard`.
* `field.h`: `useItemOutsideBattle` erases an item whose count reaches 0 instead of leaving it at 0.
* `text.h`: `messageKeys` adds `command.aboard`; `text_en.json` has it.
* Data: `enemies.json` accepts `drops`; `party.json` accepts `vehicles`; `abilities.json` adds `ram`.

The previous round's changes, grouped by header. Rules version 3, event schema 3, record format 2.

* `event.h`
  * `FailReason` is now `CancelReason` (`none`, `actor_unable`, `silenced`, `not_enough_tp`, `no_uses_left`, `no_item`, `no_target`); `Event::fail` is now `Event::cancel`.
  * `EventType::ActionFailed` and `ActionPrevented` are replaced by `ActionCancelled`; `StatusCured` and `StatusExpired` by `StatusRemoved`. New: `BattleStarted`, `CommandsLocked`, `OrderResolved`, `Retargeted`, `ComboDiscovered`, `Hit`, `Message`. Enumerator positions changed; `toString` names are stable.
  * `Event::firstDiscovery` is removed (use `ComboDiscovered`). New fields: `previousTarget`, `combo`, `message`, `params`, `hp`, `duration`, `opening`, `gameOver`, `experience`, `currency`, `macro`, `order`, `ids`, `groups`, `partyCount`, `flags`, `commands`. `DamageDealt` now sets `actor`; `Missed` and `Critical` set `ability`.
  * `StatusEnd` is now `Cured`, `Duration`, `Recovered`, `WokeOnDamage`, `Defeated`. `Outcome` adds `ScriptedEnd`.
* `battle.h`
  * `std::string Battle::setCommand(...)` is now `Message Battle::setCommand(...)`; `.empty()` still means accepted.
  * `FailReason Battle::checkUsable(...)` is now `CancelReason Battle::checkUsable(...)`.
  * New: `gameOver()`, `setPlayTime(std::int64_t)`, `startAuto(const Macro&)`, `cancelAuto()`, `fillFromAuto()`, `autoActive()`, `autoStop()` and `enum class AutoStop`.
  * `applyMacro` keeps steps whose cost cannot be paid or that have no target yet (status `Applied` with the issue) instead of falling back to a basic attack, and skips members whose status stops them or forces their command.
  * `resolveTargets` returns every living foe for `random_enemy`. `rewards()` pays out for `ScriptedEnd`. `partyState()` fills `PartyMember::statuses`.
* `macro.h`: `MacroIssue` adds `CannotAct` and `ForcedByStatus` after `CaughtOffGuard` (later enumerators moved). New `isCostIssue`, `serializeMacros`, `parseMacros`.
* `combo.h`: `ComboRecipe` adds `characters` and `ordered`. `ComboLog::discover(id)` is now `discover(id, playTimeMs, characters)`. `discovered()` now returns `std::set<std::string>` by value. New `ComboLogEntry`, `entry()`, `entries()`, `serializeComboLog`, `parseComboLog`.
* `record.h`: `BattleRecord::knownCombos` is now `std::vector<ComboLogEntry>`. `RoundInput` adds `macroName` and `playTimeMs`. `RecordedCommand` moved to `command.h`. `parseRecord` refuses a record without a matching checksum line.
* `setup.h`: new `BattleSetup::scriptedEnd` (`ScriptedEnd`, `ScriptedEndKind`). `BattleFlags` moved to `types.h`.
* `types.h`: `Targeting` adds `OneFallenAlly` and `RandomEnemy`. New `EquipmentBag`.
* `status.h`: `StatusDef::skipsTurn` is replaced by `action` (`StatusAction`). New `forcedTarget` (`ForcedTarget`), `stacking` (`StatusStacking`), `persistsOutsideBattle`, `countsTowardDefeat`, `restCures`. `ActiveStatus` moved here from `combatant.h`.
* `character.h`: `PartyMember` adds `statuses`. `Battle::create` refuses unknown statuses and statuses that do not persist outside battle.
* `combatant.h`: `Combatant` adds `lastAttacker`.
* `ability.h`: `AbilityDef` and `ItemDef` add `field`.
* `enemy.h`: `AiRule` adds `target` (`AiTarget`, `AiTargetRule`).
* `progression.h`: `canEquip` returns `Message`. `equip(member, db, id)` is now `Message equip(member, db, bag, id)`; `unequip(member, db, slot)` is now `unequip(member, db, bag, slot)`. `rest` also cures statuses with `rest_cures` and leaves a fallen member down when `rest_revives` is false.
* `database.h` and `battle_rules.h`: new `Database::startingEquipmentBag` and `BattleRules::restRevives`.
* `rng.h`: new `Rng::state()` and `Rng::restore()`.
* `names.h`: now includes `battle.h`. New `toString` and parse functions for `Side`, `StatusAction`, `ForcedTarget`, `StatusStacking`, `AiTargetRule`, `MacroStepStatus`, `MacroIssue`, `ScriptedEndKind`, `AutoStop` and `CancelReason`; new parse functions for `MacroTargetKind`, `EventType`, `Outcome`, `Effectiveness`, `StatusEnd` and `ResourceKind`. `json.h` adds `parseJsonType`.
* New headers: `message.h` (`Message`), `text.h` (`TextTable`, `parseTextTable`, `loadTextTable`, `formatText`, `messageKeys`, `statusMessageKey`), `field.h` (`FieldUse`, `useItemOutsideBattle`, `useAbilityOutsideBattle`).
* Data: `statuses.json` uses `action` instead of `skips_turn` and accepts the new status fields; `rules.json` requires `rest_revives`; revive abilities and items target `one_fallen_ally`; abilities and items accept `field`; `party.json` accepts `equipment_bag`; AI entries accept `target` and `target_status`; combo components may be objects and recipes accept `ordered`; `text_en.json` is new.
