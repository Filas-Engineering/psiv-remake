#include "fixtures.h"
#include "test.h"

using namespace rules;
using namespace fixtures;

namespace {

AiRule rule(const char* ability, int weight, AiConditionKind kind = AiConditionKind::Always, int value = 0, const char* status = "") {
	AiRule r;
	r.ability = ability;
	r.weight = weight;
	r.condition.kind = kind;
	r.condition.value = value;
	r.condition.status = status;
	return r;
}

std::optional<Event> enemyAction(const std::vector<Event>& events, int enemy) {
	return firstBy(events, EventType::ActionStarted, enemy);
}

}

TEST(aiUsesConditionalRuleOnlyBelowHpThreshold) {
	Database db = data();
	db.enemies["rust_hound"].ai = {rule("venom_spit", 1, AiConditionKind::SelfHpBelow, 50)};
	Battle battle = makeBattle(db, {"rust_hound"}, 1);
	partyDefends(battle);
	const std::optional<Event> first = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(first && first->command == CommandKind::Attack && first->ability == "bite");

	battle.combatant(kFirstEnemy).hp = 10;
	partyDefends(battle);
	const std::optional<Event> second = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(second && second->ability == "venom_spit");
}

TEST(aiRevivesDefeatedAlly) {
	Database db = data();
	db.enemies["brass_warden"].ai = {rule("rouse", 1, AiConditionKind::AllyDefeated)};
	Battle battle = makeBattle(db, {"brass_warden", "gel_mite"}, 1);
	partyDefends(battle);
	const std::optional<Event> first = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(first && first->ability == "bite");

	battle.combatant(kFirstEnemy + 1).hp = 0;
	battle.combatant(kFirstEnemy + 1).statuses = {{kDefeatedStatus, 0}};
	partyDefends(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> second = enemyAction(events, kFirstEnemy);
	CHECK(second && second->ability == "rouse" && second->target == kFirstEnemy + 1);
	CHECK(!ofType(events, EventType::Revived).empty());
	CHECK(battle.combatant(kFirstEnemy + 1).alive());
	CHECK(!battle.combatant(kFirstEnemy + 1).hasStatus(kDefeatedStatus));
}

TEST(aiTargetsFoeWithStatus) {
	Database db = data();
	db.enemies["bog_wisp"].ai = {rule("numbing_sting", 1, AiConditionKind::FoeHasStatus, 0, "sleep")};
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		Battle battle = makeBattle(db, {"bog_wisp"}, seed);
		addStatus(battle, kMedic, "sleep", 3);
		partyDefends(battle);
		const std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
		CHECK(action && action->ability == "numbing_sting" && action->target == kMedic);
	}
}

TEST(aiHealsTheWeakestAlly) {
	Database db = data();
	db.enemies["bog_wisp"].ai = {rule("patch", 1, AiConditionKind::AllyHpBelow, 50)};
	Battle battle = makeBattle(db, {"bog_wisp", "gel_mite"}, 1);
	partyDefends(battle);
	const std::optional<Event> first = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(first && first->ability == "bite");

	battle.combatant(kFirstEnemy + 1).hp = 10;
	partyDefends(battle);
	const std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "patch" && action->target == kFirstEnemy + 1);
}

TEST(aiReactsToItsOwnStatus) {
	Database db = data();
	db.enemies["rust_hound"].ai = {rule("venom_spit", 1, AiConditionKind::SelfHasStatus, 0, "poison")};
	Battle battle = makeBattle(db, {"rust_hound"}, 2);
	addStatus(battle, kFirstEnemy, "poison", 3);
	partyDefends(battle);
	const std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "venom_spit");
}

TEST(aiSkipsRulesItCannotAfford) {
	Database db = data();
	db.enemies["rust_hound"].ai = {rule("venom_spit", 1)};
	Battle battle = makeBattle(db, {"rust_hound"}, 1);
	battle.combatant(kFirstEnemy).tp = 0;
	partyDefends(battle);
	const std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
	CHECK(action && action->ability == "bite");
}

TEST(aiWeightsShapeTheChoice) {
	Database db = data();
	db.enemies["rust_hound"].ai = {rule("bite", 3), rule("venom_spit", 1)};
	int bites = 0;
	int spits = 0;
	for (std::uint64_t seed = 1; seed <= 400; ++seed) {
		Battle battle = makeBattle(db, {"rust_hound"}, seed);
		partyDefends(battle);
		const std::optional<Event> action = enemyAction(battle.resolveRound(), kFirstEnemy);
		if (action && action->ability == "bite") {
			++bites;
		} else if (action && action->ability == "venom_spit") {
			++spits;
		}
	}
	CHECK_EQ(bites + spits, 400);
	CHECK(spits > 60);
	CHECK(bites > 2 * spits);
}
