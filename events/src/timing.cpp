#include "events/timing.h"

#include "events/interpreter.h"
#include "text_util.h"

#include <cstdlib>
#include <sstream>

namespace events {

namespace {

// Ends runs that never finish, such as a loop that waits forever.
const double max_scene_seconds = 1800;

std::size_t count_characters(const std::string& text) {
	std::size_t count = 0;
	for (unsigned char c : text) {
		if ((c & 0xC0) != 0x80 && c != '\n') {
			++count;
		}
	}
	return count;
}

bool parse_action_type(const std::string& word, ActionType& type) {
	for (int i = 0; i <= static_cast<int>(ActionType::warp); ++i) {
		std::string name = action_type_name(static_cast<ActionType>(i));
		for (char& c : name) {
			if (c == ' ') {
				c = '_';
			}
		}
		if (name == word) {
			type = static_cast<ActionType>(i);
			return true;
		}
	}
	return false;
}

bool parse_seconds(const std::string& word, double& out) {
	char* end = nullptr;
	out = std::strtod(word.c_str(), &end);
	return !word.empty() && end && *end == '\0' && out >= 0;
}

}

bool load_timing_config(const std::string& file, const std::string& text, TimingConfig& config, std::vector<Diagnostic>& diagnostics) {
	TimingConfig result = config;
	bool ok = true;
	std::vector<std::string> lines = split_lines(text);
	for (std::size_t i = 0; i < lines.size(); ++i) {
		int number = static_cast<int>(i) + 1;
		std::string line = trim(lines[i].substr(0, lines[i].find('#')));
		if (line.empty()) {
			continue;
		}
		std::istringstream in(line);
		std::vector<std::string> words;
		std::string w;
		while (in >> w) {
			words.push_back(w);
		}
		DiagnosticMessage problem;
		double seconds = 0;
		const std::string& key = words[0];
		if (key == "text_speed" || key == "line_pause" || key == "choice_time" || key == "battle_time") {
			if (words.size() != 2 || !parse_seconds(words[1], seconds)) {
				problem = {"timing.expected_number", {key}};
			} else if (key == "text_speed") {
				result.chars_per_second = seconds;
			} else if (key == "line_pause") {
				result.seconds_per_line = seconds;
			} else if (key == "choice_time") {
				result.seconds_per_choice = seconds;
			} else {
				result.seconds_per_battle = seconds;
			}
		} else if (key == "duration") {
			ActionType type = ActionType::move;
			if (words.size() < 3 || words.size() > 4) {
				problem = {"timing.duration_shape", {}};
			} else if (!parse_action_type(words[1], type)) {
				problem = {"timing.unknown_action", {words[1]}};
			} else if (!parse_seconds(words.back(), seconds)) {
				problem = {"timing.duration_seconds", {words.back()}};
			} else if (words.size() == 4) {
				result.subject_seconds[{type, words[2]}] = seconds;
			} else {
				result.action_seconds[type] = seconds;
			}
		} else {
			problem = {"timing.unknown_setting", {key}};
		}
		if (!problem.empty()) {
			diagnostics.push_back(diagnostic(Severity::error, file, number, problem));
			ok = false;
		}
	}
	if (ok) {
		config = result;
	}
	return ok;
}

TimingHost::TimingHost(const TimingConfig& config) : config_(config) {
	for (const auto& d : config.action_seconds) {
		set_duration(d.first, d.second);
	}
	for (const auto& d : config.subject_seconds) {
		set_subject_duration(d.first.first, d.first.second, d.second);
	}
	set_choice_duration(config.seconds_per_choice);
	set_battle_duration(config.seconds_per_battle);
	flags = config.start_flags;
	vars = config.start_vars;
}

void TimingHost::set_decisions(const std::vector<int>& picks) {
	picks_ = picks;
}

void TimingHost::tick(double seconds) {
	bool dialogue = false;
	bool action = false;
	for (const auto& live : live_kinds_) {
		dialogue = dialogue || live.second == Kind::dialogue;
		action = action || live.second == Kind::action;
	}
	time_.total += seconds;
	if (dialogue) {
		time_.dialogue += seconds;
	} else if (action) {
		time_.action += seconds;
	} else {
		time_.wait += seconds;
	}
	advance(seconds);
}

Handle TimingHost::start_action(const Action& action) {
	Handle handle = RecordingHost::start_action(action);
	live_kinds_[handle] = Kind::action;
	return handle;
}

Handle TimingHost::show_line(const Line& line) {
	Handle handle = RecordingHost::show_line(line);
	live_kinds_[handle] = Kind::dialogue;
	return handle;
}

Handle TimingHost::present_choice(const ChoiceRequest& request) {
	Handle handle = RecordingHost::present_choice(request);
	live_kinds_[handle] = Kind::dialogue;
	return handle;
}

Handle TimingHost::start_battle(const BattleRequest& request) {
	Handle handle = RecordingHost::start_battle(request);
	live_kinds_[handle] = Kind::action;
	return handle;
}

void TimingHost::cancel(Handle handle) {
	live_kinds_.erase(handle);
	RecordingHost::cancel(handle);
}

void TimingHost::release(Handle handle) {
	live_kinds_.erase(handle);
	RecordingHost::release(handle);
}

double TimingHost::line_seconds(const Line& line) {
	std::string text = line.text.value;
	if (line.text.is_id && (!config_.strings || !resolve_text(line.text, *config_.strings, text))) {
		++unmeasured_lines_;
		text.clear();
	}
	double reveal = config_.chars_per_second > 0 ? static_cast<double>(count_characters(text)) / config_.chars_per_second : 0;
	return reveal + config_.seconds_per_line;
}

int TimingHost::next_pick(const std::string& label, const std::vector<std::string>& options) {
	std::size_t index = decisions_.size();
	int pick = index < picks_.size() ? picks_[index] : 0;
	decisions_.push_back({label, options, pick});
	return pick;
}

int TimingHost::pick_choice(const ChoiceRequest& request) {
	std::string label = request.yes_no ? "ask" : "choice";
	if (!request.prompt.value.empty()) {
		label += " " + describe(request.prompt);
	}
	std::vector<std::string> options;
	if (request.yes_no) {
		options = {"yes", "no"};
	} else {
		for (const Text& option : request.options) {
			options.push_back(describe(option));
		}
	}
	return next_pick(label, options);
}

BattleResult TimingHost::pick_battle(const BattleRequest& request) {
	std::vector<BattleResult> allowed = {BattleResult::win};
	if (request.defeat_handled) {
		allowed.push_back(BattleResult::lose);
	}
	if (request.escape_allowed) {
		allowed.push_back(BattleResult::flee);
	}
	std::vector<std::string> options;
	for (BattleResult r : allowed) {
		options.push_back(battle_result_name(r));
	}
	int pick = next_pick("battle " + request.encounter, options);
	return pick >= 0 && pick < static_cast<int>(allowed.size()) ? allowed[pick] : BattleResult::win;
}

bool time_event(const Program& program, const std::string& event, const TimingConfig& config, std::vector<TimedPath>& paths, bool& truncated,
	std::string& error, double step_seconds, std::size_t max_paths) {
	paths.clear();
	truncated = false;
	if (program.find_event(event) < 0) {
		error = "unknown event '" + event + "'";
		return false;
	}
	std::vector<std::vector<int>> pending = {{}};
	while (!pending.empty()) {
		if (paths.size() >= max_paths) {
			truncated = true;
			break;
		}
		std::vector<int> prefix = pending.back();
		pending.pop_back();
		TimingHost host(config);
		host.set_decisions(prefix);
		Interpreter interpreter(program, host);
		interpreter.start(event);
		interpreter.step(0);
		while (interpreter.status() == Status::running && host.time().total < max_scene_seconds) {
			host.tick(step_seconds);
			interpreter.step(step_seconds);
		}
		TimedPath path;
		path.decisions = host.decisions();
		path.time = host.time();
		path.status = interpreter.status();
		path.unmeasured_lines = host.unmeasured_lines();
		if (path.status == Status::error) {
			path.error = interpreter.error();
		} else if (path.status != Status::finished) {
			path.error = "did not finish within " + std::to_string(static_cast<int>(max_scene_seconds)) + " seconds";
		}
		paths.push_back(path);
		// Queue every untried branch after the prefix, so paths come out in
		// lexicographic order of their picks.
		for (std::size_t j = prefix.size(); j < path.decisions.size(); ++j) {
			for (int option = static_cast<int>(path.decisions[j].options.size()) - 1; option >= 1; --option) {
				std::vector<int> next;
				for (std::size_t k = 0; k < j; ++k) {
					next.push_back(path.decisions[k].picked);
				}
				next.push_back(option);
				pending.push_back(next);
			}
		}
	}
	return true;
}

}
