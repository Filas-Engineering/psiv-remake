#include "fixtures.h"
#include "test.h"

using namespace rules;
using namespace fixtures;

namespace {

const Macro& opener() {
	return *data().macro("opener");
}

bool partyHasCommands(const Battle& battle) {
	for (int i = 0; i < battle.partyCount(); ++i) {
		if (battle.command(i).kind != CommandKind::None) {
			return true;
		}
	}
	return false;
}

void makeSturdy(Battle& battle) {
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).stats.maxHp = 9999;
		battle.combatant(i).hp = 9999;
	}
}

}

TEST(autoBattleFillsRoundsUntilAnEntryCannotBePaid) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 3);
	makeSturdy(battle);
	CHECK(battle.startAuto(opener()));
	// The engineer's drowse costs 4 of its 8 technique points, so the third command phase stops.
	for (int round = 1; round <= 2; ++round) {
		CHECK(battle.fillFromAuto());
		CHECK(battle.command(kMystic).kind == CommandKind::Ability && battle.command(kMystic).id == "spark");
		const std::vector<Event> events = battle.resolveRound();
		CHECK_EQ(ofType(events, EventType::CommandsLocked).at(0).macro, std::string("opener"));
	}
	CHECK(!battle.fillFromAuto());
	CHECK(!battle.autoActive() && battle.autoStop() == AutoStop::CannotPay);
	CHECK(!partyHasCommands(battle));
}

TEST(autoBattleStopsWhenThePlayerCancels) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 3);
	makeSturdy(battle);
	battle.startAuto(opener());
	CHECK(battle.fillFromAuto());
	battle.cancelAuto();
	// The round already filled still runs; the stop comes at the next command phase.
	battle.resolveRound();
	CHECK(battle.autoActive());
	CHECK(!battle.fillFromAuto());
	CHECK(battle.autoStop() == AutoStop::Cancelled && !partyHasCommands(battle));
}

TEST(autoBattleStopsWhenAMemberFalls) {
	Database db = data();
	db.enemies["brass_warden"].stats.dexterity = 5000;
	Battle battle = makeBattle(db, {"brass_warden"}, 3);
	makeSturdy(battle);
	battle.combatant(kScout).hp = 1;
	battle.startAuto(opener());
	CHECK(battle.fillFromAuto());
	battle.setCommand(kFirstEnemy, Command::attack(kScout));
	battle.resolveRound();
	CHECK(!battle.combatant(kScout).alive());
	CHECK(!battle.fillFromAuto());
	CHECK(battle.autoStop() == AutoStop::MemberFell);

	// A member already down when Auto starts is not a reason to stop.
	CHECK(battle.startAuto(opener()));
	CHECK(battle.fillFromAuto());
}

TEST(autoBattleStopsAtOnceWhenTheBattleEnds) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	Battle battle = makeBattle(db, {"gel_mite"}, 1);
	battle.combatant(kFirstEnemy).hp = 1;
	battle.startAuto(opener());
	CHECK(battle.fillFromAuto());
	battle.resolveRound();
	CHECK(battle.outcome() == Outcome::Victory);
	CHECK(!battle.autoActive() && battle.autoStop() == AutoStop::BattleEnded);
	CHECK(!battle.startAuto(opener()));
}

TEST(autoBattleRoundsReplayFromTheRecord) {
	ComboLog log;
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, 12, &log);
	std::vector<Event> played;
	battle.startAuto(*data().macro("regroup"));
	while (battle.outcome() == Outcome::Ongoing && battle.round() < 30) {
		if (!battle.fillFromAuto()) {
			partyDefends(battle);
		}
		const std::vector<Event> events = battle.resolveRound();
		played.insert(played.end(), events.begin(), events.end());
	}
	ComboLog replayLog;
	std::vector<Event> replayed;
	CHECK(replayBattle(data(), battle.record(), replayLog, replayed).ok());
	CHECK_EQ(serializeEvents(replayed), serializeEvents(played));
}
