#include "fixtures.h"
#include "rules/record.h"
#include "test.h"

using namespace rules;
using namespace fixtures;

namespace {

// Plays a battle that uses a macro, items, explicit enemy commands and the auto policy.
std::vector<Event> playMixed(Battle& battle) {
	std::vector<Event> all;
	auto append = [&all](const std::vector<Event>& events) {
		all.insert(all.end(), events.begin(), events.end());
	};
	battle.applyMacro(*battle.database().macro("tempest"));
	battle.setCommand(kFirstEnemy + 1, Command::defend());
	append(battle.resolveRound());
	battle.setCommand(kMedic, Command::item("tonic", kFighter));
	battle.setCommand(kMystic, Command::item("spark_flask", kFirstEnemy));
	append(battle.resolveRound());
	append(playToEnd(battle, 30));
	return all;
}

}

TEST(recordReplaysToByteIdenticalEvents) {
	for (std::uint64_t seed : {1u, 8u, 21u}) {
		BattleSetup setup = makeSetup(data(), {}, seed);
		useEncounter(setup, data(), "marsh");
		setup.difficulty = "hard";
		setup.rewardMultiplier = RewardMultiplier::X2;
		ComboLog log;
		log.discover("storm_spark", 125000, {"mystic", "scout"});
		Battle battle = makeBattle(data(), setup, &log);
		const std::string played = serializeEvents(playMixed(battle));
		const std::string text = serializeRecord(battle.record());

		const Result<BattleRecord> parsed = parseRecord(text);
		CHECK(parsed.ok());
		if (!parsed.ok()) {
			continue;
		}
		ComboLog replayLog;
		std::vector<Event> events;
		const Result<Battle> replayed = replayBattle(data(), *parsed.value, replayLog, events);
		CHECK(replayed.ok());
		if (!replayed.ok()) {
			continue;
		}
		CHECK_EQ(serializeEvents(events), played);
		CHECK_EQ(serializeRecord(replayed.value->record()), text);
		CHECK(replayed.value->outcome() == battle.outcome());
		CHECK_EQ(replayed.value->rewards().experience, battle.rewards().experience);
		CHECK(replayLog.discovered() == log.discovered());
	}
}

TEST(replayRefusesOtherRulesOrData) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	playToEnd(battle, 10);
	ComboLog log;
	std::vector<Event> events;

	BattleRecord record = battle.record();
	record.rulesVersion = kRulesVersion + 1;
	Result<Battle> refused = replayBattle(data(), record, log, events);
	CHECK(!refused.ok() && refused.error.find("rules version") != std::string::npos);

	Database changed = data();
	changed.contentHash ^= 1;
	refused = replayBattle(changed, battle.record(), log, events);
	CHECK(!refused.ok() && refused.error.find("different data") != std::string::npos);

	record = battle.record();
	record.rounds.push_back(record.rounds.back());
	refused = replayBattle(data(), record, log, events);
	CHECK(!refused.ok() && refused.error.find("already ended") != std::string::npos);
}

TEST(recordParserReportsTheLine) {
	const std::string text = serializeRecord(makeBattle(data(), {"gel_mite"}, 1).record());
	CHECK(parseRecord(text).ok());
	CHECK(parseRecord("battle_record 9\nend\n").error.find("record line 1: record format 9 is not supported") != std::string::npos);
	std::string broken = text;
	broken.replace(broken.find("seed 1"), 6, "seed x");
	CHECK_EQ(parseRecord(broken).error, std::string("record line 5: bad value in 'seed x'"));
	CHECK(parseRecord(text.substr(0, text.find("end\n"))).error.find("without 'end'") != std::string::npos);
	CHECK_EQ(parseRecord(text.substr(0, text.find("checksum"))).error, std::string("record has no checksum line"));
}

TEST(eventTextIsStable) {
	Event damage;
	damage.type = EventType::DamageDealt;
	damage.round = 3;
	damage.target = 5;
	damage.amount = 12;
	damage.element = Element::Shock;
	damage.effectiveness = Effectiveness::Weak;
	CHECK_EQ(serializeEvent(damage), std::string("3 DamageDealt target=5 amount=12 element=shock effectiveness=weak"));

	Event combo;
	combo.type = EventType::ComboTriggered;
	combo.round = 1;
	combo.actor = 1;
	combo.ability = "storm spark";
	combo.combo = "storm_spark";
	combo.participants = {1, 2};
	CHECK_EQ(serializeEvent(combo), std::string("1 ComboTriggered actor=1 ability=storm%20spark combo=storm_spark participants=1,2"));
}

TEST(aDamagedRecordIsRefused) {
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, 5);
	playToEnd(battle, 10);
	const std::string text = serializeRecord(battle.record());
	CHECK(parseRecord(text).ok());
	int loaded = 0;
	for (std::size_t at = 0; at < text.size(); ++at) {
		std::string damaged = text;
		damaged[at] = static_cast<char>(damaged[at] ^ 0x04);
		loaded += parseRecord(damaged).ok() ? 1 : 0;
	}
	CHECK_EQ(loaded, 0);
	std::string swapped = text;
	swapped.replace(swapped.find("seed 5"), 6, "seed 6");
	CHECK_EQ(parseRecord(swapped).error, std::string("record checksum does not match: the record is damaged"));
}
