#include "fixtures.h"
#include "rules/auto_policy.h"
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

namespace {

// The skimmer facing three gel mites that take its heat at normal, with an instant-defeat skill besides its own skills.
Database hexData(bool vulnerable, bool expected) {
	Database db = data();
	db.battleRules.autoExpected = expected;
	AbilityDef hex;
	hex.id = "hull_hex";
	hex.name = "Hull Hex";
	hex.kind = AbilityKind::Skill;
	hex.uses = 2;
	hex.targeting = Targeting::EnemyGroup;
	Effect defeat;
	defeat.kind = EffectKind::InstantDefeat;
	hex.effects = {defeat};
	db.abilities[hex.id] = hex;
	db.vehicles["reed_skimmer"].abilities.push_back(hex.id);
	db.enemies["gel_mite"].vulnerable = vulnerable;
	db.enemies["gel_mite"].affinities.erase(Element::Heat);
	return db;
}

Command vehicleChoice(const Database& db) {
	BattleSetup setup = makeSetup(db, {"gel_mite", "gel_mite", "gel_mite"}, 7);
	setup.flags.vehicleBattle = true;
	setup.vehicle = "reed_skimmer";
	Battle battle = makeBattle(db, setup);
	return autoCommand(battle, static_cast<int>(setup.party.size()));
}

}

// rules.json auto_policy expected weighs each attack by its expected effect: an instant defeat that the vulnerable,
// low-defense group almost surely cannot resist beats the strongest damage, an immune group makes it worth nothing, and
// `auto: "never"` keeps it out. The default `power` policy takes the largest damage power, as before.
TEST(theExpectedPolicyWeighsInstantDefeatAgainstTheTargets) {
	const Command hex = vehicleChoice(hexData(true, true));
	CHECK(hex.kind == CommandKind::Ability && hex.id == "hull_hex");
	const Command immune = vehicleChoice(hexData(false, true));
	CHECK(immune.kind == CommandKind::Ability && immune.id == "hull_vent");
	CHECK(vehicleChoice(hexData(true, false)).id == "hull_vent");
	Database never = hexData(true, true);
	never.abilities["hull_hex"].autoHint = AutoHint::Never;
	CHECK(vehicleChoice(never).id != "hull_hex");
	never.battleRules.autoExpected = false;
	CHECK(vehicleChoice(never).id != "hull_hex");

	// Under `power` a preferred damaging ability comes before a stronger one; under `expected` the hint doubles its worth.
	Database prefer = hexData(true, false);
	prefer.abilities["hull_patch"].autoHint = AutoHint::Avoid;
	prefer.abilities["rime_drill"] = prefer.abilities["hull_vent"];
	prefer.abilities["rime_drill"].id = "rime_drill";
	prefer.abilities["rime_drill"].effects.front().power = 1;
	prefer.abilities["rime_drill"].autoHint = AutoHint::Prefer;
	prefer.vehicles["reed_skimmer"].abilities.push_back("rime_drill");
	CHECK(vehicleChoice(prefer).id == "rime_drill");
	prefer.abilities["rime_drill"].autoHint = AutoHint::None;
	CHECK(vehicleChoice(prefer).id == "hull_vent");
}

// The policy per macro and per call (R173): the battle's setAutoPolicy, then the macro's policy, then rules.json; a
// macro's policy is saved with its options.
TEST(theAutoPolicyComesFromTheCallThenTheMacroThenTheRules) {
	const Database db = hexData(true, false);
	BattleSetup setup = makeSetup(db, {"gel_mite", "gel_mite", "gel_mite"}, 7);
	setup.flags.vehicleBattle = true;
	setup.vehicle = "reed_skimmer";
	Battle battle = makeBattle(db, setup);
	const int skimmer = static_cast<int>(setup.party.size());
	Macro macro;
	macro.name = "careful";
	macro.policy = MacroPolicy::Expected;
	CHECK(!battle.autoExpected(nullptr) && battle.autoExpected(&macro));
	CHECK(autoCommand(battle, skimmer).id == "hull_vent");
	battle.setAutoPolicy(MacroPolicy::Expected);
	CHECK(autoCommand(battle, skimmer).id == "hull_hex");
	battle.setAutoPolicy(MacroPolicy::Power);
	CHECK(!battle.autoExpected(&macro));
	const Result<std::vector<Macro>> parsed = parseMacros(serializeMacros({macro}));
	CHECK(parsed.ok() && parsed.value->front().policy == MacroPolicy::Expected);
	CHECK(serializeMacros({macro}).rfind("macros 4\n", 0) == 0);
	macro.policy = MacroPolicy::Default;
	macro.unnamed = MacroUnnamed::Auto;
	CHECK(serializeMacros({macro}).rfind("macros 3\n", 0) == 0);
}
