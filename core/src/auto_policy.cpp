#include "rules/auto_policy.h"

#include "rules/formulas.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace rules {

namespace {

constexpr int kHealBelowPercent = 40;
constexpr int kSpendTpAbovePercent = 30;
constexpr int kRolls = formulas::kChanceRollMax + 1;

int damagePower(const AbilityDef& ability) {
	int total = 0;
	for (const Effect& effect : ability.effects) {
		if (effect.kind == EffectKind::PhysicalDamage || effect.kind == EffectKind::ElementalDamage) {
			total += effect.power;
		}
	}
	return total;
}

int hintRank(const AbilityDef& ability) {
	return ability.autoHint == AutoHint::Prefer ? 0 : ability.autoHint == AutoHint::Avoid ? 2 : 1;
}

// One option of the expected policy: an ability, or the basic attack with `ability` null.
struct Option {
	const AbilityDef* ability = nullptr;
	int target = -1;
	std::int64_t score = 0;
};

class Estimate {
public:
	Estimate(const Battle& battle, int actor) : battle_(battle), rules_(battle.database().battleRules), self_(battle.combatant(actor)) {}

	// The hit points the attack is expected to take from the target, in 64ths: damage at the spread's mean times the
	// chance to hit, and an instant defeat's chance to land times the hit points left, together at most the hit points.
	std::int64_t removed(const AbilityDef& attack, bool basic, int target) const {
		const Combatant& t = battle_.combatant(target);
		const int hits = basic || attack.kind == AbilityKind::Attack ? hitChance(t) : kRolls;
		std::int64_t total = 0;
		for (const Effect& effect : attack.effects) {
			if (!effect.reaches(t.kind)) {
				continue;
			}
			if (effect.kind == EffectKind::PhysicalDamage || effect.kind == EffectKind::ElementalDamage) {
				total += static_cast<std::int64_t>(hits) * std::min(damage(attack, basic, effect, t), t.hp);
			} else if (effect.kind == EffectKind::InstantDefeat) {
				total += static_cast<std::int64_t>(hits) * landChance(attack, effect, t) * t.hp / kRolls;
			}
		}
		return std::min<std::int64_t>(total, static_cast<std::int64_t>(t.hp) * kRolls);
	}

private:
	int stat(const AbilityDef& attack, bool basic, bool physical) const {
		if (attack.attackStat) {
			return self_.effective(*attack.attackStat);
		}
		if (basic) {
			return self_.effective(Stat::Attack);
		}
		return self_.effective(physical ? Stat::Strength : Stat::Mental);
	}

	int affinity(const Combatant& t, const Element& element) const {
		const int base = t.affinity(element);
		return t.defending && rules_.defends(element) ? std::min(base, rules_.defendedAffinity()) : base;
	}

	int damage(const AbilityDef& attack, bool basic, const Effect& effect, const Combatant& t) const {
		const bool physical = effect.kind == EffectKind::PhysicalDamage;
		const int power = stat(attack, basic, physical) * effect.statPermille / 1000;
		int hitAffinity = 0;
		int guard = 0;
		if (physical) {
			const Element element = effect.weaponElement ? self_.attackElement : effect.element != Element::None ? effect.element : basic ? self_.attackElement : Element::None;
			hitAffinity = element != Element::None ? affinity(t, element) : t.defending && rules_.defends(Element::None) ? rules_.defendedAffinity() : rules_.elementlessHitAffinity();
			guard = t.effective(Stat::Defense);
		} else {
			hitAffinity = affinity(t, effect.element);
			guard = t.effective(rules_.magicDefense ? Stat::MagicDefense : Stat::Mental);
		}
		return formulas::damage(power, guard, effect.power, hitAffinity, formulas::kSpreadMean, rules_.affinityDivisor, rules_.damageWraparound, rules_.immuneMinDamage);
	}

	int hitChance(const Combatant& t) const {
		const int dexterity = self_.effective(Stat::Dexterity);
		const int dodge = t.effective(Stat::Agility) + t.stats.evasion;
		int hits = 0;
		for (int roll = 0; roll < kRolls; ++roll) {
			hits += formulas::chanceScore(roll, dexterity, dodge) > formulas::kHitFloor ? 1 : 0;
		}
		return hits;
	}

	// Rolls of 64 that land the effect, as Battle::rollEffect scores them; 0 against a target it cannot fell.
	int landChance(const AbilityDef& attack, const Effect& effect, const Combatant& t) const {
		const int elementAffinity = effect.element == Element::None ? rules_.affinityDivisor : t.affinity(effect.element);
		if (!t.vulnerable || elementAffinity <= 0) {
			return 0;
		}
		if (effect.alwaysLands) {
			return kRolls;
		}
		const int fromAttackStat = attack.attackStat ? self_.effective(*attack.attackStat == Stat::Attack ? Stat::Strength : *attack.attackStat) : self_.effective(Stat::Mental);
		const int user = effect.userPower ? *effect.userPower : effect.userStat ? self_.effective(*effect.userStat) : fromAttackStat;
		const int against = t.effective(effect.targetStat.value_or(Stat::Mental));
		int lands = 0;
		for (int roll = 0; roll < kRolls; ++roll) {
			lands += formulas::effectScore(roll, user, against, elementAffinity, rules_.affinityDivisor, 0) > effect.threshold ? 1 : 0;
		}
		return lands;
	}

	const Battle& battle_;
	const BattleRules& rules_;
	const Combatant& self_;
};

// rules.json auto_policy expected: the attack (an affordable ability or the basic attack) with the largest expected
// effect, each target or group as its targeting reaches, scaled by what it leaves of its resource and by its hint.
Command expectedAttack(const Battle& battle, int actor, const std::vector<const AbilityDef*>& usable, int weakestFoe) {
	const Combatant& self = battle.combatant(actor);
	const std::vector<int> foes = battle.livingFoes(actor);
	const Estimate estimate(battle, actor);
	std::vector<Option> options;
	for (const AbilityDef* ability : usable) {
		const bool affordable = ability->kind == AbilityKind::Skill || self.tp * 100 >= self.stats.maxTp * kSpendTpAbovePercent;
		const bool attacks = std::any_of(ability->effects.begin(), ability->effects.end(), [](const Effect& e) {
			return e.kind == EffectKind::PhysicalDamage || e.kind == EffectKind::ElementalDamage || e.kind == EffectKind::InstantDefeat;
		});
		if (affordable && attacks) {
			options.push_back({ability});
		}
	}
	const AbilityDef* basicAttack = battle.database().ability(self.basicAttack);
	if (basicAttack) {
		options.push_back({nullptr});
	}
	Option best;
	for (Option& option : options) {
		const AbilityDef& attack = option.ability ? *option.ability : *basicAttack;
		const bool basic = option.ability == nullptr;
		std::int64_t removed = 0;
		int target = weakestFoe;
		switch (attack.targeting) {
		case Targeting::OneEnemy:
			for (int foe : foes) {
				const std::int64_t value = estimate.removed(attack, basic, foe);
				if (value > removed) {
					removed = value;
					target = foe;
				}
			}
			break;
		case Targeting::EnemyGroup:
			for (int foe : foes) {
				std::int64_t value = 0;
				for (int other : foes) {
					value += battle.combatant(other).group == battle.combatant(foe).group ? estimate.removed(attack, basic, other) : 0;
				}
				if (value > removed) {
					removed = value;
					target = foe;
				}
			}
			break;
		case Targeting::AllEnemies:
		case Targeting::RandomEnemy:
			for (int foe : foes) {
				removed += estimate.removed(attack, basic, foe);
			}
			if (attack.targeting == Targeting::RandomEnemy && !foes.empty()) {
				removed /= static_cast<std::int64_t>(foes.size());
			}
			break;
		case Targeting::OneAlly:
		case Targeting::OneFallenAlly:
		case Targeting::AllAllies:
		case Targeting::Self:
			break;
		}
		std::int64_t left = 1;
		std::int64_t had = 1;
		if (!basic && attack.kind == AbilityKind::Technique) {
			had = self.tp + 1;
			left = self.tp - attack.cost + 1;
		} else if (!basic && attack.kind == AbilityKind::Skill) {
			const auto uses = self.skillUses.find(attack.id);
			had = (uses == self.skillUses.end() ? 0 : uses->second) + 1;
			left = had - 1;
		}
		const std::int64_t hint = attack.autoHint == AutoHint::Prefer ? 4 : attack.autoHint == AutoHint::Avoid ? 1 : 2;
		option.target = target;
		option.score = removed * 1024 * left * hint / had;
		if (option.score > best.score) {
			best = option;
		}
	}
	if (best.ability) {
		return Command::ability(best.ability->id, best.target);
	}
	return Command::attack(best.score > 0 ? best.target : weakestFoe);
}

}

Command autoCommand(const Battle& battle, int actor) {
	const Database& db = battle.database();
	const Combatant& self = battle.combatant(actor);

	int weakestFoe = -1;
	for (int foe : battle.livingFoes(actor)) {
		if (weakestFoe < 0 || battle.combatant(foe).hp < battle.combatant(weakestFoe).hp) {
			weakestFoe = foe;
		}
	}

	std::vector<const AbilityDef*> usable;
	for (const std::string& id : self.abilities) {
		const AbilityDef* ability = db.ability(id);
		if (ability && ability->autoHint != AutoHint::Never && battle.checkUsable(actor, *ability) == CancelReason::None) {
			usable.push_back(ability);
		}
	}
	std::stable_sort(usable.begin(), usable.end(), [](const AbilityDef* a, const AbilityDef* b) { return hintRank(*a) < hintRank(*b); });
	for (const AbilityDef* ability : usable) {
		if (ability->targeting != Targeting::OneFallenAlly) {
			continue;
		}
		for (int ally : battle.allies(actor)) {
			if (!battle.combatant(ally).alive() && battle.wouldChange(ally, ability->effects)) {
				return Command::ability(ability->id, ally);
			}
		}
	}
	for (const AbilityDef* ability : usable) {
		if (ability->targeting != Targeting::OneAlly || !ability->hasEffect(EffectKind::Heal)) {
			continue;
		}
		int weakest = -1;
		for (int ally : battle.allies(actor)) {
			const Combatant& c = battle.combatant(ally);
			if (c.alive() && c.hp * 100 < c.stats.maxHp * kHealBelowPercent && battle.wouldChange(ally, ability->effects)
				&& (weakest < 0 || c.hp < battle.combatant(weakest).hp)) {
				weakest = ally;
			}
		}
		if (weakest >= 0) {
			return Command::ability(ability->id, weakest);
		}
	}
	if (db.battleRules.autoExpected) {
		return expectedAttack(battle, actor, usable, weakestFoe);
	}
	const AbilityDef* strongest = nullptr;
	for (const AbilityDef* ability : usable) {
		const bool affordable = ability->kind == AbilityKind::Skill || self.tp * 100 >= self.stats.maxTp * kSpendTpAbovePercent;
		const bool stronger = !strongest || hintRank(*ability) < hintRank(*strongest) || (hintRank(*ability) == hintRank(*strongest) && damagePower(*ability) > damagePower(*strongest));
		if (affordable && damagePower(*ability) > 0 && stronger) {
			strongest = ability;
		}
	}
	if (strongest) {
		return Command::ability(strongest->id, weakestFoe);
	}
	return Command::attack(weakestFoe);
}

}
