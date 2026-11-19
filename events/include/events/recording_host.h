#pragma once

#include "events/host.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace events {

// A fake Host for tests and the CLI. It logs every call except the polling
// ones (is_done, result, release) so traces do not depend on step size.
class RecordingHost : public Host {
public:
	std::vector<std::string> trace;
	std::map<std::string, bool> flags;
	std::map<std::string, std::int64_t> vars;
	std::map<std::string, int> items;
	std::vector<std::string> party;
	bool player_control = true;
	// Misuse such as polling a released handle; tests expect it to stay empty.
	std::vector<std::string> errors;

	// An action lasts its own `seconds` (fades, camera moves...) unless one of
	// these overrides it; the subject override wins over the type override.
	void set_duration(ActionType type, double seconds);
	void set_subject_duration(ActionType type, const std::string& subject, double seconds);
	void set_line_duration(double seconds);
	// Answers are used in order; an empty queue picks option 0 / win.
	void queue_choices(const std::vector<int>& picks);
	void queue_battle_results(const std::vector<BattleResult>& results);
	void advance(double seconds);

	// The same world and remaining scripted answers with no trace and no live
	// handles: what a game looks like after loading a save.
	RecordingHost reloaded() const;
	std::size_t live_handles() const { return live_.size(); }
	bool same_world(const RecordingHost& other) const;

	Handle start_action(const Action& action) override;
	Handle show_line(const Line& line) override;
	Handle present_choice(const ChoiceRequest& request) override;
	bool is_done(Handle handle) override;
	int result(Handle handle) override;
	void cancel(Handle handle) override;
	void release(Handle handle) override;

	bool get_flag(const std::string& name) override;
	void set_flag(const std::string& name, bool value) override;
	std::int64_t get_var(const std::string& name) override;
	void set_var(const std::string& name, std::int64_t value) override;
	void give_item(const std::string& item, int count) override;
	void take_item(const std::string& item, int count) override;
	void add_party_member(const std::string& member) override;
	void remove_party_member(const std::string& member) override;
	void set_player_control(bool enabled) override;

private:
	struct Pending {
		double remaining = 0;
		int result = 0;
		std::string description;
	};

	Handle open(double seconds, int result, const std::string& description);

	Handle next_handle_ = 1;
	std::map<Handle, Pending> live_;
	std::map<ActionType, double> durations_;
	std::map<std::pair<ActionType, std::string>, double> subject_durations_;
	double line_duration_ = 0;
	std::deque<int> choices_;
	std::deque<BattleResult> battles_;
};

}
