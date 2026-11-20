#include "fixtures.h"
#include "rules/loader.h"
#include "rules/progression.h"
#include "test.h"

using namespace rules;
using namespace fixtures;

namespace {

// Gel mites with 1 hit point fall to the first hit, so a party at level 10 wins in round 1. The "pair" encounter is
// two of them with the given drop.
Database fragileMites(ItemDrop drop) {
	Database db = data();
	db.enemies.at("gel_mite").stats.maxHp = 1;
	Encounter pair;
	pair.id = "pair";
	pair.enemies = {{"gel_mite", "gel_mite"}, {"gel_mite", "gel_mite"}};
	pair.drop = std::move(drop);
	db.encounters["pair"] = pair;
	return db;
}

BattleSetup strongSetup(const Database& db, const std::string& encounter, std::uint64_t seed) {
	BattleSetup setup = makeSetup(db, {}, seed);
	useEncounter(setup, db, encounter);
	for (PartyMember& member : setup.party) {
		member = makePartyMember(db, *db.character(member.characterId), 10);
	}
	return setup;
}

BattleSetup vehicleSetup(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed) {
	BattleSetup setup = makeSetup(db, enemies, seed);
	setup.flags.vehicleBattle = true;
	setup.vehicle = "reed_skimmer";
	return setup;
}

constexpr int kVehicle = 5;

}

// One roll of 0 to 127 per won battle must come out below the encounter's rate; the drop is one of its item.
TEST(theEncounterDropRollsOncePerWonBattle) {
	for (int rate : {0, 32, 96, 127}) {
		const Database db = fragileMites({"clear_gel", rate});
		int drops = 0;
		const int battles = 800;
		for (std::uint64_t seed = 1; seed <= static_cast<std::uint64_t>(battles); ++seed) {
			Battle battle = makeBattle(db, strongSetup(db, "pair", seed));
			const std::vector<Event> events = playToEnd(battle, 10);
			CHECK(battle.outcome() == Outcome::Victory);
			const Rewards rewards = battle.rewards();
			CHECK(rewards.items.empty() || rewards.items == (Inventory{{"clear_gel", 1}}));
			drops += rewards.items.empty() ? 0 : 1;
			const std::vector<Event> ended = ofType(events, EventType::BattleEnded);
			CHECK(ended.size() == 1 && ended[0].items == rewards.items);
		}
		const double share = 100.0 * drops / battles;
		const double expected = 100.0 * rate / 128;
		CHECK(share > expected - 5 && share < expected + 5);
		CHECK(rate > 0 || drops == 0);
	}
}

TEST(theRewardMultiplierLeavesDropsAlone) {
	const Database db = fragileMites({"tonic", 64});
	for (std::uint64_t seed = 1; seed <= 20; ++seed) {
		BattleSetup setup = strongSetup(db, "pair", seed);
		Battle single = makeBattle(db, setup);
		playToEnd(single, 10);
		setup.rewardMultiplier = RewardMultiplier::X2;
		Battle doubled = makeBattle(db, setup);
		playToEnd(doubled, 10);
		CHECK_EQ(doubled.rewards().experience, 2 * single.rewards().experience);
		CHECK(doubled.rewards().items == single.rewards().items);
	}
}

TEST(onlyAVictoryDrops) {
	Database db = fragileMites({"tonic", 127});
	db.enemies.at("brass_warden").stats.maxHp = 9999;
	db.encounters["pair"].enemies[1] = {"brass_warden", "brass_warden"};
	BattleSetup setup = strongSetup(db, "pair", 3);
	setup.scriptedEnd.kind = ScriptedEndKind::EnemyDefeated;
	setup.scriptedEnd.enemySlot = 0;
	Battle battle = makeBattle(db, setup);
	playToEnd(battle, 20);
	CHECK(battle.outcome() == Outcome::ScriptedEnd);
	CHECK(battle.rewards().items.empty());

	Database strong = db;
	strong.encounters["warden"].drop = {"tonic", 127};
	Battle lost = makeBattle(strong, strongSetup(strong, "warden", 5));
	for (int i = 0; i < lost.partyCount(); ++i) {
		lost.combatant(i).hp = 1;
	}
	const std::vector<Event> events = playToEnd(lost, 30);
	CHECK(lost.outcome() == Outcome::Defeat);
	CHECK(lost.rewards().items.empty());
	CHECK(ofType(events, EventType::BattleEnded).at(0).items.empty());
}

TEST(theLoaderChecksDrops) {
	auto files = dataTexts();
	const std::string from = "\"drop\": {\"item\": \"tonic\", \"rate\": 58}";
	const std::size_t at = files["enemies.json"].find(from);
	CHECK(at != std::string::npos);
	files["enemies.json"].replace(at, from.size(), "\"drop\": {\"item\": \"tonik\", \"rate\": 128}");
	const Result<Database> loaded = loadDatabaseFromTexts(files);
	CHECK(!loaded.ok());
	CHECK(loaded.error.find("drop names missing item 'tonik'") != std::string::npos);
	CHECK(loaded.error.find("field 'rate' must be between 0 and 127") != std::string::npos);
}

// A level-up raises the maximums and leaves current hit points and technique points alone, however many levels.
TEST(levelUpsDoNotHeal) {
	const Database& db = data();
	PartyMember fighter = makePartyMember(db, db.characters.at("fighter"), 1);
	fighter.hp = 20;
	fighter.tp = 2;
	const LevelUp up = grantExperience(fighter, db, 400);
	CHECK(up.levelsGained >= 2);
	CHECK_EQ(fighter.hp, 20);
	CHECK_EQ(fighter.tp, 2);
	CHECK(memberStats(db, fighter).maxHp > 20 + up.levelsGained);
}

TEST(theVehicleBattleFlagFieldsTheVehicleWithThePartyAboard) {
	const Database& db = data();
	Battle battle = makeBattle(db, vehicleSetup(db, {"rust_hound"}, 4));
	CHECK_EQ(battle.partyCount(), 6);
	CHECK_EQ(battle.combatant(kVehicle).defId, std::string("reed_skimmer"));
	CHECK_EQ(battle.combatant(kVehicle).basicAttack, std::string("ram"));
	CHECK(battle.combatant(kVehicle).knows("hull_vent"));
	CHECK_EQ(battle.setCommand(kFighter, Command::defend()).key, std::string("command.aboard"));
	// The vehicle attacks, uses its skills or escapes; it cannot defend or use items.
	CHECK_EQ(battle.setCommand(kVehicle, Command::defend()).key, std::string("command.vehicle_cannot"));
	CHECK_EQ(battle.setCommand(kVehicle, Command::item("tonic", kVehicle)).key, std::string("command.vehicle_cannot"));
	CHECK(battle.setCommand(kVehicle, Command::ability("hull_vent", kVehicle + 1)).empty());
	CHECK(battle.setCommand(kVehicle, Command::escape()).empty());
	CHECK(battle.setCommand(kVehicle, Command::attack(kVehicle + 1)).empty());
	CHECK(battle.livingFoes(kVehicle + 1) == std::vector<int>{kVehicle});
	CHECK(battle.allies(kVehicle) == std::vector<int>{kVehicle});
	const std::vector<Event> events = battle.resolveRound();
	const std::vector<Event> started = ofType(events, EventType::BattleStarted);
	CHECK(started.size() == 1 && started[0].flags.vehicleBattle && started[0].ids[kVehicle] == "reed_skimmer");
	for (const Event& e : ofType(events, EventType::ActionStarted)) {
		CHECK(e.actor >= kVehicle);
	}

	BattleSetup bad = vehicleSetup(db, {"rust_hound"}, 4);
	bad.vehicle = "kite";
	CHECK(!Battle::create(db, bad, nullptr).ok());
	bad = makeSetup(db, {"rust_hound"}, 4);
	bad.vehicle = "reed_skimmer";
	CHECK(!Battle::create(db, bad, nullptr).ok());
}

// The vehicle starts at full hit points; when it falls, every member aboard is defeated with it.
TEST(aFallenVehicleTakesTheCrewWithIt) {
	const Database& db = data();
	Battle battle = makeBattle(db, vehicleSetup(db, {"rust_hound", "brass_warden"}, 9));
	CHECK_EQ(battle.combatant(kVehicle).hp, battle.combatant(kVehicle).stats.maxHp);
	std::vector<Event> all;
	while (battle.outcome() == Outcome::Ongoing && battle.round() < 60) {
		battle.setCommand(kVehicle, Command::attack(-1));
		const std::vector<Event> events = battle.resolveRound();
		all.insert(all.end(), events.begin(), events.end());
	}
	CHECK(battle.outcome() == Outcome::Defeat);
	CHECK(!battle.combatant(kVehicle).alive());
	for (const Event& e : ofType(all, EventType::DamageDealt)) {
		CHECK(e.target >= kVehicle);
	}
	std::vector<int> fallen;
	for (const Event& e : ofType(all, EventType::CombatantDefeated)) {
		if (e.target <= kVehicle) {
			fallen.push_back(e.target);
		}
	}
	CHECK_EQ(fallen, (std::vector<int>{kVehicle, kFighter, kMystic, kScout, kMedic, kEngineer}));
	const std::vector<PartyMember> party = battle.partyState();
	CHECK_EQ(party.size(), std::size_t(5));
	for (int i = 0; i < kVehicle; ++i) {
		CHECK_EQ(party[i].hp, 0);
	}
}

// A vehicle battle halves the experience; the members aboard split it.
TEST(theCrewSharesAVehicleVictory) {
	Database db = fragileMites({});
	db.enemies.at("gel_mite").experience = 21;
	BattleSetup setup = vehicleSetup(db, {"gel_mite"}, 2);
	setup.party[kMedic].hp = 0;
	Battle battle = makeBattle(db, setup);
	battle.setCommand(kVehicle, Command::attack(kVehicle + 1));
	battle.resolveRound();
	CHECK(battle.outcome() == Outcome::Victory);
	const Rewards rewards = battle.rewards();
	CHECK_EQ(rewards.experience, 10);
	CHECK_EQ(rewards.memberExperience, (std::vector<int>{2, 2, 2, 0, 2}));

	ComboLog log;
	std::vector<Event> replayed;
	Result<BattleRecord> parsed = parseRecord(serializeRecord(battle.record()));
	CHECK(parsed.ok() && parsed.value->setup.vehicle == "reed_skimmer");
	Database again = db;
	CHECK(replayBattle(again, *parsed.value, log, replayed).ok());
}
