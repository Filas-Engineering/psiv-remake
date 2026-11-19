#include "rules/auto_policy.h"

#include <vector>

namespace rules {

namespace {

constexpr int kHealBelowPercent = 40;
constexpr int kSpendTpAbovePercent = 30;

int damagePower(const AbilityDef& ability) {
	int total = 0;
	for (const Effect& effect : ability.effects) {
		if (effect.kind == EffectKind::PhysicalDamage || effect.kind == EffectKind::ElementalDamage) {
			total += effect.power;
		}
	}
	return total;
}

}

Command autoCommand(const Battle& battle, int actor) {
	const Database& db = battle.database();
	const Combatant& self = battle.combatant(actor);

	int weakestAlly = -1;
	int defeatedAlly = -1;
	for (int ally : battle.allies(actor)) {
		const Combatant& c = battle.combatant(ally);
		if (!c.alive()) {
			if (defeatedAlly < 0) {
				defeatedAlly = ally;
			}
			continue;
		}
		if (c.hp * 100 < c.stats.maxHp * kHealBelowPercent && (weakestAlly < 0 || c.hp < battle.combatant(weakestAlly).hp)) {
			weakestAlly = ally;
		}
	}

	int weakestFoe = -1;
	for (int foe : battle.livingFoes(actor)) {
		if (weakestFoe < 0 || battle.combatant(foe).hp < battle.combatant(weakestFoe).hp) {
			weakestFoe = foe;
		}
	}

	std::vector<const AbilityDef*> usable;
	for (const std::string& id : self.abilities) {
		const AbilityDef* ability = db.ability(id);
		if (ability && battle.checkUsable(actor, *ability) == CancelReason::None) {
			usable.push_back(ability);
		}
	}
	if (defeatedAlly >= 0) {
		for (const AbilityDef* ability : usable) {
			if (ability->targeting == Targeting::OneFallenAlly) {
				return Command::ability(ability->id, defeatedAlly);
			}
		}
	}
	if (weakestAlly >= 0) {
		for (const AbilityDef* ability : usable) {
			if (ability->targeting == Targeting::OneAlly && ability->hasEffect(EffectKind::Heal)) {
				return Command::ability(ability->id, weakestAlly);
			}
		}
	}
	const AbilityDef* strongest = nullptr;
	for (const AbilityDef* ability : usable) {
		const bool affordable = ability->kind == AbilityKind::Skill || self.tp * 100 >= self.stats.maxTp * kSpendTpAbovePercent;
		if (affordable && damagePower(*ability) > 0 && (!strongest || damagePower(*ability) > damagePower(*strongest))) {
			strongest = ability;
		}
	}
	if (strongest) {
		return Command::ability(strongest->id, weakestFoe);
	}
	return Command::attack(weakestFoe);
}

}
