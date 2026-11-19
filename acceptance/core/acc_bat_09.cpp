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
				if ((e.type == EventType::ActionStarted || e.type == EventType::ActionFailed) && e.actor == kFirst) {
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

	t.gap("AI profile entries have no target rule field (BAT-090 lists one per entry); targets follow fixed rules per ability kind");
	t.note(std::to_string(kRounds) + " seeded rounds each for " + summary + ": action frequencies within 2 points of the profile weights");
}
