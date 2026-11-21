#include "test.h"

#include "game/profile.h"
#include "game/save.h"
#include "game/session.h"
#include "rules/progression.h"

#include <filesystem>

using namespace game;

TEST(questLogFollowsFlagRangesAndJobsOnlyMoveForward) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	QuestLog log = s.questLog();
	CHECK_EQ(log.objective, std::string("Find the harbor guide by the noticeboard."));
	CHECK_EQ(log.jobs.size(), 1u);
	CHECK(log.jobs[0].state == JobState::Available);

	World& w = s.world();
	w.flags["tidewater.met_guide"] = true;
	w.flags["job.lost_cache.accepted"] = true;
	test::placeParty(s, "reed_path", "cache_front", Facing::North);
	CHECK(s.interact().empty());
	CHECK(s.world().jobs.at("lost_cache") == JobState::Accepted);
	CHECK(s.questLog().objective.find("Bell Cove") != std::string::npos);

	// A cleared flag does not take a job back.
	s.world().flags["job.lost_cache.accepted"] = false;
	s.interact();
	CHECK(s.questLog().jobs[0].state == JobState::Accepted);
}

TEST(encyclopediaEntriesStayUnlocked) {
	Session s(test::data(), SessionOptions());
	s.newGame();
	test::confirmLines(s);
	CHECK_EQ(s.encyclopedia().size(), 1u);
	CHECK_EQ(s.encyclopedia()[0].title, std::string("Tidewater Landing"));
	s.world().flags["tidewater.arrived"] = false;
	CHECK_EQ(s.encyclopedia().size(), 1u);
	rules::Result<SaveFile> save = parseSave(serializeSave(s.makeSave(false)));
	CHECK(save.ok() && save.value->world.unlocked.count("tidewater"));
}

TEST(partyTalkPicksTheMostSpecificSet) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
	CHECK(!s.partyTalk().empty());
	s.world().flags["tidewater.met_guide"] = true;
	s.world().roster.push_back(rules::makePartyMember(test::data().db, *test::data().db.character("mystic"), 3));
	s.world().lineup.push_back("mystic");
	CHECK(s.partyTalk().empty());
	// The medic's line is left out while the medic is not in the party.
	std::vector<Output> out = s.drainOutput();
	CHECK_EQ(out.size(), 1u);
	CHECK_EQ(out[0].text, std::string("Test Mystic: Every step out here squelches. I am counting them."));
	CHECK_EQ(out[0].message.key, std::string("talk.reeds.1"));

	s.world().area = "tidewater";
	CHECK(s.partyTalk().empty());
	CHECK(test::outputHas(s.drainOutput(), "A bell that rings by itself"));
}

TEST(profileLivesOutsideSavesAndSurvivesUnknownSettings) {
	const std::string dir = test::freshDirectory("profile");
	SessionOptions options;
	options.saveDirectory = dir;
	{
		Session a(test::data(), options);
		Profile p = a.profile();
		CHECK(setProfileValue(p, "text_speed", "fast").empty());
		CHECK(setProfileValue(p, "show_shipped_names", "on").empty());
		CHECK(!setProfileValue(p, "text_size", "200").empty());
		CHECK(!setProfileValue(p, "volume", "3").empty());
		CHECK(a.setProfile(p).empty());
		a.newGame();
		test::confirmLines(a);
		CHECK(serializeSave(a.makeSave(false)).find("text_speed") == std::string::npos);
	}
	Session b(test::data(), options);
	CHECK(b.profile().textSpeed == TextSpeed::Fast);
	CHECK(b.profile().showShippedNames);

	rules::Result<Profile> future = parseProfile("PSR-PROFILE 1\nhaptics strong\nend 0\n");
	CHECK(!future.ok());
	Profile withUnknown;
	withUnknown.unknown.push_back({"haptics", "strong"});
	rules::Result<Profile> kept = parseProfile(serializeProfile(withUnknown));
	CHECK(kept.ok());
	CHECK(serializeProfile(*kept.value).find("haptics strong\n") != std::string::npos);

	std::string error;
	CHECK(writeFileAtomic(dir + "/profile.cfg", "garbage", error));
	Session c(test::data(), options);
	CHECK(c.profile().textSpeed == TextSpeed::Normal);
	CHECK(test::outputHas(c.drainOutput(), "default settings"));
}

TEST(finishingTheStoryWritesTheClearDataRecord) {
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("clear");
	Session s(test::data(), options);
	test::placeParty(s, "bell_cove", "bell_front", Facing::East);
	s.world().roster[0] = rules::makePartyMember(test::data().db, *test::data().db.character("fighter"), 30);
	s.interact();
	test::confirmLines(s);
	test::fightToTheEnd(s);
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	CHECK(s.world().flag("cove.quiet_seen"));
	CHECK(test::outputHas(s.drainOutput(), "Clear data recorded."));
	std::string text;
	CHECK(readFile(s.store()->clearRecordPath(), text));
	rules::Result<World> record = parseClearRecord(text);
	CHECK(record.ok());
	CHECK(record.value->flag("cove.warden_beaten"));
	CHECK_EQ(record.value->roster.size(), 1u);
	CHECK_EQ(record.value->roster[0].level, 30);
	CHECK(!parseClearRecord(serializeSave(s.makeSave(false))).ok());
}

namespace {

// Walks out of the area through its exit to `area`.
void walkOut(Session& s, const std::string& area) {
	for (const Exit& e : s.area()->exits) {
		if (e.toArea == area) {
			PathWalk walk = s.beginWalk(e.at);
			while (s.walkStep(walk) == WalkStatus::Walking) {
				test::confirmLines(s);
			}
			return;
		}
	}
}

int ambientLines(const std::vector<Output>& output) {
	return static_cast<int>(std::count_if(output.begin(), output.end(), [](const Output& o) { return o.kind == OutputKind::Ambient; }));
}

}

TEST(partyTalkComesUpByItselfOnceAnAreaUnderEachSet) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "east_gate", Facing::East);
	World& w = s.world();
	w.flags["tidewater.met_guide"] = true;
	w.flags["reed.scout_joined"] = true;
	for (const std::string& id : {"mystic", "medic"}) {
		w.roster.push_back(rules::makePartyMember(test::data().db, *test::data().db.character(id), 3));
		w.lineup.push_back(id);
	}
	walkOut(s, "reed_path");
	CHECK_EQ(s.world().area, std::string("reed_path"));
	const std::vector<Output> arrived = s.drainOutput();
	CHECK_EQ(ambientLines(arrived), 2);
	CHECK(test::outputHas(arrived, "Test Mystic: Every step out here squelches."));
	CHECK(s.mode() == Mode::Field && s.world().playerControl);
	CHECK(s.world().ambientHeard.count("reeds@reed_path"));

	// Back and in again: the reeds set has been heard; Tidewater falls under the chapter set, heard once there.
	walkOut(s, "tidewater");
	CHECK_EQ(ambientLines(s.drainOutput()), 1);
	walkOut(s, "reed_path");
	CHECK_EQ(ambientLines(s.drainOutput()), 0);
	CHECK(s.world().ambientHeard == (std::set<std::string>{"chapter_one@tidewater", "reeds@reed_path"}));
	rules::Result<SaveFile> saved = parseSave(serializeSave(s.makeSave(false)));
	CHECK(saved.ok() && saved.value->world.ambientHeard == s.world().ambientHeard);
	// Asked for, party talk still plays the same set.
	CHECK(s.partyTalk().empty());
	CHECK_EQ(ambientLines(s.drainOutput()), 0);

	// Turned off in the profile, nothing comes up.
	Session quiet(test::data(), SessionOptions());
	Profile p = quiet.profile();
	p.ambientTalk = false;
	CHECK(quiet.setProfile(p).empty());
	test::placeParty(quiet, "tidewater", "east_gate", Facing::East);
	quiet.world().flags["tidewater.met_guide"] = true;
	quiet.world().lineup.push_back("mystic");
	quiet.world().roster.push_back(rules::makePartyMember(test::data().db, *test::data().db.character("mystic"), 3));
	walkOut(quiet, "reed_path");
	CHECK_EQ(quiet.world().area, std::string("reed_path"));
	CHECK_EQ(ambientLines(quiet.drainOutput()), 0);
	CHECK(quiet.world().ambientHeard.empty());
}
