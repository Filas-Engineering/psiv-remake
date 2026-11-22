#include "test.h"

#include "game/save.h"
#include "game/session.h"

#include <filesystem>
#include <optional>
#include <string>

using namespace game;

// R102: Wren Den's plaque plays game.json's credits. Play time moves them on entry by entry; saving is refused while
// they play; closing them lets the event go on, and its `end game` writes the clear-data record and goes to the title.
TEST(creditsPlayByPlayTimeAndTheEndOfTheGameRecordsItAndGoesToTheTitle) {
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("ending");
	Session s(test::backgroundData(), options);
	test::placeParty(s, "wren_den", "plaque_front", Facing::East);
	CHECK(!s.credits());
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(s.menuKind() == MenuKind::Credits);
	std::optional<CreditsView> credits = s.credits();
	CHECK(credits && credits->id == "wren_end" && credits->music == "title_harbor_dawn");
	if (!credits) {
		return;
	}
	CHECK_EQ(credits->entries.size(), 4u);
	CHECK(credits->entries[0].kind == CreditsEntryKind::Heading && credits->entries[0].text == "Wren Hollow" && credits->entries[0].pauseMs == 2500);
	CHECK(credits->entries[1].kind == CreditsEntryKind::Line && credits->entries[1].key == "credits.wren_end.cast" && credits->entries[1].gap == 1);
	CHECK(credits->shown == 1 && credits->elapsedMs == 0 && credits->totalMs == 8000);
	s.update(2499);
	CHECK_EQ(s.credits()->shown, 1);
	s.update(1);
	CHECK_EQ(s.credits()->shown, 2);
	s.update(60000);
	CHECK(s.credits()->shown == 4 && s.credits()->elapsedMs == 8000);
	// The session never ends them itself: the front end says when.
	CHECK(s.menuKind() == MenuKind::Credits);
	CHECK_EQ(s.saveManual("manual_0").key, std::string("game.refuse.save_where"));
	CHECK(s.closeMenu().empty());
	CHECK(!s.credits());
	CHECK_EQ(s.lineText(), std::string("The den is quiet again."));
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Title);
	const std::vector<Output> out = s.drainOutput();
	CHECK(test::outputHas(out, "Clear data recorded."));
	CHECK(!test::outputHas(out, "Nobody reads this line."));
	std::string text;
	CHECK(readFile(s.store()->clearRecordPath(), text));
	const rules::Result<World> record = parseClearRecord(text);
	CHECK(record.ok());
	CHECK(record.ok() && record.value->roster.size() == s.world().roster.size());

	// A game a developer command marked records nothing, as when the clear flag is set.
	SessionOptions marked;
	marked.saveDirectory = test::freshDirectory("ending_marked");
	Session m(test::backgroundData(), marked);
	CHECK(m.newGame().empty());
	test::confirmLines(m);
	CHECK(m.devWarp("wren_den", "plaque_front").empty());
	CHECK(m.face(Facing::East).empty());
	CHECK(m.interact().empty());
	test::confirmLines(m);
	CHECK(m.closeMenu().empty());
	test::confirmLines(m);
	CHECK(m.mode() == Mode::Title);
	CHECK(test::outputHas(m.drainOutput(), "no clear-data record"));
	CHECK(!std::filesystem::exists(m.store()->clearRecordPath()));
}
