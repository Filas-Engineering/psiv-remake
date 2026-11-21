#include "test.h"

#include "game/save.h"
#include "game/session.h"

#include <cstdio>

using namespace game;

namespace {

// Moves, turns, hides and shows on the tidewater hooks. The guide walks to the stall and turns toward the
// dockhand, who still stands at its talk point; the peddler, whose talk point is on the reed path, is placed here.
// The hold moves the dockhand before an event battle, so the pre-battle autosave holds a scripted actor.
const char* kActorScenes = R"(event tidewater_arrival skippable
	set tidewater.arrived
end
event tidewater_guide unskippable
	lock player
	move guide to stall_front
	face guide dockhand
	hide dockhand
	say guide "Watch the dock."
	show dockhand
	move peddler to dock_front
	move ferrywoman to 2 0 6.4
	face guide fighter
	unlock player
end
event tidewater_hold unskippable
	lock player
	move dockhand to dock_front
	face dockhand fighter
	battle training
	win
	end
	move dockhand to stall_front
	unlock player
end
)";

const ActorView* find(const std::vector<ActorView>& actors, const std::string& id) {
	for (const ActorView& a : actors) {
		if (a.id == id) {
			return &a;
		}
	}
	return nullptr;
}

bool at(const ActorView* actor, Cell cell, Facing facing, bool visible, bool scripted) {
	return actor && actor->cell == cell && actor->facing == facing && actor->visible == visible && actor->scripted == scripted;
}

}

TEST(actorsFollowScriptMovesTurnsAndVisibility) {
	const GameData d = test::withScript("events/tidewater.ev", kActorScenes);
	Session s(d, SessionOptions());
	test::placeParty(s, "tidewater", "guide_front", Facing::North);
	std::vector<ActorView> here = s.actorsHere();
	CHECK_EQ(here.size(), 2u);
	CHECK(here.size() == 2 && here[0].id == "dockhand" && here[1].id == "guide");
	CHECK(at(find(here, "dockhand"), {10, 2}, Facing::South, true, false));
	CHECK(at(find(here, "guide"), {5, 2}, Facing::South, true, false));

	CHECK(s.interact().empty());
	CHECK(s.linePending());
	here = s.actorsHere();
	// Along x from the talk point, then down to the stall's front; then turned toward the dockhand's talk point.
	CHECK(at(find(here, "guide"), {8, 3}, Facing::East, true, true));
	CHECK(at(find(here, "dockhand"), {10, 2}, Facing::South, false, true));
	CHECK(!find(here, "peddler"));

	s.confirmLine();
	CHECK(s.mode() == Mode::Field);
	here = s.actorsHere();
	CHECK_EQ(here.size(), 4u);
	CHECK(at(find(here, "dockhand"), {10, 2}, Facing::South, true, true));
	CHECK(at(find(here, "guide"), {8, 3}, Facing::West, true, true));
	CHECK(at(find(here, "peddler"), {10, 3}, Facing::South, true, true));
	// A position is x and z on the grid, rounded.
	CHECK(at(find(here, "ferrywoman"), {2, 6}, Facing::South, true, true));

	// Actors keep their places when the party leaves: the peddler is no longer at its reed path talk point.
	test::placeParty(s, "reed_path", "peddler_front", Facing::South);
	s.world().actors = {{"peddler", {"tidewater", {10, 3}, Facing::South, true}}};
	CHECK(s.actorsHere().empty());
	s.world().actors.clear();
	here = s.actorsHere();
	CHECK(here.size() == 1 && at(find(here, "peddler"), {10, 7}, Facing::South, true, false));
}

TEST(actorPlacesAreSavedAndMigrated) {
	const GameData d = test::withScript("events/tidewater.ev", kActorScenes);
	Session s(d, SessionOptions());
	test::placeParty(s, "tidewater", "guide_front", Facing::North);
	s.interact();
	test::confirmLines(s);
	const SaveFile save = s.makeSave(false);
	const std::string text = serializeSave(save);
	CHECK(text.compare(0, 11, "PSR-SAVE 5\n") == 0);
	CHECK(text.find("actor guide tidewater 8 3 west 1\n") != std::string::npos);
	rules::Result<SaveFile> parsed = parseSave(text);
	CHECK(parsed.ok());
	std::string diff;
	CHECK(parsed.ok() && sameWorld(parsed.value->world, save.world, &diff));
	CHECK_EQ(diff, std::string(""));

	// A schema 2 save has no actors chunk (nor the talk and travel chunks after it); it loads with every actor at its talk point.
	std::string old = text;
	old.replace(0, 10, "PSR-SAVE 2");
	old.replace(old.find("schema 5\n"), 9, "schema 2\n");
	const std::size_t chunk = old.find("chunk actors 1 ");
	old.erase(chunk, old.find("\nend ") + 1 - chunk);
	const std::string body = old.substr(0, old.rfind("\nend ") + 1);
	std::uint64_t hash = 1469598103934665603ULL;
	for (unsigned char c : body) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	char sum[17];
	std::snprintf(sum, sizeof sum, "%016llx", static_cast<unsigned long long>(hash));
	rules::Result<SaveFile> migrated = parseSave(body + "end " + sum + "\n");
	CHECK(migrated.ok());
	CHECK(migrated.ok() && migrated.value->world.actors.empty());
	CHECK(migrated.ok() && serializeSave(*migrated.value).find("chunk actors 1 0\n") != std::string::npos);
}

TEST(theMidEventAutosaveKeepsScriptedActors) {
	const GameData d = test::withScript("events/tidewater.ev", kActorScenes);
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("actors_midevent");
	Session s(d, options);
	test::placeParty(s, "tidewater", "hold_front", Facing::North);
	s.interact();
	CHECK(s.mode() == Mode::Battle);
	const std::string slot = s.store()->latest();
	CHECK(!slot.empty());

	Session loaded(d, options);
	CHECK(loaded.load(slot).empty());
	CHECK(loaded.mode() == Mode::Event || loaded.mode() == Mode::Battle);
	std::vector<ActorView> here = loaded.actorsHere();
	CHECK(at(find(here, "dockhand"), {10, 3}, Facing::East, true, true));
	loaded.update(16);
	CHECK(loaded.mode() == Mode::Battle);
	CHECK(at(find(loaded.actorsHere(), "dockhand"), {10, 3}, Facing::East, true, true));
	loaded.playBattle();
	test::confirmLines(loaded);
	CHECK(loaded.mode() == Mode::Field);
	CHECK(at(find(loaded.actorsHere(), "dockhand"), {8, 3}, Facing::West, true, true));
}

TEST(theArrivalWalksTheDockhandOverAndBack) {
	Session s(test::data(), SessionOptions());
	CHECK(s.newGame().empty());
	CHECK(s.linePending());
	CHECK(at(find(s.actorsHere(), "dockhand"), {10, 2}, Facing::South, true, false));
	s.confirmLine();
	CHECK(s.linePending());
	CHECK(at(find(s.actorsHere(), "dockhand"), {5, 4}, Facing::West, true, true));
	s.confirmLine();
	CHECK(s.mode() == Mode::Field);
	CHECK(at(find(s.actorsHere(), "dockhand"), {10, 2}, Facing::South, true, true));
}
