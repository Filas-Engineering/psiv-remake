#include "support.h"

#include "rules/difficulty.h"
#include "rules/formulas.h"

using namespace rules;

namespace {

// Replays Battle::rollSpread: the base plus sixteen draws.
int spread(Rng& rng) {
	int total = formulas::kSpreadBase;
	for (int i = 0; i < formulas::kSpreadRolls; ++i) {
		total += rng.range(0, formulas::kSpreadRollMax);
	}
	return total;
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
	int criticals = 0;
	int misses = 0;
	for (std::uint64_t seed = 1; seed <= 500; ++seed) {
		// Physical: the fighter's basic attack, with the hit roll (which also decides a critical) and the spread replayed.
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
			const int roll = rng.range(0, formulas::kChanceRollMax);
			const int dodge = d.effective(Stat::Agility) + d.stats.evasion;
			const bool hit = formulas::chanceScore(roll, a.effective(Stat::Dexterity), dodge) > formulas::kHitFloor;
			const bool critical = hit && formulas::chanceScore(roll, a.effective(Stat::Dexterity) + a.stats.critical, dodge) > formulas::kCriticalAbove;
			int expected = 0;
			if (hit) {
				const int attack = a.effective(Stat::Strength);
				const int bonus = db.ability(a.basicAttack)->effects[0].power + (critical ? formulas::criticalBonus(attack) : 0);
				// The target defends, which quarters a hit without a weapon element.
				const int affinity = a.attackElement != Element::None ? d.affinity(a.attackElement) : 25;
				expected = formulas::damage(attack, d.effective(Stat::Defense), bonus, affinity, spread(rng));
			}
			const std::vector<Event> events = battle.resolveRound();
			const int dealt = damageTo(events, first);
			t.expect(hit ? dealt == expected : dealt == -1 && support::countType(events, EventType::Missed) == 1,
				"seed " + std::to_string(seed) + ": physical damage " + std::to_string(dealt) + ", formula gives " + std::to_string(hit ? expected : -1));
			t.expect(support::countType(events, EventType::Critical) == (critical ? 1 : 0),
				"seed " + std::to_string(seed) + ": the critical roll gives " + (critical ? "a critical" : "no critical") + ", the battle disagrees");
			criticals += critical ? 1 : 0;
			misses += hit ? 0 : 1;
			++samples;
		}
		// Elemental: the mystic's technique against a weakness, with the spread replayed.
		{
			BattleSetup s = support::setup(db, {"rust_hound"}, seed);
			s.difficulty = "original";
			Battle battle = support::create(db, s);
			support::sturdy(battle, first);
			battle.setCommand(1, Command::ability("spark", first));
			support::othersDefend(battle);
			Rng rng(seed);
			support::expectedOrder(battle, rng);
			const Combatant& a = battle.combatant(1);
			const Combatant& d = battle.combatant(first);
			// Defend does nothing against elemental damage.
			const int expected = formulas::damage(a.effective(Stat::Mental), d.effective(Stat::Mental), db.ability("spark")->effects[0].power, d.affinity(Element::Shock), spread(rng));
			const int dealt = damageTo(battle.resolveRound(), first);
			t.expect(dealt == expected, "seed " + std::to_string(seed) + ": elemental damage " + std::to_string(dealt) + ", formula gives " + std::to_string(expected));
			++samples;
		}
		// Healing in battle, with the spread replayed.
		{
			Battle battle = support::create(db, support::setup(db, {"rust_hound"}, seed));
			battle.combatant(0).hp = 1;
			battle.setCommand(3, Command::ability("mend", 0));
			support::othersDefend(battle);
			Rng rng(seed);
			support::expectedOrder(battle, rng);
			const int expected = formulas::healAmount(db.ability("mend")->effects[0].power, battle.combatant(3).effective(Stat::Mental), spread(rng));
			int healed = -1;
			for (const Event& e : battle.resolveRound()) {
				healed = e.type == EventType::Healed ? e.amount : healed;
			}
			t.expect(healed == expected, "seed " + std::to_string(seed) + ": healed " + std::to_string(healed) + ", formula gives " + std::to_string(expected));
			++samples;
		}
	}
	t.expect(criticals > 0 && misses > 0, "500 basic attacks gave " + std::to_string(criticals) + " criticals and " + std::to_string(misses) + " misses; both paths need samples");

	t.note("Original overlay is the unmodified data; " + std::to_string(samples) + " battle results reproduce the core formulas exactly (physical with the hit roll, "
		+ std::to_string(misses) + " misses and " + std::to_string(criticals) + " criticals, and the spread; elemental with affinity; heal)");
}
