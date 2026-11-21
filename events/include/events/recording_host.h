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
	// Vehicles the party has, each with "map marker" where a script last put it. Nobody rides one here.
	std::map<std::string, std::string> vehicles;
	bool player_control = true;
	// Misuse such as polling a released handle; tests expect it to stay empty.
	std::vector<std::string> errors;
	// The descriptions of the calls made with `instant` set, in order. The
	// trace shows them like any other call.
	std::vector<std::string> instant_calls;

	// An action lasts its own `seconds` (fades, camera moves...) unless one of
	// these overrides it; the subject override wins over the type override.
	// Instant actions take no time, except warps.
	void set_duration(ActionType type, double seconds);
	void set_subject_duration(ActionType type, const std::string& subject, double seconds);
	void set_line_duration(double seconds);
	void set_choice_duration(double seconds);
	void set_battle_duration(double seconds);
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
	Handle start_battle(const BattleRequest& request) override;
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
	std::string snapshot_world() override;
	bool restore_world(const std::string& snapshot) override;
	// Menus and rests are logged and done at once.
	Handle open_shop(const std::string& shop) override;
	Handle rest_party(const RestRequest& request) override;
	Handle open_save_prompt() override;
	Handle open_name_entry(const std::string& member) override;
	void log_skipped_line(const Line& line) override;
	void give_vehicle(const std::string& vehicle, const std::string& map, const std::string& marker) override;
	void place_vehicle(const std::string& vehicle, const std::string& map, const std::string& marker) override;
	void remove_vehicle(const std::string& vehicle) override;
	bool has_vehicle(const std::string& vehicle) override;
	bool in_party(const std::string& member) override;
	int item_count(const std::string& item) override;
	void teach_ability(const std::string& member, const std::string& ability) override;
	void set_follower(const std::string& actor) override;

protected:
	// Hooks for hosts built on this one (TimingHost). The defaults use the
	// fixed line duration and the queued answers.
	virtual double line_seconds(const Line& line);
	virtual int pick_choice(const ChoiceRequest& request);
	virtual BattleResult pick_battle(const BattleRequest& request);

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
	double choice_duration_ = 0;
	double battle_duration_ = 0;
	std::deque<int> choices_;
	std::deque<BattleResult> battles_;
};

}
