#include "events/host.h"

#include <algorithm>
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
	case ActionType::place_actor:
		return "place";
	case ActionType::camera_to:
		return "camera to";
	case ActionType::camera_reset:
		return "camera reset";
	}
	return "unknown";
}

const std::vector<StandardAnimation>& standard_animations() {
	static const std::vector<StandardAnimation> list = {
		{"fade_in", 0.5, AnimationEnd::shown},
		{"fade_out", 0.5, AnimationEnd::hidden},
		{"dissolve", 0.8, AnimationEnd::hidden},
		{"blink_out", 0.6, AnimationEnd::hidden},
		{"hop", 0.3, AnimationEnd::none},
		{"shake", 0.4, AnimationEnd::none},
	};
	return list;
}

const StandardAnimation* standard_animation(const std::string& name) {
	for (const StandardAnimation& a : standard_animations()) {
		if (name == a.name) {
			return &a;
		}
	}
	return nullptr;
}

bool is_direction(const std::string& word) {
	return word == "north" || word == "east" || word == "south" || word == "west";
}

bool parse_action_type(const std::string& name, ActionType& type) {
	for (int i = 0; i <= static_cast<int>(ActionType::camera_reset); ++i) {
		std::string candidate = action_type_name(static_cast<ActionType>(i));
		for (char& c : candidate) {
			if (c == ' ') {
				c = '_';
			}
		}
		if (candidate == name) {
			type = static_cast<ActionType>(i);
			return true;
		}
	}
	return false;
}

double action_seconds(const Action& action, const std::map<ActionType, double>& by_type,
	const std::map<std::pair<ActionType, std::string>, double>& by_subject) {
	auto subject = by_subject.find({action.type, action.subject});
	if (subject != by_subject.end()) {
		return subject->second;
	}
	auto type = by_type.find(action.type);
	return type != by_type.end() ? type->second : action.seconds;
}

std::string describe(const Text& text) {
	return text.is_id ? "@" + text.value : "\"" + text.value + "\"";
}

std::string describe(const Action& action) {
	std::string out = action_type_name(action.type);
	switch (action.type) {
	case ActionType::move:
	case ActionType::place_actor:
		out += " " + action.subject;
		if (!action.waypoints.empty()) {
			out += " via";
			for (const std::string& w : action.waypoints) {
				out += " " + w;
			}
		}
		out += action.type == ActionType::move ? " to " : " ";
		if (action.has_position) {
			out += num(action.position.x) + " " + num(action.position.y) + " " + num(action.position.z);
		} else {
			out += action.target;
		}
		if (action.speed > 0) {
			out += " speed " + num(action.speed);
		}
		if (action.pathfind) {
			out += " pathfind";
		}
		if (!action.facing.empty()) {
			out += " facing " + action.facing;
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
		out += " " + action.subject;
		if (!action.target.empty()) {
			out += " with " + action.target;
		}
		break;
	case ActionType::music_play:
		out += " " + action.subject;
		if (action.fade_ms == 0) {
			out += " cut";
		} else if (action.fade_ms > 0) {
			out += " fade " + std::to_string(action.fade_ms);
		}
		break;
	case ActionType::panel_hide:
	case ActionType::camera_cut:
	case ActionType::ambient_play:
	case ActionType::sound:
		out += " " + action.subject;
		break;
	case ActionType::camera_to:
		out += " " + (action.has_position ? num(action.position.x) + " " + num(action.position.y) : action.target);
		if (action.seconds > 0) {
			out += " " + num(action.seconds);
		}
		break;
	case ActionType::camera_reset:
		if (action.seconds > 0) {
			out += " " + num(action.seconds);
		}
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
	if (!request.header.value.empty()) {
		out += " header " + describe(request.header);
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
	if (!request.vehicle.empty()) {
		out += " aboard " + request.vehicle;
	}
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

std::string describe(const PartySwap& swap) {
	std::string out = "party swap " + (swap.slot > 0 ? std::to_string(swap.slot) : swap.replaced) + " " + swap.member;
	if (swap.equipment != SwapEquipment::rule) {
		out += swap.equipment == SwapEquipment::keep ? " equipment keep" : " equipment bag";
	}
	if (swap.silent) {
		out += " silent";
	}
	return out;
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

void Host::give_vehicle(const std::string&, const std::string&, const std::string&) {}

void Host::place_vehicle(const std::string&, const std::string&, const std::string&) {}

void Host::remove_vehicle(const std::string&) {}

bool Host::has_vehicle(const std::string&) {
	return false;
}

bool Host::in_party(const std::string&) {
	return false;
}

int Host::item_count(const std::string&) {
	return 0;
}

bool Host::game_state(const std::string&, const std::string&) {
	return false;
}

bool Host::aboard_vehicle(const std::string&) {
	return false;
}

void Host::teach_ability(const std::string&, const std::string&) {}

void Host::set_follower(const std::string&) {}

std::int64_t Host::money() {
	return 0;
}

void Host::change_money(std::int64_t) {}

void Host::equip_piece(const std::string&, const std::string&) {}

void Host::unequip_piece(const std::string&, const std::string&) {}

void Host::set_area_music(const std::string&) {}

void Host::remove_actor(const std::string&, bool) {}

void Host::save_party(const std::string&) {}

void Host::spawn_event(const std::string&) {}

void Host::set_actor_look(const std::string&, const std::string&, const std::string&) {}

bool Host::restore_party(const std::string&) {
	return false;
}

void Host::swap_party_member(const PartySwap&) {}

void Host::add_party_member_silently(const std::string& member) {
	add_party_member(member);
}

void Host::remove_party_member_silently(const std::string& member) {
	remove_party_member(member);
}

bool Host::order_party(const std::vector<std::string>& members) {
	return std::all_of(members.begin(), members.end(), [this](const std::string& member) { return in_party(member); });
}

Handle Host::play_credits(const std::string&) {
	return 0;
}

void Host::end_game() {}

void Host::show_picture(const std::string&) {}

void Host::hide_picture(const std::string&) {}

void Host::clear_pictures() {}

void Host::register_town(const std::string&, bool) {}

}
