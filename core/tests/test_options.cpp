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
