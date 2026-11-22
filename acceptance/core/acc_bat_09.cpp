#include "support.h"

#include "rules/formulas.h"

#include <cstdlib>

using namespace rules;

namespace {

constexpr int kFirst = 5;
constexpr int kRounds = 10000;

struct Configuration {
	std::string enemy;
	// Hit points as a percent of the maximum, so the hit point overrides switch on.
	int hpPercent;
};

// Expected share of each ability in a lone enemy's first turn, facing a healthy party: the first override whose
// condition holds, otherwise one of the eight slots, each 1 in 8 since no slot was drawn before. A slot the enemy
// cannot pay for or aim (a revive with nobody fallen) is the basic attack.
std::map<std::string, double> expectedShares(const Database& db, const EnemyDef& enemy, int hpPercent) {
	for (const AiOverride& entry : enemy.overrides) {
		bool holds = false;
		switch (entry.when) {
		case AiConditionKind::SelfHpHalf:
		case AiConditionKind::AnyAllyHpHalf:
			holds = hpPercent <= 50;
			break;
		case AiConditionKind::SelfHpQuarterOnce:
			holds = hpPercent <= 25;
			break;
		// A lone enemy at position 0 is the last one standing, its whole group, with no other kind beside it and a free
		// place next to it, on its first turn.
		case AiConditionKind::LastStanding:
		case AiConditionKind::AllyAbsent:
		case AiConditionKind::FreeSlotBeside:
		case AiConditionKind::GroupIntact:
		case AiConditionKind::FirstTurn:
			holds = true;
			break;
		case AiConditionKind::HitByAttack:
		case AiConditionKind::HitByAbility:
		case AiConditionKind::HitByTechnique:
		case AiConditionKind::HitByGroup:
		case AiConditionKind::TechniquesBlocked:
		case AiConditionKind::Ambushed:
		case AiConditionKind::AllyPresent:
		case AiConditionKind::AfterTurns:
		case AiConditionKind::All:
		case AiConditionKind::Any:
			break;
		}
		if (holds) {
			return {{entry.ability, 100.0}};
		}
	}
	std::map<std::string, double> shares;
	for (const std::string& id : enemy.slots) {
		const AbilityDef* ability = db.ability(id);
		const bool usable = ability && ability->targeting != Targeting::OneFallenAlly && (ability->kind != AbilityKind::Technique || ability->cost <= enemy.stats.maxTp);
		shares[usable ? id : enemy.basicAttack] += 100.0 / kAiSlots;
	}
	return shares;
}

int pickedTarget(const std::vector<Event>& events) {
	for (const Event& e : events) {
		if (e.type == EventType::ActionStarted && e.actor == kFirst) {
			return e.target;
		}
	}
	return -1;
}

// The party defends, so the enemy's own AI chooses its command.
void partyDefends(Battle& battle) {
	for (int i = 0; i < battle.partyCount(); ++i) {
		if (battle.combatant(i).alive() && battle.command(i).kind == CommandKind::None) {
			battle.setCommand(i, Command::defend());
		}
	}
}

Battle targetBattle(const Database& db, std::uint64_t seed) {
	Battle battle = support::create(db, support::setup(db, {"rust_hound"}, seed));
	support::sturdy(battle, kFirst);
	const int hp[] = {300, 120, 700, 500, 200};
	for (int i = 0; i < battle.partyCount(); ++i) {
		support::sturdy(battle, i);
		battle.combatant(i).hp = hp[i];
		battle.combatant(i).stats.evasion = 0;
	}
	return battle;
}

}

ACC_CHECK(acc_bat_09, "ACC-BAT-09", "BAT-090, BAT-092") {
	const Database& db = support::data(t);
	const std::vector<Configuration> configurations = {{"gel_mite", 100}, {"rust_hound", 100}, {"rust_hound", 40}, {"bog_wisp", 100}, {"bog_wisp", 40}, {"brass_warden", 100}, {"brass_warden", 40}};
	std::string summary;
	for (const Configuration& c : configurations) {
		const EnemyDef& enemy = *db.enemy(c.enemy);
		const std::map<std::string, double> expected = expectedShares(db, enemy, c.hpPercent);
		std::map<std::string, int> seen;
		for (int seed = 1; seed <= kRounds; ++seed) {
			Battle battle = support::create(db, support::setup(db, {c.enemy}, static_cast<std::uint64_t>(seed) * 31 + 7));
			for (int i = 0; i < battle.partyCount(); ++i) {
				support::sturdy(battle, i);
				battle.setCommand(i, Command::defend());
			}
			battle.combatant(kFirst).hp = enemy.stats.maxHp * c.hpPercent / 100;
			for (const Event& e : battle.resolveRound()) {
				if ((e.type == EventType::ActionStarted || e.type == EventType::ActionCancelled) && e.actor == kFirst) {
					++seen[e.ability];
				}
			}
		}
		for (const auto& [ability, share] : expected) {
			const double observed = 100.0 * seen[ability] / kRounds;
			t.expect(std::abs(observed - share) <= 2.0, c.enemy + " at " + std::to_string(c.hpPercent) + "% hp: " + ability + " chosen " + std::to_string(observed) + "%, the slots and overrides give " + std::to_string(share) + "%");
		}
		for (const auto& [ability, count] : seen) {
			t.expect(expected.count(ability) == 1, c.enemy + " chose " + ability + " " + std::to_string(count) + " times, which no slot or holding override allows");
		}
		summary += (summary.empty() ? "" : ", ") + c.enemy + "@" + std::to_string(c.hpPercent) + "%";
	}

	// The draws replayed exactly: after the initiative rolls, the enemy's foe (one roll over the weights 5:4:3:2:1), then
	// at its turn the slot (0 to 7, as no slot was drawn before). The party defends, which draws nothing.
	int mirrored = 0;
	{
		const EnemyDef& hound = *db.enemy("rust_hound");
		for (std::uint64_t seed = 1; seed <= 2000; ++seed) {
			Battle battle = targetBattle(db, seed);
			partyDefends(battle);
			Rng rng(seed);
			int highest = 0;
			for (const Combatant& c : battle.combatants()) {
				highest = std::max(highest, c.effective(Stat::Agility));
			}
			for (std::size_t i = 0; i < battle.combatants().size(); ++i) {
				rng.range(0, formulas::initiativeSpread(highest));
			}
			int roll = rng.range(0, 14);
			int foe = 0;
			while (roll >= 5 - foe) {
				roll -= 5 - foe;
				++foe;
			}
			const std::string& slot = hound.slots[static_cast<std::size_t>(rng.range(0, kAiSlots - 1))];
			std::string ability;
			int target = -1;
			for (const Event& e : battle.resolveRound()) {
				if (e.type == EventType::ActionStarted && e.actor == kFirst) {
					ability = e.ability;
					target = e.target;
				}
			}
			mirrored += ability == slot && target == foe ? 1 : 0;
		}
		t.expect(mirrored == 2000, std::to_string(2000 - mirrored) + " of 2000 first turns disagree with the replayed foe and slot draws");
	}

	// The default target: party members not defeated, in party order, weighted 5:4:3:2:1.
	{
		Database plain = db;
		plain.enemies["rust_hound"].slots.assign(kAiSlots, "bite");
		plain.enemies["rust_hound"].overrides.clear();
		std::map<int, int> seen;
		const int trials = 6000;
		for (std::uint64_t seed = 1; seed <= static_cast<std::uint64_t>(trials); ++seed) {
			Battle battle = targetBattle(plain, seed);
			partyDefends(battle);
			++seen[pickedTarget(battle.resolveRound())];
		}
		bool weighted = true;
		for (int i = 0; i < 5; ++i) {
			weighted = weighted && std::abs(100.0 * seen[i] / trials - 100.0 * (5 - i) / 15) <= 2.5;
		}
		t.expect(weighted, "the default target rule did not pick party members 5:4:3:2:1 by party order");
	}

	// Each override's target rule, on a lone enemy whose slots are all bites and whose one override always holds.
	{
		Database ruled = db;
		auto onlyRule = [&](AiTargetRule rule, const std::string& status) {
			ruled.enemies["rust_hound"].slots.assign(kAiSlots, "bite");
			AiOverride entry;
			entry.when = AiConditionKind::LastStanding;
			entry.ability = "bite";
			entry.target = {rule, status};
			ruled.enemies["rust_hound"].overrides = {entry};
		};
		auto oneRound = [&](std::uint64_t seed, void (*prepare)(Battle&)) {
			Battle battle = targetBattle(ruled, seed);
			prepare(battle);
			partyDefends(battle);
			return pickedTarget(battle.resolveRound());
		};
		onlyRule(AiTargetRule::LowestHp, "");
		t.expect(oneRound(1, [](Battle&) {}) == 1, "the lowest_hp target rule did not pick the party member with the fewest hit points");
		onlyRule(AiTargetRule::HighestHp, "");
		t.expect(oneRound(2, [](Battle&) {}) == 2, "the highest_hp target rule did not pick the party member with the most hit points");
		onlyRule(AiTargetRule::HasStatus, "poison");
		t.expect(oneRound(3, [](Battle& b) { b.combatant(3).statuses.push_back({"poison", 0}); }) == 3, "the has_status target rule did not pick the one member with the status");
		onlyRule(AiTargetRule::LacksStatus, "poison");
		t.expect(oneRound(4, [](Battle& b) {
			for (int i : {0, 1, 3, 4}) {
				b.combatant(i).statuses.push_back({"poison", 0});
			}
		}) == 2, "the lacks_status target rule did not pick the one member without the status");

		onlyRule(AiTargetRule::LastAttacker, "");
		{
			Battle battle = targetBattle(ruled, 5);
			battle.setCommand(4, Command::attack(kFirst));
			battle.combatant(4).stats.dexterity = 400;
			partyDefends(battle);
			battle.resolveRound();
			partyDefends(battle);
			t.expect(pickedTarget(battle.resolveRound()) == 4, "the last_attacker target rule did not pick the foe that last damaged the enemy");
		}

		onlyRule(AiTargetRule::Random, "");
		std::map<int, int> seen;
		for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
			++seen[oneRound(seed, [](Battle& b) {
				b.combatant(0).hp = 0;
				b.combatant(0).statuses = {{kDefeatedStatus, 0}};
			})];
		}
		bool spread = seen.count(0) == 0 && seen.count(-1) == 0;
		for (int i = 1; i < 5; ++i) {
			spread = spread && std::abs(seen[i] - 250) <= 60;
		}
		t.expect(spread, "the random target rule did not pick evenly among the living foes");

		// An override whose rule finds no target does not fire: the slot's bite stands.
		onlyRule(AiTargetRule::HasStatus, "sleep");
		t.expect(oneRound(6, [](Battle&) {}) >= 0, "an enemy whose only override found no target did not act at all");
	}

	t.note(std::to_string(kRounds) + " seeded first turns each for " + summary + ": action frequencies within 2 points of the slots (1 in 8 each before any draw) or the override that holds; " + std::to_string(mirrored) + " first turns match the replayed foe and slot draws exactly; the default target follows party order 5:4:3:2:1; lowest_hp, highest_hp, has_status, lacks_status, last_attacker and random override target rules each pick the specified target");
}
