#include "support.h"

#include <cstdlib>

using namespace rules;

namespace {

constexpr int kFirst = 5;
constexpr int kRounds = 10000;

struct Configuration {
	std::string enemy;
	// Hit points as a percent of the maximum, so self_hp_below rules switch on.
	int hpPercent;
};

// Expected share of each ability from the AI profile: entries whose conditions hold for a lone enemy at the
// given hit points, facing a healthy party with no statuses, weighted.
std::map<std::string, double> expectedShares(const Database& db, const EnemyDef& enemy, int hpPercent) {
	std::map<std::string, int> weights;
	int total = 0;
	for (const AiRule& rule : enemy.ai) {
		bool holds = false;
		switch (rule.condition.kind) {
		case AiConditionKind::Always:
			holds = true;
			break;
		case AiConditionKind::SelfHpBelow:
		case AiConditionKind::AllyHpBelow:
			holds = hpPercent < rule.condition.value;
			break;
		case AiConditionKind::AllyDefeated:
		case AiConditionKind::SelfHasStatus:
		case AiConditionKind::FoeHasStatus:
			holds = false;
			break;
		}
		const AbilityDef* ability = db.ability(rule.ability);
		if (holds && ability && (ability->kind != AbilityKind::Technique || ability->cost <= enemy.stats.maxTp)) {
			weights[rule.ability] += rule.weight;
			total += rule.weight;
		}
	}
	std::map<std::string, double> shares;
	for (const auto& [ability, weight] : weights) {
		shares[ability] = 100.0 * weight / total;
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
			t.expect(std::abs(observed - share) <= 2.0, c.enemy + " at " + std::to_string(c.hpPercent) + "% hp: " + ability + " chosen " + std::to_string(observed) + "%, profile weights give " + std::to_string(share) + "%");
		}
		for (const auto& [ability, count] : seen) {
			t.expect(expected.count(ability) == 1, c.enemy + " chose " + ability + " " + std::to_string(count) + " times, which no applicable profile entry allows");
		}
		summary += (summary.empty() ? "" : ", ") + c.enemy + "@" + std::to_string(c.hpPercent) + "%";
	}

	// Each entry's target rule, on a lone enemy whose profile is one bite.
	{
		Database ruled = db;
		auto onlyRule = [&](AiTargetRule rule, const std::string& status) {
			AiRule entry;
			entry.ability = "bite";
			entry.target.rule = rule;
			entry.target.status = status;
			ruled.enemies["rust_hound"].ai = {entry};
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

		// A rule that finds no target does not qualify that round: the enemy falls back to its basic attack on a random foe.
		onlyRule(AiTargetRule::HasStatus, "sleep");
		t.expect(oneRound(6, [](Battle&) {}) >= 0, "an enemy whose only entry found no target did not act at all");
	}

	t.note(std::to_string(kRounds) + " seeded rounds each for " + summary + ": action frequencies within 2 points of the profile weights; lowest_hp, highest_hp, has_status, lacks_status, last_attacker and random target rules each pick the specified target");
}
