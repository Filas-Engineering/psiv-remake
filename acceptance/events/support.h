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
events::Program compileSamples(acc::Context& t);

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
