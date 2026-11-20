#include "rules/difficulty.h"

#include "rules/database.h"

#include <algorithm>

namespace rules {

namespace {

int scaled(int value, int percent, int minimum) {
	return std::max(minimum, value * percent / 100);
}

}

Database applyDifficulty(const Database& db, const DifficultyDef& difficulty) {
	Database out = db;
	for (const AbilityOverlay& overlay : difficulty.abilities) {
		const auto it = out.abilities.find(overlay.ability);
		if (it == out.abilities.end()) {
			continue;
		}
		AbilityDef& ability = it->second;
		for (Effect& effect : ability.effects) {
			if (effect.kind == EffectKind::PhysicalDamage || effect.kind == EffectKind::ElementalDamage || effect.kind == EffectKind::Heal) {
				effect.power = scaled(effect.power, overlay.powerPercent, effect.kind == EffectKind::PhysicalDamage ? 0 : 1);
			}
		}
		if (overlay.cost) {
			ability.cost = *overlay.cost;
		}
		if (overlay.uses) {
			ability.uses = *overlay.uses;
		}
	}
	for (const EnemyOverlay& overlay : difficulty.enemies) {
		const auto it = out.enemies.find(overlay.enemy);
		if (it == out.enemies.end()) {
			continue;
		}
		EnemyDef& enemy = it->second;
		const Stats& p = overlay.statsPercent;
		enemy.stats.maxHp = scaled(enemy.stats.maxHp, p.maxHp, 1);
		enemy.stats.maxTp = scaled(enemy.stats.maxTp, p.maxTp, 0);
		enemy.stats.strength = scaled(enemy.stats.strength, p.strength, 1);
		enemy.stats.mental = scaled(enemy.stats.mental, p.mental, 1);
		enemy.stats.agility = scaled(enemy.stats.agility, p.agility, 1);
		enemy.stats.dexterity = scaled(enemy.stats.dexterity, p.dexterity, 1);
		enemy.stats.defense = scaled(enemy.stats.defense, p.defense, 0);
		for (const std::string& id : overlay.addAbilities) {
			if (std::find(enemy.abilities.begin(), enemy.abilities.end(), id) == enemy.abilities.end()) {
				enemy.abilities.push_back(id);
			}
		}
		if (overlay.ai) {
			enemy.ai = *overlay.ai;
		}
		for (const auto& [status, percent] : overlay.statusResist) {
			enemy.statusResist[status] = percent;
		}
	}
	return out;
}

}
