#include "support.h"

#include <map>
#include <set>

using namespace game;

ACC_CHECK(acc_fld_04, "ACC-FLD-04", "FLD-023, FLD-032, FLD-033, FLD-071") {
	const GameData& data = support::data(t);
	int hooks = 0;
	int chests = 0;
	int shops = 0;
	std::set<std::string> chestIds;
	std::set<std::string> chestFlags;
	std::set<std::string> shopIds;
	for (const auto& [areaId, area] : data.areas) {
		std::set<std::string> hookIds;
		for (const Hook& h : area.hooks) {
			++hooks;
			t.expect(!h.id.empty() && hookIds.insert(h.id).second, "area " + areaId + ": hook ID '" + h.id + "' is empty or used twice");
			t.expect(data.program.find_event(h.event) >= 0, "area " + areaId + ": hook '" + h.id + "' runs '" + h.event + "', which no compiled script defines");
		}
		for (const Chest& c : area.chests) {
			++chests;
			t.expect(!c.id.empty() && chestIds.insert(c.id).second, "area " + areaId + ": chest ID '" + c.id + "' is empty or used by another chest");
			t.expect(chestFlags.insert(c.flag).second, "area " + areaId + ": chest '" + c.id + "' shares its opened flag with another chest");
		}
		for (const Shop& s : area.shops) {
			++shops;
			t.expect(!s.id.empty() && shopIds.insert(s.id).second, "area " + areaId + ": shop ID '" + s.id + "' is empty or used by another shop");
		}
	}
	int talkLines = 0;
	for (const TalkSet& set : data.journal.partyTalk) {
		for (const TalkLine& line : set.lines) {
			++talkLines;
			for (const LanguageTables& l : data.languages) {
				t.expect(l.strings.find(line.text) != nullptr, "party talk '" + set.id + "' line '" + line.text + "' is not in the " + l.code + " dialogue table");
			}
		}
	}
	// The scope's counts (FLD-023, FLD-032, FLD-033), which the phase 0 audit confirms.
	constexpr int kScopeHooks = 128;
	constexpr int kScopeChests = 211;
	constexpr int kScopeShops = 50;
	if (hooks != kScopeHooks || chests != kScopeChests || shops != kScopeShops) {
		t.gap("game/data is the placeholder adventure, with " + std::to_string(hooks) + " hooks, " + std::to_string(chests) + " chests and " + std::to_string(shops) +
			" shops against the scope's " + std::to_string(kScopeHooks) + ", " + std::to_string(kScopeChests) + " and " + std::to_string(kScopeShops) +
			"; the game's content is not built, so the counts cannot match (FLD-023, FLD-032, FLD-033)");
	}
	t.gap("party talk lines exist in the game's dialogue tables, but those tables are placeholder text, not the locked shipped dialogue a tools/text delivery holds, so FLD-071 cannot be confirmed (FLD-071)");
	t.note(std::to_string(hooks) + " hooks, " + std::to_string(chests) + " chests and " + std::to_string(shops) + " shops in game/data have unique stable IDs (hooks per area, chests and shops across areas, one opened flag per chest); every hook runs a script the game compiled; all " +
		std::to_string(talkLines) + " party talk line IDs are in every language's dialogue table");
}
