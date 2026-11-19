#include "support.h"

#include "rules/difficulty.h"
#include "rules/loader.h"

#include <cstdlib>

using namespace rules;

namespace {

constexpr int kFirst = 5;

bool within20(int hard, int original) {
	return std::abs(hard - original) * 100 <= original * 20;
}

Rewards winWith(const Database& db, const std::string& difficulty, RewardMultiplier multiplier) {
	BattleSetup s = support::setup(db, {"gel_mite", "rust_hound"}, 12, 20);
	s.difficulty = difficulty;
	s.rewardMultiplier = multiplier;
	Battle battle = support::create(db, s);
	for (int e = kFirst; e < static_cast<int>(battle.combatants().size()); ++e) {
		battle.combatant(e).hp = 1;
		battle.combatant(e).stats.evasion = 0;
	}
	for (int round = 0; round < 10 && battle.outcome() == Outcome::Ongoing; ++round) {
		for (int i = 0; i < battle.partyCount(); ++i) {
			battle.setCommand(i, Command::ability("frostbite", -1));
		}
		battle.resolveRound();
	}
	return battle.rewards();
}

}

ACC_CHECK(acc_bat_16, "ACC-BAT-16", "BAT-122, BAT-124, BAT-125, SAV-023") {
	const Database& db = support::data(t);

	// Hard stays within 20% of Original for every enemy's hit points, attack and defense.
	const Database hard = applyDifficulty(db, *db.difficulty("hard"));
	for (const auto& [id, enemy] : db.enemies) {
		const Stats& h = hard.enemies.at(id).stats;
		t.expect(within20(h.maxHp, enemy.stats.maxHp), id + ": Hard hit points differ from Original by more than 20%");
		t.expect(within20(h.strength, enemy.stats.strength), id + ": Hard attack differs from Original by more than 20%");
		t.expect(within20(h.defense, enemy.stats.defense), id + ": Hard defense differs from Original by more than 20%");
	}

	// The loader refuses an overlay that would break the limit.
	std::map<std::string, std::string> texts = support::dataTexts(t);
	std::string& overlay = texts["difficulties.json"];
	const std::string from = "\"stats_percent\": {\"hp\": 115";
	t.expect(overlay.find(from) != std::string::npos, "setup: difficulties.json no longer has the Hard hit point overlay this check edits");
	overlay.replace(overlay.find(from), from.size(), "\"stats_percent\": {\"hp\": 130");
	t.expect(!loadDatabaseFromTexts(texts).ok(), "an overlay raising enemy hit points by 30% loaded");

	// Difficulty is data passed in setup: an overlay the library has never seen applies through the same binary.
	{
		Database custom = db;
		DifficultyDef trial;
		trial.id = "acc_trial";
		trial.name = "Trial";
		EnemyOverlay tougher;
		tougher.enemy = "gel_mite";
		tougher.statsPercent.maxHp = 110;
		trial.enemies = {tougher};
		custom.difficulties[trial.id] = trial;
		BattleSetup s = support::setup(custom, {"gel_mite"}, 1);
		s.difficulty = trial.id;
		const Battle battle = support::create(custom, s);
		t.expect(battle.combatant(kFirst).stats.maxHp == db.enemy("gel_mite")->stats.maxHp * 110 / 100, "a difficulty overlay defined only in data did not apply");
	}

	// The reward multiplier scales battle experience and money, rounding down once.
	const Rewards base = winWith(db, "original", RewardMultiplier::X1);
	const int experience = db.enemy("gel_mite")->experience + db.enemy("rust_hound")->experience;
	const int currency = db.enemy("gel_mite")->currency + db.enemy("rust_hound")->currency;
	t.expect(base.experience == experience && base.currency == currency, "Original 1x rewards are not the defeated enemies' experience and money");
	for (const auto& [multiplier, percent] : std::vector<std::pair<RewardMultiplier, int>>{{RewardMultiplier::X1_5, 150}, {RewardMultiplier::X2, 200}}) {
		const Rewards scaled = winWith(db, "original", multiplier);
		t.expect(scaled.experience == experience * percent / 100 && scaled.currency == currency * percent / 100,
			std::to_string(percent) + "% multiplier gave " + std::to_string(scaled.experience) + " xp and " + std::to_string(scaled.currency) + " money");
	}
	const int hardPercent = db.difficulty("hard")->rewardPercent;
	const Rewards hardScaled = winWith(db, "hard", RewardMultiplier::X1_5);
	t.expect(hardScaled.experience == experience * hardPercent * 150 / 10000, "Hard with 1.5x did not scale experience once and round down");

	t.note("chests, shops and guild job rewards live outside core, so SAV-023's exclusion of them is not exercised here");
	t.note("every enemy's Hard hit points, attack and defense are within 20% of Original and the loader rejects a 30% overlay; a data-only difficulty applies through the same binary; 1x, 1.5x and 2x scale battle rewards and round down");
}
