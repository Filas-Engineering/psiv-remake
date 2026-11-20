# Battle rules of the original, as read from the disassembly

Rules read from the public community disassembly (https://github.com/squidfeatures/ps4disasm, file `ps4.asm`), written in our own words. This file holds rules only: no data tables, no names and no code. Turn order, the chance roll, damage, healing, hit and critical, escape, openings and Defend are in [published-mechanics.md](published-mechanics.md) and `core/README.md`.

Line numbers are approximate and refer to `ps4.asm`. Fighter slots: 1-5 party, 6-9 enemies, 10-11 enemy-name labels (display only). "Base" stat means the equipment-adjusted value (`*_mod`, `atk_pow`, `dfs_pow`, `magic_dfs`); "battle" stat means the in-battle working copy (`*_battle`).

Status bits: 0 poisoned, 1 paralyzed, 2 dead, 3 asleep, 4 tech sealed, 5 second sleep flag, 6 android dead, 7 "woken this round" (set only by the wake-up effect, cleared when the round's turn order is built).

---

## 1. Enemy action selection and targeting

### Target (chosen once per round, before anyone acts)
- When the turn order is built, every enemy slot gets the command "basic action" plus a party target. Which skill the enemy actually uses is decided later, when its turn comes (see below). `Battle_OrderTurns` tail ~7928-7935. Confidence: high.
- The target is picked by `Enemy_TargetCharacter` (~7977) from a list of party members who are not dead (neither death bit). Sleeping and paralyzed members stay in the list. The list is in party-slot order, so front slots get more weight. `loc_56F0` (~7951) builds the list. Confidence: high.
- The chance by list position, from `EnemyTargetRates` (~8001), with one random byte compared against thresholds:
  - 1 living member: always that one.
  - 2: about 67% / 33%.
  - 3: about 50% / 33% / 17%.
  - 4: about 40% / 30% / 20% / 10%.
  - 5: about 33% / 27% / 20% / 13% / 7%.
  Confidence: high.
- When the enemy acts, if its target is now dead, gone, or carries the "woken this round" bit, it re-picks with the same weighted roll over the living members. `loc_5A98`/`loc_5ACE` (~8322-8343). Confidence: high.
- An enemy's own action routine can override the target. It can set "all party" (target = -1) or "no fighter target" (target = 0, used for actions with no party target). Single-target skills keep the pre-picked (or re-picked) target. Per-enemy routines from `EnemyAttackOffs` (~19192). Confidence: medium (generic pattern seen in several routines, not all read).

### Which action (decided at the start of the enemy's turn), `Enemy_Attack` (~19124-19188)
- Each enemy has 8 "random action" slots (per-enemy field, stats +$58..$5F). A value of 0 means the basic attack. Any other value is an enemy-skill ID. Confidence: high.
- One slot is chosen uniformly at random from 0-7, with one rule: it can never be the same slot index as the previous pick. The memory of the last pick is a single global value shared by all enemies, so with 8 slots each of the 7 allowed slots has a 1/7 chance. How often an enemy uses its basic attack is just how many of its 8 slots hold 0. Confidence: high.
- After the random pick, up to 4 condition slots are checked in order. A per-enemy field (stats +$50..$53) holds condition codes; 0 ends the list. Each condition has a paired per-enemy override action (stats +$54..$57). The first condition that holds replaces the random pick with its override action, and the rest are skipped. Confidence: high.
- Condition kinds (`EnemyAIInstructionsOffs` ~19350; generic descriptions):
  - Own HP at or below 50% of max (~23422). High.
  - Own HP at or below 25% of max, gated by a one-shot global flag. The flag is cleared at the start of one particular encounter's setup and set by one particular animation, so in that case it fires once (~20477; flag set ~53087, cleared ~18100). Medium.
  - Any living enemy, the actor included, at or below 50% HP (~21306). High.
  - The actor is the only enemy left (~22314). High.
  - Hit by a physical attack since the flag was last consumed (~22802). Firing clears all of the actor's reaction flags. High.
  - Hit by any ability damage (tech/skill/item) since last consumed (~22486). Clears all reaction flags. High.
  - Hit by a technique since last consumed (~22218). Clears all reaction flags. High.
  - "Ambush" reaction bit (~22203). I could not find the code that sets this bit. Low.
  - The actor's techniques are sealed (~22189). High.
  - A free enemy slot exists beside the actor; if both sides are free, a coin flip picks one. Used for spawn/split-style actions (~23510). High.
  - Several conditions check whether particular other enemy kinds are present or adjacent, or are all of a kind and were hit by a multi-target ability (~20351, ~21543, ~21614, ~22891, ~23003, ~23245, ~23274). Medium (two read in full).
  - One condition tests a global flag bit that nothing in the file sets, so in practice it never fires (~20789). Medium.
- Reaction flags (fighter +$2A) are set when a party action lands damage on the enemy: bit 0 for basic attacks, bit 1 for any ability, bit 2 for techniques, bit 4 for multi-target abilities. They build up across rounds until a condition consumes them. `Character_DamageEnemy` ~3909-3995. Confidence: high.
- Enemy basic attacks roll to hit and to crit (dexterity vs agility). If a basic attack hits all party members, a crit result is downgraded to a normal hit. Enemy skills always hit every living target and have no hit roll. `loc_B6A2` (~17478-17563). Confidence: high.
- A secondary effect on an enemy's basic attack comes from a per-enemy field (stats +$14) and goes through the effect pipeline in section 6. `Battle_DoAttackEffect` ~8552-8620. Confidence: medium.

## 2. Party targeting, retargeting, ranges

### Range codes (low 4 bits of a per-ability range field), `AbilityRangeOffs` ~8902, `Ability_ProcessRange` ~8974, `EffectValidTargetOffs` ~9309, menu table `Battle_ComdTechTarget` ~2643
- 0, "Varied": if the stored target is one fighter, only that fighter. If the stored target is "all" (negative), every opponent of the actor: enemy slots 6-9 when a party member acts, party slots 1-5 when an enemy acts. The menus treat it like an "all" range (no cursor). Confidence: high for the logic, low for how the player menus use it.
- 1: single target, picked from the enemies in the menu.
- 2: all enemies (slots 6-9).
- 3: self (the actor).
- 4: one ally, humans only (androids are skipped).
- 5: all allies, humans only.
- 6: one ally, androids only.
- 7: all allies, androids only.
- 8: one ally, anyone.
- 9: all allies, anyone.
- "Android" means the profession field equals the android profession. Confidence: high.
- Inside any range, a target is skipped if it is dead (either death bit), except for two effect families: "revive" applies only to dead targets, and "full revive" applies regardless of death (~8981-8990). Confidence: high.
- With only one party member, a one-ally range (4) targets the actor (`loc_5C42` ~8475; menus ~2707). Confidence: high.
- There is no "enemy group" targeting. The two group objects only show names. Players target individual enemy slots or "all". `Battle_PickTargetEnemy` ~1215. Confidence: high.

### Basic attack
- If a hand holds a multi-target weapon (two weapon types: a one-handed and a two-handed class), the attack targets all enemies. Otherwise the player picks one enemy, or it is auto-picked when only one enemy is left. `Battle_AttackCommand` ~2234-2305. At execution, only the right hand is re-checked for the two-handed multi-target class (~8453-8466). Confidence: high.

### Dead or missing target at execution (`loc_576A` ~8016-8075, `loc_5A98` ~8322-8408)
- Retargeting applies only when the action's range code is 1, 2 or 3 (basic attacks count as 1) and the stored target is one fighter (not "all"). Confidence: high.
- Manual input: retarget only if the target slot is empty, dead, or has the "woken this round" bit. Macro: always retarget, every round (macros store no target, and every round's main menu sets each member's target to the first enemy slot, ~1891). Confidence: high.
- New target if the old one was a party slot: the weighted random roll from section 1.
- New target if the old one was an enemy slot: the code reads the low 4 bits of the ability's effect field (basic attacks count as 1).
  - Value 1 (plain damage): the living enemy with the most HP missing (max minus current).
  - Any other value: the living enemy with the least HP missing.
  - Ties: each later tied candidate replaces the current choice on a coin flip, so the odds are not equal with 3 or more ties.
  - If no enemy qualifies, the default is the first enemy slot.
  Confidence: high.
- One-ally techs, skills and items (ranges 4/6/8) are never retargeted in manual play. If the ally is dead the effect is skipped. The cost is already paid by then: TP at cast start, skill use at start, disposable item consumed at start. Heals record nothing for dead targets, so nothing is applied. Confidence: high for the skip and the costs; medium that a heal on a dead ally does nothing at all.
- In macro mode, a one-ally action that is not in the special-target table (section 3) keeps the default target, which is the first enemy slot. The ally ability would then hit that enemy. Confidence: low (inferred from the default and the absent retarget; not traced to the end).

## 3. Macro special targeting (`loc_5810` ~8061-8074; routines ~8121-8318)
- These run only in macro mode, only when the action's range code is not 1-3, and only when the target is one fighter. A table keyed on (command, ability ID) picks the routine (`MacroSpecialActionTable` ~8094). Slot order means party slots 1-5.
- Largest HP missing, humans only: used for single-target HP heals (techs and items). Skips empty slots, androids and dead members. Picks the largest max−current. Ties go to the last tied member in slot order. If everyone is at full HP, the last eligible member is picked. If nobody is eligible, slot 1. Confidence: high.
- Smallest HP missing: used for one single-target ally buff. Because of a comparison bug it never picks anyone: the running minimum starts at 0 and only a strictly smaller value replaces it. The target is therefore always party slot 1, whatever slot 1's state. Confidence: high.
- Poisoned: used for poison cures (tech and item). Picks the first living poisoned member in slot order. If there is none, the actor targets themselves. Confidence: high.
- Paralyzed: used for paralysis cures. Picks the first living paralyzed member in slot order. If there is none, the actor. Confidence: high.
- Dead: used for revives (techs and items). Picks the first member with the human death bit (bit 2) in slot order; the android death bit is not checked. Otherwise the first living paralyzed member. Otherwise the "largest HP missing, humans only" rule. Confidence: high.
- Largest HP missing, everyone (variant 2): used for one healing skill. Any profession that is not dead (either bit). Ties go to the last tied member; if no one qualifies, slot 1. Confidence: high.
- Largest HP missing, androids only (variant 3): used for one android-repair item. Androids without the android death bit. Ties go to the last tied member; if no one qualifies, slot 1. Confidence: high.

## 4. Combination attacks

### Detection (`loc_27DE7C` ~310334-310448, `ComboSetup_*` ~310452-310582)
- Detection runs at the start of every actor's turn, right after retargeting. It scans the turn queue from the current actor forward, up to 4 consecutive turn slots. Confidence: high.
- For each slot in order, the scan stops at the first entry that fails any of these:
  - The fighter is paralyzed, dead (either bit), asleep (either flag) or "woken this round".
  - The fighter is an enemy. So an enemy acting in between breaks a combo; the enemy ends the scan.
  - The command is not a technique or a skill (attack, item or defend ends it).
  - For a technique: the fighter is tech-sealed, or the tech's TP cost is more than current TP.
  - For a skill: no uses are left. Or the skill has a "needs a weapon" flag and neither hand holds a weapon of the 4 real weapon classes.
  Each entry that passes adds a code: the tech ID, or 128 + the skill ID. Confidence: high.
- The collected codes are compared with a combo table, in table order; the first match wins (14 entries). Each entry gives its size (2, 3 or 4) and a list of accepted code sequences.
  - A per-combo "ordered" flag (top bit of the size byte) decides whether order matters.
  - Unordered 2-member combos accept either order. Unordered 3-member combos accept all 6 orders.
  - 4-member combos always need the exact order.
  - Only the first N collected entries are compared. The current actor must be the first participant, and the others must be the very next turn slots.
  Confidence: high.
- On a match: the current actor's command becomes "combo #k", the turn counter skips the other N−1 participants' turns (they do not act separately), and the actor runs the combo routine. The participants' original targets are ignored. If the combo's range field equals "all enemies" the target is all enemies; otherwise it is the first actor's (retargeted) target (`loc_AF74` ~16786). Confidence: high.

### Costs (`Character_DoCombo` ~16481-16500)
- Every participant pays: tech components deduct TP, skill components deduct one use. This runs after detection already confirmed each cost could be paid. Confidence: high.

### Damage (`loc_2864` ~4045-4105, then `loc_266C` ~3749)
- Summed stat S: for each participant, add the caster's battle mental for a tech component, or for a skill component the stat named by that skill's "stat" field.
- Summed power P: the sum of each component's own power field.
- The combo's own data adds a resistance stat R (taken from the target) and an element whose factor E is the target's property for it.
- Damage per target uses the normal damage formula with attack = S, power = P, defense = R, element factor = E. Result clamped to 1..999.
- Combos always hit every living target in range; no hit roll (the ability field is non-zero). `loc_B6A2`. Confidence: high.

### Effect chance (`loc_5FB4` ~8785-8868)
- Only some combo handlers call this (~16723-16748). For each present enemy slot, a chance roll uses S against the target's R and E (the combo's resistance stat and element). The combo's "threshold" field is the lower bound; passing the roll marks that enemy as affected. Confidence: high for the roll; low for what "affected" does, since the combo animation objects that read the mark were not traced.

## 5. Experience, money, drops, level-up, android recovery

- Each enemy that dies through its death routine adds its per-enemy experience and money fields to two 16-bit totals, which saturate at 65535. Enemies removed any other way add nothing. `loc_2D960` ~59614-59630. Confidence: high.
- Experience (`Battle_VictoryMessage` ~4704-4781):
  - The total is halved in vehicle battles.
  - It is then integer-divided by the number of party members who are not dead (either bit). Asleep and paralyzed members count and receive a share.
  - Every non-dead party member gets that full share, capped at 9,999,999.
  - Dead members get nothing, but every party member, dead or alive, gets `gain_exp_flag` set.
  - Until a particular story event flag is set, every character outside the party with `gain_exp_flag` also gets the same full share. After the flag is set, outside characters get nothing.
  Confidence: high.
- Money: the money total is added in full (not divided, not halved in vehicles), capped at 9,999,999 (~4798-4805). Confidence: high.
- Item drop (`Battle_CheckItemDrop` ~4830-4848): one roll per won battle.
  - A random number 0-127 must be below the formation's drop-rate field. The comparison is signed, so a rate of 128 or more never drops.
  - The item comes from the formation data.
  - The inventory is compacted first. If the last inventory slot is still occupied (inventory full), the player is offered a discard/replace flow.
  Confidence: high.
- Level-up, party (`BattleResults_PartyExp` ~5990-6043, `BattleResults_LevelUpStats` ~6100-6301):
  - Each member is checked against a per-character level table, one level at a time. The same member is re-checked until no further level is reached, so several levels per battle are possible. Level 99 stops.
  - Each level sets max HP, max TP, strength, mental, agility and dexterity to the table's absolute values (not increments) and sets the max skill uses.
  - Current HP and current TP are not changed, so level-ups do not heal.
  - Current skill uses are not refilled, except that a newly learned skill starts with full uses. A level can teach one tech and one skill.
  - In the original build, the equipment-adjusted stats are not recomputed after a level-up; a fix switch adds that.
  Confidence: high.
- Level-up, outside the party (`BattleResults_OutsidePartyExp` ~5915-5986): the same table logic, but current skill uses are also refilled. In the original build these characters gain at most one level per battle, and a pointer bug can mis-index characters at level 99; fix switches correct both. Confidence: high.
- Android recovery (`Battle_AndroidRecover` ~6303-6347): after the results, every party slot with the android death bit has that bit cleared and HP set to 1, with a message. Humans with the normal death bit are not revived. The same recovery also runs after a successful escape (~6609). Confidence: high.

## 6. Statuses and effect routines

### Poison
- No in-battle effect: no damage per round, and it does not block healing. In battle it is only set by the poison effect, cured by cure effects, and read by macro targeting. The HP drain is a field-only routine (~116420-116450). Confidence: high (every use of the bit was checked).

### Tech seal
- Blocks the party member's techniques: TP is deducted first and then the tech fizzles (`CharTech_Cast` ~14243-14248). Skills and items still work.
- A sealed member cannot be a tech participant in a combo.
- For enemies, seal is only read by an AI condition that switches to an override action. Nothing else in the enemy path checks seal.
- The seal survives death (the death routine clears every status bit except seal, `Character_Dead` ~12729-12762). It is not cleared at round end or by the stat-restore effect.
- It is cleared at battle exit, together with sleep (`Battle_LastMessage` ~6365, which clears bits 3 and 4; it runs under an end-of-battle sync-flag condition).
- No in-battle cure for it was found.
Confidence: high for the blocks and persistence; medium for the exit clear.

### Death handling
- A party member reaching 0 HP loses every status except seal. Androids get the android death bit; humans get the normal death bit. `Character_Dead` ~12729-12762. Confidence: high.

### What sets agility or dexterity to 1
- Paralysis effect: both agility and dexterity become 1, and sleep is removed.
- Android-death effect: both.
- Sleep: agility only. The sleep animation sets the sleep bit and agility to 1 (`BattleObj_SleepEffect` ~74801-74826).
- Agility-down and dexterity-down: floor at 1.
Confidence: high.

### What restores them
- Waking at round end restores agility only. Enemy paralysis is removed at round end, but agility and dexterity stay at 1 for the rest of the battle (`Battle_RestoreStatsAtTurnEnd` ~9791-9831). Confidence: high.
- The cure, revive and full-revive effects restore agility and dexterity to base. In the original build this happens even when the target did not have the status, which also wipes any agility or dexterity buff. With the fix switch it happens only when the status is present (~9370-9405). Confidence: high.
- The full restore effect also restores them (see below).

### Effect pipeline (`Ability_GetEffectAndRange` ~8885, `Battle_ProcessEffect` ~9512-9683)
- The roll: if the ability's "resistance stat" field is 0, the effect always succeeds. Otherwise: (random 0-63 + actor stat − target's resistance stat) × target's element factor for the ability's element must be above the ability's "chance" field.
- The actor stat:
  - Techs: battle mental.
  - Skills and enemy skills: the stat named by a per-ability field.
  - Items: the item's power field.
  - Party weapon attacks: the weapon's own fields (target stat, element, chance, power).
  - Enemy basic attacks: strength vs strength, against a fixed threshold, using a fixed element property per effect family (one for stat-downs, one for death, one for sleep/seal, one for poison/paralysis/android death).
- The same actor-stat value is the "amount" for stat changes.
Confidence: high.

### The effect routines (`AbilityEffectsOffs` ~9035)
- No-op codes: no secondary effect; damage and healing are handled elsewhere.
- Death: skipped if the target is already dead. On success, the death animation sets HP to 0 and the death bit. For a weapon attack only the first target found with a death or sleep result is processed (~8634-8669). High.
- Attack down / defense down: battle value = base − amount, floor 0. High.
- Agility down / dexterity down: battle value = base − amount, floor 1. High.
- "Sleep/paralyze": skipped if the target is already asleep or paralyzed. On success it applies sleep through the animation; the name says paralyze but this path sets sleep. High.
- Seal: skipped if already sealed; on success, sealed. High.
- Attack up, defense up, magic-defense up, agility up: target must match the range's human/android rule; item versions of attack-up do nothing to androids. Battle value = base + amount. High.
- Dexterity up: the same, but with no human/android check. High.
- No caps on the up effects. Agility and dexterity are single bytes, so a large amount can wrap past 255. Medium.
- Element resistance up: sets one elemental property of the target to 1. A per-ability field picks the element. Damage is multiplied by factor/4, so a factor of 1 takes about a quarter of normal damage. The routine ignores the roll result. Medium (which data field picks the element is a guess).
- Wake up: only if the target is asleep. Sets the "woken this round" bit, so the target cannot act for the rest of the round and is retargeted away from by single-target attacks. The actual wake happens through the animation object. Medium.
- "Normal logic" codes: just record the effect on success, for later resolution by the animation object. Medium.
- Cure poison / cure paralysis / revive / full revive: restore agility and dexterity as described above, then record the effect. Clearing the bit and restoring HP is done by the animation objects (`BattleObj_StatusHeal` ~74400-74430 seen). Medium.
- Poison: skipped if already poisoned; on success, sets poison. High.
- Paralyze: skipped if already paralyzed; on success, paralyzed, sleep removed, agility and dexterity 1. High.
- Android death: skipped if already set; on success, sets the android death bit and agility and dexterity 1. HP is not zeroed in this routine. High that HP is not touched here; low on what follows.
- Full restore: always applies. Resets all battle stats (strength, mental, agility, dexterity, attack, defense, magic defense) and all 14 elemental properties to base. This removes every buff and debuff and also undoes Defend for that target. High.
- All-stats up: always applies (the roll is ignored). Adds 20 to the current battle values of all 7 stats. High.

### Stacking and duration
- Up/down effects recompute from base each time, so they do not stack. The latest one overrides, and a down cast after an up replaces it.
- All-stats up is the only effect that stacks.
- No durations or timers exist. Buffs and debuffs last until the end of the battle. Battle stats are reset from base at the start of every battle (`FillBattleStats` ~11271). Nothing at round end resets them except Defend.
Confidence: high.

## 7. Defeat and victory checks (after every action, `loc_672C` ~9739-9777)

- Party "out": status has paralyzed, dead or android-dead (mask $46). Sleep does not count. Confidence: high.
- If every present party member is out, the defeat routine runs. Confidence: high.
- Enemy "out": dead or android-dead (mask $44). Dead enemies' slots are also cleared when their death routine ends, so they no longer count as present. If no enemy is left, victory. Defeat is checked first. Confidence: high.
- A party that is entirely paralyzed (or paralyzed plus dead) loses as soon as the next action ends. Confidence: high.
- A party that is entirely asleep, or asleep plus paralyzed with at least one asleep, keeps going: no commands are taken, only enemies act, and each sleeper has a 50% chance to wake at each round end. Confidence: high.
- Party paralysis never clears at round end; only enemy paralysis does. Confidence: high.
- Other masks for reference:
  - $6E (paralyzed, dead, either sleep flag, android-dead) is "cannot act" for escape (`Battle_ProcessRUN` ~7671-7701): escape fails automatically if no party member can act, and succeeds automatically if no enemy can act. The same mask is used when stepping back through command input (~2430).
  - $EE (the $6E bits plus "woken this round") is "skip this turn" at execution (~8039).
  - $C4 (dead, android-dead, woken this round) is "retarget away from" (~8335).
  Confidence: high.

## 8. Vehicle battles

- Only one fighter, in slot 1, using the vehicle's stats. Party members do not appear or act. Enemies always target that fighter. `loc_78EE` ~11307, `Character_Init` ~12420. Confidence: high.
- The menu has Attack / Skill / Run. There is no Macro, no techs, no items and no Defend. `Battle_VehMainOptions` ~2035, `Battle_VehOptionSelected` ~2067-2078. Confidence: high.
- Attack: the vehicle weapon, against one picked enemy (auto when only one). It uses the hit/crit roll; damage = vehicle attack vs enemy defense, using the enemy's property for one fixed element. `loc_684A` ~9851, `loc_280A` ~4015. Confidence: high.
- Skill: picked from a bitmask of the vehicle's available skills, using a vehicle-skill table with the same layout as enemy skills (`loc_68C0` ~9888, `loc_2812` ~4019). One vehicle skill also rolls a per-enemy effect chance (vehicle strength vs enemy mental, using the enemy's property for one fixed element, against a fixed threshold; `loc_5F5C` ~8756). Skill uses persist between battles; they are saved back at battle end (~6374-6390). Confidence: high.
- Vehicle HP is reset to full at the start of every vehicle battle (`FillBattleStats` vehicle branch `loc_77AE` ~11294). Confidence: high.
- If vehicle HP reaches 0, every party member is set to 0 HP and the death bit, and the vehicle is cleared. That is a defeat. `Character_Dead` vehicle branch `loc_850E` ~12764-12810. Confidence: high.
- Turn order uses the same agility rule; macro reordering is skipped (`Battle_OrderTurns` ~7821). Experience is halved (section 5). Confidence: high.

## 9. Ordering within a round

1. Main menu: every party member's target defaults to the first enemy slot (~1891).
2. Command input skips members who are paralyzed, dead, asleep (either flag) or android-dead (`Battle_ProcessCOMD` ~7634).
3. Turn order: bit 7 is cleared for everyone. The queue holds fighters not paralyzed, dead, asleep or android-dead; the agility sort is already known. Then macro reordering, then the enemy target pre-pick.
   - Ambush: only enemies are queued in that round.
   - Preemptive strike: only the party is queued.
4. Each turn:
   - The actor is skipped (no cost paid, no action) if it is now paralyzed, dead, asleep, android-dead or "woken this round".
   - Then retargeting, macro special targeting, combo detection, and the action.
   - Costs are paid at execution: TP at cast, skill use at start, item consumed at start. In macro mode an item fails if no unclaimed inventory slot holds it any more (~16063-16090).
   - A skill whose "needs a weapon" check fails still loses its use (~15205-15235). A tech with too little TP fails without paying.
   - Combo participants' later turns are skipped.
5. After each action: effect messages, then the defeat and victory checks (section 7).
6. Round end (`loc_5366` → `Battle_RestoreStatsAtTurnEnd`):
   - Defend's physical factor is restored.
   - Each sleeper (either flag), party or enemy, wakes on a coin flip, which also restores agility.
   - Enemies lose paralysis (their agility and dexterity stay at 1).
   - The per-fighter effect-message buffer is cleared.
   - Nothing else ticks: no buff timers, no poison damage.
7. A fighter that becomes asleep, paralyzed or dead before its turn simply loses that turn. If it wakes at round end it acts normally next round. If it is woken mid-round by the wake effect, it still loses the rest of this round (bit 7).

## Could not determine

- Where the enemy "ambush" reaction bit is set.
- What the combo effect mark ($FF in the target's effect slot) does in each combo's animation object.
- Exactly how the cure, revive and wake animation objects clear status bits and restore HP. Only one object was partly read; it may also clear sleep for every character rather than only the target.
- How player menus use the "Varied" range. The target field it would read is not set by the menu.
- Whether the battle-exit cleanup of sleep and seal runs on every exit path (it is gated by a sound/sync flag).
- What the android-death effect does to HP afterwards.
- Whether one-ally abilities not in the macro table really hit the first enemy in macro mode (inferred, not traced).
- The full set of conditions that check for particular enemy kinds (only two read in full).
