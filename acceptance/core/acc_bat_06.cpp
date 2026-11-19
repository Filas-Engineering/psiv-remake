#include "support.h"

#include "rules/difficulty.h"
#include "rules/formulas.h"

using namespace rules;

namespace {

// Replays the draws Rng::chance makes: none at 0 or 100 percent and above.
bool chance(Rng& rng, int percent) {
	if (percent >= 100) {
		return true;
	}
	if (percent <= 0) {
		return false;
	}
	return rng.range(1, 100) <= percent;
}

int damageTo(const std::vector<Event>& events, int target) {
	for (const Event& e : events) {
		if (e.type == EventType::DamageDealt && e.target == target) {
			return e.amount;
		}
	}
	return -1;
}

}

ACC_CHECK(acc_bat_06, "ACC-BAT-06", "BAT-042") {
	const Database& db = support::data(t);
	const int first = 5;

	const Database original = applyDifficulty(db, *db.difficulty("original"));
	bool identity = original.enemies.size() == db.enemies.size() && original.abilities.size() == db.abilities.size();
	for (const auto& [id, enemy] : db.enemies) {
		const EnemyDef& o = original.enemies.at(id);
		identity = identity && o.stats.maxHp == enemy.stats.maxHp && o.stats.strength == enemy.stats.strength && o.stats.defense == enemy.stats.defense && o.experience == enemy.experience;
	}
	for (const auto& [id, ability] : db.abilities) {
		const AbilityDef& o = original.abilities.at(id);
		identity = identity && o.cost == ability.cost && o.effects.size() == ability.effects.size();
		for (std::size_t i = 0; identity && i < o.effects.size(); ++i) {
			identity = o.effects[i].power == ability.effects[i].power;
		}
	}
	t.expect(identity, "the Original difficulty overlay changes base data, so Original is not the unmodified balance");

	int samples = 0;
	for (std::uint64_t seed = 1; seed <= 500; ++seed) {
		// Physical: the fighter's basic attack, with the hit, critical and variance draws replayed.
		{
			BattleSetup s = support::setup(db, {"rust_hound"}, seed);
			s.difficulty = "original";
			Battle battle = support::create(db, s);
			support::sturdy(battle, first);
			battle.setCommand(0, Command::attack(first));
			support::othersDefend(battle);
			Rng rng(seed);
			support::expectedOrder(battle, rng);
			const Combatant& a = battle.combatant(0);
			const Combatant& d = battle.combatant(first);
			const bool hit = chance(rng, formulas::hitChance(a.effective(Stat::Dexterity), d.effective(Stat::Agility), d.stats.evasion));
			int expected = 0;
			if (hit) {
				const bool critical = chance(rng, formulas::criticalChance(a.stats.critical, 0));
				const int variance = rng.range(formulas::kVarianceMin, formulas::kVarianceMax);
				expected = formulas::physicalDamage(a.effective(Stat::Strength), d.effective(Stat::Defense), 100, variance);
				if (critical) {
					expected = formulas::criticalDamage(expected, db.battleRules.criticalDamagePercent);
				}
				expected = formulas::defended(expected);
			}
			const std::vector<Event> events = battle.resolveRound();
			const int dealt = damageTo(events, first);
			t.expect(hit ? dealt == expected : dealt == -1 && support::countType(events, EventType::Missed) == 1,
				"seed " + std::to_string(seed) + ": physical damage " + std::to_string(dealt) + ", formula gives " + std::to_string(hit ? expected : -1));
			++samples;
		}
		// Elemental: the mystic's technique against a weakness, with the variance draw replayed.
		{
			BattleSetup s = support::setup(db, {"rust_hound"}, seed);
			s.difficulty = "original";
			Battle battle = support::create(db, s);
			support::sturdy(battle, first);
			battle.setCommand(1, Command::ability("spark", first));
			support::othersDefend(battle);
			Rng rng(seed);
			support::expectedOrder(battle, rng);
			const int variance = rng.range(formulas::kVarianceMin, formulas::kVarianceMax);
			const Combatant& a = battle.combatant(1);
			const Combatant& d = battle.combatant(first);
			const int expected = formulas::defended(formulas::elementalDamage(db.ability("spark")->effects[0].power, a.effective(Stat::Mental), d.effective(Stat::Mental), d.affinity(Element::Shock), variance));
			const int dealt = damageTo(battle.resolveRound(), first);
			t.expect(dealt == expected, "seed " + std::to_string(seed) + ": elemental damage " + std::to_string(dealt) + ", formula gives " + std::to_string(expected));
			++samples;
		}
	}

	// Healing has no random draw.
	{
		Battle battle = support::create(db, support::setup(db, {"rust_hound"}, 1));
		battle.combatant(0).hp = 1;
		battle.setCommand(3, Command::ability("mend", 0));
		support::othersDefend(battle);
		int healed = -1;
		for (const Event& e : battle.resolveRound()) {
			healed = e.type == EventType::Healed ? e.amount : healed;
		}
		t.expect(healed == formulas::healAmount(db.ability("mend")->effects[0].power, battle.combatant(3).effective(Stat::Mental)), "heal amount differs from the heal formula");
		++samples;
	}

	t.note("Original overlay is the unmodified data; " + std::to_string(samples) + " battle results reproduce the core formulas exactly (physical with hit, critical and variance; elemental with affinity; heal)");
}
