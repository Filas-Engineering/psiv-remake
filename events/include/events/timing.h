#pragma once

#include "events/diagnostics.h"
#include "events/program.h"
#include "events/recording_host.h"
#include "events/save_state.h"
#include "events/strings.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace events {

// Authored durations for a scene timing run (EVT-062). The file format is in
// FORMAT.md ("Timing configuration").
struct TimingConfig {
	// Text reveal speed. 0 or less means instant.
	double chars_per_second = 30;
	// Added to every line, for example the page-advance delay.
	double seconds_per_line = 0;
	double seconds_per_choice = 0;
	double seconds_per_battle = 0;
	// An action lasts its own `seconds` (fades, camera moves...) unless one
	// of these overrides it; the subject override wins.
	std::map<ActionType, double> action_seconds;
	std::map<std::pair<ActionType, std::string>, double> subject_seconds;
	// Looks up @string.id lines so they have a length. Without it, or when
	// the ID is missing, the line counts as zero characters.
	const StringTable* strings = nullptr;
	// The world each run starts from; anything else reads false or 0.
	std::map<std::string, bool> start_flags;
	std::map<std::string, std::int64_t> start_vars;
};

bool load_timing_config(const std::string& file, const std::string& text, TimingConfig& config, std::vector<Diagnostic>& diagnostics);

// Wall time split by what was on screen. When several things overlap, a
// line or choice counts as dialogue first, then an action or battle, and
// time with neither is wait time (`wait` commands). The parts add up to total.
struct SceneTime {
	double total = 0;
	double dialogue = 0;
	double action = 0;
	double wait = 0;
};

// One choice point or battle a run passed through, and the branch it took.
struct Decision {
	std::string label;
	std::vector<std::string> options;
	int picked = 0;
};

// A host that times a scene without rendering. Each line lasts its
// character count divided by the text speed, plus seconds_per_line.
// Decisions are taken from set_decisions() in order and default to the first
// option (choices) or win (battles) after that.
class TimingHost : public RecordingHost {
public:
	explicit TimingHost(const TimingConfig& config);

	void set_decisions(const std::vector<int>& picks);
	const std::vector<Decision>& decisions() const { return decisions_; }
	// Charges `seconds` to whatever is live, then advances host time.
	void tick(double seconds);
	const SceneTime& time() const { return time_; }
	// Lines whose @string.id had no text to measure.
	int unmeasured_lines() const { return unmeasured_lines_; }

	Handle start_action(const Action& action) override;
	Handle show_line(const Line& line) override;
	Handle present_choice(const ChoiceRequest& request) override;
	Handle start_battle(const BattleRequest& request) override;
	void cancel(Handle handle) override;
	void release(Handle handle) override;

protected:
	double line_seconds(const Line& line) override;
	int pick_choice(const ChoiceRequest& request) override;
	BattleResult pick_battle(const BattleRequest& request) override;

private:
	enum class Kind { dialogue, action };

	int next_pick(const std::string& label, const std::vector<std::string>& options);

	TimingConfig config_;
	std::vector<int> picks_;
	std::vector<Decision> decisions_;
	std::map<Handle, Kind> live_kinds_;
	SceneTime time_;
	int unmeasured_lines_ = 0;
};

struct TimedPath {
	std::vector<Decision> decisions;
	SceneTime time;
	Status status = Status::idle;
	// Set when the run stopped with a runtime error or did not finish.
	std::string error;
	int unmeasured_lines = 0;
};

// Plays the event once per combination of choice options and allowed battle
// results, depth first, each run starting from the config's start world.
// Returns false (with `error`) when the event does not exist.
bool time_event(const Program& program, const std::string& event, const TimingConfig& config, std::vector<TimedPath>& paths, bool& truncated,
	std::string& error, double step_seconds = 0.001, std::size_t max_paths = 256);

}
