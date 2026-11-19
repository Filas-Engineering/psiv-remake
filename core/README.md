# Battle rules core

A headless battle rules library for a turn-based party RPG. It is plain C++17 and uses only the standard library, so it can go into an Unreal Engine module unchanged. The rules never print, never read globals and never throw across the public API. All names, numbers and text in `data/` are original placeholders.

## Layout

| Path | Contents |
| --- | --- |
| `include/rules/` | Public headers (namespace `rules`) |
| `src/` | Library sources, built into `build/librules.a` |
| `cli/battle_cli.cpp` | Terminal front end |
| `tests/` | One test executable with a minimal assert helper (`tests/test.h`) |
| `data/` | Placeholder JSON data: rules, statuses, abilities, items, equipment, party, enemies, combos, difficulties |

## Build and test

Needs `clang++` and GNU Make 3.81 or later. The compile flags are `-std=c++17 -Wall -Wextra -Werror`.

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
./build/battle_cli --seed 1 --auto --encounter warden --difficulty hard --reward 1.5
./build/battle_cli --seed 1 --auto --macro tempest                  # runs that macro every round
./build/battle_cli --seed 1 --auto --opening enemy_first
./build/battle_cli --seed 1 --auto --record build/run.record        # also writes the battle record
./build/battle_cli --replay build/run.record                        # prints exactly what the recorded run printed
./build/battle_cli --seed 4 --encounter marsh                       # interactive; "save NAME" keeps the last round as a macro
```

Other options: `--data DIR`, `--level N` (party level, default 1) and `--max-rounds N` (default 50). `--difficulty` defaults to `original`. Exit codes: 0 when the battle ended (any outcome) or the player quit, 1 for a data or record error, 2 for bad arguments, 3 when the round limit was hit.

## How a host drives a battle

1. Load data once: `Result<Database> db = loadDatabaseFromDirectory("data")`, or `loadDatabaseFromTexts` with file contents the engine already read (`dataFileNames()` lists them). On failure, `error` lists every problem, one per line, as `file:line: entry: message`.
2. Keep the party as `std::vector<PartyMember>` between battles. `makePartyMember(db, character, level)` creates one in its starting equipment, and `rest(member, db)` restores hit points, technique points and skill uses. Change equipment with `equip(member, db, equipmentId)` and `unequip(member, db, slot)`; `canEquip` checks first. `memberStats` and `knownAbilities` give what the member has now.
3. Fill a `BattleSetup`: `party`, `inventory`, the enemy group (`useEncounter(setup, db, encounterId)` fills `enemyGroup`, `enemies` and `flags.escapeAllowed`), `opening` (`openingFromContact` turns a visible-mode contact into one), `flags`, `difficulty`, `rewardMultiplier` and `seed`.
4. Start: `Result<Battle> battle = Battle::create(db, setup, &comboLog)`. Combatant indexes are stable: party members first, in lineup order, then enemies in formation order. The battle keeps its own copy of the data with the difficulty overlay applied. The `ComboLog` must outlive the battle.
5. Each round:
   * Set party commands with `setCommand(index, Command::ability("spark", target))`. It returns an empty string, or the reason the command was rejected (for example escape in a battle that forbids it, or a side caught off guard by the opening; `sideActsNextRound` and `escapeAllowed` tell the menu in advance). Or call `applyMacro(macro)`, which fills the commands and returns a `MacroStepResult` for each step (applied, retargeted, fell back or skipped, with the reason).
   * Call `resolveRound()`. Party members without a command use their basic attack. Enemies without a command ask their AI. The call returns the round as a list of typed `Event`s for the renderer to play back. The host can also set enemy commands for scripted fights.
   * After a round, `lastRoundAsMacro(name)` turns that round's party commands into a macro.
6. Stop when `outcome()` is no longer `Ongoing`. Then read `rewards()` (`experience` and `currency` after scaling, and `memberExperience` per party index after the split), `partyState()` (hit points, technique points, skill uses and equipment to keep) and `inventory()`, and apply `grantExperience(member, db, rewards.memberExperience[i])` to each member. It returns a `LevelUp` with the levels gained and the abilities learned.
7. Save `serializeRecord(battle.record())` for bug reports. `parseRecord` reads it back and `replayBattle(db, record, comboLog, events)` rebuilds the battle and its full event stream. `serializeEvents(events)` gives a stable text form of an event stream.

`autoCommand(battle, actor)` is a simple deterministic auto-battle policy. The CLI and the tests use it.

### Round resolution

1. The side that lost the opening (Party first: enemies; Enemy first: the party) gets no commands for the first `surprise_rounds` rounds.
2. Every other living combatant gets a command. Defenders take half damage for the whole round.
3. Turn order: each combatant rolls an initiative of agility plus a random number from 0 to the spread. Ties go to the party, then to the lower index. Members set by a macro are pulled out and act as one block, in step order, placed at their average initiative. So a macro always keeps its members' steps adjacent.
4. At each queue position the battle first checks the combo recipes, longest first. If the next N queued actions are abilities from N different living party members, every one can be used, nobody's turn is blocked by a status, and they match a recipe's abilities in any order, then all N turns are replaced by the combo. Every participant pays its own cost. The combo's formulas use the average of the participants' strength, mental, dexterity and critical chance. Any other action between them, from an enemy or an ally, breaks the chain.
5. Otherwise the actor takes a normal turn:
   * Skip-turn statuses roll for recovery, then prevent the action.
   * Silence, technique points, skill uses and item stock are checked. A failure is an `ActionFailed` event.
   * Targets are resolved. A single foe target that is gone moves to the next living foe by formation position (wrapping to the first). A single ally target that is gone moves to the user. A revive aimed at an ally who is no longer down is cancelled; a revive with no chosen ally takes the first defeated one.
   * Group targeting (`enemy_group`) hits every living foe in the target's group. Enemies take their group from the encounter (by default their enemy id). The whole party is one group, so an enemy's group ability hits every living party member.
   * Effects apply in order. A physical miss skips the rest of that ability's effects on that target.
   * An item whose effects changed nothing (a heal at full hit points, a cure for a status the target lacks, a resisted status, immune damage) is not used up, and the battle emits `ItemHadNoEffect`. The spec does not say what happens; this is the chosen rule.
6. Escape resolves at its place in the order. On success the battle ends at once with `Escaped`, later actions are dropped and there are no rewards. On failure the round continues.
7. The battle ends as soon as one side has no one alive, or the party escapes.
8. At the end of the round each living combatant takes status damage ticks (which can defeat). Then status and stat modifier durations count down and expire.

The RNG (`Rng`, SplitMix64) belongs to the battle and is seeded from the setup. The same setup, data and commands always give the same event stream. The order of random draws per physical hit is: hit, critical (only when the chance is between 1 and 99), variance.

## Data formats

Every file is a JSON object. Unknown fields are load errors, which catches typos. References are checked across files, in this order: rules, statuses, abilities, items, equipment, party, enemies, combos, difficulties. Each file may reference only the files before it.

### rules.json

Rules that wait on measurements of the original game. All fields are required.

```json
{
	"surprise_rounds": 1,
	"critical_damage_percent": 150,
	"experience_split": "each_living",
	"escape": {"base_chance": 50, "agility_factor": 2, "min_chance": 10, "max_chance": 90}
}
```

* `surprise_rounds` (0 to 9): rounds the surprised side loses after a Party first or Enemy first opening.
* `critical_damage_percent` (100 to 400).
* `experience_split`: `each_living` (every living member gets the full amount), `split_living` (the amount is divided among living members, rounded down) or `each_member` (fallen members too).
* `escape`: inputs to the escape formula below.

### statuses.json

```json
{"statuses": [
	{"id": "sleep", "name": "Sleep", "skips_turn": true, "wakes_on_damage": true, "duration": 3},
	{"id": "defeated", "name": "Defeated"}
]}
```

Optional fields:

* `skips_turn` (bool)
* `blocks_techniques` (bool)
* `wakes_on_damage` (bool)
* `tick_damage_percent` (0 to 100, percent of max hit points lost at the end of each round)
* `duration` (rounds, 0 means until cured)
* `recover_chance` (percent, rolled at the start of each turn the status would prevent)

A status with id `defeated` is required. The battle adds it when hit points reach 0. Effects cannot apply it and nothing can resist it.

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
* `target` is one of `one_enemy`, `enemy_group`, `all_enemies`, `one_ally`, `all_allies` or `self`.
* Effects:
  * `{"type": "physical_damage", "power": N, "critical": 0-100}`. `critical` is optional and adds to the user's critical chance.
  * `{"type": "elemental_damage", "power": N, "element": "heat|chill|shock|gale"}`
  * `{"type": "heal", "power": N}`
  * `{"type": "apply_status", "status": ID, "chance": 1-100}`
  * `{"type": "cure_status", "status": ID}`
  * `{"type": "modify_stat", "stat": "strength|mental|agility|dexterity|defense", "percent": -90..200, "duration": rounds}`
  * `{"type": "revive", "power": percent of max hit points}`. The target must be `one_ally` or `all_allies`.

### items.json

```json
{"items": [{"id": "tonic", "name": "Tonic", "target": "one_ally", "effects": [{"type": "heal", "power": 30}]}]}
```

Items use the same effects, but their power is flat: user stats do not scale it, and physical item damage never misses and never lands a critical.

### equipment.json

```json
{"equipment": [
	{"id": "ember_rod", "name": "Ember Rod", "slot": "weapon", "category": "rod", "element": "heat", "stats": {"mental": 3}},
	{"id": "volley_sling", "name": "Volley Sling", "slot": "weapon", "category": "sling", "attack": "scatter_shot"},
	{"id": "calm_charm", "name": "Calm Charm", "slot": "accessory", "category": "charm", "status_resist": {"sleep": 100}},
	{"id": "spark_ring", "name": "Spark Ring", "slot": "accessory", "category": "charm", "abilities": ["glint"]}
]}
```

* `slot`: `weapon`, `head`, `body`, `arms` or `accessory`. A member wears at most one piece per slot.
* `category`: a free label. A character can equip a piece when the category is in the character's `equip` list.
* `stats` (optional): bonuses from -999 to 999 for `hp`, `tp`, `strength`, `mental`, `agility`, `dexterity`, `defense`, `critical` and `evasion`. Totals are clamped (hit points at least 1, critical and evasion 0 to 100).
* Weapons only:
  * `element`: the basic attack's physical damage takes the target's affinity for this element.
  * `attack`: an ability of kind `attack` that replaces the wearer's basic attack.
* `abilities` (optional): techniques and skills the wearer knows while wearing it. A granted skill starts with full uses the first time and keeps its remaining uses when the piece comes off, so swapping never refills it; rest refills it.
* `status_resist` (optional): added to the wearer's own resistances, capped at 100.

### party.json

* `characters`: each has these fields:
  * `id`, `name`
  * `base` and `growth` stats: `hp`, `tp`, `strength`, `mental`, `agility`, `dexterity`, `defense`, and optional `critical` and `evasion` (percent). Growth is added per level above 1.
  * `basic_attack`: an ability of kind `attack`.
  * `abilities`: techniques and skills known from level 1.
  * `learns` (optional): `[{"level": 3, "ability": "cleave"}]`. Level 2 to 50. An ability can be known or learned only once.
  * `affinities` (optional): see enemies.json.
  * `status_resist` (optional): status id to the percent taken off its success chance; 100 is immunity.
  * `equip` (optional): the equipment categories the character can wear. Each must be used by some piece.
  * `equipment` (optional): starting equipment, which `makePartyMember` puts on.
* `lineup`: 1 to 5 character ids.
* `inventory`: item id to count.
* `macros`: each has a `name` and ordered `steps`. A step has these fields:
  * `member`
  * `command`: `attack`, `ability`, `item`, `defend` or `escape`.
  * `id`: for ability and item steps. The member must know the ability from the start, by level or through equipment it can wear.
  * `target` (optional, a rule, not a combatant index): left out means the ability's default; `"self"`; `"all"`; `{"enemy": N}` for formation position N from 0; `{"member": "fighter"}` for a party member by character. The rule must fit the command's targeting.

  Each member can have only one step per macro.

When a macro step cannot run as written, it falls back like this:

* The member is defeated or not in the party, or the party is caught off guard: the step is skipped.
* Unknown ability, silenced, not enough technique points, no skill uses left, item stock used up (items are reserved across earlier steps), escape not allowed, or a revive with nobody down: the member makes a basic attack on the intended enemy, or on the first living one.
* The target is gone (dead enemy, empty formation position, member not in the party): the step is retargeted.

### enemies.json

```json
{"enemies": [{
	"id": "rust_hound", "name": "Rust Hound",
	"stats": {"hp": 90, "tp": 8, "strength": 14, "mental": 4, "agility": 11, "dexterity": 10, "defense": 6, "level": 2, "critical": 5},
	"basic_attack": "bite", "abilities": ["venom_spit"],
	"affinities": {"shock": 150},
	"status_resist": {"sleep": 50},
	"ai": [
		{"ability": "bite", "weight": 3},
		{"ability": "venom_spit", "weight": 4, "when": {"type": "self_hp_below", "value": 50}}
	],
	"experience": 10, "currency": 7
}],
"encounters": [
	{"id": "training", "enemies": ["gel_mite", "gel_mite", "rust_hound"]},
	{"id": "marsh", "enemies": [{"enemy": "bog_wisp", "group": "wisps"}, {"enemy": "bog_wisp", "group": "wisps"}, "gel_mite"]},
	{"id": "warden", "enemies": ["brass_warden", "bog_wisp"], "escape": false}
]}
```

* `affinities` is the percent of elemental damage taken. Elements left out take 100. Above 100 is a weakness, below 100 a resistance, and 0 is immunity.
* `status_resist`: as for characters.
* An encounter is an enemy group: 1 to 8 enemies in formation order. An entry is an enemy id, or `{"enemy", "group"}` to set the group label group targeting uses (default: the enemy id). `escape: false` forbids escape, for bosses.
* AI conditions (`when.type`):
  * `always`
  * `self_hp_below` (`value` in percent)
  * `ally_hp_below` (`value` in percent)
  * `ally_defeated`
  * `self_has_status` (`status`)
  * `foe_has_status` (`status`)
* Each round the AI keeps the rules that are true, usable (technique points, uses, not silenced) and have a target, then picks one by weight. If none qualify, it makes a basic attack on a random living foe.
* AI targeting:
  * Single-enemy and group abilities pick a random living foe (a group ability then hits that foe's group). With `foe_has_status`, they pick among the foes that have the status.
  * Heals pick the ally with the lowest hit point share. Cures pick an ally with the status. Revives pick the first defeated ally.

### combos.json

```json
{"combos": [{"id": "storm_spark", "abilities": ["spark", "gust"], "result": "storm_spark"}]}
```

* A recipe has 2 to 5 technique or skill ids, in any order, and a `result` ability of kind `combo`.
* No two recipes can use the same set of abilities.
* The first time a recipe fires, `ComboLog::discover` records it, and the event has `firstDiscovery` set.

### difficulties.json

A difficulty is a data overlay. `applyDifficulty(db, difficulty)` returns the overlaid data and the battle reads only that, so no rule branches on the difficulty.

```json
{"difficulties": [
	{"id": "original", "name": "Original"},
	{"id": "hard", "name": "Hard", "reward_percent": 120,
		"abilities": [{"id": "mend", "power_percent": 80}],
		"enemies": [{"id": "brass_warden", "stats_percent": {"hp": 115, "strength": 110}, "add_abilities": ["numbing_sting"], "ai": [], "status_resist": {"sleep": 100}}]}
]}
```

* `reward_percent` (optional, 1 to 1000): scales battle experience and currency, before the reward multiplier.
* `abilities`: `power_percent` scales damage and heal powers; `cost` (techniques) and `uses` (skills) replace the base values.
* `enemies`: `stats_percent` scales stats. Hit points, strength and defense must stay within 80 to 120 percent (the spec's 20% limit for Hard); the others within 50 to 200. `add_abilities` adds techniques or skills. `ai`, when present, replaces the whole AI profile and may use added abilities. `status_resist` replaces the listed resistances.

## Battle records

`serializeRecord` writes a line-based text record: format version, rules version, event schema version, content hash of the loaded data, the full setup (seed, difficulty, reward multiplier, opening, flags, enemy group and formation, inventory, each member's level, experience, hit points, technique points, skill uses and equipment), the combos already discovered, and per round the commands the host set and the macro block order. Commands the battle filled in itself are not recorded, because it fills them the same way on replay. Ids are written with letters, digits and `_` kept and every other byte as `%XX`; an empty value is `-`.

`replayBattle` refuses a record from another rules version (`kRulesVersion`), event schema (`kEventSchemaVersion`) or data (content hash). Bump `kRulesVersion` whenever a rule, a formula or the order of random draws changes.

`serializeEvents` writes one line per event: the round, the type name, then every field that differs from its default, in declaration order, as `name=value`.

## Events

`Event` has a `type` and only the fields that type uses. `actor` and `target` are combatant indexes. The types are:

* `RoundStarted`
* `ActionStarted`: `command` says attack, ability, item or defend. `ability` holds the ability or item id.
* `ActionFailed`: `fail` gives the reason.
* `ActionPrevented`: `status` gives the cause.
* `ComboTriggered`: `participants`, `firstDiscovery`.
* `ResourceSpent`: `resource` (technique points, a skill use or an item), `amount`, `ability`.
* `ItemHadNoEffect`: the item was not used up.
* `Missed`
* `Critical`: comes right before the critical hit's `DamageDealt`.
* `DamageDealt`: `amount`, `element`, `effectiveness`.
* `Healed`
* `Revived`
* `StatusApplied`, `StatusResisted` (`effectiveness` is `Immune` when the target is immune and no roll was made), `StatusCured`, `StatusTicked`
* `StatusExpired`: `statusEnd` says duration, recovered or woke on damage.
* `StatModified`, `StatModifierExpired`
* `CombatantDefeated`
* `EscapeAttempted`: `succeeded`.
* `RoundEnded`
* `BattleEnded`: `outcome`.

## Formulas

Every formula is a **placeholder to be tuned**. The math is in `src/formulas.cpp`; the inputs marked (data) come from the data files.

| What | Formula (placeholder) |
| --- | --- |
| Initiative | `agility + roll`, where roll is 0 to `8 + agility / 4` |
| Hit chance (physical) | `clamp(85 + attacker dexterity - target agility, 40, 100) * (100 - target evasion) / 100` percent. Evasion (data) comes from stats and equipment |
| Critical chance | `clamp(attacker critical + effect critical, 0, 100)` percent. Both inputs are data; items never land a critical |
| Critical damage | `max(1, damage * critical_damage_percent / 100)` (data, rules.json) |
| Variance | a random percent from 90 to 110, applied to every damage roll |
| Physical damage | `max(strength * 2 - defense, max(1, strength / 4)) * power / 100 * variance / 100`, at least 1 |
| Weapon element | basic attack damage `* affinity / 100`, at least 1, or 0 when the target is immune |
| Elemental damage | `max(power + mental - target mental / 2, max(1, power / 2)) * variance / 100 * affinity / 100`. 0 when the target is immune, otherwise at least 1 |
| Combo stats | the average of every participant's effective strength, mental, dexterity and critical chance |
| Defending | damage / 2, at least 1 |
| Heal | `power + mental / 2` (items: `power`) |
| Revive | `max(1, max hit points * power / 100)` |
| Status chance | `chance * (100 - resistance) / 100`. Resistance 100 is immunity, with no roll |
| Status tick | `max(1, max hit points * tick_damage_percent / 100)` |
| Stat modifier | `base * (100 + total percent) / 100`, total clamped to -50..+100, at least 1. Only one modifier per stat: a new one replaces the old |
| Escape chance | `clamp(base_chance + (party average agility - enemy average agility) * agility_factor, min_chance, max_chance)` percent (data, rules.json) |
| Experience to next level | `15 * level^2 + 10`. Carried-over experience counts toward the next level. Maximum level 50 |
| Stats at a level | `base + growth * (level - 1)`, plus equipment bonuses |
| Rewards | the sum of the defeated enemies' experience and currency, times `reward_percent * multiplier / 10000` (multiplier 100, 150 or 200), rounded down once. Then split per `experience_split` |

## Not built yet

* Events from the spec's minimum set that do not exist yet: `BattleStarted`, `CommandsLocked`, `OrderResolved`, `Retargeted` and `Message`. Retargeting is visible only through `ActionStarted.target` and `MacroStepResult`.
* Targeting modes `one fallen ally` and `random enemy`; revives use `one_ally` with a revive effect.
* Scripted end conditions and the `defeatHasBranch` and `vehicleBattle` flags: they are carried in the setup and the record, but no rule reads them.
* Statuses carried into or out of battle; `PartyMember` has no status list.
* Combo recipes tied to characters, and recipes where order matters.
* The random-mode opening draw (ENC-022): the host passes the opening in the setup.
* Per-character ability tuning in difficulty overlays; ability overlays apply to every user of the ability.
* Equipment inventory: equipment is not counted, so two members can wear the same piece.
* Elemental affinities from equipment (armor only adds stats and status resistances).
* Macro and combo log serialization for saves; the host stores `Macro` values itself.
