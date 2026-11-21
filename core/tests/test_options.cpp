#include "fixtures.h"
#include "rules/field.h"
#include "rules/formulas.h"
#include "rules/loader.h"
#include "rules/progression.h"
#include "rules/record.h"
#include "test.h"

#include <algorithm>
#include <set>

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
	CHECK_EQ(data().equipment.at("squall_fan").usePower, 0);
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
