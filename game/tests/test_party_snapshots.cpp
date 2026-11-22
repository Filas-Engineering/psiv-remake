#include "test.h"

#include "game/save.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace game;

namespace {

std::vector<std::string> rosterIds(const World& w) {
	std::vector<std::string> ids;
	for (const rules::PartyMember& m : w.roster) {
		ids.push_back(m.characterId);
	}
	return ids;
}

void toField(Session& s) {
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
}

const char* const kSplit = R"(event cove_bell unskippable
	party add mystic
	party add scout
	party add medic
	party remove scout
	party save before_road
	party swap mystic scout
	party swap 1 engineer
	party add engineer
	party remove medic
end

event cove_quiet skippable
	party restore before_road
end

event cove_campfire unskippable
	party restore nobody_saved
end
)";

}

// R73: a snapshot keeps the lineup and the reserve in order; restoring it after others joined and left brings back the
// lineup, puts the snapshot's reserve next and keeps newcomers at the end of the reserve, and touches nobody's level,
// pieces or hit points. Saves keep the snapshots; restoring an unknown one stops the event with its file and line.
TEST(partySnapshotsRestoreTheLineupAndReserveAndAreSaved) {
	const GameData data = test::withScript("events/bell_cove.ev", kSplit);
	Session s(data, SessionOptions());
	toField(s);
	CHECK(s.devRunEvent("cove_bell").empty());
	test::confirmLines(s);
	const World& w = s.world();
	const PartySnapshot saved{{"fighter", "mystic", "medic"}, {"scout"}};
	CHECK(w.partySnapshots.at("before_road") == saved);
	CHECK(w.lineup == std::vector<std::string>({"fighter", "scout", "engineer"}));
	CHECK(test::outputHas(s.drainOutput(), "'engineer' has not joined the party"));

	s.world().member("mystic")->hp = 7;
	const std::string mysticPieces = w.member("mystic")->equipment.begin()->second;
	const SaveFile save = s.makeSave(false);
	rules::Result<SaveFile> parsed = parseSave(serializeSave(save));
	std::string diff;
	CHECK(parsed.ok() && sameWorld(parsed.value->world, save.world, &diff));
	CHECK_EQ(diff, std::string(""));
	CHECK(parsed.ok() && parsed.value->world.partySnapshots == w.partySnapshots);

	CHECK(s.devRunEvent("cove_quiet").empty());
	test::confirmLines(s);
	CHECK(w.lineup == std::vector<std::string>({"fighter", "mystic", "medic"}));
	CHECK(rosterIds(w) == std::vector<std::string>({"fighter", "mystic", "medic", "scout", "engineer"}));
	CHECK_EQ(w.member("mystic")->hp, 7);
	CHECK_EQ(w.member("mystic")->equipment.begin()->second, mysticPieces);
	CHECK(test::outputHas(s.drainOutput(), "regroups"));

	CHECK(s.devRunEvent("cove_campfire").empty());
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	CHECK(test::outputHas(s.drainOutput(), "bell_cove.ev:18: there is no party snapshot 'nobody_saved'"));
}

// R73: the event-start world carries the snapshots, so a skip and a restart both apply them.
TEST(partySnapshotsApplyWhileSkipping) {
	const GameData data = test::withScript("events/bell_cove.ev", R"(event cove_bell skippable
	say guide "We split here."
	party add mystic
	party save pair
	party add scout
	say guide "And meet again."
	party restore pair
end
)");
	Session s(data, SessionOptions());
	toField(s);
	CHECK(s.devRunEvent("cove_bell").empty());
	CHECK(s.skip());
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	CHECK(s.world().lineup == std::vector<std::string>({"fighter", "mystic"}));
	CHECK(s.world().partySnapshots.count("pair") == 1);
}

// R74: a swap replaces a lineup member by slot or by name; the one replaced goes to the reserve and hands back what it
// wears by leave_equipment, unless the command says `equipment keep` or `equipment bag`.
TEST(aSwapReplacesALineupMemberAndFollowsTheEquipmentRule) {
	for (const LeaveEquipment rule : {LeaveEquipment::Keep, LeaveEquipment::Bag}) {
		GameData data = test::withScript("events/bell_cove.ev", R"(event cove_bell unskippable
	party add mystic
	party add scout
	party add medic
	party add engineer
	party add lancer
	party add harpooner
	party swap 2 lancer
	party swap scout harpooner equipment keep
	party swap 1 mystic equipment bag
end
)");
		data.config.leaveEquipment = rule;
		Session s(data, SessionOptions());
	toField(s);
		CHECK(s.devRunEvent("cove_bell").empty());
		test::confirmLines(s);
		const World& w = s.world();
		CHECK(w.lineup == std::vector<std::string>({"mystic", "lancer", "harpooner", "medic", "engineer"}));
		CHECK_EQ(w.member("mystic")->equipment.empty(), rule == LeaveEquipment::Bag);
		CHECK(!w.member("scout")->equipment.empty());
		CHECK(w.member("fighter")->equipment.empty());
	}
}
