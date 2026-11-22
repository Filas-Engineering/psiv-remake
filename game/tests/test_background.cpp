#include "test.h"

#include "game/save.h"
#include "game/session.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace game;

namespace {

// A new game in Wren Yard at the gate, its load hook's background event spawned.
void inYard(Session& s) {
	CHECK(s.newGame().empty());
	test::confirmLines(s);
	CHECK(s.devWarp("wren_yard", "yard_gate").empty());
	s.drainOutput();
}

const ActorView* actor(const std::vector<ActorView>& actors, const std::string& id) {
	const auto it = std::find_if(actors.begin(), actors.end(), [&](const ActorView& a) { return a.id == id; });
	return it == actors.end() ? nullptr : &*it;
}

Cell cellOf(const Session& s, const std::string& id) {
	const std::vector<ActorView> actors = s.actorsHere();
	const ActorView* a = actor(actors, id);
	return a ? a->cell : Cell{-1, -1};
}

std::vector<std::string> running(const Session& s) {
	std::vector<std::string> out;
	for (const EventPosition& at : s.backgroundEvents()) {
		out.push_back(at.event);
	}
	return out;
}

}

TEST(aBackgroundEventRunsBesideTheFieldWhileThePlayerWalks) {
	Session s(test::backgroundData(), SessionOptions());
	inYard(s);
	CHECK(s.mode() == Mode::Field);
	CHECK(running(s) == std::vector<std::string>({"yard_lamps"}));
	CHECK(s.world().playerControl);
	const Cell before = s.world().position;
	CHECK(s.walk(Facing::North).empty());
	CHECK(s.world().position == stepFrom(before, Facing::North));
	CHECK(cellOf(s, "yard_lamplighter") == (Cell{12, 3}));
	CHECK(test::outputHas(s.drainOutput(), "yard_lamps: move yard_lamplighter to lamp_east"));
	s.update(1000);
	CHECK(cellOf(s, "yard_lamplighter") == (Cell{1, 3}));
	// The same inputs give the same world: background events step at fixed field ticks after the leader's step.
	Session again(test::backgroundData(), SessionOptions());
	inYard(again);
	CHECK(again.walk(Facing::North).empty());
	again.update(1000);
	std::string diff;
	CHECK(sameWorld(s.world(), again.world(), &diff));
}

TEST(backgroundEventsPauseInForegroundEventsAndStopOnEnteringAnArea) {
	Session s(test::backgroundData(), SessionOptions());
	inYard(s);
	s.update(1000);
	s.world().position = s.area()->markers.at("bell_front");
	s.world().facing = Facing::North;
	CHECK(s.interact().empty());
	CHECK(s.mode() == Mode::Event && s.linePending());
	const Cell lamplighter = cellOf(s, "yard_lamplighter");
	s.update(5000);
	CHECK(cellOf(s, "yard_lamplighter") == lamplighter);
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	// The bell spawned the crow's flight; the dust's spawn passed background_limit 2 and was refused.
	const std::vector<Output> out = s.drainOutput();
	CHECK(test::outputHas(out, "spawn yard_crow_flight"));
	CHECK(test::outputHas(out, "background event 'yard_dust' not started: 2 already run (background_limit)"));
	CHECK(running(s) == std::vector<std::string>({"yard_lamps", "yard_crow_flight"}));
	CHECK(s.devWarp("wren_hollow", "hollow_door").empty());
	CHECK(running(s).empty());
	CHECK(test::outputHas(s.drainOutput(), "yard_crow_flight: stopped"));
}

TEST(aSaveMidBackgroundEventResumesIt) {
	Session s(test::backgroundData(), SessionOptions());
	inYard(s);
	s.update(1700);
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("chunk background 1 ") != std::string::npos);
	const rules::Result<SaveFile> parsed = parseSave(text);
	CHECK(parsed.ok() && parsed.value->background.size() == 1u && parsed.value->background[0].first == "yard_lamps");
	CHECK(parsed.ok() && serializeSave(*parsed.value) == text);
	Session loaded(test::backgroundData(), SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK(loaded.backgroundEvents().size() == 1u && loaded.backgroundEvents()[0].line == s.backgroundEvents()[0].line);
	s.update(3000);
	loaded.update(3000);
	std::string diff;
	CHECK(sameWorld(s.world(), loaded.world(), &diff));
	CHECK(running(loaded) == running(s));
	// A save with no background event writes no chunk, as before.
	Session quiet(test::data(), SessionOptions());
	CHECK(quiet.newGame().empty());
	test::confirmLines(quiet);
	std::string plain;
	CHECK(quiet.saveText(plain).empty());
	CHECK(plain.find("chunk background") == std::string::npos);
}

TEST(aWanderingActorStaysWhereItMayAndStandsStillInAnEvent) {
	Session s(test::backgroundData(), SessionOptions());
	inYard(s);
	const Cell home{3, 4};
	bool moved = false;
	for (int tick = 0; tick < 400; ++tick) {
		s.update(test::backgroundData().config.tickMs);
		const Cell goat = cellOf(s, "yard_goat");
		moved = moved || goat != home;
		CHECK(std::abs(goat.x - home.x) + std::abs(goat.y - home.y) <= 2);
		CHECK(s.area()->rows[goat.y][goat.x] == '.');
		CHECK(goat != s.world().position);
		CHECK(goat != cellOf(s, "yard_lamplighter"));
		if (s.mode() != Mode::Field) {
			test::confirmLines(s);
		}
	}
	CHECK(moved);
	const std::vector<ActorView> actors = s.actorsHere();
	CHECK(actor(actors, "yard_goat")->wanders && actor(actors, "yard_goat")->solid && actor(actors, "yard_goat")->look == "goat");
	CHECK(!actor(actors, "yard_trader")->wanders);
	// During a foreground event no field tick runs, so it stands still.
	s.world().position = s.area()->markers.at("bell_front");
	s.world().facing = Facing::North;
	CHECK(s.interact().empty());
	const Cell goat = cellOf(s, "yard_goat");
	s.update(5000);
	CHECK(cellOf(s, "yard_goat") == goat);
	// Its wander is saved: progress and pause come back with the actor.
	test::confirmLines(s);
	std::string text;
	CHECK(s.saveText(text).empty());
	Session loaded(test::backgroundData(), SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK(sameWorld(s.world(), loaded.world()));
}

TEST(walkingIntoAWandererOrTalkingToItFiresItsTouchHook) {
	Session s(test::backgroundData(), SessionOptions());
	inYard(s);
	s.world().position = {3, 5};
	s.world().facing = Facing::North;
	const std::optional<InteractTarget> t = s.interactTarget();
	CHECK(t && t->kind == InteractKind::Actor && t->id == "yard_goat" && t->cell == (Cell{3, 4}));
	CHECK(s.walk(Facing::North).empty());
	CHECK(s.mode() == Mode::Event && s.linePending());
	CHECK(s.lineText().find("The goat bleats") != std::string::npos);
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	CHECK(s.interact().empty());
	CHECK(s.mode() == Mode::Event);
	test::confirmLines(s);
	// A touch hook whose flags do not hold starts nothing, and the goat blocks the way as any solid actor does.
	GameData d = test::backgroundData();
	for (Hook& h : d.areas.at("wren_yard").hooks) {
		if (h.trigger == HookTrigger::Touch) {
			h.requireFlag = "yard.lit_seen";
		}
	}
	Session off(d, SessionOptions());
	inYard(off);
	off.world().position = {3, 5};
	off.world().facing = Facing::North;
	CHECK(!off.interactTarget());
	CHECK_EQ(off.walk(Facing::North).key, std::string("game.refuse.blocked"));
}

namespace {

// The yard with the goat's touch hook turned into its talk point at its home, (3, 4).
GameData goatWithTalkPoint() {
	GameData d = test::backgroundData();
	for (Hook& h : d.areas.at("wren_yard").hooks) {
		if (h.trigger == HookTrigger::Touch) {
			h.trigger = HookTrigger::Interact;
			h.id = "yard_goat";
			h.actor.clear();
			h.at = {3, 4};
		}
	}
	return d;
}

ActorDef& goatDef(GameData& d) {
	return *std::find_if(d.config.actorDefs.begin(), d.config.actorDefs.end(), [](const ActorDef& a) { return a.id == "yard_goat"; });
}

// Ticks until the goat stands off its home, then puts the leader on a free floor cell beside it, facing it.
Cell besideTheWanderingGoat(Session& s) {
	Cell goat = cellOf(s, "yard_goat");
	for (int tick = 0; tick < 400 && goat == Cell{3, 4}; ++tick) {
		s.update(s.data().config.tickMs);
		goat = cellOf(s, "yard_goat");
	}
	CHECK(goat != (Cell{3, 4}));
	for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
		const Cell from = stepFrom(goat, f);
		if (from != Cell{3, 4} && s.area()->walkable(from) && from != cellOf(s, "yard_lamplighter") && from != cellOf(s, "yard_trader")) {
			s.world().position = from;
			s.world().facing = static_cast<Facing>((static_cast<int>(f) + 2) % 4);
			break;
		}
	}
	return goat;
}

}

// A wandering actor's talk point: by default it answers on its own cell, which the actor leaves for good; under
// `talk_at: actor` the actor answers where it stands, stands still while its event runs, turns toward the leader with
// `turns`, and the talk point's cell is floor.
TEST(aMovingActorsTalkPointAnswersWhereTheActorStandsUnderTalkAtActor) {
	GameData d = goatWithTalkPoint();
	Session point(d, SessionOptions());
	inYard(point);
	const Cell wandered = besideTheWanderingGoat(point);
	std::optional<InteractTarget> t = point.interactTarget();
	CHECK(!t || t->cell != wandered);
	point.world().position = {3, 5};
	point.world().facing = Facing::North;
	t = point.interactTarget();
	CHECK(t && t->kind == InteractKind::Actor && t->id == "yard_goat" && t->cell == (Cell{3, 4}) && t->ready);
	CHECK(!point.canStep(Facing::North));

	d.config.talkAt = TalkAt::Actor;
	for (Hook& h : d.areas.at("wren_yard").hooks) {
		h.turns = h.id == "yard_goat";
	}
	Session s(d, SessionOptions());
	inYard(s);
	const Cell goat = besideTheWanderingGoat(s);
	t = s.interactTarget();
	CHECK(t && t->kind == InteractKind::Actor && t->id == "yard_goat" && t->cell == goat && t->ready);
	CHECK(s.interactAt(goat) && s.interactAt(goat)->id == "yard_goat");
	const Cell leader = s.world().position;
	CHECK(s.interact().empty());
	CHECK(s.mode() == Mode::Event);
	const std::vector<ActorView> actors = s.actorsHere();
	CHECK(actor(actors, "yard_goat")->facing == static_cast<Facing>((static_cast<int>(s.world().facing) + 2) % 4));
	s.update(5000);
	CHECK(cellOf(s, "yard_goat") == goat && s.world().position == leader);
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	// The talk point's own cell answers nothing while the goat is elsewhere, and the party may walk onto it.
	s.world().position = {3, 5};
	s.world().facing = Facing::North;
	t = s.interactTarget();
	CHECK(!t || t->cell != (Cell{3, 4}));
	CHECK(s.canStep(Facing::North));
}

// game_cli's walk, the C and C# walks and click-to-walk go through walkStep: by default a planned step into an actor
// with a touch hook fires it, as a step of the player's own does; with `touch_from: keys` only the player's own steps
// and interact fire it, and the actor's own walk into the leader does not.
TEST(touchFromKeysLeavesPathWalksAndTheActorsOwnWalkOut) {
	GameData d = test::backgroundData();
	goatDef(d).wander.reset();
	goatDef(d).solid = false;
	for (bool keys : {false, true}) {
		for (Hook& h : d.areas.at("wren_yard").hooks) {
			if (h.trigger == HookTrigger::Touch) {
				h.touchFrom = keys ? TouchFrom::Keys : TouchFrom::Any;
			}
		}
		Session s(d, SessionOptions());
		inYard(s);
		s.world().position = {1, 4};
		PathWalk walk = s.beginWalk({5, 4});
		WalkStatus status = WalkStatus::Walking;
		while (status == WalkStatus::Walking && s.mode() == Mode::Field) {
			status = s.walkStep(walk);
		}
		CHECK_EQ(s.mode() == Mode::Event, !keys);
		CHECK(keys ? s.world().position == (Cell{5, 4}) : s.world().position == (Cell{2, 4}));
		test::confirmLines(s);
		s.world().position = {2, 4};
		s.world().facing = Facing::East;
		CHECK(s.walk(Facing::East).empty());
		CHECK(s.mode() == Mode::Event);
		test::confirmLines(s);
		CHECK(s.interact().empty());
		CHECK(s.mode() == Mode::Event);
		test::confirmLines(s);
	}
	GameData wandering = test::backgroundData();
	int fired[2] = {0, 0};
	for (bool keys : {false, true}) {
		for (Hook& h : wandering.areas.at("wren_yard").hooks) {
			if (h.trigger == HookTrigger::Touch) {
				h.touchFrom = keys ? TouchFrom::Keys : TouchFrom::Any;
			}
		}
		Session s(wandering, SessionOptions());
		inYard(s);
		s.world().position = {3, 5};
		for (int tick = 0; tick < 2000; ++tick) {
			s.update(s.data().config.tickMs);
			if (s.mode() == Mode::Event && s.linePending() && s.lineText().find("The goat bleats") != std::string::npos) {
				++fired[keys];
			}
			if (s.mode() != Mode::Field) {
				test::confirmLines(s);
			}
		}
	}
	CHECK(fired[0] > 0);
	CHECK_EQ(fired[1], 0);
}

// Return to a town from an interior of it: the list is the visited towns with an arrival marker in the region of the
// area the party is in, that area left out; a town's interior with no arrival of its own and the same region offers it.
TEST(aTownsInteriorOffersTheReturnToTownsOfItsRegion) {
	GameData d = test::data();
	std::string text = R"({"id": "inn_room", "kind": "town", "region": "coast", "map": ["###", "#.#", "###"], "markers": {"bed": [1, 1]}})";
	rules::Result<Area> room = parseArea("inn_room.json", text);
	CHECK(room.ok());
	d.areas["inn_room"] = *room.value;
	const auto visitTownsThenRoom = [&](Session& s) {
		CHECK(s.newGame().empty());
		test::confirmLines(s);
		for (const auto& [id, a] : d.areas) {
			if (a.kind == AreaKind::Town && !a.arrival.empty()) {
				CHECK(s.devWarp(id, a.arrival).empty());
				test::confirmLines(s);
			}
		}
		CHECK(s.devWarp("inn_room", "bed").empty());
		test::confirmLines(s);
	};
	Session s(d, SessionOptions());
	visitTownsThenRoom(s);
	const std::vector<TravelDestination> towns = s.travelDestinations(TravelKind::Town);
	CHECK(!towns.empty());
	CHECK(std::none_of(towns.begin(), towns.end(), [](const TravelDestination& t) { return t.area == "inn_room"; }));
	CHECK(std::all_of(towns.begin(), towns.end(), [](const TravelDestination& t) { return t.region == "coast"; }));
	// No region, while the towns name one, offers nothing.
	d.areas["inn_room"].region = "";
	Session other(d, SessionOptions());
	visitTownsThenRoom(other);
	CHECK(other.travelDestinations(TravelKind::Town).empty());
}

TEST(visibleEnemiesAnswerToTheirReservedActorNames) {
	Session s(test::backgroundData(), SessionOptions());
	inYard(s);
	CHECK(s.world().enemies.size() == 1u);
	const int id = s.world().enemies[0].id;
	CHECK_EQ(s.enemyActor(id), std::string("enemy.yard_patch.1"));
	CHECK_EQ(s.enemyActor(id + 1), std::string());
	std::string zone;
	int n = 0;
	CHECK(parseVisibleEnemyActor("enemy.yard_patch.1", zone, n) && zone == "yard_patch" && n == 1);
	CHECK(!parseVisibleEnemyActor("enemy.yard_patch", zone, n));
	CHECK(!parseVisibleEnemyActor("enemy.yard_patch.0", zone, n));
	CHECK(!parseVisibleEnemyActor("yard_goat", zone, n));
	CHECK(test::backgroundData().registry.has(events::NameKind::actor, "enemy.yard_patch.1"));
	GameData d = test::backgroundDataWithScript("events/yard.ev", R"(event yard_bell unskippable
	face enemy.yard_patch.1 north
	move enemy.yard_patch.1 via stall_front to patch_corner
end
event yard_lamps background
	wait 1
end
event yard_goat_bleat unskippable
	remove enemy.yard_patch.1
end
event yard_lit unskippable
	wait 1
end
)");
	Session moved(d, SessionOptions());
	inYard(moved);
	moved.world().position = moved.area()->markers.at("bell_front");
	moved.world().facing = Facing::North;
	CHECK(moved.interact().empty());
	test::confirmLines(moved);
	const VisibleEnemy& e = moved.world().enemies[0];
	CHECK(e.position == (Cell{12, 6}) && e.spawn == e.position && e.state == EnemyState::Idle && e.facing == Facing::South);
	moved.world().position = {3, 5};
	CHECK(moved.walk(Facing::North).empty());
	test::confirmLines(moved);
	CHECK(moved.world().enemies.empty());
}

TEST(aScriptChangesAnActorsLookAndShowsOrHidesItWithATransition) {
	GameData d = test::backgroundDataWithScript("events/yard.ev", R"(event yard_bell unskippable
	look yard_trader enemy aegis_sentinel
	look yard_crow lantern
	hide yard_trader with dissolve
	show yard_crow with fade_in nowait
end
event yard_lamps background
	wait 1
end
event yard_goat_bleat unskippable
	look yard_crow default
	show yard_trader
end
event yard_lit unskippable
	wait 1
end
)");
	Session s(d, SessionOptions());
	inYard(s);
	s.world().position = s.area()->markers.at("bell_front");
	s.world().facing = Facing::North;
	CHECK(s.interact().empty());
	test::confirmLines(s);
	std::vector<ActorView> actors = s.actorsHere();
	const ActorView* trader = actor(actors, "yard_trader");
	const ActorView* crow = actor(actors, "yard_crow");
	CHECK(trader->spriteEnemy == "aegis_sentinel" && trader->look.empty() && !trader->visible && trader->vanish == ActorVanish::Dissolved);
	CHECK(crow->look == "lantern" && crow->spriteEnemy.empty() && crow->visible && crow->animation == "fade_in" && crow->animationTotalMs == 500);
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("look yard_trader aegis_sentinel -") != std::string::npos);
	Session loaded(d, SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK(sameWorld(s.world(), loaded.world()));
	s.world().position = {3, 5};
	s.world().facing = Facing::North;
	CHECK(s.walk(Facing::North).empty());
	test::confirmLines(s);
	actors = s.actorsHere();
	CHECK(actor(actors, "yard_crow")->look == "crow");
	CHECK(actor(actors, "yard_trader")->visible && actor(actors, "yard_trader")->vanish == ActorVanish::None);
}

TEST(interactAtNamesWhatACellHoldsAndWhetherItIsUsedFromHere) {
	Session s(test::backgroundData(), SessionOptions());
	inYard(s);
	s.world().position = s.area()->markers.at("stall_front");
	s.world().facing = Facing::South;
	const std::optional<InteractTarget> stall = s.interactAt({9, 2});
	CHECK(stall && stall->kind == InteractKind::Shop && stall->id == "yard_stall" && stall->facing == Facing::North && stall->keeper == "yard_trader");
	CHECK(!s.interactAt({3, 0}));
	CHECK(!s.interactAt({9, 1}));
	// Across a counter, as interact would once turned.
	Session cellar(test::interactData(), SessionOptions());
	test::placeParty(cellar, "wren_cellar", "clerk_front", Facing::East);
	const std::optional<InteractTarget> clerk = cellar.interactAt({2, 1});
	CHECK(clerk && clerk->kind == InteractKind::Talk && clerk->id == "cellar_clerk" && clerk->across && clerk->facing == Facing::North);
	CHECK(!cellar.interactAt({2, 2}));
}

TEST(fieldMotionReportsTheStepTheBeltAndTheWayOpen) {
	Session s(test::interactData(), SessionOptions());
	test::placeParty(s, "wren_cellar", "cook_side", Facing::West);
	FieldMotion m = s.fieldMotion();
	CHECK_EQ(m.stepMs, 250);
	CHECK_EQ(m.runStepMs, 125);
	CHECK_EQ(m.stepProgressMs, 0);
	CHECK_EQ(m.conveyorStepMs, 0);
	int mask = 0;
	int bit = 1;
	for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
		mask |= s.canStep(f) ? bit : 0;
		bit <<= 1;
	}
	CHECK_EQ(m.stepMask, mask);
	s.update(s.data().config.tickMs * 2, {true, Facing::West, false});
	CHECK_EQ(s.fieldMotion().stepProgressMs, s.data().config.tickMs * 2);
	s.quitToTitle();
	CHECK_EQ(s.fieldMotion().stepMs, 0);
}

TEST(runningChangesNeitherPlayTimeNorVisibleEnemies) {
	const GameData& d = test::interactData();
	const auto run = [&](bool running) {
		Session s(d, SessionOptions());
		test::placeParty(s, "wren_cellar", "clerk_front", Facing::North);
		s.world().enemies.push_back(VisibleEnemy{1, "cellar_pool", "training", {11, 3}, {11, 3}, Facing::South});
		s.world().nextEnemyId = 2;
		const std::int64_t before = s.world().playTimeMs;
		// Held against the wall, the leader never moves; only the run input differs.
		for (int i = 0; i < 40; ++i) {
			s.update(d.config.tickMs, {true, Facing::North, running});
		}
		CHECK_EQ(s.world().playTimeMs - before, std::int64_t(40) * d.config.tickMs);
		return s.world();
	};
	const World walking = run(false);
	const World runningWorld = run(true);
	std::string diff;
	CHECK(sameWorld(walking, runningWorld, &diff));
}
