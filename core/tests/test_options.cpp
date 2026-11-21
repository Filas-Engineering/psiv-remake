#include "fixtures.h"
#include "rules/field.h"
#include "rules/formulas.h"
#include "rules/loader.h"
#include "rules/progression.h"
#include "rules/record.h"
#include "test.h"

#include <algorithm>

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
	sentinel.overrides = {{AiConditionKind::LastStanding, "bite", {}, ""}, {AiConditionKind::Ambushed, "hamstring", {}, ""}};

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
