#include "events/recording_host.h"

#include <algorithm>

namespace events {

namespace {

const char* battle_name(int result) {
	switch (result) {
	case 0:
		return "win";
	case 1:
		return "lose";
	default:
		return "flee";
	}
}

}

void RecordingHost::set_duration(ActionType type, double seconds) {
	durations_[type] = seconds;
}

void RecordingHost::set_subject_duration(ActionType type, const std::string& subject, double seconds) {
	subject_durations_[{type, subject}] = seconds;
}

void RecordingHost::set_line_duration(double seconds) {
	line_duration_ = seconds;
}

void RecordingHost::queue_choices(const std::vector<int>& picks) {
	choices_.insert(choices_.end(), picks.begin(), picks.end());
}

void RecordingHost::queue_battle_results(const std::vector<BattleResult>& results) {
	battles_.insert(battles_.end(), results.begin(), results.end());
}

void RecordingHost::advance(double seconds) {
	for (auto& entry : live_) {
		entry.second.remaining -= seconds;
	}
}

RecordingHost RecordingHost::reloaded() const {
	RecordingHost copy = *this;
	copy.trace.clear();
	copy.errors.clear();
	copy.live_.clear();
	return copy;
}

bool RecordingHost::same_world(const RecordingHost& other) const {
	std::vector<std::string> a = party;
	std::vector<std::string> b = other.party;
	std::sort(a.begin(), a.end());
	std::sort(b.begin(), b.end());
	return flags == other.flags && vars == other.vars && items == other.items && a == b && player_control == other.player_control;
}

Handle RecordingHost::open(double seconds, int result, const std::string& description) {
	Handle handle = next_handle_++;
	live_[handle] = {seconds, result, description};
	return handle;
}

Handle RecordingHost::start_action(const Action& action) {
	double seconds = action.seconds;
	auto by_type = durations_.find(action.type);
	if (by_type != durations_.end()) {
		seconds = by_type->second;
	}
	auto by_subject = subject_durations_.find({action.type, action.subject});
	if (by_subject != subject_durations_.end()) {
		seconds = by_subject->second;
	}
	std::string description = describe(action);
	int result = 0;
	if (action.type == ActionType::battle) {
		if (!battles_.empty()) {
			result = static_cast<int>(battles_.front());
			battles_.pop_front();
		}
		trace.push_back("action " + description + " -> " + battle_name(result));
	} else {
		trace.push_back("action " + description);
	}
	return open(seconds, result, description);
}

Handle RecordingHost::show_line(const Line& line) {
	std::string description = describe(line);
	trace.push_back(description);
	return open(line_duration_, 0, description);
}

Handle RecordingHost::present_choice(const ChoiceRequest& request) {
	int pick = 0;
	if (!choices_.empty()) {
		pick = choices_.front();
		choices_.pop_front();
	}
	std::string description = describe(request);
	trace.push_back(description + " -> " + std::to_string(pick));
	return open(0, pick, description);
}

bool RecordingHost::is_done(Handle handle) {
	auto found = live_.find(handle);
	if (found == live_.end()) {
		errors.push_back("is_done on unknown handle " + std::to_string(handle));
		return true;
	}
	return found->second.remaining <= 1e-9;
}

int RecordingHost::result(Handle handle) {
	auto found = live_.find(handle);
	if (found == live_.end()) {
		errors.push_back("result on unknown handle " + std::to_string(handle));
		return -1;
	}
	return found->second.result;
}

void RecordingHost::cancel(Handle handle) {
	auto found = live_.find(handle);
	if (found == live_.end()) {
		errors.push_back("cancel on unknown handle " + std::to_string(handle));
		return;
	}
	trace.push_back("cancel " + found->second.description);
	live_.erase(found);
}

void RecordingHost::release(Handle handle) {
	if (!live_.erase(handle)) {
		errors.push_back("release on unknown handle " + std::to_string(handle));
	}
}

bool RecordingHost::get_flag(const std::string& name) {
	auto found = flags.find(name);
	bool value = found != flags.end() && found->second;
	trace.push_back("get_flag " + name + " -> " + (value ? "true" : "false"));
	return value;
}

void RecordingHost::set_flag(const std::string& name, bool value) {
	trace.push_back("set_flag " + name + " " + (value ? "true" : "false"));
	flags[name] = value;
}

std::int64_t RecordingHost::get_var(const std::string& name) {
	auto found = vars.find(name);
	std::int64_t value = found == vars.end() ? 0 : found->second;
	trace.push_back("get_var " + name + " -> " + std::to_string(value));
	return value;
}

void RecordingHost::set_var(const std::string& name, std::int64_t value) {
	trace.push_back("set_var " + name + " " + std::to_string(value));
	vars[name] = value;
}

void RecordingHost::give_item(const std::string& item, int count) {
	trace.push_back("give " + item + " " + std::to_string(count));
	items[item] += count;
}

void RecordingHost::take_item(const std::string& item, int count) {
	trace.push_back("take " + item + " " + std::to_string(count));
	int& held = items[item];
	held = std::max(0, held - count);
	if (held == 0) {
		items.erase(item);
	}
}

void RecordingHost::add_party_member(const std::string& member) {
	trace.push_back("party add " + member);
	if (std::find(party.begin(), party.end(), member) == party.end()) {
		party.push_back(member);
	}
}

void RecordingHost::remove_party_member(const std::string& member) {
	trace.push_back("party remove " + member);
	party.erase(std::remove(party.begin(), party.end(), member), party.end());
}

void RecordingHost::set_player_control(bool enabled) {
	trace.push_back(std::string("player_control ") + (enabled ? "on" : "off"));
	player_control = enabled;
}

}
