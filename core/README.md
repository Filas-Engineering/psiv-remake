# Battle rules core

A headless battle rules library for a turn-based party RPG. It is plain C++17 and uses only the standard library, so it can go into an Unreal Engine module unchanged. The rules never print, never read globals and never throw across the public API. All names, numbers and text in `data/` are original placeholders.

## Layout

| Path | Contents |
| --- | --- |
| `include/rules/` | Public headers (namespace `rules`) |
| `src/` | Library sources, built into `build/librules.a` |
| `cli/battle_cli.cpp` | Terminal front end |
| `tests/` | One test executable with a minimal assert helper (`tests/test.h`) |
| `data/` | Placeholder JSON data: statuses, abilities, items, party, enemies, combos |

## Build and test

Needs `clang++` and GNU Make 3.81 or later. The compile flags are `-std=c++17 -Wall -Wextra -Werror`.

```
make          # library, CLI and tests in build/
make test     # runs build/rules_tests, then a full auto battle through the CLI
make clean
```

Run one test group by name: `./build/rules_tests combo`. Run tests from `core/`, because they load `data/`.

The CLI:

```
./build/battle_cli --seed 1 --auto                      # plays the "training" encounter, prints the event log
./build/battle_cli --seed 1 --auto --encounter warden
./build/battle_cli --seed 1 --auto --macro tempest      # runs that macro every round
./build/battle_cli --seed 4 --encounter marsh           # interactive
```

Other options: `--data DIR`, `--level N` (party level, default 1) and `--max-rounds N` (default 50). Exit codes: 0 when the battle ended (any outcome) or the player quit, 1 for a data error, 2 for bad arguments, 3 when the round limit was hit.

## How a host drives a battle

1. Load data once: `Result<Database> db = loadDatabaseFromDirectory("data")`, or `loadDatabaseFromTexts` with file contents the engine already read. On failure, `error` lists every problem, one per line, as `file:line: entry: message`.
2. Keep the party as `std::vector<PartyMember>` between battles. `makePartyMember(db, character, level)` creates one, and `rest(member, db)` restores hit points, technique points and skill uses.
3. Start a battle: `Battle::create(db, party, enemyIds, inventory, seed, &comboLog)`. Combatant indexes are stable: party members first, in lineup order, then enemies. The `Database` and `ComboLog` must outlive the battle.
4. Each round:
   * Set party commands with `setCommand(index, Command::ability("spark", target))`. It returns an empty string, or the reason the command was rejected. Or call `applyMacro(macro)`, which fills the commands and returns a `MacroStepResult` for each step (applied, retargeted, fell back or skipped, with the reason).
   * Call `resolveRound()`. Party members without a command use their basic attack. Enemies without a command ask their AI. The call returns the round as a list of typed `Event`s for the renderer to play back. The host can also set enemy commands for scripted fights.
5. Stop when `outcome()` is no longer `Ongoing`. Then read `rewards()`, `partyState()` (hit points, technique points and skill uses to keep) and `inventory()`, and apply `grantExperience` to each member.

`autoCommand(battle, actor)` is a simple deterministic auto-battle policy. The CLI and the tests use it.

### Round resolution

1. Every living combatant gets a command. Defenders take half damage for the whole round.
2. Turn order: each combatant rolls an initiative of agility plus a random number from 0 to the spread. Ties go to the party, then to the lower index. Members set by a macro are pulled out and act as one block, in step order, placed at their average initiative. So a macro always keeps its members' steps adjacent.
3. At each queue position the battle first checks the combo recipes, longest first. If the next N queued actions are abilities from N different living party members, every one can be used, nobody's turn is blocked by a status, and they match a recipe's abilities in any order, then all N turns are replaced by the combo. Every participant pays its own cost. Any other action between them, from an enemy or an ally, breaks the chain.
4. Otherwise the actor takes a normal turn:
   * Skip-turn statuses roll for recovery, then prevent the action.
   * Silence, technique points, skill uses and item stock are checked. A failure is an `ActionFailed` event.
   * Targets are resolved. A single target that is gone moves to the first valid one: the first living foe, the user for heals, or any defeated ally for revives.
   * Effects apply in order. A physical miss skips the rest of that ability's effects on that target.
5. The battle ends as soon as one side has no one alive, or the party flees.
6. At the end of the round each living combatant takes status damage ticks (which can defeat). Then status and stat modifier durations count down and expire.

The RNG (`Rng`, SplitMix64) belongs to the battle and is seeded at creation. The same seed, data and commands always give the same event stream.

## Data formats

Every file is a JSON object. Unknown fields are load errors, which catches typos. References are checked across files, in this order: statuses, abilities, items, party, enemies, combos. Each file may reference only the files before it.

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

A status with id `defeated` is required. The battle adds it when hit points reach 0. Effects cannot apply it.

### abilities.json

```json
{"abilities": [
	{"id": "spark", "name": "Spark", "kind": "technique", "cost": 3, "target": "one_enemy", "effects": [
		{"type": "elemental_damage", "power": 26, "element": "shock"}
	]}
]}
```

* `kind` is one of:
  * `attack`: basic attacks.
  * `technique`: needs `cost` in technique points.
  * `skill`: needs `uses`, which refill on rest.
  * `combo`: only reachable through a combo recipe.
* `target` is one of `one_enemy`, `enemy_group`, `all_enemies`, `one_ally`, `all_allies` or `self`. An enemy group is every living foe that has the same enemy id as the chosen target.
* Effects:
  * `{"type": "physical_damage", "power": N}`
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

Items use the same effects, but their power is flat: user stats do not scale it, and physical item damage never misses.

### party.json

* `characters`: each has these fields:
  * `id`, `name`
  * `base` and `growth` stats: `hp`, `tp`, `strength`, `mental`, `agility`, `dexterity`, `defense`. Growth is added per level above 1.
  * `basic_attack`: an ability of kind `attack`.
  * `abilities`: techniques and skills.
  * `affinities` (optional): see enemies.json.
* `lineup`: 1 to 5 character ids.
* `inventory`: item id to count.
* `macros`: each has a `name` and ordered `steps`. A step has these fields:
  * `member`
  * `command`: `attack`, `ability`, `item`, `defend` or `flee`.
  * `id`: for ability and item steps.
  * `target`: an enemy slot, from 0.
  * `target_member`: a character id, for ally targets.

  Each member can have only one step per macro.

When a macro step cannot run as written, it falls back like this:

* The member is defeated or not in the party: the step is skipped.
* Silenced, not enough technique points, no skill uses left, item stock used up (items are reserved across earlier steps), or a revive with nobody down: the member makes a basic attack on the intended enemy, or on the first living one.
* The target is gone: the step is retargeted.

### enemies.json

```json
{"enemies": [{
	"id": "rust_hound", "name": "Rust Hound",
	"stats": {"hp": 90, "tp": 8, "strength": 14, "mental": 4, "agility": 11, "dexterity": 10, "defense": 6, "level": 2},
	"basic_attack": "bite", "abilities": ["venom_spit"],
	"affinities": {"shock": 150},
	"ai": [
		{"ability": "bite", "weight": 3},
		{"ability": "venom_spit", "weight": 4, "when": {"type": "self_hp_below", "value": 50}}
	],
	"experience": 10, "currency": 7
}],
"encounters": [{"id": "training", "enemies": ["gel_mite", "gel_mite", "rust_hound"]}]}
```

* `affinities` is the percent of elemental damage taken. Elements left out take 100. Above 100 is a weakness, below 100 a resistance, and 0 is immunity.
* AI conditions (`when.type`):
  * `always`
  * `self_hp_below` (`value` in percent)
  * `ally_hp_below` (`value` in percent)
  * `ally_defeated`
  * `self_has_status` (`status`)
  * `foe_has_status` (`status`)
* Each round the AI keeps the rules that are true, usable (technique points, uses, not silenced) and have a target, then picks one by weight. If none qualify, it makes a basic attack on a random living foe.
* AI targeting:
  * Single-enemy abilities pick a random living foe. With `foe_has_status`, they pick among the foes that have the status.
  * Heals pick the ally with the lowest hit point share. Cures pick an ally with the status. Revives pick the first defeated ally.

### combos.json

```json
{"combos": [{"id": "storm_spark", "abilities": ["spark", "gust"], "result": "storm_spark"}]}
```

* A recipe has 2 to 5 technique or skill ids, in any order, and a `result` ability of kind `combo`.
* No two recipes can use the same set of abilities.
* The first time a recipe fires, `ComboLog::discover` records it, and the event has `firstDiscovery` set.

## Events

`Event` has a `type` and only the fields that type uses. `actor` and `target` are combatant indexes. The types are:

* `RoundStarted`
* `ActionStarted`: `command` says attack, ability, item or defend. `ability` holds the ability or item id.
* `ActionFailed`: `fail` gives the reason.
* `ActionPrevented`: `status` gives the cause.
* `ComboTriggered`: `participants`, `firstDiscovery`.
* `Missed`
* `DamageDealt`: `amount`, `element`, `effectiveness`.
* `Healed`
* `Revived`
* `StatusApplied`, `StatusResisted`, `StatusCured`, `StatusTicked`
* `StatusExpired`: `statusEnd` says duration, recovered or woke on damage.
* `StatModified`, `StatModifierExpired`
* `CombatantDefeated`
* `FleeAttempted`: `succeeded`.
* `RoundEnded`
* `BattleEnded`: `outcome`.

## Formulas

Every formula is a **placeholder to be tuned**. All of them are in `src/formulas.cpp`.

| What | Formula (placeholder) |
| --- | --- |
| Initiative | `agility + roll`, where roll is 0 to `8 + agility / 4` |
| Hit chance (physical) | `clamp(85 + attacker dexterity - target agility, 40, 100)` percent |
| Variance | a random percent from 90 to 110, applied to every damage roll |
| Physical damage | `max(strength * 2 - defense, max(1, strength / 4)) * power / 100 * variance / 100`, at least 1 |
| Elemental damage | `max(power + mental - target mental / 2, max(1, power / 2)) * variance / 100 * affinity / 100`. 0 when the target is immune, otherwise at least 1 |
| Defending | damage / 2, at least 1 |
| Heal | `power + mental / 2` (items: `power`) |
| Revive | `max(1, max hit points * power / 100)` |
| Status tick | `max(1, max hit points * tick_damage_percent / 100)` |
| Stat modifier | `base * (100 + total percent) / 100`, total clamped to -50..+100, at least 1. Only one modifier per stat: a new one replaces the old |
| Flee chance | `clamp(50 + (party average agility - enemy average agility) * 2, 10, 90)` percent |
| Experience to next level | `15 * level^2 + 10`. Carried-over experience counts toward the next level. Maximum level 50 |
| Stats at a level | `base + growth * (level - 1)` |
| Rewards | the sum of the defeated enemies' experience and currency. On victory, every living member gets the full experience |

## Known limitations

* An item with no effect on its target is still used up. Examples: a cure on someone without the status, or a heal on someone at full hit points.
* The party members in a combo pay their costs, but the combo uses only the lead's stats, the first participant in turn order.
* No equipment, critical hits or per-enemy status immunities yet.
