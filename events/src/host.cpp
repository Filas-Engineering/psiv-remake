#include "events/host.h"

#include <cstdio>

namespace events {

namespace {

std::string num(double value) {
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%g", value);
	return buffer;
}

}

const char* action_type_name(ActionType type) {
	switch (type) {
	case ActionType::move:
		return "move";
	case ActionType::face:
		return "face";
	case ActionType::animate:
		return "anim";
	case ActionType::show_actor:
		return "show";
	case ActionType::hide_actor:
		return "hide";
	case ActionType::panel_show:
		return "panel show";
	case ActionType::panel_hide:
		return "panel hide";
	case ActionType::panel_clear:
		return "panel clear";
	case ActionType::camera_cut:
		return "camera cut";
	case ActionType::camera_move:
		return "camera move";
	case ActionType::fade_in:
		return "fade in";
	case ActionType::fade_out:
		return "fade out";
	case ActionType::shake:
		return "shake";
	case ActionType::music_play:
		return "music play";
	case ActionType::music_stop:
		return "music stop";
	case ActionType::music_fade:
		return "music fade";
	case ActionType::ambient_play:
		return "ambient play";
	case ActionType::ambient_stop:
		return "ambient stop";
	case ActionType::sound:
		return "sound";
	case ActionType::warp:
		return "warp";
	}
	return "unknown";
}

std::string describe(const Text& text) {
	return text.is_id ? "@" + text.value : "\"" + text.value + "\"";
}

std::string describe(const Action& action) {
	std::string out = action_type_name(action.type);
	switch (action.type) {
	case ActionType::move:
		out += " " + action.subject + " to ";
		if (action.has_position) {
			out += num(action.position.x) + " " + num(action.position.y) + " " + num(action.position.z);
		} else {
			out += action.target;
		}
		if (action.speed > 0) {
			out += " speed " + num(action.speed);
		}
		break;
	case ActionType::face:
	case ActionType::warp:
		out += " " + action.subject + " " + action.target;
		break;
	case ActionType::animate:
		out += " " + action.subject + " " + action.target;
		if (action.loop) {
			out += " loop";
		}
		break;
	case ActionType::panel_show:
		out += " " + action.subject + " camera " + action.target + " layout " + action.slot;
		break;
	case ActionType::show_actor:
	case ActionType::hide_actor:
	case ActionType::panel_hide:
	case ActionType::camera_cut:
	case ActionType::music_play:
	case ActionType::ambient_play:
	case ActionType::sound:
		out += " " + action.subject;
		break;
	case ActionType::camera_move:
		out += " " + action.subject + " " + num(action.seconds);
		break;
	case ActionType::fade_in:
	case ActionType::fade_out:
	case ActionType::music_fade:
		out += " " + num(action.seconds);
		break;
	case ActionType::shake:
		out += " " + num(action.seconds) + " strength " + num(action.strength);
		break;
	case ActionType::panel_clear:
	case ActionType::music_stop:
	case ActionType::ambient_stop:
		break;
	}
	return out;
}

std::string describe(const Line& line) {
	if (line.speaker.empty()) {
		return "narrate " + describe(line.text);
	}
	std::string out = "say " + line.speaker + " " + describe(line.text);
	if (!line.portrait.empty()) {
		out += " as " + line.portrait;
	}
	return out;
}

std::string describe(const ChoiceRequest& request) {
	std::string out = request.yes_no ? "ask" : "choice";
	if (!request.prompt.value.empty()) {
		out += " " + describe(request.prompt);
	}
	if (!request.yes_no) {
		out += " [";
		for (std::size_t i = 0; i < request.options.size(); ++i) {
			out += (i ? " | " : "") + describe(request.options[i]);
		}
		out += "]";
	}
	return out;
}

std::string describe(const BattleRequest& request) {
	std::string out = "battle " + request.encounter;
	out += request.escape_allowed ? " escape" : " no-escape";
	out += request.defeat_handled ? " defeat-branch" : " defeat-game-over";
	if (request.opening != BattleOpening::normal) {
		out += std::string(" opening ") + battle_opening_name(request.opening);
	}
	if (request.end == BattleEndKind::survive_rounds) {
		out += " until survive " + std::to_string(request.end_rounds);
	} else if (request.end == BattleEndKind::enemy_defeated) {
		out += " until defeat " + request.end_enemy;
	}
	return out;
}

std::string describe(const RestRequest& request) {
	return request.point.empty() ? "rest" : "rest " + request.point;
}

const char* battle_result_name(BattleResult result) {
	switch (result) {
	case BattleResult::win:
		return "win";
	case BattleResult::lose:
		return "lose";
	case BattleResult::flee:
		return "flee";
	}
	return "unknown";
}

const char* battle_opening_name(BattleOpening opening) {
	switch (opening) {
	case BattleOpening::normal:
		return "normal";
	case BattleOpening::party_first:
		return "party_first";
	case BattleOpening::enemy_first:
		return "enemy_first";
	}
	return "unknown";
}

bool presentation_only(ActionType type) {
	return type != ActionType::warp;
}

Handle Host::open_shop(const std::string&) {
	return 0;
}

Handle Host::rest_party(const RestRequest&) {
	return 0;
}

Handle Host::open_save_prompt() {
	return 0;
}

Handle Host::open_name_entry(const std::string&) {
	return 0;
}

void Host::log_skipped_line(const Line&) {}

}
