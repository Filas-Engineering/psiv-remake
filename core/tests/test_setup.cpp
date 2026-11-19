#include "fixtures.h"
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
	db.battleRules.escape = {100, 0, 100, 100};
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
	db.enemies["brass_warden"].ai = {{"steam_vent", 1, {}}};
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

TEST(singleTargetMovesToTheNextFormationPosition) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	Battle battle = makeBattle(db, {"gel_mite", "gel_mite", "gel_mite"}, 1);
	battle.combatant(kFirstEnemy + 1).hp = 0;
	CHECK_EQ(battle.resolveTargets(kFighter, Targeting::OneEnemy, kFirstEnemy + 1, false), (std::vector<int>{kFirstEnemy + 2}));
	battle.combatant(kFirstEnemy + 2).hp = 0;
	CHECK_EQ(battle.resolveTargets(kFighter, Targeting::OneEnemy, kFirstEnemy + 2, false), (std::vector<int>{kFirstEnemy}));
	CHECK_EQ(battle.resolveTargets(kFirstEnemy, Targeting::OneEnemy, kMystic, false), (std::vector<int>{kMystic}));
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
