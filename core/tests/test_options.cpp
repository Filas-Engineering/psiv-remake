#include "fixtures.h"
#include "rules/difficulty.h"
#include "rules/field.h"
#include "rules/formulas.h"
#include "rules/loader.h"
#include "rules/progression.h"
#include "rules/record.h"
#include "test.h"

#include <algorithm>
#include <set>
#include <tuple>

using namespace rules;
using namespace fixtures;

namespace {

void sturdy(Battle& battle, int index) {
	battle.combatant(index).stats.maxHp = 5000;
	battle.combatant(index).hp = 5000;
}

void sturdyParty(Battle& battle) {
	for (int i = 0; i < battle.partyCount(); ++i) {
		sturdy(battle, i);
	}
}

BattleSetup encounterSetup(const Database& db, const std::string& encounter, std::uint64_t seed) {
	BattleSetup setup = makeSetup(db, {"gel_mite"}, seed);
	CHECK_EQ(useEncounter(setup, db, encounter), std::string());
	return setup;
}

void replaceOnce(std::string& text, const std::string& from, const std::string& to) {
	const std::size_t at = text.find(from);
	if (at == std::string::npos) {
		::test::fail(__FILE__, __LINE__, "fixture text not found: " + from);
		return;
	}
	text.replace(at, from.size(), to);
}

int livingEnemies(const Battle& battle) {
	int living = 0;
	for (int i = battle.partyCount(); i < static_cast<int>(battle.combatants().size()); ++i) {
		living += battle.combatant(i).alive() ? 1 : 0;
	}
	return living;
}

}

// R5: rules.json ambush_opening enemy_first marks enemies after an Enemy first opening and keeps the mark until an
// ambushed override reads it; the default keeps the Party first reading, spent at the first turn.
TEST(theAmbushMarkCanFollowTheEnemyOpeningAndLastUntilRead) {
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "\"surprise_rounds\"", "\"ambush_opening\": \"enemy_first\",\n\t\"surprise_rounds\"");
	Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	Database db = *loaded.value;
	CHECK(db.battleRules.ambushOpening == Opening::EnemyFirst);
	CHECK(data().battleRules.ambushOpening == Opening::PartyFirst);
	EnemyDef& sentinel = db.enemies["aegis_sentinel"];
	sentinel.slots.assign(static_cast<std::size_t>(kAiSlots), "bite");
	sentinel.overrides.assign(2, AiOverride());
	sentinel.overrides[0].when = AiConditionKind::LastStanding;
	sentinel.overrides[0].ability = "bite";
	sentinel.overrides[1].when = AiConditionKind::Ambushed;
	sentinel.overrides[1].ability = "hamstring";

	BattleSetup partyFirst = makeSetup(db, {"aegis_sentinel"}, 12);
	partyFirst.opening = Opening::PartyFirst;
	CHECK(!makeBattle(db, partyFirst).combatant(kFirstEnemy).ambushed);

	BattleSetup enemyFirst = makeSetup(db, {"aegis_sentinel"}, 12);
	enemyFirst.opening = Opening::EnemyFirst;
	Battle battle = makeBattle(db, enemyFirst);
	sturdyParty(battle);
	CHECK(battle.combatant(kFirstEnemy).ambushed);
	partyDefends(battle);
	battle.resolveRound();
	CHECK(battle.combatant(kFirstEnemy).ambushed);
	battle.combatant(kFirstEnemy).hp = battle.combatant(kFirstEnemy).stats.maxHp;
	Database& shared = const_cast<Database&>(battle.database());
	shared.enemies["aegis_sentinel"].overrides.erase(shared.enemies["aegis_sentinel"].overrides.begin());
	partyDefends(battle);
	const std::optional<Event> action = firstBy(battle.resolveRound(), EventType::ActionStarted, kFirstEnemy);
	CHECK(action && action->ability == "hamstring");
	CHECK(!battle.combatant(kFirstEnemy).ambushed);

	replaceOnce(texts["rules.json"], "\"enemy_first\"", "\"sideways\"");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("unknown ambush opening 'sideways'") != std::string::npos);
}

// R6: merges_with names a partner of another kind; the pair becomes the merged form, which alone pays out.
TEST(aMergeCanTakeInAnEnemyOfAnotherKind) {
	const Database& db = data();
	Battle battle = makeBattle(db, encounterSetup(db, "sprites", 4));
	sturdyParty(battle);
	partyDefends(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> left = ofType(events, EventType::EnemyLeft);
	const std::vector<Event> changed = ofType(events, EventType::EnemyTransformed);
	CHECK(left.size() == 1 && battle.combatant(left[0].target).defId == "shale_sprite" && left[0].actor == kFirstEnemy);
	CHECK(changed.size() == 1 && changed[0].ids == std::vector<std::string>{"kiln_heart"});
	CHECK_EQ(livingEnemies(battle), 1);
	battle.combatant(kFirstEnemy).hp = 1;
	playToEnd(battle, 10);
	CHECK(battle.outcome() == Outcome::Victory);
	CHECK_EQ(battle.rewards().currency, db.enemies.at("kiln_heart").currency);
}

// R6: a transform needs no partner; the after_turns override makes the pupa molt at its third turn.
TEST(anEnemyTransformsAloneAfterItsTurns) {
	const Database& db = data();
	Battle battle = makeBattle(db, encounterSetup(db, "pupa", 3));
	sturdyParty(battle);
	sturdy(battle, kFirstEnemy);
	std::vector<int> transformedAt;
	for (int round = 1; round <= 4; ++round) {
		partyDefends(battle);
		if (!ofType(battle.resolveRound(), EventType::EnemyTransformed).empty()) {
			transformedAt.push_back(round);
		}
	}
	CHECK(transformedAt == std::vector<int>{3});
	CHECK_EQ(battle.combatant(kFirstEnemy).defId, std::string("moss_moth"));

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["enemies.json"], "\"transforms_into\": \"moss_moth\",", "");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("transforms, so the enemy needs transforms_into") != std::string::npos);
}

// R7: null entries keep formation places free; the jelly stands in the middle and splits into three droplets that
// fill its place and the free ones beside it. The droplets pay, the jelly does not.
TEST(freePlacesInAFormationTakeASplit) {
	const Database& db = data();
	Battle battle = makeBattle(db, encounterSetup(db, "jelly", 5));
	CHECK_EQ(static_cast<int>(battle.combatants().size()), kFirstEnemy + 1);
	CHECK_EQ(battle.formationSize(), 3);
	CHECK_EQ(battle.combatant(kFirstEnemy).position, 1);
	CHECK_EQ(battle.enemyAt(0), -1);
	CHECK_EQ(battle.enemyAt(1), kFirstEnemy);
	sturdyParty(battle);
	battle.combatant(kFirstEnemy).hp = battle.combatant(kFirstEnemy).stats.maxHp / 2;
	partyDefends(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> joined = ofType(events, EventType::EnemyJoined);
	CHECK_EQ(ofType(events, EventType::EnemyTransformed).size(), std::size_t(1));
	CHECK(joined.size() == 2 && joined[0].amount == 0 && joined[1].amount == 2 && joined[0].actor == kFirstEnemy);
	CHECK_EQ(livingEnemies(battle), 3);
	for (int i = kFirstEnemy; i < static_cast<int>(battle.combatants().size()); ++i) {
		CHECK_EQ(battle.combatant(i).defId, std::string("brine_droplet"));
		battle.combatant(i).hp = 1;
	}
	playToEnd(battle, 10);
	CHECK(battle.outcome() == Outcome::Victory);
	CHECK_EQ(battle.rewards().currency, 3 * db.enemies.at("brine_droplet").currency);

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["enemies.json"], "[null, \"brine_jelly\", null]", "[null, null]");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("encounter 'jelly'") != std::string::npos);
}

// R7: a macro's enemy position and a scripted end name formation places, so a free place before an enemy counts.
TEST(formationPositionsCountFreePlaces) {
	const Database& db = data();
	BattleSetup setup = encounterSetup(db, "jelly", 5);
	setup.scriptedEnd = {ScriptedEndKind::EnemyDefeated, 0, 0};
	CHECK(!Battle::create(db, setup, nullptr).ok());
	setup.scriptedEnd = {ScriptedEndKind::EnemyDefeated, 0, 1};
	Battle battle = makeBattle(db, setup);
	battle.combatant(kFirstEnemy).hp = 1;
	battle.combatant(kFirstEnemy).stats.maxHp = 1;
	Macro macro;
	macro.name = "aim";
	macro.steps.push_back({"fighter", CommandKind::Attack, "", {MacroTargetKind::Enemy, "", 1}});
	battle.applyMacro(macro);
	CHECK_EQ(battle.command(kFighter).target, kFirstEnemy);
}

// R8: a self_destruct effect runs the ability's effects, then the enemy leaves without paying unless data says so.
TEST(aSelfDestructingEnemyLeavesAfterItsBlast) {
	for (bool pays : {false, true}) {
		Database db = data();
		db.abilities["cinder_burst"].effects.back().rewards = pays;
		Battle battle = makeBattle(db, encounterSetup(db, "beetles", 8));
		sturdyParty(battle);
		CHECK_EQ(battle.combatant(kFirstEnemy + 1).position, 2);
		battle.combatant(kFirstEnemy).hp = 10;
		battle.combatant(kFirstEnemy + 1).hp = 10;
		partyDefends(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<Event> left = ofType(events, EventType::EnemyLeft);
		CHECK(left.size() == 2 && left[0].actor == -1);
		CHECK(!ofType(events, EventType::DamageDealt).empty());
		CHECK(battle.outcome() == Outcome::Victory);
		CHECK_EQ(battle.rewards().currency, pays ? 2 * db.enemies.at("fuse_beetle").currency : 0);
		CHECK(battle.combatant(kFirstEnemy).gone);
	}
}

// R9: a stat change with a fixed power moves the stat by that amount whatever the user's stats.
TEST(aFixedPowerSetsTheStatChange) {
	const Database& db = data();
	Battle battle = makeBattle(db, {"moss_moth"}, 2);
	sturdyParty(battle);
	battle.combatant(kFirstEnemy).stats.mental = 90;
	battle.setCommand(kFirstEnemy, Command::ability("dust_veil", -1));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	int lowered = 0;
	for (const Event& e : ofType(events, EventType::StatModified)) {
		CHECK_EQ(e.amount, std::max(1, battle.combatant(e.target).base(Stat::Agility) - 6) - battle.combatant(e.target).base(Stat::Agility));
		++lowered;
	}
	CHECK(lowered > 0);
}

// R10: an item with consumed false stays in the bag, in battle and in the field.
TEST(anItemThatIsNotConsumedStaysInTheBag) {
	const Database& db = data();
	Battle battle = makeBattle(db, {"gel_mite"}, 3, Inventory{{"dawn_bell", 1}});
	sturdyParty(battle);
	addStatus(battle, kMystic, "sleep", 0);
	battle.setCommand(kFighter, Command::item("dawn_bell", -1));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK(!battle.combatant(kMystic).hasStatus("sleep"));
	CHECK(ofType(events, EventType::ResourceSpent).empty());
	CHECK_EQ(battle.inventory().at("dawn_bell"), 1);
}

// R10: a piece with use_from_bag casts from the bag for any member, at the item rule's flat power, and stays there.
TEST(aBagPieceCastsItsUseAtItsOwnPower) {
	const Database& db = data();
	BattleSetup setup = makeSetup(db, {"gel_mite"}, 6);
	CHECK(!makeBattle(db, setup).setCommand(kFighter, Command::item("squall_fan", kFirstEnemy)).empty());
	setup.pieces["squall_fan"] = 1;
	Battle battle = makeBattle(db, setup);
	sturdyParty(battle);
	sturdy(battle, kFirstEnemy);
	CHECK(battle.setCommand(kFighter, Command::item("squall_fan", kFirstEnemy)).empty());
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> started = firstBy(events, EventType::ActionStarted, kFighter);
	CHECK(started && started->ability == "squall_fan" && started->command == CommandKind::Item);
	CHECK_EQ(ofType(events, EventType::DamageDealt).size(), std::size_t(1));
	CHECK(ofType(events, EventType::ResourceSpent).empty());
	CHECK(!makeBattle(db, setup).setCommand(kFighter, Command::item("storm_rod", kFirstEnemy)).empty());

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["equipment.json"], "\"use\": \"gust\", \"use_from_bag\": true", "\"use_from_bag\": true");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("use_from_bag needs a use") != std::string::npos);
}

// R11: a recipe whose part a character can learn only from events (teachable) or an object loads, and fires once known.
TEST(recipesCountAbilitiesTaughtByEventsAndObjects) {
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["abilities.json"], "\t\t{\"id\": \"fuse\",", "\t\t{\"id\": \"keen_edge\", \"name\": \"Keen Edge\", \"kind\": \"technique\", \"cost\": 1, \"target\": \"one_enemy\", \"effects\": [{\"type\": \"physical_damage\", \"power\": 4}]},\n"
		"\t\t{\"id\": \"edge_storm\", \"name\": \"Edge Storm\", \"kind\": \"combo\", \"target\": \"all_enemies\", \"effects\": [{\"type\": \"elemental_damage\", \"element\": \"gale\"}]},\n\t\t{\"id\": \"fuse\",");
	replaceOnce(texts["combos.json"], "\"combos\": [", "\"combos\": [\n\t\t{\"id\": \"edge_storm\", \"abilities\": [{\"ability\": \"keen_edge\", \"character\": \"fighter\"}, \"gust\"], \"result\": \"edge_storm\"},");
	std::map<std::string, std::string> untaught = texts;
	const Result<Database> refused = loadDatabaseFromTexts(untaught);
	CHECK(!refused.ok());
	replaceOnce(texts["party.json"], "\"teachable\": [\"hallow\"]", "\"teachable\": [\"hallow\", \"keen_edge\"]");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	const Database& db = *loaded.value;
	BattleSetup setup = makeSetup(db, {"gel_mite"}, 2);
	setup.party[kFighter].taught.push_back("keen_edge");
	setup.party[kFighter].tp = 6;
	setup.party[kScout].tp = 10;
	ComboLog log;
	Battle battle = makeBattle(db, setup, &log);
	sturdyParty(battle);
	sturdy(battle, kFirstEnemy);
	battle.combatant(kFighter).stats.agility = 200;
	battle.combatant(kScout).stats.agility = 199;
	battle.combatant(kFirstEnemy).stats.agility = 0;
	battle.setCommand(kFighter, Command::ability("keen_edge", kFirstEnemy));
	CHECK(battle.setCommand(kScout, Command::ability("gust", kFirstEnemy)).empty());
	othersDefend(battle);
	const std::vector<Event> triggered = ofType(battle.resolveRound(), EventType::ComboTriggered);
	CHECK(triggered.size() == 1 && triggered[0].combo == "edge_storm");
}

// R12: armor scales the wearer's affinity per element; an encounter can drop a piece, which BattleEnded lists too.
TEST(armorAdjustsAffinitiesAndEncountersDropPieces) {
	Database db = data();
	PartyMember member = makePartyMember(db, *db.character("fighter"), 1);
	member.equipment[EquipSlot::Body] = "ember_mail";
	const Combatant worn = memberCombatant(db, member);
	CHECK_EQ(worn.affinity(Element::Heat), 50);
	CHECK_EQ(worn.affinity(Element::Chill), 150);
	CHECK_EQ(worn.affinity(Element::Shock), 100);

	db.encounters["beetles"].drop.rate = 127;
	Battle battle = makeBattle(db, encounterSetup(db, "beetles", 8));
	sturdyParty(battle);
	for (int i = kFirstEnemy; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).hp = 1;
	}
	const std::vector<Event> events = playToEnd(battle, 10);
	CHECK(battle.outcome() == Outcome::Victory);
	CHECK_EQ(battle.rewards().equipment.at("ember_mail"), 1);
	CHECK(battle.rewards().items.empty());
	const std::vector<Event> ended = ofType(events, EventType::BattleEnded);
	CHECK(ended.size() == 1 && ended[0].items.at("ember_mail") == 1);
}

// Records keep free formation places and bag pieces, so such a battle replays exactly.
TEST(recordsReplayFreePlacesAndBagPieces) {
	const Database& db = data();
	BattleSetup setup = encounterSetup(db, "jelly", 9);
	setup.pieces["squall_fan"] = 1;
	ComboLog log;
	Battle battle = makeBattle(db, setup, &log);
	battle.setCommand(kFighter, Command::item("squall_fan", kFirstEnemy));
	std::vector<Event> played = battle.resolveRound();
	for (int round = 0; round < 6 && battle.outcome() == Outcome::Ongoing; ++round) {
		const std::vector<Event> more = battle.resolveRound();
		played.insert(played.end(), more.begin(), more.end());
	}
	const std::string text = serializeRecord(battle.record());
	CHECK(text.find("battle_record 8\n") == 0);
	const Result<BattleRecord> parsed = parseRecord(text);
	CHECK(parsed.ok());
	CHECK(parsed.value->setup.enemies.size() == 3 && parsed.value->setup.enemies[0].enemy.empty());
	CHECK_EQ(parsed.value->setup.pieces.at("squall_fan"), 1);
	ComboLog replayLog;
	std::vector<Event> replayed;
	CHECK(replayBattle(db, *parsed.value, replayLog, replayed).ok());
	CHECK_EQ(serializeEvents(replayed), serializeEvents(played));
}

// R13: weak, resisted and immune compare an affinity with the divisor, so an element the data leaves out is normal
// at any divisor, and a plain hit does the same damage at 100 and 200.
TEST(effectivenessComparesWithTheDivisor) {
	std::vector<int> plainHits;
	for (int divisor : {100, 200}) {
		Database db = data();
		db.battleRules.affinityDivisor = divisor;
		for (const auto& [affinity, expected] : std::vector<std::pair<int, Effectiveness>>{{-1, Effectiveness::Normal}, {divisor * 3 / 2, Effectiveness::Weak},
				{divisor / 2, Effectiveness::Resisted}, {0, Effectiveness::Immune}}) {
			db.enemies["gel_mite"].affinities.erase(Element::Shock);
			if (affinity >= 0) {
				db.enemies["gel_mite"].affinities[Element::Shock] = affinity;
			}
			Battle battle = makeBattle(db, {"gel_mite"}, 4);
			sturdyParty(battle);
			sturdy(battle, kFirstEnemy);
			battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
			othersDefend(battle);
			const std::vector<Event> hits = ofType(battle.resolveRound(), EventType::DamageDealt);
			CHECK(hits.size() == 1 && hits[0].effectiveness == expected);
		}
		Battle battle = makeBattle(db, {"gel_mite"}, 4);
		sturdy(battle, kFirstEnemy);
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::optional<Event> hit = firstBy(battle.resolveRound(), EventType::DamageDealt, kFighter);
		CHECK(hit && hit->effectiveness == Effectiveness::Normal);
		plainHits.push_back(hit ? hit->amount : -1);
	}
	CHECK(plainHits.size() == 2 && plainHits[0] == plainHits[1]);
}

// R106: rules.json effectiveness_baseline, a declared element's normal and element_baselines move only what weak,
// resisted and immune compare with; damage still divides by the divisor.
TEST(effectivenessBaselineRelabelsWithoutChangingDamage) {
	std::map<std::string, std::string> plainTexts = dataTexts();
	replaceOnce(plainTexts["rules.json"], "\"elements\": [\"tide\"]", "\"affinity_divisor\": 200, \"elements\": [\"tide\"]");
	std::map<std::string, std::string> baselineTexts = plainTexts;
	replaceOnce(baselineTexts["rules.json"], "\"affinity_divisor\": 200,", "\"affinity_divisor\": 200, \"effectiveness_baseline\": 100,");
	std::map<std::string, std::string> overrideTexts = baselineTexts;
	replaceOnce(overrideTexts["rules.json"], "\"elements\": [\"tide\"]", "\"elements\": [{\"id\": \"tide\", \"normal\": 150}], \"element_baselines\": {\"shock\": 300}");
	const Result<Database> plain = loadDatabaseFromTexts(plainTexts);
	const Result<Database> baseline = loadDatabaseFromTexts(baselineTexts);
	const Result<Database> overrides = loadDatabaseFromTexts(overrideTexts);
	CHECK(plain.ok() && baseline.ok() && overrides.ok());
	if (!plain.ok() || !baseline.ok() || !overrides.ok()) {
		return;
	}
	CHECK((overrides.value->battleRules.elements == std::vector<std::string>{"tide"}));
	auto spark = [](Database db, const Element& element, int affinity) {
		db.abilities["spark"].effects.front().element = element;
		db.enemies["gel_mite"].affinities[element] = affinity;
		Battle battle = makeBattle(db, {"gel_mite"}, 4);
		sturdyParty(battle);
		sturdy(battle, kFirstEnemy);
		battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
		othersDefend(battle);
		const std::optional<Event> hit = firstBy(battle.resolveRound(), EventType::DamageDealt, kMystic);
		return hit ? std::make_pair(hit->effectiveness, hit->amount) : std::make_pair(Effectiveness::Normal, -1);
	};
	for (const auto& [affinity, label, plainLabel] : std::vector<std::tuple<int, Effectiveness, Effectiveness>>{{100, Effectiveness::Normal, Effectiveness::Resisted},
			{200, Effectiveness::Weak, Effectiveness::Normal}, {50, Effectiveness::Resisted, Effectiveness::Resisted}, {0, Effectiveness::Immune, Effectiveness::Immune}}) {
		const auto before = spark(*plain.value, Element::Shock, affinity);
		const auto after = spark(*baseline.value, Element::Shock, affinity);
		CHECK(before.first == plainLabel && after.first == label);
		CHECK(after.second >= 0 && after.second == before.second);
	}
	CHECK(spark(*overrides.value, Element::Shock, 200).first == Effectiveness::Resisted);
	CHECK(spark(*overrides.value, Element::Shock, 300).first == Effectiveness::Normal);
	CHECK(spark(*overrides.value, Element("tide"), 100).first == Effectiveness::Resisted);
	CHECK(spark(*overrides.value, Element("tide"), 150).first == Effectiveness::Normal);
	CHECK(spark(*overrides.value, Element("tide"), 200).first == Effectiveness::Weak);
	CHECK(spark(*overrides.value, Element::Heat, 100).first == Effectiveness::Normal);
	CHECK_EQ(spark(*overrides.value, Element::Shock, 200).second, spark(*plain.value, Element::Shock, 200).second);

	for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{{"\"effectiveness_baseline\": 100", "\"effectiveness_baseline\": 0"},
			{"\"effectiveness_baseline\": 100", "\"effectiveness_baseline\": 801"}, {"\"normal\": 150", "\"normal\": 0"}, {"\"normal\": 150", "\"normal\": \"high\""},
			{"\"normal\": 150", "\"normal\": 150, \"weak\": 300"}, {"{\"id\": \"tide\", \"normal\": 150}", "\"tide\", {\"normal\": 150}"},
			{"{\"id\": \"tide\", \"normal\": 150}", "\"tide\", {\"id\": \"heat\", \"normal\": 150}"}, {"{\"id\": \"tide\", \"normal\": 150}", "\"tide\", 7"},
			{"{\"shock\": 300}", "{\"tide\": 300}"}, {"{\"shock\": 300}", "{\"none\": 300}"}, {"{\"shock\": 300}", "{\"lava\": 300}"},
			{"{\"shock\": 300}", "{\"shock\": 801}"}, {"{\"shock\": 300}", "[\"shock\"]"}}) {
		std::map<std::string, std::string> bad = overrideTexts;
		replaceOnce(bad["rules.json"], from, to);
		CHECK(!loadDatabaseFromTexts(bad).ok());
	}
}

// R14: attack_stat names the user's stat an ability attacks with.
TEST(anAbilityCanAttackWithAnotherStat) {
	Database db = data();
	db.abilities["spark"].attackStat = Stat::Defense;
	Battle battle = makeBattle(db, {"gel_mite"}, 4);
	sturdyParty(battle);
	sturdy(battle, kFirstEnemy);
	battle.combatant(kMystic).stats.defense = 300;
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	othersDefend(battle);
	const std::optional<Event> hit = firstBy(battle.resolveRound(), EventType::DamageDealt, kMystic);
	const Combatant& mite = battle.combatant(kFirstEnemy);
	const int power = db.abilities.at("spark").effects.front().power;
	CHECK(hit && hit->amount >= formulas::damage(300, mite.effective(Stat::Mental), power, mite.affinity(Element::Shock), formulas::kSpreadMin));

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["abilities.json"], "{\"id\": \"fuse\",", "{\"id\": \"fuse\", \"attack_stat\": \"luck\",");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("luck") != std::string::npos);
}

// R15: stat changes can move magic defense, which elemental damage then subtracts under the magic_defense switch.
TEST(statChangesReachMagicDefense) {
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["abilities.json"], "{\"type\": \"modify_stat\", \"stat\": \"agility\", \"direction\": \"down\", \"power\": 6}",
		"{\"type\": \"modify_stat\", \"stat\": \"magic_defense\", \"direction\": \"up\", \"power\": 9}");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	Database db = *loaded.value;
	db.abilities["dust_veil"].targeting = Targeting::Self;
	Battle battle = makeBattle(db, {"moss_moth"}, 2);
	sturdyParty(battle);
	battle.setCommand(kFirstEnemy, Command::ability("dust_veil", kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> changed = ofType(battle.resolveRound(), EventType::StatModified);
	CHECK(changed.size() == 1 && changed[0].stat == Stat::MagicDefense && changed[0].amount == 9);
	CHECK_EQ(battle.combatant(kFirstEnemy).effective(Stat::MagicDefense), battle.combatant(kFirstEnemy).base(Stat::MagicDefense) + 9);
}

// R16: an enemy's basic attack can carry an element, which the target's affinity then scales and Defend does not.
TEST(anEnemyBasicAttackCanHaveAnElement) {
	Database db = data();
	db.enemies["gel_mite"].attackElement = Element::Heat;
	for (auto& [id, character] : db.characters) {
		character.affinities[Element::Heat] = 0;
	}
	Battle battle = makeBattle(db, {"gel_mite"}, 5);
	sturdyParty(battle);
	partyDefends(battle);
	bool seen = false;
	for (const Event& e : ofType(battle.resolveRound(), EventType::DamageDealt)) {
		if (e.actor == kFirstEnemy) {
			CHECK(e.element == Element::Heat && e.effectiveness == Effectiveness::Immune && e.amount == 0);
			seen = true;
		}
	}
	CHECK(seen);

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["enemies.json"], "\"id\": \"gel_mite\",", "\"id\": \"gel_mite\", \"attack_element\": \"magma\",");
	CHECK(!loadDatabaseFromTexts(texts).ok());
}

// R17: rules.json declares more elements, usable wherever an element is; they list after the built-in ones.
TEST(dataDeclaresMoreElements) {
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "[\"tide\"]", "[\"tide\", \"acid\", \"void\"]");
	replaceOnce(texts["abilities.json"], "{\"type\": \"elemental_damage\", \"power\": 30, \"element\": \"heat\"},\n\t\t\t{\"type\": \"self_destruct\"}",
		"{\"type\": \"elemental_damage\", \"power\": 30, \"element\": \"acid\"},\n\t\t\t{\"type\": \"self_destruct\"}");
	replaceOnce(texts["equipment.json"], "\"affinities\": {\"heat\": 50, \"chill\": 150}", "\"affinities\": {\"heat\": 50, \"acid\": 0}");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	if (!loaded.ok()) {
		return;
	}
	const Database& db = *loaded.value;
	CHECK((db.elementIds() == std::vector<std::string>{"heat", "chill", "shock", "gale", "holy", "tide", "acid", "void"}));
	CHECK_EQ(db.elementIndex(Element("acid")), 7);
	CHECK_EQ(db.elementIndex(Element::None), 0);
	CHECK_EQ(db.equipment.at("ember_mail").affinities.at(Element("acid")), 0);

	Battle battle = makeBattle(db, encounterSetup(db, "beetles", 8));
	sturdyParty(battle);
	battle.combatant(kFirstEnemy).hp = 10;
	partyDefends(battle);
	bool acid = false;
	for (const Event& e : ofType(battle.resolveRound(), EventType::DamageDealt)) {
		acid = acid || (e.element == Element("acid") && e.effectiveness == Effectiveness::Normal);
	}
	CHECK(acid);

	std::map<std::string, std::string> unknown = texts;
	replaceOnce(unknown["equipment.json"], "\"acid\": 0", "\"lava\": 0");
	const Result<Database> refused = loadDatabaseFromTexts(unknown);
	CHECK(!refused.ok() && refused.error.find("lava") != std::string::npos);
	std::map<std::string, std::string> clash = texts;
	replaceOnce(clash["rules.json"], "\"acid\", \"void\"]", "\"acid\", \"heat\"]");
	const Result<Database> builtIn = loadDatabaseFromTexts(clash);
	CHECK(!builtIn.ok() && builtIn.error.find("element 'heat'") != std::string::npos);
}

namespace {

// The fighter's basic attack on a sturdy gel mite that defends or not: whether it took `affinity` and the label.
bool fighterHitsAt(const Database& db, bool defending, int affinity, Effectiveness label) {
	Battle battle = makeBattle(db, {"gel_mite"}, 3);
	sturdy(battle, kFirstEnemy);
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	battle.setCommand(kFirstEnemy, defending ? Command::defend() : Command::attack(kEngineer));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> hit = firstBy(events, EventType::DamageDealt, kFighter);
	if (!hit) {
		return false;
	}
	const bool critical = std::any_of(events.begin(), events.end(), [](const Event& e) { return e.type == EventType::Critical; });
	const int strength = battle.combatant(kFighter).stats.strength;
	const int bonus = critical ? formulas::criticalBonus(strength) : 0;
	const int defense = battle.combatant(kFirstEnemy).effective(Stat::Defense);
	const int divisor = db.battleRules.affinityDivisor;
	return hit->effectiveness == label && hit->amount >= formulas::damage(strength, defense, bonus, affinity, formulas::kSpreadMin, divisor) &&
		hit->amount <= formulas::damage(strength, defense, bonus, affinity, formulas::kSpreadMax, divisor);
}

Database strongFighter() {
	Database db = data();
	db.characters["fighter"].base.strength = 200;
	db.characters["fighter"].base.dexterity = 500;
	return db;
}

}

// R29: rules.json `neutral_affinity` is what a hit without an element takes, from either side, so 100 at a divisor of
// 200 halves plain hits; it is never labeled, and leaving it out keeps the divisor.
TEST(aHitWithoutAnElementTakesTheNeutralAffinity) {
	Database db = strongFighter();
	db.battleRules.affinityDivisor = 200;
	CHECK(fighterHitsAt(db, false, 200, Effectiveness::Normal));
	db.battleRules.elementlessAffinity = 100;
	CHECK(fighterHitsAt(db, false, 100, Effectiveness::Normal));
	CHECK(fighterHitsAt(db, true, 50, Effectiveness::Normal));

	db.enemies["gel_mite"].stats.strength = 300;
	db.enemies["gel_mite"].stats.dexterity = 500;
	Battle battle = makeBattle(db, {"gel_mite"}, 4);
	sturdyParty(battle);
	sturdy(battle, kFirstEnemy);
	for (int i = 0; i < battle.partyCount(); ++i) {
		battle.setCommand(i, Command::ability("rally", -1));
	}
	const std::vector<Event> events = battle.resolveRound();
	const std::optional<Event> bite = firstBy(events, EventType::DamageDealt, kFirstEnemy);
	CHECK(bite.has_value());
	if (bite) {
		const bool critical = std::any_of(events.begin(), events.end(), [](const Event& e) { return e.type == EventType::Critical && e.actor == kFirstEnemy; });
		const int bonus = critical ? formulas::criticalBonus(300) : 0;
		const int defense = battle.combatant(bite->target).effective(Stat::Defense);
		CHECK(bite->effectiveness == Effectiveness::Normal && bite->element == Element::None);
		CHECK(bite->amount >= formulas::damage(300, defense, bonus, 100, formulas::kSpreadMin, 200));
		CHECK(bite->amount <= formulas::damage(300, defense, bonus, 100, formulas::kSpreadMax, 200));
	}

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "\"elements\"", "\"affinity_divisor\": 200, \"neutral_affinity\": 100, \"elements\"");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->battleRules.elementlessHitAffinity() == 100 && loaded.value->battleRules.defendedAffinity() == 50);
	CHECK_EQ(data().battleRules.elementlessHitAffinity(), 100);
	for (const char* bad : {"\"neutral_affinity\": 0,", "\"neutral_affinity\": 801,"}) {
		std::map<std::string, std::string> refused = dataTexts();
		replaceOnce(refused["rules.json"], "\"elements\"", std::string("\"affinity_divisor\": 200, ") + bad + " \"elements\"");
		CHECK(!loadDatabaseFromTexts(refused).ok());
	}
}

// R30: a physical_damage effect may name an element, which wins over the weapon's: the target's affinity for it, its
// label, and Defend leaves it alone.
TEST(aPhysicalAbilityCanNameItsElement) {
	Database db = strongFighter();
	db.abilities["strike"].effects[0].element = Element::Heat;
	db.equipment["copper_blade"].element = Element::Chill;
	CHECK(fighterHitsAt(db, false, 150, Effectiveness::Weak));
	CHECK(fighterHitsAt(db, true, 150, Effectiveness::Weak));

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["abilities.json"], "\"cost\": 4, \"target\": \"enemy_group\", \"effects\": [\n\t\t\t{\"type\": \"physical_damage\", \"power\": 0}",
		"\"cost\": 4, \"target\": \"enemy_group\", \"effects\": [\n\t\t\t{\"type\": \"physical_damage\", \"power\": 0, \"element\": \"tide\"}");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->abilities.at("cleave").effects[0].element == Element("tide"));
	replaceOnce(texts["abilities.json"], "\"element\": \"tide\"}", "\"element\": \"magma\"}");
	const Result<Database> refused = loadDatabaseFromTexts(texts);
	CHECK(!refused.ok() && refused.error.find("magma") != std::string::npos);
}

// R31: rules.json `defend_affinity` is a defender's affinity for a hit without an element; `defend_elemental` also holds
// an elemental hit's at most at it. The research says Defend does nothing against elements, which stays the default.
TEST(defendSetsTheAffinityForPlainHitsAndElementalOnesWhenAsked) {
	Database db = strongFighter();
	db.battleRules.defendAffinity = 10;
	CHECK(fighterHitsAt(db, true, 10, Effectiveness::Normal));
	CHECK(fighterHitsAt(db, false, 100, Effectiveness::Normal));
	db.abilities["strike"].effects[0].element = Element::Heat;
	CHECK(fighterHitsAt(db, true, 150, Effectiveness::Weak));
	db.battleRules.defendElemental = true;
	CHECK(fighterHitsAt(db, true, 10, Effectiveness::Resisted));
	CHECK(fighterHitsAt(db, false, 150, Effectiveness::Weak));
	db.enemies["gel_mite"].affinities[Element::Heat] = 0;
	CHECK(fighterHitsAt(db, true, 0, Effectiveness::Immune));

	Database spark = data();
	spark.battleRules.defendAffinity = 10;
	spark.battleRules.defendElemental = true;
	Battle battle = makeBattle(spark, {"gel_mite"}, 4);
	sturdy(battle, kFirstEnemy);
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	battle.setCommand(kFirstEnemy, Command::defend());
	othersDefend(battle);
	const std::optional<Event> hit = firstBy(battle.resolveRound(), EventType::DamageDealt, kMystic);
	CHECK(hit && hit->element == Element::Shock && hit->effectiveness == Effectiveness::Resisted);

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "\"elements\"", "\"defend_affinity\": 10, \"defend_elemental\": true, \"elements\"");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->battleRules.defendedAffinity() == 10 && loaded.value->battleRules.defendElemental);
	CHECK(!data().battleRules.defendElemental && data().battleRules.defendedAffinity() == 25);
	replaceOnce(texts["rules.json"], "\"defend_affinity\": 10", "\"defend_affinity\": -1");
	CHECK(!loadDatabaseFromTexts(texts).ok());
}

// R32: a transform effect may aim at another combatant: an item or ability on an enemy turns it into its
// transforms_into form, an enemy's one-ally transform goes to an ally that has one, and a target without one is unchanged.
TEST(aTransformCanTurnATargetIntoItsNextForm) {
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["items.json"], "\"items\": [", "\"items\": [\n\t\t{\"id\": \"molting_dust\", \"name\": \"Molting Dust\", \"target\": \"one_enemy\", \"effects\": [{\"type\": \"transform\"}]},");
	replaceOnce(texts["abilities.json"], "\"abilities\": [",
		"\"abilities\": [\n\t\t{\"id\": \"spur_molt\", \"name\": \"Spur Molt\", \"kind\": \"technique\", \"cost\": 0, \"target\": \"one_ally\", \"effects\": [{\"type\": \"transform\"}]},");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	if (!loaded.ok()) {
		return;
	}
	Database db = *loaded.value;

	Battle battle = makeBattle(db, {"gel_mite", "moss_pupa"}, 3, Inventory{{"molting_dust", 2}});
	sturdyParty(battle);
	const int pupa = kFirstEnemy + 1;
	battle.setCommand(kFighter, Command::item("molting_dust", kFirstEnemy));
	battle.setCommand(kMystic, Command::item("molting_dust", pupa));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> changed = ofType(events, EventType::EnemyTransformed);
	CHECK(changed.size() == 1 && changed[0].target == pupa);
	CHECK_EQ(battle.combatant(pupa).defId, std::string("moss_moth"));
	CHECK_EQ(battle.combatant(kFirstEnemy).defId, std::string("gel_mite"));
	CHECK_EQ(ofType(events, EventType::ItemHadNoEffect).size(), std::size_t(1));
	CHECK_EQ(battle.inventory().at("molting_dust"), 1);

	db.enemies["gel_mite"].abilities.push_back("spur_molt");
	db.enemies["gel_mite"].slots.assign(kAiSlots, "spur_molt");
	Battle spur = makeBattle(db, {"gel_mite", "moss_pupa"}, 3);
	sturdyParty(spur);
	partyDefends(spur);
	const std::vector<Event> spurred = ofType(spur.resolveRound(), EventType::EnemyTransformed);
	CHECK(spurred.size() == 1 && spurred[0].target == pupa);

	std::map<std::string, std::string> combo = texts;
	replaceOnce(combo["abilities.json"], "\"kind\": \"technique\", \"cost\": 0, \"target\": \"one_ally\", \"effects\": [{\"type\": \"transform\"}]",
		"\"kind\": \"combo\", \"target\": \"one_ally\", \"effects\": [{\"type\": \"transform\"}]");
	CHECK(!loadDatabaseFromTexts(combo).ok());
}

// R33: a character may carry its own experience and stat tables: totals per level rising, stats per level in place of
// base plus growth; past either table the curve and the growth go on.
TEST(aCharacterCanCarryItsOwnExperienceAndStatTables) {
	const std::string base = "\"base\": {\"hp\": 60, \"tp\": 6, \"strength\": 14, \"mental\": 4, \"agility\": 8, \"dexterity\": 10, \"defense\": 8, \"critical\": 4},";
	const std::string tables = "\"level_stats\": [{\"hp\": 50, \"tp\": 5, \"strength\": 10, \"mental\": 4, \"agility\": 8, \"dexterity\": 10, \"defense\": 8},"
		" {\"hp\": 70, \"tp\": 6, \"strength\": 13, \"mental\": 4, \"agility\": 9, \"dexterity\": 11, \"defense\": 9},"
		" {\"hp\": 75, \"tp\": 8, \"strength\": 17, \"mental\": 5, \"agility\": 9, \"dexterity\": 12, \"defense\": 11}],"
		" \"experience_table\": [10, 30, 70],";
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["party.json"], base, tables);
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	if (!loaded.ok()) {
		return;
	}
	const Database& db = *loaded.value;
	const CharacterDef& fighter = db.characters.at("fighter");
	CHECK_EQ(experienceToNextLevel(fighter, 1), 10);
	CHECK_EQ(experienceToNextLevel(fighter, 3), 40);
	CHECK_EQ(experienceToNextLevel(fighter, 4), formulas::experienceToNextLevel(4));
	CHECK_EQ(experienceToNextLevel(db.characters.at("mystic"), 1), formulas::experienceToNextLevel(1));
	CHECK(statsAtLevel(fighter, 2).maxHp == 70 && statsAtLevel(fighter, 2).level == 2);
	// Past the table: the last row plus the growth (strength 2, hit points 8) per level.
	CHECK(statsAtLevel(fighter, 5).strength == 21 && statsAtLevel(fighter, 5).maxHp == 91 && statsAtLevel(fighter, 5).level == 5);

	PartyMember member = makePartyMember(db, fighter, 1);
	CHECK_EQ(member.hp, 50 + db.equipment.at("padded_coat").bonus.maxHp + db.equipment.at("tin_cap").bonus.maxHp + db.equipment.at("copper_blade").bonus.maxHp);
	CHECK_EQ(grantExperience(member, db, 9).levelsGained, 0);
	CHECK_EQ(grantExperience(member, db, 1).levelsGained, 1);
	CHECK(grantExperience(member, db, 60).levelsGained == 2 && member.level == 4 && member.experience == 0);
	CHECK_EQ(makePartyMember(db, fighter, 3).hp, memberStats(db, makePartyMember(db, fighter, 3)).maxHp);

	for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
			{"[10, 30, 70]", "[10, 30, 30]"},
			{"\"level_stats\": [", base + " \"level_stats\": ["},
			{"[{\"hp\": 50,", "[{\"level\": 1, \"hp\": 50,"},
			{"\"experience_table\": [10, 30, 70]", "\"experience_table\": []"}}) {
		std::map<std::string, std::string> bad = texts;
		replaceOnce(bad["party.json"], from, to);
		CHECK(!loadDatabaseFromTexts(bad).ok());
	}
}

namespace {

AbilityDef transformingAbility(const std::string& id, Targeting targeting) {
	AbilityDef ability;
	ability.id = id;
	ability.name = id;
	ability.kind = AbilityKind::Technique;
	ability.targeting = targeting;
	Effect effect;
	effect.kind = EffectKind::Transform;
	ability.effects = {effect};
	return ability;
}

}

// R37: a piece's use may transform, aimed by the ability's own targeting, worn or cast from the bag; a use that
// transforms its user is refused, since a member has no form to take.
TEST(aPieceUseCanTransformItsTarget) {
	Database db = data();
	db.abilities["hasten_molt"] = transformingAbility("hasten_molt", Targeting::OneEnemy);
	db.equipment["storm_rod"].use = "hasten_molt";
	db.equipment["squall_fan"].use = "hasten_molt";
	const int pupa = kFirstEnemy + 1;
	for (bool fromBag : {false, true}) {
		BattleSetup setup = makeSetup(db, {"gel_mite", "moss_pupa"}, 3);
		const int user = fromBag ? kFighter : kMystic;
		if (fromBag) {
			setup.pieces["squall_fan"] = 1;
		} else {
			setup.party[kMystic].equipment[EquipSlot::Weapon] = "storm_rod";
		}
		Battle battle = makeBattle(db, setup);
		sturdyParty(battle);
		CHECK(battle.setCommand(user, Command::item(fromBag ? "squall_fan" : "storm_rod", pupa)).empty());
		othersDefend(battle);
		const std::vector<Event> changed = ofType(battle.resolveRound(), EventType::EnemyTransformed);
		CHECK(changed.size() == 1 && changed[0].target == pupa);
		CHECK_EQ(battle.combatant(pupa).defId, std::string("moss_moth"));
	}

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["equipment.json"], "\"use\": \"gust\", \"stats\": {\"mental\": 2}", "\"use\": \"molt\", \"stats\": {\"mental\": 2}");
	const Result<Database> refused = loadDatabaseFromTexts(texts);
	CHECK(!refused.ok() && refused.error.find("use 'molt' transforms its user") != std::string::npos);
	replaceOnce(texts["abilities.json"], "\"id\": \"molt\", \"name\": \"Molt\", \"kind\": \"technique\", \"cost\": 0, \"target\": \"self\"",
		"\"id\": \"molt\", \"name\": \"Molt\", \"kind\": \"technique\", \"cost\": 0, \"target\": \"one_enemy\"");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->equipment.at("storm_rod").use == "molt");
}

namespace {

AiCondition leaf(AiConditionKind kind) {
	AiCondition condition;
	condition.when = kind;
	return condition;
}

AiCondition hounds(int count, AiCount compare) {
	AiCondition condition = leaf(AiConditionKind::AllyPresent);
	condition.enemy = "rust_hound";
	condition.count = count;
	condition.compare = compare;
	return condition;
}

// The lead hound's action in one round, with every slot its bite and the one override given.
std::string leadHoundAction(const Database& base, const AiOverride& entry, int othersStanding, bool halfHp, bool hitFirst) {
	Database db = base;
	EnemyDef& hound = db.enemies["rust_hound"];
	hound.slots.assign(static_cast<std::size_t>(kAiSlots), "bite");
	hound.overrides = {entry};
	Battle battle = makeBattle(db, {"rust_hound", "rust_hound", "rust_hound"}, 5);
	sturdyParty(battle);
	for (int i = othersStanding; i < 2; ++i) {
		battle.combatant(kFirstEnemy + 2 - i).hp = 0;
	}
	if (halfHp) {
		battle.combatant(kFirstEnemy).hp = battle.combatant(kFirstEnemy).stats.maxHp / 2;
	}
	battle.combatant(kFirstEnemy).hitByAttack = hitFirst;
	for (int i = kFirstEnemy + 1; i < static_cast<int>(battle.combatants().size()); ++i) {
		if (battle.combatant(i).alive()) {
			battle.setCommand(i, Command::defend());
		}
	}
	partyDefends(battle);
	const std::optional<Event> action = firstBy(battle.resolveRound(), EventType::ActionStarted, kFirstEnemy);
	const std::string id = action ? action->ability : std::string();
	return battle.combatant(kFirstEnemy).hitByAttack ? id + "+marked" : id;
}

}

// R38: an override's condition may be several held together (all) or any of several, nested, and ally_present may
// count the others of its kind, so a trigger can need an exact grouping. An any spends the marks of the parts that held.
TEST(overridesCanCombineConditionsAndCountAllies) {
	const Database& db = data();
	AiOverride exact;
	exact.ability = "venom_spit";
	exact.when = AiConditionKind::All;
	exact.parts = {hounds(2, AiCount::Exactly), leaf(AiConditionKind::SelfHpHalf)};
	CHECK_EQ(leadHoundAction(db, exact, 2, false, false), std::string("bite"));
	CHECK_EQ(leadHoundAction(db, exact, 2, true, false), std::string("venom_spit"));
	CHECK_EQ(leadHoundAction(db, exact, 1, true, false), std::string("bite"));
	exact.parts[0] = hounds(1, AiCount::AtMost);
	CHECK_EQ(leadHoundAction(db, exact, 1, true, false), std::string("venom_spit"));
	CHECK_EQ(leadHoundAction(db, exact, 2, true, false), std::string("bite"));
	exact.parts[0] = hounds(2, AiCount::AtLeast);
	CHECK_EQ(leadHoundAction(db, exact, 2, true, false), std::string("venom_spit"));

	AiOverride either;
	either.ability = "venom_spit";
	either.when = AiConditionKind::Any;
	either.parts = {leaf(AiConditionKind::LastStanding), leaf(AiConditionKind::HitByAttack)};
	CHECK_EQ(leadHoundAction(db, either, 2, false, false), std::string("bite"));
	CHECK_EQ(leadHoundAction(db, either, 0, false, false), std::string("venom_spit"));
	CHECK_EQ(leadHoundAction(db, either, 2, false, true), std::string("venom_spit"));
	CHECK_EQ(leadHoundAction(db, either, 0, false, true), std::string("venom_spit"));
	either.parts[0] = leaf(AiConditionKind::LastStanding);
	either.parts[1] = exact;
	CHECK_EQ(leadHoundAction(db, either, 2, true, false), std::string("venom_spit"));
	CHECK_EQ(leadHoundAction(db, either, 1, true, false), std::string("bite"));
	exact.parts[1] = leaf(AiConditionKind::HitByAttack);
	CHECK_EQ(leadHoundAction(db, exact, 2, false, true), std::string("venom_spit"));
	CHECK_EQ(leadHoundAction(db, exact, 1, false, true), std::string("bite+marked"));

	const std::string from = "{\"when\": \"self_hp_half\", \"ability\": \"venom_spit\"}";
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["enemies.json"], from, "{\"all\": [{\"when\": \"ally_present\", \"enemy\": \"gel_mite\", \"count\": 2, \"compare\": \"exactly\"},"
		" {\"any\": [{\"when\": \"self_hp_half\"}, {\"when\": \"last_standing\"}]}], \"ability\": \"venom_spit\"}");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	if (loaded.ok()) {
		const AiOverride& read = loaded.value->enemies.at("rust_hound").overrides.at(0);
		CHECK(read.when == AiConditionKind::All && read.parts.size() == 2);
		CHECK(read.parts[0].count == 2 && read.parts[0].compare == AiCount::Exactly && read.parts[0].enemy == "gel_mite");
		CHECK(read.parts[1].when == AiConditionKind::Any && read.parts[1].parts.size() == 2);
		CHECK(!read.parts[0].parts.size() && !read.parts[1].parts[0].count);
	}
	const std::pair<const char*, const char*> refusals[] = {
		{"{\"when\": \"self_hp_half\", \"all\": [{\"when\": \"self_hp_half\"}, {\"when\": \"last_standing\"}], \"ability\": \"venom_spit\"}", "exactly one of when, all and any"},
		{"{\"any\": [{\"when\": \"self_hp_half\"}], \"ability\": \"venom_spit\"}", "take 2 to 8 conditions"},
		{"{\"when\": \"all\", \"ability\": \"venom_spit\"}", "unknown AI condition 'all'"},
		{"{\"when\": \"self_hp_half\", \"count\": 1, \"ability\": \"venom_spit\"}", "only ally_present has count"},
		{"{\"when\": \"ally_present\", \"enemy\": \"gel_mite\", \"compare\": \"exactly\", \"ability\": \"venom_spit\"}", "compare needs a count"},
		{"{\"when\": \"ally_present\", \"enemy\": \"gel_mite\", \"count\": 0, \"ability\": \"venom_spit\"}", "count"},
		{"{\"when\": \"ally_present\", \"enemy\": \"gel_mite\", \"count\": 1, \"compare\": \"most\", \"ability\": \"venom_spit\"}", "unknown compare 'most'"},
		{"{\"all\": [{\"when\": \"ally_present\", \"enemy\": \"nobody\"}, {\"when\": \"last_standing\"}], \"ability\": \"venom_spit\"}", "nobody"},
		{"{\"all\": [{\"when\": \"self_hp_half\", \"ability\": \"bite\"}, {\"when\": \"last_standing\"}], \"ability\": \"venom_spit\"}", "unknown field 'ability'"},
		{"{\"all\": [{\"when\": \"self_hp_half\"}, {\"when\": \"last_standing\"}], \"turns\": 2, \"ability\": \"venom_spit\"}", "'turns' goes on one of them"},
	};
	for (const auto& [entry, message] : refusals) {
		std::map<std::string, std::string> bad = dataTexts();
		replaceOnce(bad["enemies.json"], from, entry);
		const Result<Database> refused = loadDatabaseFromTexts(bad);
		CHECK(!refused.ok() && refused.error.find(message) != std::string::npos);
	}
}

namespace {

// Enemies at the given formation places, an empty id a free place.
BattleSetup formation(const Database& db, const std::vector<std::string>& places, std::uint64_t seed) {
	BattleSetup setup = makeSetup(db, {}, seed);
	for (const std::string& id : places) {
		setup.enemies.push_back({id, id});
	}
	return setup;
}

}

// R39: remove_beside takes the enemies standing in the places right beside the user out of the battle, a free place
// stopping the reach on its side, paying only with rewards; with self_destruct the user goes too.
TEST(anEnemyCanSweepAwayItsNeighbors) {
	for (bool pays : {false, true}) {
		Database db = data();
		AbilityDef sweep;
		sweep.id = "sweeping_burst";
		sweep.name = "Sweeping Burst";
		sweep.kind = AbilityKind::Technique;
		sweep.targeting = Targeting::AllEnemies;
		Effect blast;
		blast.kind = EffectKind::PhysicalDamage;
		blast.power = 5;
		Effect remove;
		remove.kind = EffectKind::RemoveBeside;
		remove.rewards = pays;
		Effect leave;
		leave.kind = EffectKind::SelfDestruct;
		sweep.effects = {blast, remove, leave};
		db.abilities[sweep.id] = sweep;
		EnemyDef& beetle = db.enemies["fuse_beetle"];
		beetle.abilities.push_back(sweep.id);
		beetle.slots.assign(static_cast<std::size_t>(kAiSlots), sweep.id);
		beetle.overrides.clear();
		Battle battle = makeBattle(db, formation(db, {"gel_mite", "fuse_beetle", "", "rust_hound"}, 7));
		sturdyParty(battle);
		const int mite = kFirstEnemy;
		const int beetleIndex = kFirstEnemy + 1;
		const int hound = kFirstEnemy + 2;
		battle.setCommand(mite, Command::defend());
		battle.setCommand(hound, Command::defend());
		partyDefends(battle);
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<Event> left = ofType(events, EventType::EnemyLeft);
		CHECK(left.size() == 2 && left[0].target == mite && left[0].actor == -1 && left[1].target == beetleIndex);
		const std::vector<Event> messages = ofType(events, EventType::Message);
		CHECK(std::any_of(messages.begin(), messages.end(), [&](const Event& e) {
			return e.message == "battle.removed_beside" && e.actor == beetleIndex && e.target == mite && e.params == std::vector<std::string>{"gel_mite"};
		}));
		CHECK(!ofType(events, EventType::DamageDealt).empty());
		CHECK(battle.combatant(hound).alive() && !battle.combatant(hound).gone);
		battle.combatant(hound).hp = 1;
		playToEnd(battle, 10);
		CHECK(battle.outcome() == Outcome::Victory);
		CHECK_EQ(battle.rewards().currency, db.enemies.at("rust_hound").currency + (pays ? db.enemies.at("gel_mite").currency : 0));
	}

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["abilities.json"], "{\"type\": \"self_destruct\"}", "{\"type\": \"remove_beside\", \"rewards\": true},\n\t\t\t{\"type\": \"self_destruct\"}");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->abilities.at("cinder_burst").hasEffect(EffectKind::RemoveBeside));
	replaceOnce(texts["items.json"], "\"items\": [", "\"items\": [\n\t\t{\"id\": \"broom\", \"name\": \"Broom\", \"target\": \"self\", \"effects\": [{\"type\": \"remove_beside\"}]},");
	const Result<Database> refused = loadDatabaseFromTexts(texts);
	CHECK(!refused.ok() && refused.error.find("remove_beside") != std::string::npos);
}

// R40: use_power makes a piece's own power the attacking stat of its use in the normal formula, worn or from the bag.
TEST(aPieceCanCastItsUseWithItsOwnPower) {
	Database db = data();
	db.equipment["storm_rod"].usePower = 60;
	db.equipment["squall_fan"].usePower = 60;
	const int power = db.abilities.at("gust").effects[0].power;
	for (bool fromBag : {false, true}) {
		BattleSetup setup = makeSetup(db, {"gel_mite"}, 6);
		const int user = fromBag ? kFighter : kMystic;
		if (fromBag) {
			setup.pieces["squall_fan"] = 1;
		} else {
			setup.party[kMystic].equipment[EquipSlot::Weapon] = "storm_rod";
		}
		Battle battle = makeBattle(db, setup);
		sturdyParty(battle);
		sturdy(battle, kFirstEnemy);
		battle.setCommand(user, Command::item(fromBag ? "squall_fan" : "storm_rod", kFirstEnemy));
		othersDefend(battle);
		const std::optional<Event> hit = firstBy(battle.resolveRound(), EventType::DamageDealt, user);
		const int guard = battle.combatant(kFirstEnemy).effective(Stat::Mental);
		CHECK(hit && hit->amount >= formulas::damage(60, guard, power, 100, formulas::kSpreadMin, 100) && hit->amount <= formulas::damage(60, guard, power, 100, formulas::kSpreadMax, 100));
	}

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["equipment.json"], "\"use\": \"gust\", \"use_from_bag\": true", "\"use\": \"gust\", \"use_from_bag\": true, \"use_power\": 60");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->equipment.at("squall_fan").usePower == 60);
	CHECK(!data().equipment.at("squall_fan").usePower);
	replaceOnce(texts["equipment.json"], "\"use\": \"gust\", \"use_from_bag\": true, \"use_power\": 60", "\"use_power\": 60");
	const Result<Database> refused = loadDatabaseFromTexts(texts);
	CHECK(!refused.ok() && refused.error.find("use_power needs a use") != std::string::npos);
}

// R41: a piece's affinity may set the element outright ({"set": N}); a set wins over every piece's scaling, and the
// lowest set wins.
TEST(armorCanSetAnAffinityOutright) {
	Database db = data();
	PartyMember member = makePartyMember(db, *db.character("fighter"), 1);
	member.equipment[EquipSlot::Body] = "ember_mail";
	member.equipment[EquipSlot::Accessory] = "calm_charm";
	db.equipment["calm_charm"].setAffinities[Element::Heat] = 80;
	CHECK_EQ(memberCombatant(db, member).affinity(Element::Heat), 80);
	CHECK_EQ(memberCombatant(db, member).affinity(Element::Chill), 150);
	db.equipment["tin_cap"].setAffinities[Element::Heat] = 30;
	CHECK_EQ(memberCombatant(db, member).affinity(Element::Heat), 30);
	db.equipment["tin_cap"].setAffinities[Element::Heat] = 300;
	CHECK_EQ(memberCombatant(db, member).affinity(Element::Heat), 80);

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["equipment.json"], "\"affinities\": {\"heat\": 50, \"chill\": 150}", "\"affinities\": {\"heat\": {\"set\": 20}, \"chill\": 150}");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->equipment.at("ember_mail").setAffinities.at(Element::Heat) == 20 && !loaded.value->equipment.at("ember_mail").affinities.count(Element::Heat));
	for (const char* bad : {"{\"set\": 20, \"scale\": 50}", "{\"set\": 401}", "{}"}) {
		std::map<std::string, std::string> refused = texts;
		replaceOnce(refused["equipment.json"], "{\"set\": 20}", bad);
		CHECK(!loadDatabaseFromTexts(refused).ok());
	}
}

// R42: a damage effect's stat_percent scales the user's attacking stat and its power may be negative; a variance (per
// ability, else rules.json's damage_variance) replaces the original's spread with an even or bell-shaped spread around
// the mean damage. Without one the spread is the original's.
TEST(damageCanScaleTheStatTakeANegativePowerAndVary) {
	CHECK_EQ(formulas::variedDamage(50, 10, 5, 100, 1, 1, 100), 100);
	CHECK_EQ(formulas::variedDamage(50, 10, 5, 100, 90, 100, 100), 89);
	CHECK_EQ(formulas::variedDamage(50, 10, -60, 100, 1, 1, 100), 1);
	CHECK_EQ(formulas::variedDamage(50, 10, 5, 0, 1, 1, 100), 0);

	Database db = strongFighter();
	Effect& strike = db.abilities["strike"].effects[0];
	strike.statPermille = 500;
	strike.power = -10;
	auto fighterHit = [&](const Database& use, std::uint64_t seed, bool& critical) {
		Battle battle = makeBattle(use, {"gel_mite"}, seed);
		sturdy(battle, kFirstEnemy);
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		battle.setCommand(kFirstEnemy, Command::attack(kEngineer));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		critical = firstBy(events, EventType::Critical, kFighter).has_value();
		const std::optional<Event> hit = firstBy(events, EventType::DamageDealt, kFighter);
		return hit ? hit->amount : -1;
	};
	const Combatant fighter = memberCombatant(db, makePartyMember(db, *db.character("fighter"), 1));
	const int attack = fighter.stats.strength / 2;
	const int defense = db.enemies.at("gel_mite").stats.defense;
	bool critical = false;
	for (std::uint64_t seed = 1; seed <= 8; ++seed) {
		const int amount = fighterHit(db, seed, critical);
		const int bonus = -10 + (critical ? formulas::criticalBonus(attack) : 0);
		CHECK(amount >= formulas::damage(attack, defense, bonus, 100, formulas::kSpreadMin, 100) && amount <= formulas::damage(attack, defense, bonus, 100, formulas::kSpreadMax, 100));
	}

	db.battleRules.damageVariance = DamageVariance{0, VarianceShape::Uniform};
	for (std::uint64_t seed = 1; seed <= 4; ++seed) {
		const int amount = fighterHit(db, seed, critical);
		const int bonus = -10 + (critical ? formulas::criticalBonus(attack) : 0);
		CHECK_EQ(amount, formulas::variedDamage(attack, defense, bonus, 100, 1, 1, 100));
	}
	for (VarianceShape shape : {VarianceShape::Uniform, VarianceShape::Bell}) {
		db.battleRules.damageVariance = DamageVariance{50, VarianceShape::Uniform};
		db.abilities["strike"].variance = DamageVariance{20, shape};
		std::set<int> seen;
		for (std::uint64_t seed = 1; seed <= 40; ++seed) {
			const int amount = fighterHit(db, seed, critical);
			const int bonus = -10 + (critical ? formulas::criticalBonus(attack) : 0);
			CHECK(amount >= formulas::variedDamage(attack, defense, bonus, 100, 80, 100, 100) && amount <= formulas::variedDamage(attack, defense, bonus, 100, 120, 100, 100));
			seen.insert(amount);
		}
		CHECK(seen.size() > 3);
	}

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "\"elements\"", "\"damage_variance\": {\"percent\": 15, \"distribution\": \"bell\"}, \"elements\"");
	replaceOnce(texts["abilities.json"], "{\"type\": \"physical_damage\", \"power\": 11}", "{\"type\": \"physical_damage\", \"power\": -11, \"stat_percent\": 150}");
	replaceOnce(texts["abilities.json"], "\"id\": \"focus_shot\",", "\"id\": \"focus_shot\", \"variance\": {\"percent\": 5},");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	if (loaded.ok()) {
		const Database& read = *loaded.value;
		CHECK(read.battleRules.damageVariance && read.battleRules.damageVariance->percent == 15 && read.battleRules.damageVariance->shape == VarianceShape::Bell);
		const AbilityDef& shot = read.abilities.at("focus_shot");
		CHECK(shot.variance && shot.variance->percent == 5 && shot.variance->shape == VarianceShape::Uniform);
		CHECK(shot.effects[0].power == -11 && shot.effects[0].statPermille == 1500);
	}
	CHECK(!data().battleRules.damageVariance && !data().abilities.at("focus_shot").variance);
	const std::pair<std::string, std::string> refusals[] = {
		{"\"percent\": 5}", "\"percent\": 101}"},
		{"\"percent\": 5}", "\"percent\": 5, \"distribution\": \"normal\"}"},
		{"\"stat_percent\": 150}", "\"stat_percent\": 1001}"},
	};
	for (const auto& [from, to] : refusals) {
		std::map<std::string, std::string> bad = texts;
		replaceOnce(bad["abilities.json"], from, to);
		CHECK(!loadDatabaseFromTexts(bad).ok());
	}
	std::map<std::string, std::string> item = dataTexts();
	replaceOnce(item["items.json"], "\"items\": [", "\"items\": [\n\t\t{\"id\": \"sling_stone\", \"name\": \"Sling Stone\", \"target\": \"one_enemy\", \"effects\": [{\"type\": \"physical_damage\", \"power\": 9, \"stat_percent\": 50}]},");
	const Result<Database> flat = loadDatabaseFromTexts(item);
	CHECK(!flat.ok() && flat.error.find("no stat_percent") != std::string::npos);
	replaceOnce(item["items.json"], "\"power\": 9, \"stat_percent\": 50", "\"power\": -9");
	CHECK(!loadDatabaseFromTexts(item).ok());
}

// R43: element "weapon" on a physical effect takes the user's weapon element as the basic attack does: the right
// hand's, else the left hand's, else none.
TEST(aPhysicalSkillCanTakeTheWeaponsElement) {
	Database db = strongFighter();
	db.abilities["focus_shot"].effects[0].weaponElement = true;
	auto shot = [&](const std::string& right, const std::string& left) {
		BattleSetup setup = makeSetup(db, {"gel_mite"}, 3);
		setup.party[kFighter].equipment[EquipSlot::Weapon] = right;
		if (!left.empty()) {
			setup.party[kFighter].equipment[EquipSlot::LeftHand] = left;
		}
		Battle battle = makeBattle(db, setup);
		sturdy(battle, kFirstEnemy);
		battle.setCommand(kFighter, Command::ability("focus_shot", kFirstEnemy));
		othersDefend(battle);
		const std::optional<Event> hit = firstBy(battle.resolveRound(), EventType::DamageDealt, kFighter);
		return hit ? std::make_pair(hit->element, hit->effectiveness) : std::make_pair(Element("?"), Effectiveness::Normal);
	};
	CHECK(shot("copper_blade", "") == std::make_pair(Element::None, Effectiveness::Normal));
	db.equipment["copper_blade"].element = Element::Heat;
	db.equipment["gill_knife"].element = Element::Chill;
	CHECK(shot("copper_blade", "") == std::make_pair(Element::Heat, Effectiveness::Weak));
	CHECK(shot("copper_blade", "gill_knife") == std::make_pair(Element::Heat, Effectiveness::Weak));
	CHECK(shot("gill_knife", "copper_blade") == std::make_pair(Element::Chill, Effectiveness::Resisted));
	db.equipment["gill_knife"].element = Element::None;
	CHECK(shot("gill_knife", "copper_blade") == std::make_pair(Element::Heat, Effectiveness::Weak));

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["abilities.json"], "{\"type\": \"physical_damage\", \"power\": 11}", "{\"type\": \"physical_damage\", \"power\": 11, \"element\": \"weapon\"}");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->abilities.at("focus_shot").effects[0].weaponElement && loaded.value->abilities.at("focus_shot").effects[0].element == Element::None);
	std::map<std::string, std::string> item = dataTexts();
	replaceOnce(item["items.json"], "\"items\": [", "\"items\": [\n\t\t{\"id\": \"sling_stone\", \"name\": \"Sling Stone\", \"target\": \"one_enemy\", \"effects\": [{\"type\": \"physical_damage\", \"power\": 9, \"element\": \"weapon\"}]},");
	const Result<Database> refused = loadDatabaseFromTexts(item);
	CHECK(!refused.ok() && refused.error.find("element 'weapon'") != std::string::npos);
	std::map<std::string, std::string> declared = dataTexts();
	replaceOnce(declared["rules.json"], "\"tide\"", "\"tide\", \"weapon\"");
	CHECK(!loadDatabaseFromTexts(declared).ok());
}

// R44: rules.json defend_elements lists the elements Defend reaches, "none" for hits without one; defend_affinity and
// defend_elemental stay the shorthand for none alone and for every element.
TEST(defendCanReachNamedElementsOnly) {
	Database db = strongFighter();
	db.battleRules.defendAffinity = 10;
	db.battleRules.defendElements = std::vector<Element>{Element::Heat};
	CHECK(fighterHitsAt(db, true, 100, Effectiveness::Normal));
	db.abilities["strike"].effects[0].element = Element::Heat;
	CHECK(fighterHitsAt(db, true, 10, Effectiveness::Resisted));
	db.abilities["strike"].effects[0].element = Element::Chill;
	CHECK(fighterHitsAt(db, true, 50, Effectiveness::Resisted));
	db.battleRules.defendElements = std::vector<Element>{Element::None, Element::Chill};
	CHECK(fighterHitsAt(db, true, 10, Effectiveness::Resisted));
	db.abilities["strike"].effects[0].element = Element::None;
	CHECK(fighterHitsAt(db, true, 10, Effectiveness::Normal));
	db.battleRules.defendElements = std::vector<Element>{};
	CHECK(fighterHitsAt(db, true, 100, Effectiveness::Normal));

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "\"elements\"", "\"defend_elements\": [\"none\", \"tide\"], \"elements\"");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->battleRules.defendElements == std::vector<Element>({Element::None, Element("tide")}));
	CHECK(loaded.ok() && loaded.value->battleRules.defends(Element::None) && !loaded.value->battleRules.defends(Element::Heat));
	CHECK(!data().battleRules.defendElements && data().battleRules.defends(Element::None) && !data().battleRules.defends(Element::Heat));
	for (const char* bad : {"\"defend_elements\": [\"none\", \"none\"],", "\"defend_elements\": [\"magma\"],", "\"defend_elements\": [\"heat\"], \"defend_elemental\": true,"}) {
		std::map<std::string, std::string> refused = dataTexts();
		replaceOnce(refused["rules.json"], "\"elements\"", std::string(bad) + " \"elements\"");
		CHECK(!loadDatabaseFromTexts(refused).ok());
	}
}

// R48: a data macro's item step names a piece with "piece": true; the piece needs a battle use.
TEST(dataMacrosCanNamePieces) {
	std::map<std::string, std::string> texts = dataTexts();
	const std::string step = "{\"member\": \"engineer\", \"command\": \"item\", \"id\": \"tonic\", \"target\": {\"member\": \"mystic\"}}";
	std::map<std::string, std::string> fan = texts;
	replaceOnce(fan["party.json"], step, "{\"member\": \"engineer\", \"command\": \"item\", \"piece\": true, \"id\": \"squall_fan\", \"target\": {\"enemy\": 0}}");
	const Result<Database> loaded = loadDatabaseFromTexts(fan);
	CHECK(loaded.ok() && loaded.value->macro("regroup")->steps.back().piece);
	std::map<std::string, std::string> plain = texts;
	replaceOnce(plain["party.json"], step, "{\"member\": \"engineer\", \"command\": \"item\", \"piece\": true, \"id\": \"rivet_plate\"}");
	const Result<Database> noUse = loadDatabaseFromTexts(plain);
	CHECK(!noUse.ok() && noUse.error.find("piece 'rivet_plate' has no battle use") != std::string::npos);
	std::map<std::string, std::string> attack = texts;
	replaceOnce(attack["party.json"], step, "{\"member\": \"engineer\", \"command\": \"attack\", \"piece\": true}");
	const Result<Database> wrong = loadDatabaseFromTexts(attack);
	CHECK(!wrong.ok() && wrong.error.find("only an item step names a piece") != std::string::npos);
}

// R53: with defend_timing turn, Defend holds from the defender's turn; a hit before it lands in full.
TEST(defendCanStartAtTheDefendersTurn) {
	Database db = data();
	db.enemies["gel_mite"].stats.agility = 900;
	db.characters["fighter"].base.agility = 1;
	auto hit = [&db](bool fromTurn, CommandKind fighter) {
		db.battleRules.defendFromTurn = fromTurn;
		Battle battle = makeBattle(db, {"gel_mite"}, 7);
		sturdyParty(battle);
		sturdy(battle, kFirstEnemy);
		battle.setCommand(kFighter, fighter == CommandKind::Defend ? Command::defend() : Command::attack(kFirstEnemy));
		battle.setCommand(kFirstEnemy, Command::attack(kFighter));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		for (const Event& e : events) {
			if (e.type == EventType::DamageDealt && e.target == kFighter) {
				return e.amount;
			}
		}
		return -1;
	};
	const int open = hit(false, CommandKind::Attack);
	CHECK(open > 0);
	CHECK(hit(false, CommandKind::Defend) < open);
	CHECK_EQ(hit(true, CommandKind::Defend), open);

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "{", "{\"defend_timing\": \"later\",");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("defend_timing is 'later'; it takes round, turn") != std::string::npos);
}

// R54 to R56: the immune floor, the low-byte critical bonus and 16-bit wraparound of the attack times the spread.
TEST(damageFormulaOptionsChangeOnlyWhatTheyName) {
	CHECK_EQ(formulas::damage(50, 0, 0, 0, 64, 100), 0);
	CHECK_EQ(formulas::damage(50, 0, 0, 0, 64, 100, false, true), 1);
	CHECK_EQ(formulas::damage(50, 10, 3, 100, 70, 100, false, false), formulas::damage(50, 10, 3, 100, 70, 100));
	CHECK_EQ(formulas::criticalBonus(300), 75);
	CHECK_EQ(formulas::criticalBonusLowByte(300), 11);
	CHECK_EQ(formulas::criticalBonusLowByte(100), formulas::criticalBonus(100));
	// 600 x 120 = 72000 wraps to 6464, over 64 is 101, plus the attack 600.
	CHECK_EQ(formulas::damage(600, 0, 0, 100, 120, 100, true, false), 701);
	CHECK_EQ(formulas::damage(600, 0, 0, 100, 120, 100), 999);
	CHECK_EQ(formulas::damage(300, 0, 0, 100, 120, 100, true, false), formulas::damage(300, 0, 0, 100, 120, 100));

	Database db = data();
	db.battleRules.immuneMinDamage = true;
	db.enemies["gel_mite"].affinities[Element::Shock] = 0;
	Battle battle = makeBattle(db, {"gel_mite"}, 3);
	sturdyParty(battle);
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	othersDefend(battle);
	battle.setCommand(kFirstEnemy, Command::defend());
	const std::optional<Event> spark = firstBy(battle.resolveRound(), EventType::DamageDealt, kMystic);
	CHECK(spark && spark->amount == 1);

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "{", "{\"critical_bonus\": \"low_byte\", \"damage_wraparound\": true, \"immune_min_damage\": true,");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->battleRules.criticalLowByte && loaded.value->battleRules.damageWraparound && loaded.value->battleRules.immuneMinDamage);
}

// R57: an item heal with a stat_value rolls (P x spread / 64 + P + 2V) / 2, the technique heal with the power as the stat;
// its mean is about P + V. Without it the heal is the flat power.
TEST(anItemCanHealWithItsStatValueAndTheSpread) {
	Database db = data();
	auto heals = [&db](std::uint64_t seed) {
		Battle battle = makeBattle(db, {"gel_mite"}, seed);
		sturdyParty(battle);
		sturdy(battle, kFirstEnemy);
		battle.combatant(kFighter).hp = 1;
		battle.setCommand(kFighter, Command::item("tonic", kFighter));
		othersDefend(battle);
		battle.setCommand(kFirstEnemy, Command::defend());
		const std::optional<Event> healed = firstBy(battle.resolveRound(), EventType::Healed, kFighter);
		return healed ? healed->amount : -1;
	};
	CHECK_EQ(heals(1), 30);
	db.items["tonic"].effects.front().statValue = 10;
	long long total = 0;
	int lowest = 999;
	int highest = 0;
	const int runs = 400;
	for (int seed = 1; seed <= runs; ++seed) {
		const int amount = heals(static_cast<std::uint64_t>(seed));
		total += amount;
		lowest = std::min(lowest, amount);
		highest = std::max(highest, amount);
	}
	// The spread's step rounds down, so the mean sits a little under 40.
	CHECK(total * 10 / runs >= 385 && total * 10 / runs <= 400);
	CHECK(lowest >= formulas::healAmount(10, 30, formulas::kSpreadMin) && highest <= formulas::healAmount(10, 30, formulas::kSpreadMax) && lowest < highest);

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["abilities.json"], "{\"type\": \"heal\", \"kinds\": [\"living\"], \"power\": 28}", "{\"type\": \"heal\", \"kinds\": [\"living\"], \"power\": 28, \"stat_value\": 4}");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("stat_value is for items") != std::string::npos);
}

// R58: stat_permille scales the attacking stat in thousandths; stat_percent is the same value times 10.
TEST(statPermilleIsAFinerStatPercent) {
	std::map<std::string, std::string> texts = dataTexts();
	const std::string ram = "{\"type\": \"physical_damage\", \"power\": 3}";
	std::map<std::string, std::string> fine = texts;
	replaceOnce(fine["abilities.json"], ram, "{\"type\": \"physical_damage\", \"power\": 3, \"stat_permille\": 1255}");
	const Result<Database> loaded = loadDatabaseFromTexts(fine);
	CHECK(loaded.ok() && loaded.value->ability("ram")->effects.front().statPermille == 1255);
	std::map<std::string, std::string> percent = texts;
	replaceOnce(percent["abilities.json"], ram, "{\"type\": \"physical_damage\", \"power\": 3, \"stat_percent\": 125}");
	const Result<Database> coarse = loadDatabaseFromTexts(percent);
	CHECK(coarse.ok() && coarse.value->ability("ram")->effects.front().statPermille == 1250);
	std::map<std::string, std::string> both = texts;
	replaceOnce(both["abilities.json"], ram, "{\"type\": \"physical_damage\", \"power\": 3, \"stat_permille\": 1255, \"stat_percent\": 125}");
	const Result<Database> bad = loadDatabaseFromTexts(both);
	CHECK(!bad.ok() && bad.error.find("stat_percent and stat_permille") != std::string::npos);
	Database db = data();
	db.characters["fighter"].base.strength = 400;
	Effect& strike = db.abilities["strike"].effects.front();
	auto first = [&db](std::uint64_t seed) {
		Battle battle = makeBattle(db, {"gel_mite"}, seed);
		sturdy(battle, kFirstEnemy);
		battle.combatant(kFirstEnemy).stats.defense = 0;
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		const std::optional<Event> hit = firstBy(battle.resolveRound(), EventType::DamageDealt, kFighter);
		return hit ? hit->amount : -1;
	};
	strike.statPermille = 100;
	const int tenth = first(9);
	strike.statPermille = 200;
	CHECK(tenth > 0 && first(9) > tenth);
}

// R59: with escape_party_first_round an escape in the first round of a Party first opening always works.
TEST(escapeCanAlwaysWorkInAPartyFirstRound) {
	Database db = data();
	for (auto& [id, character] : db.characters) {
		character.base.agility = 1;
	}
	auto escapes = [&db](int round) {
		BattleSetup setup = makeSetup(db, {"gel_mite"}, 4);
		setup.opening = Opening::PartyFirst;
		setup.escapeRating = 230;
		Battle battle = makeBattle(db, setup);
		sturdyParty(battle);
		std::optional<Event> attempt;
		for (int r = 1; r <= round; ++r) {
			battle.setCommand(kFighter, r == round ? Command::escape() : Command::defend());
			othersDefend(battle);
			attempt = firstBy(battle.resolveRound(), EventType::EscapeAttempted, kFighter);
		}
		return attempt && attempt->succeeded;
	};
	CHECK(!escapes(1));
	db.battleRules.escapePartyFirstRound = true;
	CHECK(escapes(1));
	CHECK(!escapes(2));
}

// R60: target_weights replaces the 3:2:1-style draw for a party of that size, in 256ths by party order.
TEST(enemyTargetWeightsComeFromTheRules) {
	Database db = data();
	db.battleRules.targetWeights[5] = {0, 0, 256, 0, 0};
	for (std::uint64_t seed = 1; seed <= 40; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite", "gel_mite"}, seed);
		sturdyParty(battle);
		partyDefends(battle);
		for (const Event& e : battle.resolveRound()) {
			if (e.type == EventType::ActionStarted && e.actor >= kFirstEnemy) {
				CHECK_EQ(e.target, kScout);
			}
		}
	}
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "{", "{\"target_weights\": {\"2\": [200, 50], \"3\": [128, 85, 43]},");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("target_weights '2'") != std::string::npos && bad.error.find("target_weights '3'") == std::string::npos);
}

// R61: a raise of a stat named in stat_wraparound wraps past 255 as a byte would.
TEST(statRaisesCanWrapPast255) {
	Database db = data();
	db.characters["fighter"].base.strength = 250;
	db.abilities["rally"].effects.front().power = 10;
	auto raised = [&db] {
		Battle battle = makeBattle(db, {"gel_mite"}, 2);
		sturdyParty(battle);
		sturdy(battle, kFirstEnemy);
		battle.combatant(kFighter).tp = 99;
		battle.setCommand(kFighter, Command::ability("rally", -1));
		othersDefend(battle);
		battle.resolveRound();
		return battle.combatant(kFighter).effective(Stat::Strength);
	};
	const int capless = raised();
	CHECK(capless > 255);
	db.battleRules.statWraparound = {Stat::Strength};
	CHECK_EQ(raised(), capless % 256);
}

namespace {

int silencedOf(const Database& db, int mysticMental, int miteMental, std::uint64_t seeds) {
	int count = 0;
	for (std::uint64_t seed = 1; seed <= seeds; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite"}, seed);
		sturdyParty(battle);
		sturdy(battle, kFirstEnemy);
		battle.combatant(kMystic).stats.mental = mysticMental;
		battle.combatant(kMystic).stats.defense = 300;
		battle.combatant(kFirstEnemy).stats.mental = miteMental;
		battle.combatant(kFirstEnemy).stats.agility = 1;
		battle.setCommand(kMystic, Command::ability("hush", kFirstEnemy));
		othersDefend(battle);
		count += ofType(battle.resolveRound(), EventType::StatusApplied).size() == 1 ? 1 : 0;
	}
	return count;
}

}

// R62: user_stat, target_stat and user_power choose the two sides of an effect roll; user_stat wins over attack_stat and
// an item rolls with user_power in place of 0. Keys that repeat the defaults play and draw exactly as before.
TEST(effectRollsCanNameTheirStatsOrAFlatUserPower) {
	Database db = data();
	Effect& hush = db.abilities["hush"].effects.front();
	// (roll + 50 - 200) * 4 is never above 73.
	CHECK_EQ(silencedOf(db, 50, 200, 8), 0);
	// (roll + 300 - 200) * 4, from the Mystic's defense, always is.
	hush.userStat = Stat::Defense;
	CHECK_EQ(silencedOf(db, 50, 200, 8), 8);
	hush.userStat.reset();
	// (roll + 50 - 1) * 4, against the mite's agility.
	hush.targetStat = Stat::Agility;
	CHECK_EQ(silencedOf(db, 50, 200, 8), 8);
	hush.targetStat.reset();
	hush.userPower = 250;
	CHECK_EQ(silencedOf(db, 50, 200, 8), 8);
	hush.userPower.reset();
	db.abilities["hush"].attackStat = Stat::Defense;
	CHECK_EQ(silencedOf(db, 50, 200, 8), 8);
	hush.userStat = Stat::Mental;
	CHECK_EQ(silencedOf(db, 50, 200, 8), 0);

	Database itemDb = data();
	itemDb.items["spark_flask"].effects = {Effect()};
	Effect& potion = itemDb.items["spark_flask"].effects.front();
	potion.kind = EffectKind::ApplyStatus;
	potion.status = "silence";
	auto itemSilences = [&itemDb](std::uint64_t seed) {
		Battle battle = makeBattle(itemDb, {"gel_mite"}, seed, Inventory{{"spark_flask", 1}});
		sturdyParty(battle);
		battle.combatant(kFirstEnemy).stats.mental = 70;
		battle.setCommand(kFighter, Command::item("spark_flask", kFirstEnemy));
		othersDefend(battle);
		return ofType(battle.resolveRound(), EventType::StatusApplied).size() == 1;
	};
	CHECK(!itemSilences(1) && !itemSilences(2));
	potion.userPower = 100;
	CHECK(itemSilences(1) && itemSilences(2));

	Database same = data();
	for (const char* id : {"hush", "weaken"}) {
		Effect& effect = same.abilities[id].effects.front();
		effect.userStat = Stat::Mental;
		effect.targetStat = Stat::Mental;
		effect.element = Element("tide");
	}
	int landed = 0;
	int resisted = 0;
	for (std::uint64_t seed = 1; seed <= 30; ++seed) {
		auto play = [seed](const Database& rules) {
			Battle battle = makeBattle(rules, {"brass_warden"}, seed);
			sturdyParty(battle);
			sturdy(battle, kFirstEnemy);
			std::vector<Event> events;
			for (int round = 0; round < 3; ++round) {
				battle.combatant(kMystic).tp = 99;
				battle.combatant(kScout).tp = 99;
				battle.setCommand(kMystic, Command::ability("hush", kFirstEnemy));
				battle.setCommand(kScout, Command::ability("weaken", kFirstEnemy));
				battle.setCommand(kFighter, Command::attack(kFirstEnemy));
				othersDefend(battle);
				const std::vector<Event> round_events = battle.resolveRound();
				events.insert(events.end(), round_events.begin(), round_events.end());
			}
			return events;
		};
		const std::vector<Event> before = play(data());
		CHECK_EQ(serializeEvents(play(same)), serializeEvents(before));
		landed += static_cast<int>(ofType(before, EventType::StatusApplied).size() + ofType(before, EventType::StatModified).size());
		resisted += static_cast<int>(ofType(before, EventType::StatusResisted).size());
	}
	CHECK(landed > 0 && resisted > 0);
}

// R63: an element on apply_status, a lowering modify_stat or instant_defeat scales the roll's factor by the target's
// affinity, together with the status resistance in one rounding: affinity * (100 - resistance) * 4 / (divisor * 100).
// An affinity of 0 is immunity, with no roll.
TEST(anEffectsElementScalesItsRollAndAffinityZeroIsImmunity) {
	for (int roll : {0, 17, 63}) {
		for (int resistance : {0, 25, 50, 76, 99}) {
			CHECK_EQ(formulas::effectScore(roll, 20, 9, 100, 100, resistance), formulas::statusScore(roll, 20, 9, resistance));
			CHECK_EQ(formulas::effectScore(roll, 20, 9, 400, 400, resistance), formulas::statusScore(roll, 20, 9, resistance));
		}
		for (int affinity : {0, 1, 50, 150, 300}) {
			CHECK_EQ(formulas::effectScore(roll, 20, 9, affinity, 100, 0), formulas::elementScore(roll, 20, 9, affinity, 100));
		}
	}
	CHECK_EQ(formulas::effectScore(10, 20, 10, 150, 100, 50), 60);

	// The warden resists silence by 50: factor 2 without an element, 4 for an element it takes at 200.
	Database db = data();
	db.abilities["hush"].effects.front().threshold = 254;
	db.enemies["brass_warden"].affinities[Element::Chill] = 200;
	db.enemies["brass_warden"].affinities[Element::Holy] = 0;
	auto hushes = [&db](std::uint64_t seed) {
		Battle battle = makeBattle(db, {"brass_warden"}, seed);
		sturdyParty(battle);
		battle.combatant(kMystic).stats.mental = battle.combatant(kFirstEnemy).effective(Stat::Mental) + 64;
		battle.setCommand(kMystic, Command::ability("hush", kFirstEnemy));
		othersDefend(battle);
		return battle.resolveRound();
	};
	Effect& hush = db.abilities["hush"].effects.front();
	for (std::uint64_t seed = 1; seed <= 6; ++seed) {
		hush.element = Element::None;
		CHECK(ofType(hushes(seed), EventType::StatusApplied).empty());
		hush.element = Element::Chill;
		CHECK_EQ(ofType(hushes(seed), EventType::StatusApplied).size(), 1u);
		hush.element = Element::Holy;
		const std::vector<Event> resisted = ofType(hushes(seed), EventType::StatusResisted);
		CHECK(resisted.size() == 1 && resisted[0].effectiveness == Effectiveness::Immune && resisted[0].message == "status.silence.immune");
	}

	Database lower = data();
	lower.enemies["brass_warden"].affinities[Element::Holy] = 0;
	lower.abilities["weaken"].effects.front().element = Element::Holy;
	Battle battle = makeBattle(lower, {"brass_warden"}, 3);
	sturdyParty(battle);
	battle.combatant(kScout).stats.mental = 400;
	battle.setCommand(kScout, Command::ability("weaken", kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> resisted = ofType(events, EventType::StatusResisted);
	CHECK(ofType(events, EventType::StatModified).empty());
	CHECK(resisted.size() == 1 && resisted[0].effectiveness == Effectiveness::Immune && resisted[0].message == "battle.stat_resisted");
}

// R64: always_lands takes no roll and draws no random number; immunity that needs no roll still holds.
TEST(alwaysLandsSkipsTheRollAndItsDraw) {
	Database db = data();
	db.characters["mystic"].abilities.push_back("unmake");
	db.enemies["brass_warden"].affinities[Element::Holy] = 0;
	Effect& hush = db.abilities["hush"].effects.front();
	auto roundTwo = [&db](std::uint64_t seed, int mysticMental, bool* silenced) {
		Battle battle = makeBattle(db, {"brass_warden"}, seed);
		sturdyParty(battle);
		sturdy(battle, kFirstEnemy);
		battle.combatant(kMystic).stats.mental = mysticMental;
		battle.combatant(kFirstEnemy).stats.mental = 200;
		battle.setCommand(kMystic, Command::ability("hush", kFirstEnemy));
		othersDefend(battle);
		*silenced = !ofType(battle.resolveRound(), EventType::StatusApplied).empty();
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		std::string kept;
		for (const Event& e : battle.resolveRound()) {
			if (e.type == EventType::OrderResolved || e.type == EventType::Missed || e.type == EventType::DamageDealt) {
				kept += serializeEvent(e) + "\n";
			}
		}
		return kept;
	};
	int differed = 0;
	for (std::uint64_t seed = 1; seed <= 10; ++seed) {
		bool silenced = false;
		const std::string rolled = roundTwo(seed, 1, &silenced);
		CHECK(!silenced);
		hush.alwaysLands = true;
		const std::string landed = roundTwo(seed, 1, &silenced);
		CHECK(silenced);
		// An element the warden is immune to stops it with no roll either, so the stream after it is the same.
		hush.element = Element::Holy;
		CHECK_EQ(roundTwo(seed, 1, &silenced), landed);
		CHECK(!silenced);
		hush.element = Element::None;
		hush.alwaysLands = false;
		differed += rolled != landed ? 1 : 0;
	}
	CHECK(differed > 0);

	db.abilities["unmake"].effects.front().alwaysLands = true;
	for (std::uint64_t seed = 1; seed <= 6; ++seed) {
		Battle battle = makeBattle(db, {"twin_ooze", "brass_warden"}, seed);
		sturdyParty(battle);
		battle.combatant(kMystic).stats.mental = 1;
		battle.combatant(kMystic).tp = 99;
		battle.setCommand(kMystic, Command::ability("unmake", kFirstEnemy));
		othersDefend(battle);
		battle.resolveRound();
		CHECK(!battle.combatant(kFirstEnemy).alive());
		battle.combatant(kMystic).tp = 99;
		battle.setCommand(kMystic, Command::ability("unmake", kFirstEnemy + 1));
		othersDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		CHECK(battle.combatant(kFirstEnemy + 1).alive());
		CHECK(std::any_of(events.begin(), events.end(), [](const Event& e) { return e.message == "battle.instant_defeat_immune"; }));
	}
}

// R62 to R64: the roll keys load only where a roll can use them.
TEST(rollKeysLoadOnlyWhereARollUsesThem) {
	const std::map<std::string, std::string> texts = dataTexts();
	const std::string hush = "{\"type\": \"apply_status\", \"status\": \"silence\", \"threshold\": 73}";
	auto loads = [&texts](const char* file, const std::string& from, const std::string& to) {
		std::map<std::string, std::string> changed = texts;
		replaceOnce(changed[file], from, to);
		return loadDatabaseFromTexts(changed);
	};
	const Result<Database> fine = loads("abilities.json", hush,
		"{\"type\": \"apply_status\", \"status\": \"silence\", \"threshold\": 73, \"user_stat\": \"attack\", \"target_stat\": \"agility\", \"element\": \"tide\"}");
	CHECK(fine.ok());
	if (fine.ok()) {
		const Effect& effect = fine.value->ability("hush")->effects.front();
		CHECK(effect.userStat == Stat::Attack && effect.targetStat == Stat::Agility && effect.element == Element("tide") && !effect.alwaysLands);
	}
	const std::vector<std::pair<std::string, std::string>> refused = {
		{"\"threshold\": 73, \"always_lands\": true", "always_lands has no roll, so it has no threshold"},
		{"\"threshold\": 73, \"user_stat\": \"mental\", \"user_power\": 9", "give user_power or user_stat, not both"},
		{"\"threshold\": 73, \"target_stat\": \"luck\"", "unknown stat 'luck'"},
		{"\"threshold\": 73, \"element\": \"magma\"", "unknown element 'magma'"},
	};
	for (const auto& [keys, message] : refused) {
		const Result<Database> bad = loads("abilities.json", "\"silence\", \"threshold\": 73", "\"silence\", " + keys);
		CHECK(!bad.ok() && bad.error.find(message) != std::string::npos);
	}
	const Result<Database> raise = loads("abilities.json", "{\"type\": \"modify_stat\", \"stat\": \"strength\", \"direction\": \"up\"}",
		"{\"type\": \"modify_stat\", \"stat\": \"strength\", \"direction\": \"up\", \"element\": \"holy\"}");
	CHECK(!raise.ok() && raise.error.find("only a stat change with direction down rolls, so this one has no element") != std::string::npos);
	const Result<Database> combo = loads("abilities.json", "{\"type\": \"apply_status\", \"status\": \"sleep\", \"threshold\": 47},",
		"{\"type\": \"apply_status\", \"status\": \"sleep\", \"threshold\": 47, \"user_stat\": \"mental\"},");
	CHECK(!combo.ok() && combo.error.find("user_stat names the user's stat") != std::string::npos);
	const Result<Database> item = loads("items.json", "{\"type\": \"elemental_damage\", \"power\": 25, \"element\": \"shock\"}",
		"{\"type\": \"apply_status\", \"status\": \"silence\", \"user_stat\": \"mental\"}");
	CHECK(!item.ok() && item.error.find("user_stat names the user's stat") != std::string::npos);
	const Result<Database> itemPower = loads("items.json", "{\"type\": \"elemental_damage\", \"power\": 25, \"element\": \"shock\"}",
		"{\"type\": \"apply_status\", \"status\": \"silence\", \"user_power\": 40, \"always_lands\": false}");
	CHECK(itemPower.ok());
}

namespace {

// The Fighter's damage with a blade in each hand whose attack bonuses are given, at extra base strength.
int bladeDamage(Database db, int rightBonus, int leftBonus, int extraStrength, const Command& command, std::uint64_t seed) {
	db.equipment["copper_blade"].attackBonus = rightBonus;
	db.equipment["gill_knife"].attackBonus = leftBonus;
	db.characters["fighter"].base.strength += extraStrength;
	BattleSetup setup = makeSetup(db, {"gel_mite"}, seed);
	setup.party[kFighter].equipment[EquipSlot::LeftHand] = "gill_knife";
	Battle battle = makeBattle(db, setup);
	sturdy(battle, kFirstEnemy);
	battle.setCommand(kFighter, command);
	othersDefend(battle);
	const std::optional<Event> hit = firstBy(battle.resolveRound(), EventType::DamageDealt, kFighter);
	return hit ? hit->amount : -1;
}

}

// R65: the attack stat is strength plus the attack bonuses of the weapons in both hands. Basic attacks hit with it;
// `attack_stat: "attack"` names it and `"strength"` the plain stat. Enemies and vehicles have no bonus.
TEST(theAttackStatAddsBothHandsWeaponAttackBonuses) {
	Database db = data();
	const Command attack = Command::attack(kFirstEnemy);
	const Command focus = Command::ability("focus_shot", kFirstEnemy);
	int bonused = 0;
	int plain = 0;
	for (std::uint64_t seed = 1; seed <= 8; ++seed) {
		const int withBonuses = bladeDamage(db, 20, 7, 0, attack, seed);
		CHECK_EQ(withBonuses, bladeDamage(db, 0, 0, 27, attack, seed));
		bonused += withBonuses;
		plain += bladeDamage(db, 0, 0, 0, attack, seed);
		CHECK_EQ(bladeDamage(db, 20, 7, 0, focus, seed), bladeDamage(db, 0, 0, 0, focus, seed));
	}
	CHECK(bonused > plain);
	Database named = data();
	named.abilities["strike"].attackStat = Stat::Strength;
	named.abilities["focus_shot"].attackStat = Stat::Attack;
	for (std::uint64_t seed = 1; seed <= 8; ++seed) {
		CHECK_EQ(bladeDamage(named, 20, 7, 0, attack, seed), bladeDamage(named, 0, 0, 0, attack, seed));
		CHECK_EQ(bladeDamage(named, 20, 7, 0, focus, seed), bladeDamage(named, 0, 0, 27, focus, seed));
	}

	named.equipment["copper_blade"].attackBonus = 20;
	named.equipment["gill_knife"].attackBonus = 7;
	named.abilities["rally"].attackStat = Stat::Attack;
	BattleSetup setup = makeSetup(named, {"gel_mite"}, 2);
	setup.party[kFighter].equipment[EquipSlot::LeftHand] = "gill_knife";
	Battle battle = makeBattle(named, setup);
	const Combatant& fighter = battle.combatant(kFighter);
	const int strength = fighter.effective(Stat::Strength);
	CHECK_EQ(strength, makeBattle(data(), setup).combatant(kFighter).effective(Stat::Strength));
	CHECK_EQ(fighter.effective(Stat::Attack), strength + 27);
	CHECK_EQ(battle.combatant(kFirstEnemy).effective(Stat::Attack), battle.combatant(kFirstEnemy).effective(Stat::Strength));
	sturdyParty(battle);
	battle.setCommand(kFighter, Command::ability("rally", -1));
	othersDefend(battle);
	const std::optional<Event> raised = firstBy(battle.resolveRound(), EventType::StatModified, kFighter);
	CHECK(raised && raised->target == kFighter && raised->amount == strength + 27);
	CHECK_EQ(battle.combatant(kFighter).effective(Stat::Attack), 2 * strength + 27 + 27);

	const std::map<std::string, std::string> texts = dataTexts();
	auto loads = [&texts](const std::string& from, const std::string& to) {
		std::map<std::string, std::string> changed = texts;
		replaceOnce(changed["equipment.json"], from, to);
		return loadDatabaseFromTexts(changed);
	};
	const Result<Database> weapon = loads("\"stats\": {\"strength\": 4, \"critical\": 4}", "\"stats\": {\"strength\": 4, \"critical\": 4, \"attack\": 9}");
	CHECK(weapon.ok() && weapon.value->equipmentPiece("copper_blade")->attackBonus == 9 && weapon.value->equipmentPiece("copper_blade")->bonus.strength == 4);
	const Result<Database> shield = loads("\"stats\": {\"defense\": 4}", "\"stats\": {\"defense\": 4, \"attack\": 2}");
	CHECK(shield.ok() && shield.value->equipmentPiece("driftwood_buckler")->attackBonus == 2);
	std::map<std::string, std::string> lowered = texts;
	replaceOnce(lowered["abilities.json"], "\"stat\": \"strength\", \"direction\": \"up\"", "\"stat\": \"attack\", \"direction\": \"up\"");
	CHECK(loadDatabaseFromTexts(lowered).ok());
	std::map<std::string, std::string> wrapped = texts;
	replaceOnce(wrapped["rules.json"], "{", "{\"stat_wraparound\": [\"attack\"],");
	CHECK(!loadDatabaseFromTexts(wrapped).ok());
	std::map<std::string, std::string> enemy = texts;
	replaceOnce(enemy["enemies.json"], "\"strength\": 11,", "\"strength\": 11, \"attack\": 4,");
	CHECK(loadDatabaseFromTexts(enemy).ok());
}

namespace {

// How often the Fighter's basic attack, with a silence that rolls (roll + U - T) * 4 > 254, lands it over eight seeds:
// strength and mental 50 on both sides, so it never lands at U 50 and always at U 350, the Fighter's attack with a
// blade bonus of 300.
int strikeSilences(const Database& db) {
	int count = 0;
	for (std::uint64_t seed = 1; seed <= 8; ++seed) {
		Battle battle = makeBattle(db, {"gel_mite"}, seed);
		sturdyParty(battle);
		sturdy(battle, kFirstEnemy);
		for (int index : {kFighter, kFirstEnemy}) {
			battle.combatant(index).stats.strength = 50;
			battle.combatant(index).stats.mental = 50;
		}
		battle.combatant(kFighter).stats.dexterity = 400;
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		othersDefend(battle);
		count += ofType(battle.resolveRound(), EventType::StatusApplied).size() == 1 ? 1 : 0;
	}
	return count;
}

}

// An effect roll reads the plain stat it names: `user_stat: "strength"` leaves out the weapons' attack bonus, which only
// `user_stat: "attack"` adds, and an ability whose attack_stat is `attack` rolls with plain strength, so a basic attack's
// secondary roll never takes the bonus unless the data says `user_stat: "attack"`.
TEST(anEffectRollReadsThePlainStatAndTheAttackStatOnlyWhenNamed) {
	Database db = data();
	db.equipment["copper_blade"].attackBonus = 300;
	Effect silence;
	silence.kind = EffectKind::ApplyStatus;
	silence.status = "silence";
	silence.threshold = 254;
	silence.targetStat = Stat::Strength;
	db.abilities["strike"].effects.push_back(silence);
	Effect& roll = db.abilities["strike"].effects.back();
	CHECK_EQ(strikeSilences(db), 0);
	roll.userStat = Stat::Strength;
	CHECK_EQ(strikeSilences(db), 0);
	roll.userStat = Stat::Attack;
	CHECK_EQ(strikeSilences(db), 8);
	roll.userStat.reset();
	db.abilities["strike"].attackStat = Stat::Attack;
	CHECK_EQ(strikeSilences(db), 0);
	roll.userStat = Stat::Attack;
	CHECK_EQ(strikeSilences(db), 8);
	db.abilities["strike"].attackStat = Stat::Strength;
	roll.userStat.reset();
	CHECK_EQ(strikeSilences(db), 0);
}

namespace {

// The Fighter's first event of the type in one round with the command: blades with attack bonuses of 20 in the right
// hand and 7 in the left, no damage variance, an enemy without defense, and dexterity 5 over its dodge with a critical
// of -6, so every roll hits and none is critical.
int bonusedBladeAmount(Database db, const Command& command, EventType type) {
	db.equipment["copper_blade"].attackBonus = 20;
	db.equipment["gill_knife"].attackBonus = 7;
	db.characters["fighter"].abilities.push_back("overclock");
	db.battleRules.damageVariance = DamageVariance{};
	BattleSetup setup = makeSetup(db, {"gel_mite"}, 3);
	setup.party[kFighter].equipment[EquipSlot::LeftHand] = "gill_knife";
	Battle battle = makeBattle(db, setup);
	sturdyParty(battle);
	sturdy(battle, kFirstEnemy);
	Combatant& enemy = battle.combatant(kFirstEnemy);
	enemy.stats.defense = 0;
	enemy.stats.agility = 0;
	enemy.stats.evasion = 0;
	battle.combatant(kFighter).stats.dexterity = 5;
	battle.combatant(kFighter).stats.critical = -6;
	battle.setCommand(kFighter, command);
	partyDefends(battle);
	const std::optional<Event> event = firstBy(battle.resolveRound(), type, kFighter);
	return event ? event->amount : -1;
}

}

// A weapon's attack bonus reaches only what names the attack stat: the Fighter's strength is 21 and its attack 48 with
// bonuses of 20 and 7. An undefended hit deals 2 * (stat + power): the basic attack (Strike, power 0) 96 with the attack
// stat, 42 when Strike names `attack_stat: "strength"`; Focus Shot (power 11) 64 with plain strength, also when it names `attack_stat: "strength"`, and 118 when it names
// `"attack"`. A skill's stat change moves by plain strength, 21, and by 48 when the skill names `"attack"`.
TEST(strengthAndTheAttackStatGiveTheirOwnNumbersWithABonusedWeapon) {
	Database db = data();
	const Command attack = Command::attack(kFirstEnemy);
	const Command focus = Command::ability("focus_shot", kFirstEnemy);
	const Command overclock = Command::ability("overclock", -1);
	CHECK_EQ(bonusedBladeAmount(db, attack, EventType::DamageDealt), 96);
	CHECK_EQ(bonusedBladeAmount(db, focus, EventType::DamageDealt), 64);
	CHECK_EQ(bonusedBladeAmount(db, overclock, EventType::StatModified), 21);
	db.abilities["focus_shot"].attackStat = Stat::Strength;
	db.abilities["overclock"].attackStat = Stat::Strength;
	CHECK_EQ(bonusedBladeAmount(db, focus, EventType::DamageDealt), 64);
	CHECK_EQ(bonusedBladeAmount(db, overclock, EventType::StatModified), 21);
	db.abilities["focus_shot"].attackStat = Stat::Attack;
	db.abilities["overclock"].attackStat = Stat::Attack;
	CHECK_EQ(bonusedBladeAmount(db, focus, EventType::DamageDealt), 118);
	CHECK_EQ(bonusedBladeAmount(db, overclock, EventType::StatModified), 48);
	db.abilities["strike"].attackStat = Stat::Strength;
	CHECK_EQ(bonusedBladeAmount(db, attack, EventType::DamageDealt), 42);
	for (const auto& entry : data().equipment) {
		CHECK_EQ(entry.second.attackBonus, 0);
	}
}

namespace {

std::string actionOf(const std::vector<Event>& events, int actor) {
	const std::optional<Event> started = firstBy(events, EventType::ActionStarted, actor);
	return started ? started->ability : std::string();
}

void ramsOnItsFirstTurn(Database& db, const std::string& enemy) {
	AiOverride first;
	first.when = AiConditionKind::FirstTurn;
	first.ability = "ram";
	db.enemies[enemy].overrides = {first};
}

}

// The first_turn override holds at a combatant's first turn of the battle and at its first turn after it joins or
// takes a new form: a next form, a transform, a summon and a replacement each act on it once.
TEST(theFirstTurnOverrideHoldsAfterJoiningOrTakingANewForm) {
	Database db = data();
	for (const char* id : {"gel_mite", "shellback", "shellback_bare", "moss_moth"}) {
		ramsOnItsFirstTurn(db, id);
	}

	Battle shell = makeBattle(db, encounterSetup(db, "shellback", 2));
	sturdyParty(shell);
	shell.combatant(kFighter).stats.dexterity = 400;
	shell.combatant(kFighter).stats.agility = 999;
	std::vector<std::string> shellActions;
	for (int round = 1; round <= 4; ++round) {
		if (round == 2) {
			shell.combatant(kFirstEnemy).hp = 1;
			shell.setCommand(kFighter, Command::attack(kFirstEnemy));
		}
		partyDefends(shell);
		shellActions.push_back(actionOf(shell.resolveRound(), kFirstEnemy));
	}
	CHECK_EQ(shell.combatant(kFirstEnemy).defId, std::string("shellback_bare"));
	CHECK(shellActions == std::vector<std::string>({"ram", "ram", "bite", "bite"}));

	Battle pupa = makeBattle(db, encounterSetup(db, "pupa", 3));
	sturdyParty(pupa);
	sturdy(pupa, kFirstEnemy);
	std::vector<std::string> pupaActions;
	for (int round = 1; round <= 5; ++round) {
		partyDefends(pupa);
		pupaActions.push_back(actionOf(pupa.resolveRound(), kFirstEnemy));
	}
	CHECK(std::vector<std::string>(pupaActions.begin(), pupaActions.begin() + 4) == std::vector<std::string>({"bite", "bite", "molt", "ram"}));
	CHECK(pupaActions[4] != "ram");

	Battle lantern = makeBattle(db, {"hive_lantern"}, 4);
	sturdyParty(lantern);
	sturdy(lantern, kFirstEnemy);
	partyDefends(lantern);
	CHECK_EQ(ofType(lantern.resolveRound(), EventType::EnemyJoined).size(), std::size_t(1));
	const int summoned = kFirstEnemy + 1;
	partyDefends(lantern);
	CHECK_EQ(actionOf(lantern.resolveRound(), summoned), std::string("ram"));
	partyDefends(lantern);
	CHECK_EQ(actionOf(lantern.resolveRound(), summoned), std::string("bite"));

	Battle endless = makeBattle(db, encounterSetup(db, "endless", 5));
	sturdyParty(endless);
	endless.combatant(kFighter).stats.dexterity = 400;
	endless.combatant(kFighter).stats.agility = 999;
	partyDefends(endless);
	const std::vector<Event> opening = endless.resolveRound();
	CHECK(actionOf(opening, kFirstEnemy) == "ram" && actionOf(opening, kFirstEnemy + 1) == "ram");
	endless.combatant(kFirstEnemy).hp = 1;
	endless.setCommand(kFighter, Command::attack(kFirstEnemy));
	partyDefends(endless);
	const std::vector<Event> fall = endless.resolveRound();
	CHECK_EQ(ofType(fall, EventType::EnemyJoined).size(), std::size_t(1));
	CHECK_EQ(actionOf(fall, kFirstEnemy + 1), std::string("bite"));
	partyDefends(endless);
	const std::vector<Event> after = endless.resolveRound();
	CHECK(actionOf(after, kFirstEnemy + 2) == "ram" && actionOf(after, kFirstEnemy + 1) == "bite");

	for (AiConditionKind compound : {AiConditionKind::All, AiConditionKind::Any}) {
		Database both = data();
		AiOverride halfOnFirst;
		halfOnFirst.when = compound;
		halfOnFirst.parts = {leaf(AiConditionKind::FirstTurn), leaf(AiConditionKind::SelfHpHalf)};
		halfOnFirst.ability = "ram";
		both.enemies["shellback"].overrides = {halfOnFirst};
		const bool all = compound == AiConditionKind::All;
		for (bool half : {false, true}) {
			Battle battle = makeBattle(both, encounterSetup(both, "shellback", 2));
			sturdyParty(battle);
			if (half) {
				battle.combatant(kFirstEnemy).hp = battle.combatant(kFirstEnemy).stats.maxHp / 2;
			}
			partyDefends(battle);
			CHECK_EQ(actionOf(battle.resolveRound(), kFirstEnemy), std::string(half || !all ? "ram" : "bite"));
			partyDefends(battle);
			CHECK_EQ(actionOf(battle.resolveRound(), kFirstEnemy), std::string(half && !all ? "ram" : "bite"));
		}
	}

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["enemies.json"], "{\"when\": \"after_turns\", \"turns\": 2, \"ability\": \"molt\"}", "{\"when\": \"first_turn\", \"ability\": \"molt\"}");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->enemies.at("moss_pupa").overrides.at(0).when == AiConditionKind::FirstTurn);
	replaceOnce(texts["enemies.json"], "{\"when\": \"first_turn\", \"ability\": \"molt\"}", "{\"when\": \"first_turn\", \"turns\": 2, \"ability\": \"molt\"}");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("only an after_turns condition has turns") != std::string::npos);
}

// keeps_ambush: an enemy's next form or transform keeps the ambushed mark it had; by default the new form starts
// without it.
TEST(keepsAmbushCarriesTheMarkToTheNextFormOrTransform) {
	for (bool keeps : {false, true}) {
		Database db = data();
		db.enemies["shellback"].keepsAmbush = keeps;
		AiOverride ambush;
		ambush.when = AiConditionKind::Ambushed;
		ambush.ability = "ram";
		db.enemies["shellback_bare"].overrides = {ambush};
		BattleSetup setup = encounterSetup(db, "shellback", 2);
		setup.opening = Opening::PartyFirst;
		Battle battle = makeBattle(db, setup);
		sturdyParty(battle);
		CHECK(battle.combatant(kFirstEnemy).ambushed);
		battle.combatant(kFirstEnemy).hp = 1;
		battle.combatant(kFighter).stats.dexterity = 400;
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		partyDefends(battle);
		battle.resolveRound();
		CHECK_EQ(battle.combatant(kFirstEnemy).defId, std::string("shellback_bare"));
		CHECK(battle.combatant(kFirstEnemy).ambushed == keeps);
		partyDefends(battle);
		CHECK_EQ(actionOf(battle.resolveRound(), kFirstEnemy), std::string(keeps ? "ram" : "bite"));
		CHECK(!battle.combatant(kFirstEnemy).ambushed);

		Database held = data();
		held.battleRules.ambushOpening = Opening::EnemyFirst;
		held.enemies["moss_pupa"].keepsAmbush = keeps;
		BattleSetup pupa = encounterSetup(held, "pupa", 3);
		pupa.opening = Opening::EnemyFirst;
		Battle molting = makeBattle(held, pupa);
		sturdyParty(molting);
		sturdy(molting, kFirstEnemy);
		CHECK(molting.combatant(kFirstEnemy).ambushed);
		for (int round = 1; round <= 3; ++round) {
			partyDefends(molting);
			molting.resolveRound();
		}
		CHECK_EQ(molting.combatant(kFirstEnemy).defId, std::string("moss_moth"));
		CHECK(molting.combatant(kFirstEnemy).ambushed == keeps);
	}

	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["enemies.json"], "\"next_form\": \"shellback_bare\",", "\"next_form\": \"shellback_bare\", \"keeps_ambush\": true,");
	const Result<Database> kept = loadDatabaseFromTexts(texts);
	CHECK(kept.ok() && kept.value->enemies.at("shellback").keepsAmbush && !kept.value->enemies.at("shellback_bare").keepsAmbush);
	replaceOnce(texts["enemies.json"], "\"id\": \"gel_mite\",", "\"id\": \"gel_mite\", \"keeps_ambush\": true,");
	const Result<Database> bad = loadDatabaseFromTexts(texts);
	CHECK(!bad.ok() && bad.error.find("keeps_ambush needs a next_form or transforms_into") != std::string::npos);
}

// Powers and stats of 0: every formula stays defined and none goes negative where it must not; hit points alone keep a
// floor of 1 in the data, a basic hit's damage keeps its floor of 1 and a revive gives at least 1 hit point.
TEST(powersAndStatsOfZeroWorkInEveryFormula) {
	CHECK_EQ(formulas::initiativeSpread(0), 0);
	CHECK_EQ(formulas::initiative(0, 0), 0);
	CHECK_EQ(formulas::chanceScore(0, 0, 0), 0);
	CHECK_EQ(formulas::criticalBonus(0), 0);
	CHECK_EQ(formulas::criticalBonusLowByte(0), 0);
	for (int spread : {formulas::kSpreadMin, formulas::kSpreadMean, formulas::kSpreadMax}) {
		CHECK_EQ(formulas::damage(0, 0, 0, 100, spread), 1);
		CHECK_EQ(formulas::damage(0, 0, 0, 0, spread), 0);
		CHECK_EQ(formulas::damage(0, 0, 0, 100, spread, 100, true, false), 1);
		CHECK_EQ(formulas::healAmount(0, 0, spread), 0);
		CHECK_EQ(formulas::healAmount(0, 10, spread), (10 * spread / 64 + 10) / 2);
	}
	CHECK_EQ(formulas::variedDamage(0, 0, 0, 100, 1, 1, 100), 1);
	CHECK_EQ(formulas::reviveHp(1, 1), 1);
	for (int roll : {0, 31, 63}) {
		CHECK_EQ(formulas::effectScore(roll, 0, 0, 100, 100, 0), roll * 4);
		CHECK_EQ(formulas::effectScore(roll, 0, 0, 0, 100, 0), 0);
		CHECK_EQ(formulas::statusScore(roll, 0, 0, 100), 0);
		CHECK_EQ(formulas::elementScore(roll, 0, 0, 200, 100), roll * 8);
	}
	CHECK_EQ(formulas::experienceToNextLevel(1), 25);

	// The loader takes them.
	const std::map<std::string, std::string> texts = dataTexts();
	std::map<std::string, std::string> zero = texts;
	replaceOnce(zero["enemies.json"], "\"strength\": 11, \"mental\": 3, \"agility\": 5, \"dexterity\": 8", "\"strength\": 0, \"mental\": 0, \"agility\": 0, \"dexterity\": 0");
	replaceOnce(zero["abilities.json"], "{\"type\": \"heal\", \"kinds\": [\"living\"], \"power\": 28}", "{\"type\": \"heal\", \"kinds\": [\"living\"], \"power\": 0}");
	replaceOnce(zero["items.json"], "{\"type\": \"elemental_damage\", \"power\": 25, \"element\": \"shock\"}", "{\"type\": \"elemental_damage\", \"power\": 0, \"element\": \"shock\"}");
	replaceOnce(zero["equipment.json"], "\"use\": \"gust\", \"use_from_bag\": true", "\"use\": \"gust\", \"use_from_bag\": true, \"use_power\": 0");
	const Result<Database> loaded = loadDatabaseFromTexts(zero);
	CHECK(loaded.ok());
	if (!loaded.ok()) {
		return;
	}
	const Database& db = *loaded.value;
	CHECK(db.enemy("gel_mite")->stats.strength == 0 && db.enemy("gel_mite")->stats.agility == 0);
	CHECK(db.ability("mend")->effects.front().power == 0 && db.item("spark_flask")->effects.front().power == 0);
	CHECK(db.equipment.at("squall_fan").usePower == 0);
	std::map<std::string, std::string> noHp = texts;
	replaceOnce(noHp["enemies.json"], "\"hp\": 60,", "\"hp\": 0,");
	CHECK(!loadDatabaseFromTexts(noHp).ok());

	// A difficulty keeps a stat of 0 at 0 and one of 1 at 1, as before.
	DifficultyDef halved;
	EnemyOverlay overlay;
	overlay.enemy = "gel_mite";
	overlay.statsPercent = Stats::filled(50);
	halved.enemies.push_back(overlay);
	Database ones = db;
	ones.enemies["gel_mite"].stats.mental = 1;
	const Database scaled = applyDifficulty(ones, halved);
	CHECK(scaled.enemy("gel_mite")->stats.strength == 0 && scaled.enemy("gel_mite")->stats.mental == 1);

	// A member's piece takes a stat of 0 nowhere below 0, and one of 1 nowhere below 1, as before.
	Database members = data();
	members.characters["fighter"].base.agility = 0;
	members.characters["fighter"].growth.agility = 0;
	PartyMember fighter = makePartyMember(members, *members.character("fighter"), 1);
	fighter.equipment[EquipSlot::Body] = "rivet_plate";
	CHECK_EQ(memberStats(members, fighter).agility, 0);
	members.characters["fighter"].base.agility = 1;
	CHECK_EQ(memberStats(members, fighter).agility, 1);

	// In battle: 0 agility still acts, last; 0 strength hits for the floor of 1; a heal of power 0 with 0 mental
	// recovers 0; lowering a stat of 0 leaves it at 0; a mite with every stat 0 still bites.
	Database fight = db;
	fight.abilities["weaken"].effects.front().alwaysLands = true;
	fight.abilities["weaken"].effects.front().threshold = 0;
	fight.abilities["weaken"].effects.front().stat = Stat::Agility;
	for (std::uint64_t seed = 1; seed <= 6; ++seed) {
		Battle battle = makeBattle(fight, {"gel_mite"}, seed);
		sturdyParty(battle);
		sturdy(battle, kFirstEnemy);
		battle.combatant(kFighter).stats.strength = 0;
		battle.combatant(kFighter).stats.agility = 0;
		battle.combatant(kFighter).stats.dexterity = 400;
		battle.combatant(kMedic).stats.mental = 0;
		battle.combatant(kMedic).tp = 99;
		battle.combatant(kScout).tp = 99;
		battle.combatant(kMystic).hp = 10;
		battle.setCommand(kFighter, Command::attack(kFirstEnemy));
		battle.setCommand(kMedic, Command::ability("mend", kMystic));
		battle.setCommand(kScout, Command::ability("weaken", kFirstEnemy));
		battle.setCommand(kMystic, Command::defend());
		battle.setCommand(kEngineer, Command::defend());
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<int>& order = battle.lastTurnOrder();
		CHECK(std::find(order.begin(), order.end(), kFighter) != order.end());
		const std::optional<Event> hit = firstBy(events, EventType::DamageDealt, kFighter);
		CHECK(hit && hit->amount == 1);
		const std::optional<Event> healed = firstBy(events, EventType::Healed, kMedic);
		CHECK(healed && healed->amount == 0);
		CHECK_EQ(battle.combatant(kFirstEnemy).effective(Stat::Agility), 0);
		CHECK(firstBy(events, EventType::ActionStarted, kFirstEnemy).has_value());
	}
}

// A macro may hold a step for a vehicle beside the members' steps: in a battle aboard it the vehicle takes it and the
// members aboard pass theirs by; in a party battle the vehicle's step is passed by. Data macros take such steps.
TEST(aMacroStepForAVehicleActsOnlyInBattlesAboard) {
	Macro macro;
	macro.name = "hull";
	macro.steps.push_back({"fighter", CommandKind::Attack, "", {MacroTargetKind::Enemy, "", 0}});
	macro.steps.push_back({"reed_skimmer", CommandKind::Ability, "hull_vent", {MacroTargetKind::Enemy, "", 0}});
	BattleSetup aboard = makeSetup(data(), {"gel_mite"}, 4);
	aboard.flags.vehicleBattle = true;
	aboard.vehicle = "reed_skimmer";
	Battle vehicleBattle = makeBattle(data(), aboard);
	const int vehicle = static_cast<int>(aboard.party.size());
	const std::vector<MacroStepResult> afloat = vehicleBattle.applyMacro(macro, MacroFallback::Automatic);
	CHECK(afloat.size() == 2 && afloat[0].status == MacroStepStatus::Skipped && afloat[0].issue == MacroIssue::NotInParty);
	CHECK(afloat[1].actor == vehicle && afloat[1].status == MacroStepStatus::Applied);
	CHECK(vehicleBattle.command(vehicle).id == "hull_vent" && vehicleBattle.command(vehicle).target == vehicle + 1);
	Battle partyBattle = makeBattle(data(), {"gel_mite"}, 4);
	const std::vector<MacroStepResult> ashore = partyBattle.applyMacro(macro, MacroFallback::Automatic);
	CHECK(ashore[0].actor == kFighter && ashore[1].status == MacroStepStatus::Skipped && ashore[1].issue == MacroIssue::NotInParty);

	const std::map<std::string, std::string> texts = dataTexts();
	const std::string step = "{\"member\": \"medic\", \"command\": \"defend\"}";
	auto loads = [&texts, &step](const std::string& to) {
		std::map<std::string, std::string> changed = texts;
		replaceOnce(changed["party.json"], step, step + ", " + to);
		return loadDatabaseFromTexts(changed);
	};
	const Result<Database> fine = loads("{\"member\": \"reed_skimmer\", \"command\": \"ability\", \"id\": \"hull_vent\", \"target\": {\"enemy\": 0}}");
	CHECK(fine.ok() && fine.value->macro("tempest")->steps.back().member == "reed_skimmer");
	const Result<Database> item = loads("{\"member\": \"reed_skimmer\", \"command\": \"item\", \"id\": \"tonic\"}");
	CHECK(!item.ok() && item.error.find("vehicle 'reed_skimmer' cannot item") != std::string::npos);
	const Result<Database> unknown = loads("{\"member\": \"reed_skimmer\", \"command\": \"ability\", \"id\": \"rime_drill\"}");
	CHECK(!unknown.ok());
	const Result<Database> member = loads("{\"member\": \"reed_skimmer\", \"command\": \"ability\", \"id\": \"hull_patch\", \"target\": {\"member\": \"fighter\"}}");
	CHECK(!member.ok());
}

// Enemies' and vehicles' optional stats.attack is what their basic attacks hit with, strength staying for rolls and
// abilities that name it; left out, attack is strength. Any worn piece's stats.attack adds to a member's attack.
TEST(enemiesAndVehiclesMayHaveAnAttackApartFromStrength) {
	const std::map<std::string, std::string> texts = dataTexts();
	std::map<std::string, std::string> changed = texts;
	replaceOnce(changed["enemies.json"], "\"strength\": 11,", "\"strength\": 11, \"attack\": 30,");
	replaceOnce(changed["party.json"], "\"strength\": 20, \"mental\": 8, \"agility\": 6", "\"strength\": 20, \"attack\": 45, \"mental\": 8, \"agility\": 6");
	replaceOnce(changed["equipment.json"], "\"stats\": {\"defense\": 4}", "\"stats\": {\"defense\": 4, \"attack\": 3}");
	const Result<Database> loaded = loadDatabaseFromTexts(changed);
	CHECK(loaded.ok());
	if (!loaded.ok()) {
		return;
	}
	const Database& db = *loaded.value;
	CHECK_EQ(db.enemy("gel_mite")->stats.attack, 30);
	BattleSetup setup = makeSetup(db, {"gel_mite"}, 3);
	setup.party[kFighter].equipment[EquipSlot::LeftHand] = "driftwood_buckler";
	const Battle battle = makeBattle(db, setup);
	CHECK_EQ(battle.combatant(kFirstEnemy).effective(Stat::Strength), 11);
	CHECK_EQ(battle.combatant(kFirstEnemy).effective(Stat::Attack), 30);
	CHECK_EQ(battle.combatant(kFighter).effective(Stat::Attack), makeBattle(data(), setup).combatant(kFighter).effective(Stat::Attack) + 3);
	BattleSetup aboard = makeSetup(db, {"gel_mite"}, 3);
	aboard.flags.vehicleBattle = true;
	aboard.vehicle = "reed_skimmer";
	const Battle vehicle = makeBattle(db, aboard);
	const int index = static_cast<int>(aboard.party.size());
	CHECK_EQ(vehicle.combatant(index).effective(Stat::Strength), 20);
	CHECK_EQ(vehicle.combatant(index).effective(Stat::Attack), 45);
	// The default data has none, so attack is strength.
	const Battle plain = makeBattle(data(), {"gel_mite"}, 3);
	CHECK_EQ(plain.combatant(kFirstEnemy).effective(Stat::Attack), plain.combatant(kFirstEnemy).effective(Stat::Strength));
	std::map<std::string, std::string> character = texts;
	replaceOnce(character["party.json"], "\"strength\": ", "\"attack\": 5, \"strength\": ");
	const Result<Database> refused = loadDatabaseFromTexts(character);
	CHECK(!refused.ok() && refused.error.find("a stats 'attack' is for enemies, vehicles and equipment") != std::string::npos);
}

// Damage and heal events carry the rolled amount beside the amount that changed, which the hit points cap; the rolled
// amount is not serialized, so records and transcripts read as before.
TEST(damageAndHealEventsCarryTheRolledAmount) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 3, Inventory{{"tonic", 1}});
	sturdyParty(battle);
	sturdy(battle, kFirstEnemy);
	battle.setCommand(1, Command::item("tonic", kFighter));
	othersDefend(battle);
	const std::optional<Event> heal = firstBy(battle.resolveRound(), EventType::Healed, 1);
	CHECK(heal && heal->amount == 0 && heal->rolled > 0);
	battle.combatant(kFirstEnemy).hp = 1;
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	othersDefend(battle);
	const std::optional<Event> hit = firstBy(battle.resolveRound(), EventType::DamageDealt, kFighter);
	CHECK(hit && hit->amount == 1 && hit->rolled > 1);
	CHECK(hit && serializeEvent(*hit).find("rolled") == std::string::npos);
}

// party.json `skill_uses` sets a member's maximum uses of a skill by level, stepwise; rest refills to it.
TEST(skillUsesFollowTheCharactersTableByLevel) {
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["party.json"], "\"abilities\": [\"rally\", \"focus_shot\"],", "\"abilities\": [\"rally\", \"focus_shot\"], \"skill_uses\": {\"focus_shot\": [[2, 5], [6, 8]]},");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	if (!loaded.ok()) {
		return;
	}
	const Database& db = *loaded.value;
	PartyMember low = makePartyMember(db, *db.character("fighter"), 1);
	CHECK_EQ(maxSkillUses(db, low, "focus_shot"), 3);
	CHECK_EQ(low.skillUses.at("focus_shot"), 3);
	PartyMember mid = makePartyMember(db, *db.character("fighter"), 4);
	CHECK_EQ(mid.skillUses.at("focus_shot"), 5);
	PartyMember high = makePartyMember(db, *db.character("fighter"), 7);
	high.skillUses["focus_shot"] = 0;
	rest(high, db);
	CHECK_EQ(high.skillUses.at("focus_shot"), 8);
	replaceOnce(texts["party.json"], "[[2, 5], [6, 8]]", "[[6, 5], [2, 8]]");
	CHECK(!loadDatabaseFromTexts(texts).ok());
}

// A revive with `living_target` may be aimed at a living ally: it passes the ally by and the other effects apply; `hp`
// stands a fallen one up with that many hit points; and a fallen target gets its revive first whatever the order.
TEST(reviveOptionsAndOrderOnFallenTargets) {
	Database db = data();
	AbilityDef& rekindle = db.abilities["rekindle"];
	Effect heal;
	heal.kind = EffectKind::Heal;
	heal.power = 20;
	rekindle.effects.insert(rekindle.effects.begin(), heal);
	rekindle.effects.back().livingTarget = true;
	Battle battle = makeBattle(db, {"brass_warden"}, 1);
	sturdy(battle, kFirstEnemy);
	battle.combatant(kScout).hp = 5;
	CHECK(battle.setCommand(kMedic, Command::ability("rekindle", kScout)).key.empty());
	othersDefend(battle);
	std::vector<Event> events = battle.resolveRound();
	CHECK(ofType(events, EventType::Revived).empty());
	CHECK(battle.combatant(kScout).hp > 5);
	battle.combatant(kScout).hp = 0;
	battle.combatant(kScout).statuses = {{kDefeatedStatus, 0}};
	rekindle.effects.back().flatHp = 7;
	Battle again = makeBattle(db, {"brass_warden"}, 1);
	sturdy(again, kFirstEnemy);
	again.combatant(kScout).hp = 0;
	again.combatant(kScout).statuses = {{kDefeatedStatus, 0}};
	CHECK(again.setCommand(kMedic, Command::ability("rekindle", kScout)).key.empty());
	othersDefend(again);
	events = again.resolveRound();
	const std::vector<Event> revived = ofType(events, EventType::Revived);
	CHECK(revived.size() == 1 && revived[0].amount == 7);
	// The heal listed before the revive still reaches the member it stood up.
	CHECK(!ofType(events, EventType::Healed).empty() && again.combatant(kScout).hp > 7);
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["abilities.json"], "{\"type\": \"revive\", \"kinds\": [\"living\"], \"power\": 50}", "{\"type\": \"revive\", \"kinds\": [\"living\"], \"hp\": 40, \"living_target\": true}");
	CHECK(loadDatabaseFromTexts(texts).ok());
	replaceOnce(texts["abilities.json"], "\"hp\": 40,", "\"hp\": 40, \"power\": 5,");
	CHECK(!loadDatabaseFromTexts(texts).ok());
}

// With no ally named, a one-fallen-ally revive passes by fallen allies it cannot reach (a machine for a living-only revive).
TEST(theDefaultFallenAllyPickSkipsWhatTheReviveCannotReach) {
	Database db = data();
	db.characters["fighter"].kind = CreatureKind::Machine;
	Battle battle = makeBattle(db, {"brass_warden"}, 1);
	sturdy(battle, kFirstEnemy);
	for (int member : {kFighter, kScout}) {
		battle.combatant(member).hp = 0;
		battle.combatant(member).statuses = {{kDefeatedStatus, 0}};
	}
	CHECK_EQ(battle.setCommand(kMedic, Command::ability("rekindle", kFighter)).key, std::string("command.no_effect_on_target"));
	CHECK(battle.setCommand(kMedic, Command::ability("rekindle", -1)).key.empty());
	othersDefend(battle);
	const std::vector<Event> revived = ofType(battle.resolveRound(), EventType::Revived);
	CHECK(revived.size() == 1 && revived[0].target == kScout);
}

// set_hp_percent sets hit points to a percent of the maximum, lowering them too; grant_affinity changes an element's
// affinity, kept by the member when permanent; strip_modifiers with equipment_affinities takes the pieces' share away.
TEST(setHpPercentGrantAffinityAndStrippingEquipmentAffinities) {
	Database db = data();
	AbilityDef halve;
	halve.id = "halve";
	halve.name = "Halve";
	halve.kind = AbilityKind::Technique;
	halve.targeting = Targeting::OneAlly;
	Effect set;
	set.kind = EffectKind::SetHpPercent;
	set.power = 50;
	Effect grant;
	grant.kind = EffectKind::GrantAffinity;
	grant.element = Element::Heat;
	grant.affinity = 0;
	grant.permanent = true;
	halve.effects = {set, grant};
	db.abilities[halve.id] = halve;
	db.characters["medic"].abilities.push_back(halve.id);
	Battle battle = makeBattle(db, {"brass_warden"}, 1);
	sturdy(battle, kFirstEnemy);
	const int max = battle.combatant(kScout).stats.maxHp;
	CHECK(battle.setCommand(kMedic, Command::ability("halve", kScout)).key.empty());
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK_EQ(battle.combatant(kScout).hp, std::max(1, max * 50 / 100));
	CHECK(!ofType(events, EventType::DamageDealt).empty() || battle.combatant(kScout).hp == max);
	CHECK_EQ(battle.combatant(kScout).affinity(Element::Heat), 0);
	const std::vector<PartyMember> after = battle.partyState();
	CHECK_EQ(after[kScout].affinities.at(Element::Heat), 0);
	// The member carries it into the next battle and the record keeps it.
	BattleSetup next = makeSetup(db, {"brass_warden"}, 2);
	next.party = after;
	const Battle carried = makeBattle(db, next);
	CHECK_EQ(carried.combatant(kScout).affinity(Element::Heat), 0);
	const Result<BattleRecord> parsed = parseRecord(serializeRecord(carried.record()));
	CHECK(parsed.ok() && parsed.value->setup.party[kScout].affinities.at(Element::Heat) == 0);

	Database pieces = data();
	pieces.equipment["calm_charm"].affinities[Element::Chill] = 50;
	pieces.abilities["purge"].effects.clear();
	Effect strip;
	strip.kind = EffectKind::StripModifiers;
	strip.equipmentAffinities = true;
	pieces.abilities["purge"].effects = {strip};
	BattleSetup setup = makeSetup(pieces, {"brass_warden"}, 1);
	setup.party[kMystic].equipment[EquipSlot::Accessory] = "calm_charm";
	Battle stripped = makeBattle(pieces, setup);
	sturdy(stripped, kFirstEnemy);
	const int worn = stripped.combatant(kMystic).affinity(Element::Chill);
	CHECK(stripped.setCommand(kMedic, Command::ability("purge", kMystic)).key.empty());
	othersDefend(stripped);
	stripped.resolveRound();
	CHECK(stripped.combatant(kMystic).affinity(Element::Chill) != worn);
	CHECK_EQ(stripped.combatant(kMystic).affinity(Element::Chill), makeBattle(data(), {"brass_warden"}, 1).combatant(kMystic).affinity(Element::Chill));
}

// rules.json field_poison_every and field_paralysis load with their status ids checked.
TEST(fieldStatusRulesLoadAndCheckTheirStatuses) {
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "{", "{\"field_poison_every\": 4, \"field_paralysis\": {\"from\": 7, \"chance\": 8, \"by\": 25},");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok() && loaded.value->battleRules.fieldPoisonEvery == 4 && loaded.value->battleRules.fieldParalysisBy == 25);
	std::map<std::string, std::string> wrong = texts;
	replaceOnce(wrong["rules.json"], "{", "{\"field_poison_status\": \"blight\",");
	CHECK(!loadDatabaseFromTexts(wrong).ok());
	replaceOnce(texts["rules.json"], "\"by\": 25", "\"by\": 5");
	CHECK(!loadDatabaseFromTexts(texts).ok());
}
