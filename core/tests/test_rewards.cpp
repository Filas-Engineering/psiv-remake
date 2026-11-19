#include "fixtures.h"
#include "rules/loader.h"
#include "rules/progression.h"
#include "test.h"

using namespace rules;
using namespace fixtures;

namespace {

// Gel mites with 1 hit point fall to the first hit, so a party at level 10 wins in round 1.
Database fragileMites(std::vector<ItemDrop> drops) {
	Database db = data();
	EnemyDef& mite = db.enemies.at("gel_mite");
	mite.stats.maxHp = 1;
	mite.drops = std::move(drops);
	return db;
}

BattleSetup strongSetup(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed) {
	BattleSetup setup = makeSetup(db, enemies, seed);
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

TEST(dropsComeFromTheDropTableAndRideOnBattleEnded) {
	const Database db = fragileMites({{"tonic", 100, 2}, {"clear_gel", 50, 1}});
	int gels = 0;
	for (std::uint64_t seed = 1; seed <= 200; ++seed) {
		Battle battle = makeBattle(db, strongSetup(db, {"gel_mite", "gel_mite"}, seed));
		const std::vector<Event> events = playToEnd(battle, 10);
		CHECK(battle.outcome() == Outcome::Victory);
		const Rewards rewards = battle.rewards();
		CHECK_EQ(rewards.items.at("tonic"), 4);
		gels += rewards.items.count("clear_gel") ? rewards.items.at("clear_gel") : 0;
		const std::vector<Event> ended = ofType(events, EventType::BattleEnded);
		CHECK(ended.size() == 1 && ended[0].items == rewards.items);
		CHECK(battle.rewards().items == rewards.items);
	}
	// Two draws at 50% per battle.
	CHECK(gels > 160 && gels < 240);
}

TEST(theRewardMultiplierLeavesDropsAlone) {
	const Database db = fragileMites({{"tonic", 100, 3}, {"clear_gel", 40, 1}});
	for (std::uint64_t seed = 1; seed <= 20; ++seed) {
		BattleSetup setup = strongSetup(db, {"gel_mite"}, seed);
		Battle single = makeBattle(db, setup);
		playToEnd(single, 10);
		setup.rewardMultiplier = RewardMultiplier::X2;
		Battle doubled = makeBattle(db, setup);
		playToEnd(doubled, 10);
		CHECK_EQ(doubled.rewards().experience, 2 * single.rewards().experience);
		CHECK(doubled.rewards().items == single.rewards().items);
		CHECK_EQ(single.rewards().items.at("tonic"), 3);
	}
}

TEST(aScriptedEndDropsOnlyFromTheFallen) {
	Database db = fragileMites({{"tonic", 100, 1}});
	db.enemies.at("brass_warden").stats.maxHp = 9999;
	BattleSetup setup = strongSetup(db, {"gel_mite", "brass_warden"}, 3);
	setup.scriptedEnd.kind = ScriptedEndKind::EnemyDefeated;
	setup.scriptedEnd.enemySlot = 0;
	Battle battle = makeBattle(db, setup);
	playToEnd(battle, 20);
	CHECK(battle.outcome() == Outcome::ScriptedEnd);
	CHECK(battle.rewards().items == (Inventory{{"tonic", 1}}));
}

TEST(noDropsWithoutAWin) {
	Database db = data();
	db.enemies.at("brass_warden").drops = {{"tonic", 100, 1}};
	Battle battle = makeBattle(db, {"brass_warden", "brass_warden"}, 5);
	for (int i = 0; i < battle.partyCount(); ++i) {
		battle.combatant(i).hp = 1;
	}
	const std::vector<Event> events = playToEnd(battle, 30);
	CHECK(battle.outcome() == Outcome::Defeat);
	CHECK(battle.rewards().items.empty());
	CHECK(ofType(events, EventType::BattleEnded).at(0).items.empty());
}

TEST(theLoaderChecksDropTables) {
	auto files = dataTexts();
	const std::string from = "\"drops\": [{\"item\": \"tonic\", \"chance\": 20}]";
	const std::size_t at = files["enemies.json"].find(from);
	CHECK(at != std::string::npos);
	files["enemies.json"].replace(at, from.size(), "\"drops\": [{\"item\": \"tonik\", \"chance\": 0}]");
	const Result<Database> loaded = loadDatabaseFromTexts(files);
	CHECK(!loaded.ok());
	CHECK(loaded.error.find("drops missing item 'tonik'") != std::string::npos);
	CHECK(loaded.error.find("'chance'") != std::string::npos);
}

TEST(theVehicleBattleFlagFieldsTheVehicleWithThePartyAboard) {
	const Database& db = data();
	Battle battle = makeBattle(db, vehicleSetup(db, {"rust_hound"}, 4));
	CHECK_EQ(battle.partyCount(), 6);
	CHECK_EQ(battle.combatant(kVehicle).defId, std::string("reed_skimmer"));
	CHECK_EQ(battle.combatant(kVehicle).basicAttack, std::string("ram"));
	CHECK(battle.combatant(kVehicle).knows("steam_vent"));
	CHECK_EQ(battle.setCommand(kFighter, Command::defend()).key, std::string("command.aboard"));
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

TEST(damageToTheVehicleReachesTheCrewByItsData) {
	const Database& db = data();
	Battle battle = makeBattle(db, vehicleSetup(db, {"rust_hound", "brass_warden"}, 9));
	int vehicleHits = 0;
	while (battle.outcome() == Outcome::Ongoing && battle.round() < 40) {
		battle.setCommand(kVehicle, Command::defend());
		const std::vector<Event> events = battle.resolveRound();
		for (std::size_t i = 0; i < events.size(); ++i) {
			const Event& e = events[i];
			if (e.type != EventType::DamageDealt || e.target != kVehicle || e.amount == 0) {
				continue;
			}
			++vehicleHits;
			const int share = e.amount * 25 / 100;
			std::size_t next = i + 1;
			while (next < events.size() && events[next].type != EventType::DamageDealt) {
				++next;
			}
			if (share > 0 && next < events.size()) {
				CHECK(events[next].target < kVehicle && events[next].amount <= share);
			}
		}
	}
	CHECK(vehicleHits > 0);
	// The vehicle falling loses the battle even with members aboard still standing.
	CHECK(battle.outcome() == Outcome::Defeat);
	CHECK(!battle.combatant(kVehicle).alive());
	const std::vector<PartyMember> party = battle.partyState();
	CHECK_EQ(party.size(), std::size_t(5));
	for (int i = 0; i < kVehicle; ++i) {
		CHECK_EQ(party[i].hp, battle.combatant(i).hp);
		CHECK(party[i].hp < memberStats(db, party[i]).maxHp);
	}
}

TEST(theCrewSharesAVehicleVictory) {
	const Database db = fragileMites({});
	BattleSetup setup = vehicleSetup(db, {"gel_mite"}, 2);
	Battle battle = makeBattle(db, setup);
	battle.setCommand(kVehicle, Command::attack(kVehicle + 1));
	battle.resolveRound();
	CHECK(battle.outcome() == Outcome::Victory);
	const Rewards rewards = battle.rewards();
	CHECK_EQ(rewards.memberExperience.size(), std::size_t(5));
	for (int xp : rewards.memberExperience) {
		CHECK_EQ(xp, rewards.experience);
	}

	ComboLog log;
	std::vector<Event> replayed;
	Result<BattleRecord> parsed = parseRecord(serializeRecord(battle.record()));
	CHECK(parsed.ok() && parsed.value->setup.vehicle == "reed_skimmer");
	Database again = db;
	CHECK(replayBattle(again, *parsed.value, log, replayed).ok());
}
