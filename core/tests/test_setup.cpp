#include "fixtures.h"
#include "rules/formulas.h"
#include "test.h"

#include <set>

using namespace rules;
using namespace fixtures;

namespace {

bool anyActionBy(const std::vector<Event>& events, bool party, const Battle& battle) {
	for (const Event& e : events) {
		if ((e.type == EventType::ActionStarted || e.type == EventType::ComboTriggered) && battle.isParty(e.actor) == party) {
			return true;
		}
	}
	return false;
}

}

TEST(partyFirstOpeningCostsTheEnemiesARound) {
	BattleSetup setup = makeSetup(data(), {"gel_mite", "rust_hound"}, 3);
	setup.opening = Opening::PartyFirst;
	Battle battle = makeBattle(data(), setup);
	CHECK(!battle.sideActsNextRound(Side::Enemy));
	CHECK(!battle.setCommand(kFirstEnemy, Command::defend()).empty());
	partyDefends(battle);
	const std::vector<Event> first = battle.resolveRound();
	CHECK(anyActionBy(first, true, battle));
	CHECK(!anyActionBy(first, false, battle));
	CHECK(battle.sideActsNextRound(Side::Enemy));
	partyDefends(battle);
	CHECK(anyActionBy(battle.resolveRound(), false, battle));
}

TEST(enemyFirstOpeningCostsThePartyARound) {
	BattleSetup setup = makeSetup(data(), {"gel_mite", "rust_hound"}, 3);
	setup.opening = Opening::EnemyFirst;
	Battle battle = makeBattle(data(), setup);
	const Message refused = battle.setCommand(kFighter, Command::defend());
	CHECK_EQ(refused.key, std::string("command.caught_off_guard"));
	CHECK_EQ(refused.params, (std::vector<std::string>{"fighter"}));
	const std::vector<MacroStepResult> results = battle.applyMacro(*data().macro("opener"));
	CHECK(results[0].status == MacroStepStatus::Skipped && results[0].issue == MacroIssue::CaughtOffGuard);
	const std::vector<Event> first = battle.resolveRound();
	CHECK(!anyActionBy(first, true, battle));
	CHECK(anyActionBy(first, false, battle));
	CHECK(battle.lastRoundAsMacro("empty").steps.empty());
	CHECK(anyActionBy(playRound(battle), true, battle));
}

TEST(surpriseLengthIsData) {
	Database db = data();
	db.battleRules.surpriseRounds = 0;
	BattleSetup setup = makeSetup(db, {"gel_mite"}, 3);
	setup.opening = Opening::EnemyFirst;
	Battle battle = makeBattle(db, setup);
	CHECK(battle.setCommand(kFighter, Command::defend()).empty());

	db.battleRules.surpriseRounds = 2;
	Battle longer = makeBattle(db, setup);
	longer.resolveRound();
	CHECK(!longer.sideActsNextRound(Side::Party));
	longer.resolveRound();
	CHECK(longer.sideActsNextRound(Side::Party));
}

TEST(openingFollowsContactRules) {
	CHECK(openingFromContact(true, false, false) == Opening::PartyFirst);
	CHECK(openingFromContact(true, true, false) == Opening::Normal);
	CHECK(openingFromContact(false, true, true) == Opening::EnemyFirst);
	CHECK(openingFromContact(false, false, true) == Opening::Normal);
	CHECK(openingFromContact(false, true, false) == Opening::Normal);
}

TEST(openingFromRollFollowsTheChanceScore) {
	// The score is (roll + party agility - awareness) * 2, so it is always even.
	CHECK(openingFromRoll(6, 10, 10) == Opening::EnemyFirst);
	CHECK(openingFromRoll(7, 10, 10) == Opening::Normal);
	CHECK(openingFromRoll(58, 10, 10) == Opening::Normal);
	CHECK(openingFromRoll(59, 10, 10) == Opening::PartyFirst);
	CHECK(openingFromRoll(0, 16, 10) == Opening::EnemyFirst);
	CHECK(openingFromRoll(0, 17, 10) == Opening::Normal);
	CHECK(openingFromRoll(63, 5, 10) == Opening::Normal);
	CHECK(openingFromRoll(63, 6, 10) == Opening::PartyFirst);
	CHECK(openingFromRoll(0, 5, 30) == Opening::EnemyFirst);
	CHECK(openingFromRoll(63, 200, 0) == Opening::PartyFirst);
}

TEST(encountersFallBackToTheirFastestEnemy) {
	const Database& db = data();
	CHECK_EQ(db.encounter("training")->awareness, db.enemy("rust_hound")->stats.agility);
	CHECK_EQ(db.encounter("training")->escapeRating, db.enemy("rust_hound")->stats.agility);
	CHECK_EQ(db.encounter("warden")->escapeRating, db.enemy("bog_wisp")->stats.agility);

	BattleSetup setup = makeSetup(db, {}, 1);
	CHECK(useEncounter(setup, db, "training").empty());
	CHECK_EQ(setup.escapeRating, db.encounter("training")->escapeRating);

	// Without an encounter the battle takes the highest enemy agility at the start, after the difficulty overlay.
	Database faster = db;
	faster.enemies["gel_mite"].stats.agility = 40;
	Battle battle = makeBattle(faster, {"rust_hound", "gel_mite"}, 1);
	CHECK_EQ(battle.setup().escapeRating, 40);
	BattleSetup rated = makeSetup(faster, {"rust_hound", "gel_mite"}, 1);
	rated.escapeRating = 3;
	CHECK_EQ(makeBattle(faster, rated).setup().escapeRating, 3);
}

TEST(escapeRollsAgainstTheEscapeRating) {
	Database db = data();
	db.characters["scout"].base.agility = 100;
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		for (int rating : {0, 160}) {
			BattleSetup setup = makeSetup(db, {"gel_mite"}, seed);
			setup.escapeRating = rating;
			Battle battle = makeBattle(db, setup);
			const int agility = battle.combatant(kScout).effective(Stat::Agility);
			// These ratings decide the escape whatever the roll.
			CHECK(formulas::chanceScore(formulas::kChanceRollMax, agility, 160) <= formulas::kEscapeFloor);
			CHECK(formulas::chanceScore(0, agility, 0) > formulas::kEscapeFloor);
			battle.setCommand(kScout, Command::escape());
			othersDefend(battle);
			const std::optional<Event> attempt = firstBy(battle.resolveRound(), EventType::EscapeAttempted, kScout);
			CHECK(attempt && attempt->succeeded == (rating == 0));
		}
	}
}

TEST(anEscapeRatingOf240ForbidsEscape) {
	for (int rating : {239, 240, 255}) {
		BattleSetup setup = makeSetup(data(), {"gel_mite"}, 1);
		setup.escapeRating = rating;
		Battle battle = makeBattle(data(), setup);
		CHECK_EQ(battle.escapeAllowed(), rating < 240);
		CHECK_EQ(battle.setCommand(kScout, Command::escape()).key, std::string(rating < 240 ? "" : "command.escape_forbidden"));
		othersDefend(battle);
		const std::vector<Event> started = ofType(battle.resolveRound(), EventType::BattleStarted);
		CHECK(started.size() == 1 && started[0].flags.escapeAllowed == (rating < 240));
	}
	// An encounter's rating goes through useEncounter.
	Database db = data();
	db.encounters["training"].escapeRating = 240;
	BattleSetup setup = makeSetup(db, {}, 1);
	CHECK(useEncounter(setup, db, "training").empty());
	CHECK(!makeBattle(db, setup).escapeAllowed());
}

TEST(encounterCanForbidEscape) {
	BattleSetup setup = makeSetup(data(), {}, 1);
	CHECK(useEncounter(setup, data(), "warden").empty());
	CHECK(!setup.flags.escapeAllowed);
	CHECK_EQ(setup.enemies.size(), std::size_t(2));
	CHECK(!useEncounter(setup, data(), "nowhere").empty());
	CHECK(useEncounter(setup, data(), "warden").empty());
	Battle battle = makeBattle(data(), setup);
	CHECK(!battle.escapeAllowed());
	CHECK_EQ(battle.setCommand(kScout, Command::escape()).key, std::string("command.escape_forbidden"));

	Macro macro;
	macro.name = "run";
	macro.steps.push_back({"scout", CommandKind::Escape, "", {}});
	const std::vector<MacroStepResult> results = battle.applyMacro(macro);
	CHECK(results[0].status == MacroStepStatus::FellBack && results[0].issue == MacroIssue::EscapeNotAllowed);
	CHECK(battle.command(kScout).kind == CommandKind::Attack);
}

TEST(escapeEndsTheBattleAtOnce) {
	Database db = data();
	db.characters["scout"].base.agility = 500;
	Battle battle = makeBattle(db, {"gel_mite", "rust_hound"}, 1);
	battle.setCommand(kScout, Command::escape());
	const std::vector<Event> events = battle.resolveRound();
	CHECK(battle.outcome() == Outcome::Escaped);
	CHECK(battle.lastTurnOrder().front() == kScout);
	std::vector<EventType> types;
	for (const Event& e : events) {
		types.push_back(e.type);
	}
	// Nothing runs between the escape and the end of the round.
	CHECK_EQ(types, (std::vector<EventType>{EventType::BattleStarted, EventType::RoundStarted, EventType::CommandsLocked, EventType::OrderResolved,
		EventType::EscapeAttempted, EventType::RoundEnded, EventType::BattleEnded}));
	CHECK(events.size() == 7 && events[4].succeeded);
	CHECK_EQ(battle.rewards().experience, 0);
	CHECK_EQ(battle.rewards().memberExperience, (std::vector<int>{0, 0, 0, 0, 0}));
}

TEST(groupAbilityFromAnEnemyHitsTheWholeParty) {
	Database db = data();
	db.enemies["brass_warden"].slots.assign(static_cast<std::size_t>(kAiSlots), "steam_vent");
	db.enemies["brass_warden"].overrides.clear();
	Battle battle = makeBattle(db, {"brass_warden"}, 4);
	battle.combatant(kMedic).hp = 0;
	battle.combatant(kMedic).statuses = {{kDefeatedStatus, 0}};
	partyDefends(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> vent = firstBy(events, EventType::ActionStarted, kFirstEnemy);
	CHECK(vent && vent->ability == "steam_vent" && vent->target == -1);
	std::vector<int> hit;
	for (const Event& e : ofType(events, EventType::DamageDealt)) {
		hit.push_back(e.target);
	}
	CHECK_EQ(hit, (std::vector<int>{kFighter, kMystic, kScout, kEngineer}));
}

TEST(groupAbilityFromThePartyHitsTheFormationGroup) {
	BattleSetup setup = makeSetup(data(), {}, 2);
	setup.enemies = {{"gel_mite", "front"}, {"rust_hound", "front"}, {"gel_mite", "back"}};
	Battle battle = makeBattle(data(), setup);
	battle.setCommand(kMystic, Command::ability("ember", kFirstEnemy + 1));
	othersDefend(battle);
	std::vector<int> hit;
	for (const Event& e : ofType(battle.resolveRound(), EventType::DamageDealt)) {
		hit.push_back(e.target);
	}
	CHECK_EQ(hit, (std::vector<int>{kFirstEnemy, kFirstEnemy + 1}));
}

// Plain damage turns to the living enemy missing the most hit points; anything else to the one missing the fewest.
TEST(aGoneFoeIsReplacedByHitPointsMissing) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	db.characters["scout"].base.mental = 500;
	db.combos.clear();
	for (bool damage : {true, false}) {
		Battle battle = makeBattle(db, {"gel_mite", "gel_mite", "gel_mite", "gel_mite"}, 1);
		for (int i = kFirstEnemy; i < kFirstEnemy + 4; ++i) {
			battle.combatant(i).stats.maxHp = 500;
			battle.combatant(i).hp = 500;
		}
		battle.combatant(kFirstEnemy).hp = 0;
		battle.combatant(kFirstEnemy).statuses = {{kDefeatedStatus, 0}};
		battle.combatant(kFirstEnemy + 1).hp = 450;
		battle.combatant(kFirstEnemy + 2).hp = 300;
		battle.combatant(kFirstEnemy + 3).hp = 400;
		const int actor = damage ? kFighter : kScout;
		battle.setCommand(actor, damage ? Command::attack(kFirstEnemy) : Command::ability("weaken", kFirstEnemy));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::optional<Event> turned = firstBy(events, EventType::Retargeted, actor);
		const int expected = damage ? kFirstEnemy + 2 : kFirstEnemy + 1;
		CHECK(turned && turned->previousTarget == kFirstEnemy && turned->target == expected);
		const std::optional<Event> started = firstBy(events, EventType::ActionStarted, actor);
		CHECK(started && started->target == expected);
	}
	// Outside a battle's turn nothing is drawn: a gone foe resolves to the first living one.
	Battle battle = makeBattle(db, {"gel_mite", "gel_mite", "gel_mite"}, 1);
	battle.combatant(kFirstEnemy + 1).hp = 0;
	CHECK_EQ(battle.resolveTargets(kFighter, Targeting::OneEnemy, kFirstEnemy + 1, false), (std::vector<int>{kFirstEnemy}));
	CHECK_EQ(battle.resolveTargets(kFirstEnemy, Targeting::OneEnemy, kMystic, false), (std::vector<int>{kMystic}));
}

// Each later tied enemy takes the place on a coin flip, so with three tied the last wins half the time.
TEST(retargetTiesGoByACoinFlipPerLaterCandidate) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	std::vector<int> picks(3, 0);
	const int trials = 4000;
	for (int seed = 1; seed <= trials; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite", "gel_mite", "gel_mite", "gel_mite"}, static_cast<std::uint64_t>(seed));
		battle.combatant(kFirstEnemy).hp = 0;
		battle.combatant(kFirstEnemy).statuses = {{kDefeatedStatus, 0}};
		for (int i = kFirstEnemy + 1; i < kFirstEnemy + 4; ++i) {
			battle.combatant(i).hp = 50;
		}
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::optional<Event> started = firstBy(battle.resolveRound(), EventType::ActionStarted, kFighter);
		if (started && started->target > kFirstEnemy) {
			++picks[static_cast<std::size_t>(started->target - kFirstEnemy - 1)];
		}
	}
	const double shares[] = {25, 25, 50};
	for (int k = 0; k < 3; ++k) {
		const double share = 100.0 * picks[static_cast<std::size_t>(k)] / trials;
		CHECK(share > shares[k] - 3 && share < shares[k] + 3);
	}
}

// A one-ally action on a fallen ally is not moved: it does nothing, and its cost is still paid.
TEST(aFallenAllyTargetIsKeptAndTheCostPaid) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	battle.combatant(kFighter).hp = 0;
	battle.combatant(kFighter).statuses = {{kDefeatedStatus, 0}};
	const int tp = battle.combatant(kMedic).tp;
	const int tonics = battle.inventory().at("tonic");
	battle.setCommand(kMedic, Command::ability("mend", kFighter));
	battle.setCommand(kScout, Command::item("tonic", kFighter));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK(ofType(events, EventType::Retargeted).empty());
	CHECK(ofType(events, EventType::Healed).empty());
	const std::optional<Event> mend = firstBy(events, EventType::ActionStarted, kMedic);
	CHECK(mend && mend->target == kFighter);
	CHECK_EQ(battle.combatant(kMedic).tp, tp - 3);
	CHECK_EQ(battle.inventory().at("tonic"), tonics - 1);
	CHECK(ofType(events, EventType::ItemHadNoEffect).empty());
	CHECK(!battle.combatant(kFighter).alive());
}

TEST(oneFallenAllyTargetsTheFallenOnly) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	for (int i : {kScout, kEngineer}) {
		battle.combatant(i).hp = 0;
		battle.combatant(i).statuses = {{kDefeatedStatus, 0}};
	}
	CHECK_EQ(battle.resolveTargets(kMedic, Targeting::OneFallenAlly, -1, true), (std::vector<int>{kScout}));
	CHECK_EQ(battle.resolveTargets(kMedic, Targeting::OneFallenAlly, kEngineer, true), (std::vector<int>{kEngineer}));
	CHECK(battle.resolveTargets(kMedic, Targeting::OneFallenAlly, kFighter, true).empty());
	battle.setCommand(kMedic, Command::ability("rekindle", -1));
	othersDefend(battle);
	const std::vector<Event> revived = ofType(battle.resolveRound(), EventType::Revived);
	CHECK(revived.size() == 1 && revived[0].target == kScout);
}

TEST(randomEnemyPicksOneLivingFoeAtExecution) {
	std::set<int> hit;
	for (std::uint64_t seed = 1; seed <= 30; ++seed) {
		Battle battle = makeBattle(data(), {"gel_mite", "rust_hound", "gel_mite"}, seed);
		battle.combatant(kFirstEnemy + 1).hp = 0;
		battle.combatant(kFirstEnemy + 1).statuses = {{kDefeatedStatus, 0}};
		battle.setCommand(kMystic, Command::item("jolt_pod", kFirstEnemy + 1));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<Event> damage = ofType(events, EventType::DamageDealt);
		CHECK_EQ(damage.size(), std::size_t(1));
		CHECK(ofType(events, EventType::Retargeted).empty());
		if (!damage.empty()) {
			CHECK(damage[0].target == kFirstEnemy || damage[0].target == kFirstEnemy + 2);
			hit.insert(damage[0].target);
		}
	}
	CHECK_EQ(hit.size(), std::size_t(2));
}
