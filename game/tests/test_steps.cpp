#include "test.h"

#include "game/check.h"

#include <algorithm>

using namespace game;

namespace {

// The adventure with the Drowned Kiln's lower floor emptied for walking: its first row open from wall to wall, no hooks,
// zones or chests, and the step time given (with a fraction kept in microseconds).
GameData openKiln(double stepMs, bool exact) {
	GameData d = test::data();
	Area& kiln = d.areas.at("kiln_lower");
	kiln.hooks.clear();
	kiln.zones.clear();
	kiln.chests.clear();
	kiln.stepUs = static_cast<std::int64_t>(stepMs * 1000 + 0.5);
	kiln.stepMs = static_cast<int>(stepMs + 0.5);
	d.config.exactSteps = exact;
	return d;
}

Session& onKiln(Session& s, Cell cell, Facing facing) {
	s.newGame();
	test::confirmLines(s);
	CHECK(s.devWarpCell("kiln_lower", cell).empty());
	test::confirmLines(s);
	s.devEncounters(false);
	s.face(facing);
	return s;
}

const Chest& someChest() {
	for (const auto& [id, area] : test::data().areas) {
		if (!area.chests.empty()) {
			return area.chests.front();
		}
	}
	static Chest none;
	return none;
}

}

// R108: under step_timing exact a fractional step time is kept on average over any distance; ticks round each step up
// to whole ticks as before.
TEST(exactStepTimingKeepsFractionalStepTimes) {
	const int tick = test::data().config.tickMs;
	for (bool exact : {false, true}) {
		const GameData d = openKiln(133.5, exact);
		Session s(d, SessionOptions());
		onKiln(s, {1, 1}, Facing::East);
		const int ticks = 40;
		for (int i = 0; i < ticks; ++i) {
			s.update(tick, {true, Facing::East});
		}
		const int moved = s.world().position.x - 1;
		const int expected = exact ? ticks * tick * 1000 / 133500 : ticks / ((134 + tick - 1) / tick);
		CHECK_EQ(moved, expected);
		if (exact) {
			// The progress carries what is past the last step, in microseconds.
			CHECK_EQ(s.fieldMotion().stepProgressUs, static_cast<std::int64_t>(ticks) * tick * 1000 - expected * 133500LL);
			CHECK_EQ(s.fieldMotion().stepUs, 133500);
			CHECK_EQ(s.fieldMotion().stepMs, 134);
		}
	}
}

// R108: positions do not depend on how the host splits its frame time: one update of the whole time, and many small ones
// of uneven sizes, leave the leader on the same cell with the same carried progress.
TEST(exactStepTimingDoesNotDependOnUpdateSize) {
	const GameData d = openKiln(133.5, true);
	Session whole(d, SessionOptions());
	Session pieces(d, SessionOptions());
	onKiln(whole, {1, 1}, Facing::East);
	onKiln(pieces, {1, 1}, Facing::East);
	const int sizes[] = {7, 16, 1, 33, 50, 3, 120, 17, 9, 61};
	int total = 0;
	for (int round = 0; round < 6; ++round) {
		for (int ms : sizes) {
			pieces.update(ms, {true, Facing::East});
			total += ms;
		}
		whole.update(total, {true, Facing::East});
		total = 0;
		CHECK(whole.world().position == pieces.world().position);
		CHECK_EQ(whole.fieldMotion().stepProgressUs, pieces.fieldMotion().stepProgressUs);
	}
	CHECK(whole.world().position.x > 10);
}

// R108: a save under exact timing keeps the carried step progress and the part of a tick not yet played.
TEST(exactStepTimingSavesTheCarry) {
	const GameData d = openKiln(133.5, true);
	Session s(d, SessionOptions());
	onKiln(s, {1, 1}, Facing::East);
	s.update(1237, {true, Facing::East});
	const std::int64_t carried = s.fieldMotion().stepProgressUs;
	CHECK(carried > 0);
	std::string text;
	CHECK(s.saveText(text).empty());
	CHECK(text.find("step_carry") != std::string::npos);
	Session loaded(d, SessionOptions());
	CHECK(loaded.loadSaveText(text).empty());
	CHECK_EQ(loaded.fieldMotion().stepProgressUs, carried);
	s.update(500, {true, Facing::East});
	loaded.update(500, {true, Facing::East});
	CHECK(s.world().position == loaded.world().position);

	// Under ticks nothing new is written.
	const GameData ticks = openKiln(250, false);
	Session t(ticks, SessionOptions());
	onKiln(t, {1, 1}, Facing::East);
	t.update(1237, {true, Facing::East});
	CHECK(t.saveText(text).empty());
	CHECK(text.find("step_carry") == std::string::npos);
	CHECK(text.find("tick_carry") == std::string::npos);
}

// R107: arrive_notice walked announces an area only when the party walks in through an exit; never announces none; an
// area's own arrive_notice false silences it under always.
TEST(arriveNoticeFollowsHowThePartyArrives) {
	GameData walked = test::data();
	walked.config.arriveNotice = ArriveNotice::Walked;
	Session s(walked, SessionOptions());
	s.newGame();
	CHECK(!test::outputHas(s.drainOutput(), "game.arrive"));
	CHECK(!s.arrivalAnnounced());
	test::confirmLines(s);
	CHECK(s.devWarp("reed_path", "west_gate").empty());
	CHECK(!test::outputHas(s.drainOutput(), "game.arrive"));
	CHECK(!s.arrivalAnnounced());
	// Walk back west through the reed path's exit to Tidewater Landing.
	const Area& path = *s.area();
	const Exit& exit = *std::find_if(path.exits.begin(), path.exits.end(), [](const Exit& e) { return e.toArea == "tidewater"; });
	CHECK(s.devWarpCell("reed_path", {exit.at.x + 1, exit.at.y}).empty());
	s.drainOutput();
	s.devEncounters(false);
	s.update(path.stepMs + walked.config.tickMs, {true, Facing::West});
	s.update(walked.config.tickMs);
	CHECK_EQ(s.world().area, std::string("tidewater"));
	CHECK(s.arrivalAnnounced());

	GameData never = test::data();
	never.config.arriveNotice = ArriveNotice::Never;
	Session n(never, SessionOptions());
	n.newGame();
	CHECK(!test::outputHas(n.drainOutput(), "game.arrive"));

	GameData quiet = test::data();
	quiet.areas.at("tidewater").arriveNotice = false;
	Session q(quiet, SessionOptions());
	q.newGame();
	CHECK(!test::outputHas(q.drainOutput(), "game.arrive"));
	CHECK(!q.arrivalAnnounced());
	test::confirmLines(q);
	CHECK(q.devWarp("reed_path", "west_gate").empty());
	CHECK(q.arrivalAnnounced());
}

// R109: a marked counter (a '=' cell, or one the area's `counters` lists) passes interaction to what stands behind it,
// used as if faced, and the target query reports it; a keeper standing behind one opens its shop.
TEST(markedCountersPassInteractionAcross) {
	GameData d = openKiln(250, false);
	Area& kiln = d.areas.at("kiln_lower");
	kiln.rows[2][5] = '=';
	kiln.counters.insert({5, 2});
	Chest chest = someChest();
	chest.at = {5, 3};
	kiln.chests.push_back(chest);
	// A listed counter on a water cell.
	kiln.counters.insert({3, 2});
	Chest second = chest;
	second.id = chest.id + "_second";
	second.flag = chest.flag + "_second";
	second.at = {3, 3};
	kiln.chests.push_back(second);
	Session s(d, SessionOptions());
	onKiln(s, {5, 1}, Facing::South);
	std::optional<InteractTarget> t = s.interactTarget();
	CHECK(t.has_value());
	CHECK(t && t->kind == InteractKind::Chest && t->across && (t->cell == Cell{5, 3}));
	CHECK(s.interactAt({5, 3}).has_value());
	CHECK(s.interact().empty());
	test::confirmLines(s);
	CHECK(s.world().flag(chest.flag));
	CHECK(s.devWarpCell("kiln_lower", {3, 1}).empty());
	test::confirmLines(s);
	s.face(Facing::South);
	t = s.interactTarget();
	CHECK(t && t->kind == InteractKind::Chest && t->id == second.id && t->across);

	// The yard's trader stands behind a counter in front of its stall, which is elsewhere.
	GameData yard = test::backgroundData();
	Area& w = yard.areas.at("wren_yard");
	w.shops[0].at = Cell{12, 0};
	w.rows[2][9] = '=';
	w.counters.insert({9, 2});
	Session y(yard, SessionOptions());
	y.newGame();
	test::confirmLines(y);
	CHECK(y.devWarpCell("wren_yard", {9, 3}).empty());
	test::confirmLines(y);
	y.face(Facing::North);
	t = y.interactTarget();
	CHECK(t && t->kind == InteractKind::Shop && t->keeper == "yard_trader" && t->across);
	CHECK(y.interact().empty());
	CHECK(y.mode() == Mode::Shop);
}

// R109: the data check counts a point behind a marked counter as reached from the floor two cells away.
TEST(dataCheckReachesAcrossMarkedCounters) {
	GameData d = openKiln(250, false);
	Area& kiln = d.areas.at("kiln_lower");
	Chest chest = someChest();
	chest.at = {20, 3};
	kiln.rows[3][19] = '#';
	kiln.rows[3][21] = '#';
	kiln.rows[4][20] = '#';
	kiln.rows[2][20] = '#';
	kiln.chests.push_back(chest);
	const auto unreached = [&](const GameData& data) {
		for (const rules::Problem& p : crossCheck(data, true)) {
			if (p.message.key == "check.object_not_reached" && !p.message.params.empty() && p.message.params[0].find(chest.id) != std::string::npos) {
				return true;
			}
		}
		return false;
	};
	CHECK(unreached(d));
	kiln.rows[2][20] = '=';
	kiln.counters.insert({20, 2});
	CHECK(!unreached(d));
}
