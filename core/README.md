# Battle rules core

A headless battle rules library for a turn-based party RPG. It is plain C++17 and uses only the standard library, so it can go into an Unreal Engine module unchanged. The rules never print, never read globals and never throw across the public API. All names, numbers and text in `data/` are original placeholders.

## Layout

| Path | Contents |
| --- | --- |
| `include/rules/` | Public headers (namespace `rules`) |
| `src/` | Library sources, built into `build/librules.a` |
| `cli/battle_cli.cpp` | Terminal front end |
| `tests/` | One test executable with a minimal assert helper (`tests/test.h`) |
| `data/` | Placeholder JSON data: rules, statuses, abilities, items, equipment, party, enemies, combos, difficulties, and the English text table `text_en.json` |

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
./build/battle_cli --seed 1 --auto --macro tempest                  # auto-battle with that macro until a stop rule fires
./build/battle_cli --seed 1 --auto --opening enemy_first
./build/battle_cli --seed 1 --auto --record build/run.record        # also writes the battle record
./build/battle_cli --replay build/run.record                        # prints exactly what the recorded run printed
./build/battle_cli --seed 4 --encounter marsh                       # interactive; "save NAME" keeps the last round as a macro
```

Other options: `--data DIR`, `--level N` (party level, default 1) and `--max-rounds N` (default 50). `--difficulty` defaults to `original`. Exit codes: 0 when the battle ended (any outcome) or the player quit, 1 for a data or record error, 2 for bad arguments, 3 when the round limit was hit.

## How a host drives a battle

1. Load data once: `Result<Database> db = loadDatabaseFromDirectory("data")`, or `loadDatabaseFromTexts` with file contents the engine already read (`dataFileNames()` lists them). On failure, `error` lists every problem, one per line, as `file:line: entry: message`. Load the string table with `loadTextTable("data/text_en.json")` (or `parseTextTable`); it is not part of the `Database` or its content hash.
2. Keep the party as `std::vector<PartyMember>` between battles, with an `EquipmentBag` of unworn pieces (`db.startingEquipmentBag` to begin). `makePartyMember(db, character, level)` creates one in its starting equipment. `rest(member, db)` restores hit points, technique points and skill uses and cures statuses per data. Change equipment with `equip(member, db, bag, equipmentId)` and `unequip(member, db, bag, slot)`, which move pieces between the bag and the member; `canEquip` checks the category first. `memberStats` and `knownAbilities` give what the member has now. `useItemOutsideBattle` and `useAbilityOutsideBattle` apply field-usable items and techniques to the party through the battle's own effect code.
3. Fill a `BattleSetup`: `party`, `inventory`, the enemy group (`useEncounter(setup, db, encounterId)` fills `enemyGroup`, `enemies` and `flags.escapeAllowed`), `opening` (`openingFromContact` turns a visible-mode contact into one), `flags`, `scriptedEnd`, `difficulty`, `rewardMultiplier` and `seed`.
4. Start: `Result<Battle> battle = Battle::create(db, setup, &comboLog)`. Combatant indexes are stable: party members first, in lineup order, then enemies in formation order. The battle keeps its own copy of the data with the difficulty overlay applied. The `ComboLog` must outlive the battle.
5. Each round:
   * Optionally `setPlayTime(ms)`: the combo log stores it when a combination is first discovered.
   * Set party commands with `setCommand(index, Command::ability("spark", target))`. It returns an empty `Message`, or a message key with parameters saying why the command was rejected (for example escape in a battle that forbids it, a side caught off guard by the opening, or a member whose status forces its command; `sideActsNextRound` and `escapeAllowed` tell the menu in advance). Or call `applyMacro(macro)`, which fills the commands and returns a `MacroStepResult` for each step (applied, retargeted, fell back or skipped, with the issue).
   * Or run auto-battle: `startAuto(macro)` once, then call `fillFromAuto()` at each command phase. It fills the round from the macro and returns true, or turns auto-battle off and returns false, with `autoStop()` saying why. `cancelAuto()` is the player's cancel.
   * Call `resolveRound()`. Party members without a command use their basic attack. Enemies without a command ask their AI. The call returns the round as a list of typed `Event`s for the renderer to play back. The host can also set enemy commands for scripted fights.
   * After a round, `lastRoundAsMacro(name)` turns that round's party commands into a macro.
6. Stop when `outcome()` is no longer `Ongoing`. `gameOver()` is true for a Defeat without a defeat branch. Then read `rewards()` (`experience` and `currency` after scaling, and `memberExperience` per party index after the split), `partyState()` (hit points, technique points, skill uses, equipment and the statuses that persist outside battle) and `inventory()`, and apply `grantExperience(member, db, rewards.memberExperience[i])` to each member. It returns a `LevelUp` with the levels gained and the abilities learned.
7. Save `serializeRecord(battle.record())` for bug reports. `parseRecord` reads it back and `replayBattle(db, record, comboLog, events)` rebuilds the battle and its full event stream. `serializeEvents(events)` gives a stable text form of an event stream. Macros and the combo log go into saves with `serializeMacros`/`parseMacros` and `serializeComboLog`/`parseComboLog`.

`autoCommand(battle, actor)` is a simple deterministic per-member policy. The CLI and the tests use it.

Player-facing results are `Message` values: a stable key and parameters (ids or numbers), never English. `formatText(table, key, params)` fills a template's `{0}`, `{1}` and so on, and `messageKeys(db)` lists every key core can return or emit with that data, so a build can check a string table covers them. Developer-facing errors (data loading, record parsing, `Battle::create`, `useEncounter`) stay plain English strings.

### Round resolution

1. The first round's events open with `BattleStarted`, then a `Message` for a Party first or Enemy first opening.
2. The side that lost the opening (Party first: enemies; Enemy first: the party) gets no commands for the first `surprise_rounds` rounds.
3. Every other living combatant gets a command. A status whose action is `forced` replaces it with a basic attack on a random target from its pool (BAT-012), drawn in combatant index order. Defenders take half damage for the whole round. `CommandsLocked` lists every command and the macro used.
4. Turn order: each combatant rolls an initiative of agility plus a random number from 0 to the spread. Ties go to the party, then to the lower index. Members set by a macro are pulled out and act as one block, in step order, placed at their average initiative. So a macro always keeps its members' steps adjacent. `OrderResolved` lists the order.
5. At each queue position the battle first checks the combo recipes, longest first. If the next N queued actions are abilities from N different living party members, every one can be used, nobody's turn is blocked by a status, and they match a recipe (in any order, or in the listed order for an `ordered` recipe, with each tied component used by its character), then all N turns are replaced by the combo. Every participant pays its own cost. The combo's formulas use the average of the participants' strength, mental, dexterity and critical chance. Any other action between them, from an enemy or an ally, breaks the chain. The first time a recipe fires the log records it and the battle emits `ComboDiscovered`.
6. Otherwise the actor takes a normal turn. Every action is validated when it runs (BAT-015); a failed check is one `ActionCancelled` with a `cancel` reason:
   * An actor that fell earlier in the round: `actor_unable` with status `defeated`. A `cannot_act` status rolls for recovery, then cancels with `actor_unable` and that status.
   * Silence, technique points, skill uses and item stock are checked: `silenced`, `not_enough_tp`, `no_uses_left`, `no_item`.
   * Targets are resolved. A single foe target that is gone moves to the next living foe by formation position (wrapping to the first). A single ally target that is gone moves to the user. Either emits `Retargeted` before `ActionStarted`. A `one_fallen_ally` target that is back up cancels with `no_target`; with no chosen ally it takes the first fallen one. `random_enemy` draws one living foe at execution.
   * Group targeting (`enemy_group`) hits every living foe in the target's group. Enemies take their group from the encounter (by default their enemy id). The whole party is one group, so an enemy's group ability hits every living party member.
   * Effects apply in order. A physical hit emits `Hit`; a miss emits `Missed` and skips the rest of that ability's effects on that target.
   * An item whose effects changed nothing (a heal at full hit points, a cure for a status the target lacks, a resisted status, immune damage) is not used up, and the battle emits `ItemHadNoEffect`. The spec does not say what happens; this is the chosen rule.
7. Escape resolves at its place in the order. On success the battle ends at once with `Escaped`, later actions are dropped and there are no rewards. On failure the round continues.
8. The battle ends as soon as one side has nobody standing, the party escapes, or the scripted end condition holds. A combatant counts as down when it has fallen or has a status that `counts_toward_defeat`, on either side. Victory and Defeat take precedence over a scripted end that happens at the same time.
9. At the end of the round each living combatant takes status damage ticks (which can defeat). Then status and stat modifier durations count down and expire. A `survive_rounds` scripted end is checked last.

The RNG (`Rng`, SplitMix64) belongs to the battle and is seeded from the setup. `state()` and `restore()` save and continue a generator. The same setup, data and commands always give the same event stream. The order of random draws is part of the rules version: forced-command targets and enemy AI choices, in combatant index order; initiative rolls, in combatant index order; then per action: recovery rolls, a `random_enemy` target, and per physical hit: hit, critical (only when the chance is between 1 and 99), variance.

## Data formats

Every file is a JSON object. Unknown fields are load errors, which catches typos. References are checked across files, in this order: rules, statuses, abilities, items, equipment, party, enemies, combos, difficulties. Each file may reference only the files before it.

### rules.json

Rules that wait on measurements of the original game. All fields are required.

```json
{
	"surprise_rounds": 1,
	"critical_damage_percent": 150,
	"experience_split": "each_living",
	"escape": {"base_chance": 50, "agility_factor": 2, "min_chance": 10, "max_chance": 90},
	"rest_revives": true
}
```

* `surprise_rounds` (0 to 9): rounds the surprised side loses after a Party first or Enemy first opening.
* `critical_damage_percent` (100 to 400).
* `experience_split`: `each_living` (every living member gets the full amount), `split_living` (the amount is divided among living members, rounded down) or `each_member` (fallen members too).
* `escape`: inputs to the escape formula below.
* `rest_revives`: whether `rest` stands a fallen member up (FLD-035 is to measure; true keeps the earlier behavior).

### statuses.json

```json
{"statuses": [
	{"id": "sleep", "name": "Sleep", "action": "cannot_act", "wakes_on_damage": true, "duration": 3},
	{"id": "jumble", "name": "Jumble", "action": "forced", "forced_target": "random_any", "duration": 2, "stacking": "keep"},
	{"id": "gear_lock", "name": "Gear Lock", "action": "cannot_act", "counts_toward_defeat": true, "persists_outside_battle": true},
	{"id": "defeated", "name": "Defeated"}
]}
```

Optional fields, which cover BAT-081:

* Action rule: `action` is `normal` (default), `cannot_act` (the turn is cancelled) or `forced` (the battle supplies a basic attack on a random target). `forced_target` (forced only): `random_any` (default, anyone else), `random_ally` or `random_foe`. `blocks_techniques` (bool) is a partial action rule: techniques are cancelled as silenced.
* Duration rule: `duration` in rounds (0, the default, has no round limit: until cured, or until the battle ends). `persists_outside_battle` (bool): `partyState()` carries the status out of battle with its remaining rounds, and `PartyMember::statuses` carries it into the next one.
* Tick effect: `tick_damage_percent` (0 to 100, percent of max hit points lost at the end of each round).
* Cure conditions: cure effects; `wakes_on_damage` (bool); `recover_chance` (percent, `cannot_act` only, rolled at the start of each turn the status would cancel); `rest_cures` (bool, default true).
* Stacking rule: `stacking` is `refresh` (default; reapplying restarts the duration), `extend` (adds the duration to what is left, up to 99) or `keep` (a reapplication changes nothing and makes no roll; it emits `StatusResisted` with the `unchanged` key).
* `counts_toward_defeat` (bool): the combatant counts as down when the battle checks for Victory and Defeat (BAT-014 is still to measure; the flag keeps it a data change).

A status with id `defeated` is required, with only an id and a name. The battle adds it when hit points reach 0. Effects cannot apply it and nothing can resist it.

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
* `field` (optional bool): usable outside battle. Only techniques and skills with a party target and only heal, cure_status and revive effects qualify.
* Effects:
  * `{"type": "physical_damage", "power": N, "critical": 0-100}`. `critical` is optional and adds to the user's critical chance.
  * `{"type": "elemental_damage", "power": N, "element": "heat|chill|shock|gale"}`
  * `{"type": "heal", "power": N}`
  * `{"type": "apply_status", "status": ID, "chance": 1-100}`
  * `{"type": "cure_status", "status": ID}`
  * `{"type": "modify_stat", "stat": "strength|mental|agility|dexterity|defense", "percent": -90..200, "duration": rounds}`
  * `{"type": "revive", "power": percent of max hit points}`. The target must be `one_fallen_ally` or `all_allies`, and `one_fallen_ally` needs a revive effect.

### items.json

```json
{"items": [{"id": "tonic", "name": "Tonic", "target": "one_ally", "effects": [{"type": "heal", "power": 30}]}]}
```

Items use the same effects and targets, but their power is flat: user stats do not scale it, and physical item damage never misses and never lands a critical. `field` works as for abilities.

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
* `equipment_bag` (optional): equipment id to the number of spare pieces, which `db.startingEquipmentBag` holds.
* `macros`: each has a `name` and ordered `steps`. A step has these fields:
  * `member`
  * `command`: `attack`, `ability`, `item`, `defend` or `escape`.
  * `id`: for ability and item steps. The member must know the ability from the start, by level or through equipment it can wear.
  * `target` (optional, a rule, not a combatant index): left out means the ability's default; `"self"`; `"all"`; `{"enemy": N}` for formation position N from 0; `{"member": "fighter"}` for a party member by character. The rule must fit the command's targeting.

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
* AI target rules (optional `target` per entry, BAT-090/091): `default`, `random`, `lowest_hp`, `highest_hp` (current hit points, ties to the lower index), `has_status` and `lacks_status` (with `target_status`; a random match), and `last_attacker` (the foe that last damaged this enemy). They need a single-target ability, and `last_attacker` one aimed at foes. An entry whose rule finds no target does not qualify that round.
* AI conditions (`when.type`):
  * `always`
  * `self_hp_below` (`value` in percent)
  * `ally_hp_below` (`value` in percent)
  * `ally_defeated`
  * `self_has_status` (`status`)
  * `foe_has_status` (`status`)
* Each round the AI keeps the rules that are true, usable (technique points, uses, not silenced) and have a target, then picks one by weight. If none qualify, it makes a basic attack on a random living foe.
* Default AI targeting:
  * Single-enemy and group abilities pick a random living foe (a group ability then hits that foe's group). With `foe_has_status`, they pick among the foes that have the status.
  * Heals pick the ally with the lowest hit point share. Cures pick an ally with the status. Revives pick the first defeated ally. `random_enemy` abilities draw at execution.

### combos.json

```json
{"combos": [
	{"id": "storm_spark", "abilities": ["spark", "gust"], "result": "storm_spark"},
	{"id": "twin_arc", "abilities": [{"ability": "glint", "character": "medic"}, "spark"], "result": "twin_arc"},
	{"id": "cinder_gale", "abilities": ["gust", "ember"], "ordered": true, "result": "cinder_gale"}
]}
```

* A recipe has 2 to 5 technique or skill components and a `result` ability of kind `combo`. A component is an ability id, or `{"ability", "character"}` to tie it to a character who can use it (BAT-060). `ordered: true` requires the listed order; otherwise any order fires it.
* No two recipes can use the same set of abilities, and some set of different characters must be able to use each recipe's components together.
* The first time a recipe fires, `ComboLog::discover` records it with the play time the host set and the participants' character ids (BAT-064), and the battle emits `ComboDiscovered`.
* The placeholder data has 15 recipes, the count the scope gives.

### text_en.json

`{"text": {"key": "template"}}`. Templates take positional parameters `{0}`, `{1}`. Status keys are `status.<id>.applied`, `.resisted`, `.immune`, `.removed` and, for `keep` stacking, `.unchanged`; their templates take `{0}` the target and `{1}` the status. The CLI also keeps its own `cancel.*` and `auto.stopped.*` lines here. A test checks the table covers `messageKeys(db)`.

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
* `enemies`: AI entries in an overlay may use target rules. `stats_percent` scales stats. Hit points, strength and defense must stay within 80 to 120 percent (the spec's 20% limit for Hard); the others within 50 to 200. `add_abilities` adds techniques or skills. `ai`, when present, replaces the whole AI profile and may use added abilities. `status_resist` replaces the listed resistances.

## Battle records

`serializeRecord` writes a line-based text record (format 2): format version, rules version, event schema version, content hash of the loaded data, the full setup (seed, difficulty, reward multiplier, opening, flags, scripted end, enemy group and formation, inventory, each member's level, experience, hit points, technique points, skill uses, equipment and carried statuses), the combo log entries already known, and per round the play time, the macro name, the commands the host set and the macro block order. Commands the battle filled in itself are not recorded, because it fills them the same way on replay. Auto-battle commands are set before `resolveRound`, so they are recorded like the host's. Ids are written with letters, digits and `_` kept and every other byte as `%XX`; an empty value is `-`.

The last line is `checksum <FNV-1a 64 of everything before it>`. `parseRecord` refuses a record whose checksum is missing or does not match (SAV-014), so a damaged record is never replayed. It is a guard against accidental damage, not tampering.

`replayBattle` refuses a record from another rules version (`kRulesVersion`), event schema (`kEventSchemaVersion`) or data (content hash). Bump `kRulesVersion` whenever a rule, a formula or the order of random draws changes.

`serializeEvents` writes one line per event: the round, the type name, then every field that differs from its default, in declaration order, as `name=value`.

## Events

`Event` has a `type` and only the fields that type uses. `actor` and `target` are combatant indexes. Status events carry a `message` key for the string table (BAT-083). The types, in the BAT-115 set (spec names in brackets where core's differ):

* `BattleStarted` (round 0, first in round 1's list): `ids` and `groups` per combatant index, `partyCount`, `opening`, `flags`.
* `RoundStarted`, `RoundEnded`: `round`.
* `CommandsLocked`: `commands` (every combatant's command for the round, by index; a target of -1 means the battle picks), `macro`.
* `OrderResolved`: `order`.
* `Retargeted`: `actor`, `previousTarget`, `target`.
* `ActionStarted`: `command` says attack, ability, item or defend. `ability` holds the ability or item id; `target` is the single resolved target or -1.
* `ActionCancelled`: `cancel` gives the reason; `actor_unable` names the cause in `status`.
* `ComboTriggered`: `combo` (recipe id), `ability` (result), `participants`.
* `ComboDiscovered`: `combo`.
* `ResourceSpent`: `resource` (technique points, a skill use or an item), `amount`, `ability`.
* `ItemHadNoEffect`: the item was not used up.
* `Hit`, `Missed`, `Critical`: `actor`, `target`, `ability`. `Critical` comes right before the critical hit's `DamageDealt`.
* `DamageDealt` (Damaged): `actor`, `amount`, `hp` (after), `element`, `effectiveness`.
* `Healed`: `amount`, `hp`. `Revived`: `amount` and `hp` (the hit points it stands up with).
* `StatusApplied` (`duration`), `StatusResisted` (`effectiveness` is `Immune` when the target is immune and no roll was made), `StatusRemoved` (`statusEnd`: cured, duration, recovered, woke on damage or defeated; the text form omits `cured`, the default).
* `StatusTicked`: `amount`, `hp`.
* `StatModified` (StatChanged): `stat`, `amount` (percent), `duration`. `StatModifierExpired`.
* `CombatantDefeated` (Fell): hit points 0, the `defeated` status, modifiers cleared. Each other status it clears gets a `StatusRemoved` first.
* `EscapeAttempted`: `succeeded`.
* `Message`: `message`, `params`.
* `BattleEnded`: `outcome`, `experience`, `currency`, `gameOver`.

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
| Rewards | Victory: every enemy's experience and currency; scripted end: the fallen enemies' only. Times `reward_percent * multiplier / 10000` (multiplier 100, 150 or 200), rounded down once. Then split per `experience_split` |

## Not built yet

* Vehicle battles (BAT-140): `flags.vehicleBattle` is carried in the setup, the record and `BattleStarted`, but no rule reads it, because the spec defines no rule effect yet (BAT-141 is to measure).
* AI conditions on round number, living ally count and the party's earlier actions (BAT-091); the target rules are built.
* Presentation keys and type tags on abilities and combinations (BAT-040, BAT-060).
* Item drops in rewards (SAV-031 is to measure); `BattleEnded` carries experience and currency only.
* The random-mode opening draw (ENC-022): the host passes the opening in the setup.
* Per-character ability tuning in difficulty overlays; ability overlays apply to every user of the ability.
* Elemental affinities from equipment (armor only adds stats and status resistances).
* `BattleStarted` reaches the host with the first round's events; before that the host reads the battle state.

## Changes a host must make

Every public API change in this round, grouped by header. Rules version 3, event schema 3, record format 2: older records are refused.

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
