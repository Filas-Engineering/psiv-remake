#pragma once

#include "acc.h"

#include "events/compiler.h"
#include "events/interpreter.h"
#include "events/recording_host.h"

#include <map>
#include <string>
#include <vector>

namespace support {

constexpr double kStep = 0.1;

std::string readFile(const std::string& path);
std::vector<std::string> listFiles(const std::string& directory, const std::string& suffix);

// Every script the repository ships today: events/samples/*.ev.
std::vector<events::Source> sampleSources(const acc::Context& t);
// events/sample_text/registry.txt, the game content the samples may name. Exits with a FAIL line when it does not load.
const events::Registry& sampleRegistry(const acc::Context& t);
// The samples compiled against the registry, as a game build compiles them (EVT-003, EVT-004).
events::Program compileSamples(acc::Context& t);

// The host calls that change game state (EVT-023), in order: flags, variables, items, party, player control,
// warps and rests. Reads such as get_flag are left out.
std::vector<std::string> stateChanges(const std::vector<std::string>& trace);

// What a player would see and hear, rebuilt from host calls alone, independently of the interpreter's own
// PresentationState: each action counts at its end state, as an instant call would leave it.
struct Screen {
	std::string music;
	std::string ambient;
	std::string camera;
	bool fadedOut = false;
	// Panel id, camera and layout, in the order first shown.
	std::vector<std::vector<std::string>> panels;
	// Per actor: visibility ("" unset, "shown", "hidden"), position, facing, looping pose.
	std::map<std::string, std::vector<std::string>> actors;

	// Applies one trace line; lines that are not actions are ignored.
	void apply(const std::string& line);
	bool operator==(const Screen& other) const;
	std::string describe() const;
};
Screen screenOf(const std::vector<std::string>& trace);
// The instant calls a host received, as trace lines.
std::vector<std::string> instantLines(const events::RecordingHost& host);

// A recording host that answers choices and battles from a list and remembers how many options each
// decision offered, so tests can walk every path.
class DecisionHost : public events::RecordingHost {
public:
	std::vector<int> picks;
	std::vector<int> optionCounts;
	// Each decision as the host saw it, and the pick it gave.
	std::vector<std::string> labels;
	std::vector<int> given;
	// When set, picks come from here by decision label instead of by position.
	std::map<std::string, int> byLabel;
	bool pickByLabel = false;

protected:
	int pick_choice(const events::ChoiceRequest& request) override;
	events::BattleResult pick_battle(const events::BattleRequest& request) override;

private:
	int next(int count, const std::string& label);
};

// Durations long enough that every action, line and battle spans several steps.
void configure(events::RecordingHost& host);

// A starting world for the host: flags set to true.
using World = std::vector<std::string>;
void seed(events::RecordingHost& host, const World& world);

// Steps host and interpreter until the event stops running; returns the steps taken.
int run(events::Interpreter& interpreter, events::RecordingHost& host, int maxSteps = 20000);

struct Path {
	std::string event;
	World world;
	std::vector<int> picks;
};

// Every combination of choice options and allowed battle results the event can take from each world.
std::vector<Path> allPaths(const events::Program& program, const std::vector<World>& worlds);
// Entry events: events no other event calls.
std::vector<std::string> entryEvents(const events::Program& program);

std::string describe(const Path& path);

}
