#include "test.h"

#include "game/save.h"
#include "game/session.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace game;

namespace {

void inGallery(Session& s) {
	CHECK(s.newGame().empty());
	test::confirmLines(s);
	CHECK(s.devWarp("wren_gallery", "gallery_door").empty());
	s.drainOutput();
}

std::string textOf(const Session& s) {
	std::string text;
	CHECK(s.saveText(text).empty());
	return text;
}

void use(Session& s, const std::string& marker) {
	s.world().position = s.area()->markers.at(marker);
	s.world().facing = Facing::North;
	CHECK(s.interact().empty());
}

// A copy of the scene data with one file's text replaced, loaded; the error when it does not load.
std::string loadProblem(const std::string& name, const std::string& file, const std::string& text) {
	namespace fs = std::filesystem;
	const std::string root = test::freshDirectory(name);
	std::error_code error;
	fs::copy(fs::path(test::sceneDataDirectory()).parent_path().parent_path(), root, fs::copy_options::recursive, error);
	std::string problem;
	CHECK(!error && writeFileAtomic(root + "/game/data/" + file, text, problem));
	const rules::Result<GameData> loaded = loadGameData(root + "/game/data");
	return loaded.ok() ? std::string() : loaded.error;
}

}

TEST(storyPicturesStackThroughLinesAndSaves) {
	Session s(test::sceneData(), SessionOptions());
	inGallery(s);
	CHECK(s.pictures().empty());
	use(s, "easel_front");
	CHECK(s.linePending() && s.pictures() == std::vector<std::string>({"harbor_dawn"}));
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	CHECK(s.pictures() == std::vector<std::string>({"harbor_dawn", "beacon"}));
	const std::string text = textOf(s);
	CHECK(text.find("\npicture harbor_dawn\npicture beacon\n") != std::string::npos);
	const rules::Result<SaveFile> parsed = parseSave(text);
	CHECK(parsed.ok() && parsed.value->world.pictures == s.pictures());
	use(s, "window_front");
	test::confirmLines(s);
	CHECK(s.pictures() == std::vector<std::string>({"farewell"}));
}

TEST(aSkipAppliesPictureCommands) {
	Session s(test::sceneData(), SessionOptions());
	inGallery(s);
	use(s, "easel_front");
	CHECK(s.canSkip());
	CHECK(s.skip());
	test::confirmLines(s);
	CHECK(s.pictures() == std::vector<std::string>({"harbor_dawn", "beacon"}));
}

TEST(hidingAPictureThatIsNotUpWarnsAndChangesNothing) {
	const GameData d = test::withScript("events/pictures.ev", "event pictures unskippable\n\tpicture hide\n\tpicture show a\n\tpicture show b\n"
		"\tpicture show a\n\tpicture hide c\n\tpicture hide a\nend\n");
	Session s(d, SessionOptions());
	CHECK(s.newGame().empty());
	test::confirmLines(s);
	s.drainOutput();
	CHECK(s.devRunEvent("pictures").empty());
	test::confirmLines(s);
	const std::vector<Output> out = s.drainOutput();
	CHECK(test::outputHas(out, "picture hide: no picture is up"));
	CHECK(test::outputHas(out, "picture hide: 'c' is not up"));
	CHECK(s.pictures() == std::vector<std::string>({"a", "b"}));
}

TEST(theDataChecksPictureNamesAndExitSounds) {
	CHECK(loadProblem("scene_bad_picture", "events/gallery.ev", "event gallery_easel skippable\n\tpicture show sunset\nend\n\nevent gallery_window skippable\n\twarp wren_gallery gallery_door\nend\n")
			  .find("unknown picture 'sunset'") != std::string::npos);
	std::string area;
	CHECK(readFile(test::sceneDataDirectory() + "/areas/wren_gallery.json", area));
	const std::string sound = "\"sound\": \"gallery_door\"";
	area.replace(area.find(sound), sound.size(), "\"sound\": \"door_slam\"");
	CHECK(loadProblem("scene_bad_sound", "areas/wren_gallery.json", area).find("the exit's sound 'door_slam' is not one of game.json's presentation.audio") !=
		std::string::npos);
}

TEST(theSceneCameraMovesHoldsAndResets) {
	Session s(test::sceneData(), SessionOptions());
	inGallery(s);
	CHECK(!s.sceneCamera().held && s.sceneCamera().ms == 0);
	use(s, "easel_front");
	test::confirmLines(s);
	// The easel cut to 8 3, then moved toward window_front over 1.5 s; field ticks run the move down.
	CHECK(s.sceneCamera().held && s.sceneCamera().cell == s.area()->markers.at("window_front"));
	CHECK(s.sceneCamera().ms == 1500 && s.sceneCamera().totalMs == 1500);
	s.update(500);
	CHECK(s.sceneCamera().ms == 1000);
	const rules::Result<SaveFile> parsed = parseSave(textOf(s));
	CHECK(parsed.ok() && parsed.value->world.sceneCamera == s.sceneCamera());
	s.update(1500);
	CHECK(s.sceneCamera().held && s.sceneCamera().ms == 0 && s.sceneCamera().totalMs == 0);
	// The window resets it over 0.5 s, then its warp into the gallery ends any scene camera.
	use(s, "window_front");
	test::confirmLines(s);
	CHECK(!s.sceneCamera().held && s.sceneCamera().ms == 0);
}

TEST(cameraResetReturnsToTheLeaderOverItsTime) {
	const GameData d = test::withScript("events/camera.ev", "event camera unskippable\n\tcamera to 3 2\n\tcamera reset 0.5 nowait\nend\n");
	Session s(d, SessionOptions());
	CHECK(s.newGame().empty());
	test::confirmLines(s);
	CHECK(s.devRunEvent("camera").empty());
	test::confirmLines(s);
	CHECK(!s.sceneCamera().held && s.sceneCamera().ms == 500);
	s.update(500);
	CHECK(s.sceneCamera().ms == 0);
}

TEST(musicFadesComeFromTheCommandOrTheData) {
	Session s(test::sceneData(), SessionOptions());
	inGallery(s);
	use(s, "easel_front");
	test::confirmLines(s);
	use(s, "window_front");
	test::confirmLines(s);
	std::vector<int> fades;
	for (const PresentedAction& a : s.pollActions()) {
		if (a.phase == ActionPhase::Begin && a.action.type == events::ActionType::music_play) {
			fades.push_back(a.action.fade_ms);
		}
	}
	CHECK(fades == std::vector<int>({800, 0, 400}));
	CHECK(test::sceneData().config.presentation.music.fadeMs == 400);
	CHECK(test::data().config.presentation.music.fadeMs == -1);
}

TEST(anExitsSoundIsReportedForThatEntryOnly) {
	Session s(test::sceneData(), SessionOptions());
	inGallery(s);
	CHECK(s.exitSound().empty());
	s.world().position = s.area()->markers.at("gallery_door");
	CHECK(s.walk(Facing::South).empty());
	CHECK(s.world().area == "wren_loft");
	CHECK(s.exitSound() == "gallery_door");
	CHECK(test::outputHas(s.drainOutput(), "exit sound gallery_door"));
	CHECK(s.devWarp("wren_gallery", "gallery_door").empty());
	CHECK(s.exitSound().empty());
}

TEST(fieldMotionSaysWhenTheNextTickAndTheStepsLandingCome) {
	Session s(test::sceneData(), SessionOptions());
	inGallery(s);
	FieldMotion m = s.fieldMotion();
	CHECK(m.nextTickUs == 50000 && m.stepLandsInUs == 250000);
	FieldInput held;
	held.held = true;
	held.direction = Facing::South;
	s.update(20, held);
	m = s.fieldMotion();
	CHECK(m.nextTickUs == 30000);
	s.update(30, held);
	m = s.fieldMotion();
	// One tick into a 250 ms step: four more ticks.
	CHECK(m.stepProgressUs == 50000 && m.nextTickUs == 50000 && m.stepLandsInUs == 200000);
}
