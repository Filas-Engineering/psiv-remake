#include "test.h"

#include "game/save.h"
#include "game/session.h"

#include <cstdio>
#include <optional>

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
	CHECK(text.compare(0, 12, "PSR-SAVE 12\n") == 0);
	CHECK(text.find("actor guide tidewater 8 3 west 1\n") != std::string::npos);
	rules::Result<SaveFile> parsed = parseSave(text);
	CHECK(parsed.ok());
	std::string diff;
	CHECK(parsed.ok() && sameWorld(parsed.value->world, save.world, &diff));
	CHECK_EQ(diff, std::string(""));

	// A schema 2 save has no actors chunk (nor the talk, travel and dev chunks after it); it loads with every actor at its talk point.
	std::string old = text;
	old.replace(0, 11, "PSR-SAVE 2");
	old.replace(old.find("schema 12\n"), 10, "schema 2\n");
	// The header chunk's payload is a byte shorter.
	const std::size_t size = old.find("chunk header 1 ") + 15;
	old.replace(size, old.find('\n', size) - size, std::to_string(std::stoi(old.substr(size)) - 1));
	const std::size_t chunk = old.find("chunk actors 2 ");
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
	CHECK(migrated.ok() && serializeSave(*migrated.value).find("chunk actors 2 0\n") != std::string::npos);
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

namespace {

std::optional<ActorView> copy(const std::vector<ActorView>& actors, const std::string& id) {
	const ActorView* found = find(actors, id);
	return found ? std::optional<ActorView>(*found) : std::nullopt;
}

std::optional<ActorView> view(const Session& s, const std::string& id) {
	return copy(s.actorsHere(), id);
}

bool at(const std::optional<ActorView>& actor, Cell cell, Facing facing, bool visible, bool scripted) {
	return actor && at(&*actor, cell, facing, visible, scripted);
}

// Animations, removals and a place in Wren Hollow, talked to from the counter's front.
const char* kHollowScene = R"(event hollow_keeper unskippable
	anim tallykeeper hop
	anim hollow_sentinel dissolve
	say tallykeeper "Hm."
	anim hollow_sentinel fade_in
	place hollow_drifter west_post facing north
	anim hollow_drifter bow
	anim hollow_drifter wave loop
	remove hollow_drifter until reload
	face tallykeeper west
	say tallykeeper "Done."
	remove tallykeeper
end
)";

}

TEST(actorAnimationsShowHideAndRunDownWithTime) {
	GameData d = test::actorDataWithScript("events/hollow.ev", kHollowScene);
	d.config.presentation.animationSeconds["dissolve"] = 1.0;
	Session s(d, SessionOptions());
	test::placeParty(s, "wren_hollow", "counter_front", Facing::North);
	std::optional<ActorView> keeper = view(s, "tallykeeper");
	// The keeper stands on its hook's actor_at, behind the counter, not on the hook's cell.
	CHECK(at(keeper, {4, 1}, Facing::South, true, false));
	CHECK(keeper && keeper->solid && keeper->animation.empty());
	CHECK(s.interact().empty());
	CHECK(s.linePending());
	std::vector<ActorView> here = s.actorsHere();
	keeper = copy(here, "tallykeeper");
	std::optional<ActorView> sentinel = copy(here, "hollow_sentinel");
	CHECK(keeper && keeper->animation == "hop" && keeper->animationMs == 300 && keeper->animationTotalMs == 300 && keeper->visible);
	// The data's length for dissolve stands in for its own; a hidden actor stops blocking its cell.
	CHECK(sentinel && !sentinel->visible && sentinel->vanish == ActorVanish::Dissolved && sentinel->animationMs == 1000 && !sentinel->solid);
	s.update(100);
	keeper = view(s, "tallykeeper");
	CHECK(keeper && keeper->animationMs == 200 && keeper->animationTotalMs == 300);

	s.confirmLine();
	CHECK(s.linePending());
	here = s.actorsHere();
	sentinel = copy(here, "hollow_sentinel");
	std::optional<ActorView> drifter = copy(here, "hollow_drifter");
	CHECK(sentinel && sentinel->visible && sentinel->vanish == ActorVanish::None && sentinel->animation == "fade_in" && sentinel->animationMs == 500);
	// A pose with no length and a loop are the front end's; the removal hides the drifter until the area reloads.
	CHECK(at(drifter, {1, 4}, Facing::North, false, true));
	CHECK(drifter && drifter->removed == ActorRemoval::UntilReload && drifter->animation.empty());
	CHECK(at(find(here, "tallykeeper"), {4, 1}, Facing::West, true, true));

	s.confirmLine();
	CHECK(s.mode() == Mode::Field);
	keeper = view(s, "tallykeeper");
	CHECK(keeper && keeper->removed == ActorRemoval::ForGood && !keeper->visible && !keeper->solid);
	CHECK_EQ(s.interact().key, std::string("game.refuse.no_answer"));
	for (int i = 0; i < 4; ++i) {
		s.update(d.config.tickMs);
	}
	sentinel = view(s, "hollow_sentinel");
	CHECK(sentinel && sentinel->animationMs == 300);
	s.update(400);
	CHECK(view(s, "hollow_sentinel")->animation.empty());

	// The next area load brings the drifter back where it stood; the keeper stays gone for good.
	CHECK(s.devWarp("wren_hollow", "hollow_door").empty());
	drifter = view(s, "hollow_drifter");
	CHECK(drifter && drifter->removed == ActorRemoval::None && drifter->visible);
	CHECK(view(s, "tallykeeper")->removed == ActorRemoval::ForGood);
}

TEST(actorStateMidAnimationIsSavedAndOlderSavesLoad) {
	const GameData d = test::actorDataWithScript("events/hollow.ev", kHollowScene);
	Session s(d, SessionOptions());
	test::placeParty(s, "wren_hollow", "counter_front", Facing::North);
	s.interact();
	s.confirmLine();
	s.update(100);
	const SaveFile save = s.makeSave(false);
	const std::string text = serializeSave(save);
	CHECK(text.find("chunk actors 2 ") != std::string::npos);
	CHECK(text.find("anim hollow_sentinel fade_in 400 500\n") != std::string::npos);
	CHECK(text.find("removed hollow_drifter until_reload\n") != std::string::npos);
	CHECK(text.find("vanish hollow_sentinel") == std::string::npos);
	rules::Result<SaveFile> parsed = parseSave(text);
	std::string diff;
	CHECK(parsed.ok() && sameWorld(parsed.value->world, save.world, &diff));
	CHECK_EQ(diff, std::string(""));
	// The mid-event save resumes with the animation where it was.
	Session loaded(d, SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	std::optional<ActorView> sentinel = view(loaded, "hollow_sentinel");
	CHECK(sentinel && sentinel->animation == "fade_in" && sentinel->animationMs == 400);

	// A schema 10 save has actors chunk 1, with none of the new lines; it loads with nothing vanished, playing or removed.
	Session plain(test::data(), SessionOptions());
	test::placeParty(plain, "tidewater", "guide_front", Facing::North);
	plain.world().actors["guide"] = ActorPlace{"tidewater", {8, 3}, Facing::West, false};
	std::string old = serializeSave(plain.makeSave(false));
	old.replace(0, 11, "PSR-SAVE 10");
	old.replace(old.find("schema 12\n"), 10, "schema 10\n");
	old.replace(old.find("chunk actors 2 "), 15, "chunk actors 1 ");
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
	const ActorPlace* guide = migrated.ok() && migrated.value->world.actors.count("guide") ? &migrated.value->world.actors.at("guide") : nullptr;
	CHECK(guide && !guide->visible && guide->vanish == ActorVanish::None && guide->animation.empty() && guide->removed == ActorRemoval::None);
}

TEST(anEnemyFigureActorStandsSolidAndNeverFights) {
	Session s(test::actorData(), SessionOptions());
	test::placeParty(s, "wren_hollow", "hollow_door", Facing::North);
	std::optional<ActorView> sentinel = view(s, "hollow_sentinel");
	CHECK(at(sentinel, {7, 4}, Facing::West, true, false));
	CHECK(sentinel && sentinel->solid && sentinel->spriteEnemy == "aegis_sentinel" && sentinel->look.empty());
	CHECK(s.world().enemies.empty());
	s.world().position = {6, 4};
	CHECK_EQ(s.walk(Facing::East).key, std::string("game.refuse.blocked"));
	CHECK(s.mode() == Mode::Field);
	CHECK(s.world().position == (Cell{6, 4}));
	// A walk goes around it.
	PathWalk walk = s.beginWalk({8, 4});
	WalkStatus status = WalkStatus::Walking;
	while (status == WalkStatus::Walking) {
		status = s.walkStep(walk);
	}
	CHECK(status == WalkStatus::Arrived);
	CHECK(s.mode() == Mode::Field);
	// Hidden, it no longer blocks.
	s.world().actors["hollow_sentinel"] = ActorPlace{"wren_hollow", {7, 4}, Facing::West, false};
	s.world().position = {6, 4};
	CHECK(s.walk(Facing::East).empty());
	CHECK(s.world().position == (Cell{7, 4}));
}

TEST(aMoveRoutesThroughItsWaypointsOrAroundWhatStandsInTheWay) {
	const GameData d = test::actorDataWithScript("events/hollow.ev", R"(event hollow_keeper unskippable
	place hollow_drifter west_post facing east
	move hollow_drifter to east_post pathfind
	say tallykeeper "Around."
	move hollow_drifter via shelf_front to west_post
	say tallykeeper "Back."
end
)");
	Session s(d, SessionOptions());
	test::placeParty(s, "wren_hollow", "counter_front", Facing::North);
	s.pollActions();
	CHECK(s.interact().empty());
	std::vector<PresentedAction> actions = s.pollActions();
	CHECK_EQ(actions.size(), 2u);
	CHECK(actions.size() == 2 && actions[0].action.type == events::ActionType::place_actor && actions[0].route == std::vector<Cell>({{1, 4}}));
	// Breadth first, north first: up a row, along it past the sentinel's cell, and down to the post.
	const std::vector<Cell> around = {{1, 4}, {1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 3}, {7, 3}, {8, 3}, {8, 4}};
	CHECK(actions.size() == 2 && actions[1].action.pathfind && actions[1].route == around);
	CHECK(at(view(s, "hollow_drifter"), {8, 4}, Facing::South, true, true));
	s.confirmLine();
	actions = s.pollActions();
	// Without pathfind each leg goes along x, then y, through whatever stands there.
	const std::vector<Cell> via = {{8, 4}, {7, 4}, {6, 4}, {5, 4}, {4, 4}, {3, 4}, {2, 4}, {2, 3}, {1, 3}, {1, 4}};
	CHECK(actions.size() == 1 && actions[0].route == via);
	CHECK(at(view(s, "hollow_drifter"), {1, 4}, Facing::South, true, true));
	s.confirmLine();
	CHECK(s.mode() == Mode::Field);
}
