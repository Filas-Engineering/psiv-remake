#include "fixtures.h"
#include "rules/loader.h"
#include "rules/record.h"
#include "test.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace rules;
using namespace fixtures;

namespace {

void replaceOnce(std::string& text, const std::string& from, const std::string& to) {
	const std::size_t at = text.find(from);
	if (at == std::string::npos) {
		::test::fail(__FILE__, __LINE__, "fixture text not found: " + from);
		return;
	}
	text.replace(at, from.size(), to);
}

bool hasEvent(const std::vector<Event>& events, EventType type) {
	return std::any_of(events.begin(), events.end(), [type](const Event& e) { return e.type == type; });
}

}

// A tonic on a fallen ally and a revive on a standing one reach nobody: refused with nothing spent unless the item is used
// up when it changes nothing and rules.json item_no_reach is consume, when it is spent with its no-effect event, in battle
// and in the field. The query names exactly the allies the command would be refused on.
TEST(itemsOnAnAllyOutOfReachAreRefusedOrSpent) {
	for (const auto& [rulesKeys, spent] : std::vector<std::pair<std::string, bool>>{
			 {"", false},
			 {"\"item_no_reach\": \"refuse\", \"consume_when_no_effect\": true,", false},
			 {"\"item_no_reach\": \"consume\",", false},
			 {"\"item_no_reach\": \"consume\", \"consume_when_no_effect\": true,", true}}) {
		std::map<std::string, std::string> texts = dataTexts();
		replaceOnce(texts["rules.json"], "{", "{" + rulesKeys);
		const Result<Database> loaded = loadDatabaseFromTexts(texts);
		CHECK(loaded.ok());
		if (!loaded.ok()) {
			continue;
		}
		const Database& db = *loaded.value;
		for (const char* item : {"tonic", "second_wind"}) {
			const bool revive = std::string(item) == "second_wind";
			Battle battle = makeBattle(db, {"gel_mite"}, 1, Inventory{{item, 2}});
			if (!revive) {
				battle.combatant(kFighter).hp = 0;
			}
			const std::vector<int> outOfReach = battle.alliesOutOfReach(kMedic, Command::item(item, -1));
			CHECK_EQ(outOfReach.empty(), spent);
			const Message set = battle.setCommand(kMedic, Command::item(item, kFighter));
			CHECK_EQ(set.key.empty(), spent);
			if (!spent) {
				CHECK_EQ(set.key, std::string("command.no_effect_on_target"));
				const std::vector<int> expected = revive ? std::vector<int>{0, 1, 2, 3, 4} : std::vector<int>{kFighter};
				CHECK(outOfReach == expected);
			} else {
				othersDefend(battle);
				const std::vector<Event> events = battle.resolveRound();
				CHECK(hasEvent(events, EventType::ItemHadNoEffect));
				CHECK(hasEvent(events, EventType::ResourceSpent));
				CHECK_EQ(battle.inventory().at(item), 1);
			}

			std::vector<PartyMember> party = makeSetup(db, {"gel_mite"}, 1).party;
			if (!revive) {
				party[kFighter].hp = 0;
			}
			const std::vector<int> fieldOut = fieldTargetsOutOfReach(db, party, item, true, -1);
			CHECK_EQ(fieldOut.empty(), spent);
			Inventory inventory{{item, 2}};
			const FieldUse use = useItemOutsideBattle(db, party, inventory, item, kFighter);
			CHECK_EQ(use.rejection.key.empty(), spent);
			CHECK_EQ(inventory.at(item), spent ? 1 : 2);
			if (spent) {
				CHECK(hasEvent(use.events, EventType::ItemHadNoEffect));
			} else {
				CHECK_EQ(use.rejection.key, std::string("field.no_target"));
				CHECK(std::find(fieldOut.begin(), fieldOut.end(), kFighter) != fieldOut.end());
			}
		}
	}
	std::map<std::string, std::string> broken = dataTexts();
	replaceOnce(broken["rules.json"], "{", "{\"item_no_reach\": \"waste\",");
	CHECK(!loadDatabaseFromTexts(broken).ok());
}

// The query for abilities: the medic's mend reaches every standing ally and no fallen one, in battle and in the field.
TEST(abilitiesOutOfReachAreTheAlliesTheyCannotAffect) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	battle.combatant(kScout).hp = 0;
	CHECK(battle.alliesOutOfReach(kMedic, Command::ability("mend", -1)) == std::vector<int>{kScout});
	CHECK(battle.alliesOutOfReach(kMedic, Command::attack(kFirstEnemy)).empty());
	std::vector<PartyMember> party = makeSetup(data(), {"gel_mite"}, 1).party;
	party[kScout].hp = 0;
	CHECK(fieldTargetsOutOfReach(data(), party, "mend", false, kMedic) == std::vector<int>{kScout});
	CHECK(fieldTargetsOutOfReach(data(), party, "no_such_thing", false, kMedic).empty());
}

// discardable defaults to true and loads as false.
TEST(itemsAreDiscardableUnlessTheDataSaysNot) {
	CHECK(data().item("tonic")->discardable);
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["items.json"], "{\"id\": \"tonic\",", "{\"id\": \"tonic\", \"discardable\": false,");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	if (loaded.ok()) {
		CHECK(!loaded.value->item("tonic")->discardable);
	}
}

// With the spark and the gust leading the round, storm_spark fires on every seed; the lead order is recorded and the
// record replays to the same events.
TEST(aLeadOrderMakesACombinationFireEverySeed) {
	int withoutLead = 0;
	for (std::uint64_t seed = 1; seed <= 12; ++seed) {
		for (bool lead : {false, true}) {
			ComboLog log;
			Battle battle = makeBattle(data(), {"gel_mite", "gel_mite"}, seed, &log);
			battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
			battle.setCommand(kScout, Command::ability("gust", kFirstEnemy));
			if (lead) {
				battle.setLeadOrder({kScout, kMystic});
			}
			partyDefends(battle);
			const std::vector<Event> events = battle.resolveRound();
			const bool fired = hasEvent(events, EventType::ComboTriggered);
			if (!lead) {
				withoutLead += fired ? 0 : 1;
				continue;
			}
			CHECK(fired);
			CHECK(battle.lastTurnOrder().size() >= 2 && battle.lastTurnOrder()[0] == kScout && battle.lastTurnOrder()[1] == kMystic);
			const std::string text = serializeRecord(battle.record());
			CHECK(text.find("lead_order 2 1\n") != std::string::npos);
			const Result<BattleRecord> parsed = parseRecord(text);
			CHECK(parsed.ok());
			if (!parsed.ok()) {
				continue;
			}
			ComboLog replayLog;
			std::vector<Event> replayed;
			CHECK(replayBattle(data(), *parsed.value, replayLog, replayed).ok());
			CHECK_EQ(serializeEvents(replayed), serializeEvents(events));
		}
	}
	// Without the lead order an enemy's turn falls between them on some seeds.
	CHECK(withoutLead > 0);
}
