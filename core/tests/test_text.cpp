#include "fixtures.h"
#include "rules/field.h"
#include "rules/progression.h"
#include "rules/text.h"
#include "test.h"

#include <algorithm>

using namespace rules;
using namespace fixtures;

namespace {

bool listed(const std::string& key) {
	const std::vector<std::string> keys = messageKeys(data());
	return std::binary_search(keys.begin(), keys.end(), key);
}

}

TEST(everyMessageKeyHasEnglishText) {
	const Result<TextTable> table = loadTextTable("data/text_en.json");
	CHECK(table.ok());
	if (!table.ok()) {
		return;
	}
	for (const std::string& key : messageKeys(data())) {
		if (!table.value->count(key)) {
			::test::fail(__FILE__, __LINE__, "no English text for " + key);
		}
	}
	CHECK(listed("status.jumble.unchanged"));
	CHECK(!listed("status.poison.unchanged"));
	CHECK(!listed("status.defeated.applied"));
}

TEST(textTemplatesTakePositionalParameters) {
	const TextTable table = {{"equip.wrong_category", "{0} cannot equip {1}."}, {"odd", "{1}{0}{2} {x} {}"}};
	CHECK_EQ(formatText(table, "equip.wrong_category", {"Mira", "Copper Blade"}), std::string("Mira cannot equip Copper Blade."));
	CHECK_EQ(formatText(table, "odd", {"a", "b"}), std::string("ba {x} {}"));
	CHECK_EQ(formatText(table, "missing.key", {"a"}), std::string("missing.key"));
	CHECK(!parseTextTable("{\"text\": {\"a\": 1}}").ok());
	CHECK(!parseTextTable("{\"words\": {}}").ok());
}

TEST(rejectionsAreListedKeysNotSentences) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	std::vector<Message> refused = {
		battle.setCommand(99, Command::defend()),
		battle.setCommand(kFighter, Command::ability("spark", kFirstEnemy)),
		battle.setCommand(kMystic, Command::ability("storm_spark", kFirstEnemy)),
		battle.setCommand(kFighter, Command::item("nothing", -1)),
		battle.setCommand(kFirstEnemy, Command::escape()),
	};
	battle.combatant(kScout).hp = 0;
	refused.push_back(battle.setCommand(kScout, Command::defend()));
	PartyMember mystic = makePartyMember(data(), data().characters.at("mystic"), 1);
	EquipmentBag bag;
	refused.push_back(equip(mystic, data(), bag, "copper_blade"));
	refused.push_back(equip(mystic, data(), bag, "calm_charm"));
	std::vector<PartyMember> party = {mystic};
	Inventory inventory = {{"wake_chime", 1}};
	refused.push_back(useItemOutsideBattle(data(), party, inventory, "wake_chime", 0).rejection);
	refused.push_back(useAbilityOutsideBattle(data(), party, 0, "spark", 0).rejection);
	for (const Message& message : refused) {
		CHECK(!message.empty());
		if (!listed(message.key)) {
			::test::fail(__FILE__, __LINE__, "unlisted key " + message.key);
		}
	}
	CHECK_EQ(refused[1].params, (std::vector<std::string>{"fighter", "spark"}));
	CHECK_EQ(refused[7].key, std::string("equip.not_in_bag"));
}
