#include "events/recording_host.h"

#include <algorithm>
#include <sstream>

namespace events {

void RecordingHost::set_duration(ActionType type, double seconds) {
	durations_[type] = seconds;
}

void RecordingHost::set_subject_duration(ActionType type, const std::string& subject, double seconds) {
	subject_durations_[{type, subject}] = seconds;
}

void RecordingHost::set_line_duration(double seconds) {
	line_duration_ = seconds;
}

void RecordingHost::set_choice_duration(double seconds) {
	choice_duration_ = seconds;
}

void RecordingHost::set_battle_duration(double seconds) {
	battle_duration_ = seconds;
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
	copy.instant_calls.clear();
	copy.errors.clear();
	copy.live_.clear();
	return copy;
}

bool RecordingHost::same_world(const RecordingHost& other) const {
	std::vector<std::string> a = party;
	std::vector<std::string> b = other.party;
	std::sort(a.begin(), a.end());
	std::sort(b.begin(), b.end());
	return flags == other.flags && vars == other.vars && items == other.items && a == b && vehicles == other.vehicles && removed == other.removed &&
		party_snapshots == other.party_snapshots && player_control == other.player_control;
}

Handle RecordingHost::open(double seconds, int result, const std::string& description) {
	Handle handle = next_handle_++;
	live_[handle] = {seconds, result, description};
	return handle;
}

Handle RecordingHost::start_action(const Action& action) {
	double seconds = action_seconds(action, durations_, subject_durations_);
	std::string description = describe(action);
	trace.push_back("action " + description);
	if (action.instant) {
		instant_calls.push_back(description);
		// An instant warp skips its transition, but the map still has to load.
		if (presentation_only(action.type)) {
			seconds = 0;
		}
	}
	return open(seconds, 0, description);
}

Handle RecordingHost::show_line(const Line& line) {
	std::string description = describe(line);
	trace.push_back(description);
	return open(line_seconds(line), 0, description);
}

Handle RecordingHost::present_choice(const ChoiceRequest& request) {
	int pick = pick_choice(request);
	std::string description = describe(request);
	trace.push_back(description + " -> " + std::to_string(pick));
	return open(choice_duration_, pick, description);
}

Handle RecordingHost::start_battle(const BattleRequest& request) {
	BattleResult result = pick_battle(request);
	std::string description = describe(request);
	trace.push_back(description + " -> " + battle_result_name(result));
	return open(battle_duration_, static_cast<int>(result), description);
}

double RecordingHost::line_seconds(const Line&) {
	return line_duration_;
}

int RecordingHost::pick_choice(const ChoiceRequest&) {
	int pick = 0;
	if (!choices_.empty()) {
		pick = choices_.front();
		choices_.pop_front();
	}
	return pick;
}

BattleResult RecordingHost::pick_battle(const BattleRequest&) {
	BattleResult result = BattleResult::win;
	if (!battles_.empty()) {
		result = battles_.front();
		battles_.pop_front();
	}
	return result;
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

void RecordingHost::give_vehicle(const std::string& vehicle, const std::string& map, const std::string& marker) {
	trace.push_back("vehicle give " + vehicle + " " + map + " " + marker);
	vehicles[vehicle] = map + " " + marker;
}

void RecordingHost::place_vehicle(const std::string& vehicle, const std::string& map, const std::string& marker) {
	trace.push_back("vehicle place " + vehicle + " " + map + " " + marker);
	auto it = vehicles.find(vehicle);
	if (it != vehicles.end()) {
		it->second = map + " " + marker;
	}
}

void RecordingHost::remove_vehicle(const std::string& vehicle) {
	trace.push_back("vehicle remove " + vehicle);
	vehicles.erase(vehicle);
}

void RecordingHost::teach_ability(const std::string& member, const std::string& ability) {
	trace.push_back("learn " + member + " " + ability);
}

void RecordingHost::set_follower(const std::string& actor) {
	trace.push_back("follower " + (actor.empty() ? std::string("none") : actor));
}

std::int64_t RecordingHost::money() {
	trace.push_back("money -> " + std::to_string(purse));
	return purse;
}

void RecordingHost::change_money(std::int64_t delta) {
	trace.push_back("money " + std::to_string(delta));
	purse += delta;
}

void RecordingHost::equip_piece(const std::string& member, const std::string& piece) {
	trace.push_back("equip " + member + " " + piece);
}

void RecordingHost::unequip_piece(const std::string& member, const std::string& piece) {
	trace.push_back("unequip " + member + " " + piece);
}

void RecordingHost::set_area_music(const std::string& track) {
	trace.push_back("music area " + (track.empty() ? std::string("default") : track));
}

void RecordingHost::remove_actor(const std::string& actor, bool until_reload) {
	trace.push_back("remove " + actor + (until_reload ? " until reload" : ""));
	removed[actor] = until_reload;
}

void RecordingHost::spawn_event(const std::string& event) {
	trace.push_back("spawn " + event);
	spawned.push_back(event);
}

void RecordingHost::set_actor_look(const std::string& actor, const std::string& enemy, const std::string& look) {
	trace.push_back("look " + actor + (!enemy.empty() ? " enemy " + enemy : !look.empty() ? " " + look : std::string(" default")));
}

Handle RecordingHost::play_credits(const std::string& credits) {
	trace.push_back("credits " + credits);
	return open(0, 0, "credits " + credits);
}

void RecordingHost::end_game() {
	trace.push_back("end game");
}

void RecordingHost::show_picture(const std::string& picture) {
	trace.push_back("picture show " + picture);
}

void RecordingHost::hide_picture(const std::string& picture) {
	trace.push_back("picture hide" + (picture.empty() ? std::string() : " " + picture));
}

void RecordingHost::clear_pictures() {
	trace.push_back("picture clear");
}

void RecordingHost::register_town(const std::string& area, bool registered) {
	trace.push_back((registered ? "register town " : "unregister town ") + area);
}

void RecordingHost::save_party(const std::string& snapshot) {
	trace.push_back("party save " + snapshot);
	party_snapshots[snapshot] = party;
}

bool RecordingHost::restore_party(const std::string& snapshot) {
	const auto saved = party_snapshots.find(snapshot);
	trace.push_back("party restore " + snapshot + " -> " + (saved == party_snapshots.end() ? "false" : "true"));
	if (saved == party_snapshots.end()) {
		return false;
	}
	party = saved->second;
	return true;
}

void RecordingHost::swap_party_member(const PartySwap& swap) {
	trace.push_back(describe(swap));
	const std::size_t slot = swap.slot > 0 ? static_cast<std::size_t>(swap.slot - 1) : static_cast<std::size_t>(std::find(party.begin(), party.end(), swap.replaced) - party.begin());
	if (slot < party.size() && std::find(party.begin(), party.end(), swap.member) == party.end()) {
		party[slot] = swap.member;
	}
}

void RecordingHost::add_party_member_silently(const std::string& member) {
	trace.push_back("party add " + member + " silent");
	if (std::find(party.begin(), party.end(), member) == party.end()) {
		party.push_back(member);
	}
}

void RecordingHost::remove_party_member_silently(const std::string& member) {
	trace.push_back("party remove " + member + " silent");
	party.erase(std::remove(party.begin(), party.end(), member), party.end());
}

bool RecordingHost::order_party(const std::vector<std::string>& members) {
	std::string line = "party order";
	bool known = true;
	for (const std::string& member : members) {
		line += " " + member;
		known = known && std::find(party.begin(), party.end(), member) != party.end();
	}
	trace.push_back(line + " -> " + (known ? "true" : "false"));
	if (!known) {
		return false;
	}
	std::vector<std::string> ordered = members;
	for (const std::string& member : party) {
		if (std::find(members.begin(), members.end(), member) == members.end()) {
			ordered.push_back(member);
		}
	}
	party = std::move(ordered);
	return true;
}

bool RecordingHost::has_vehicle(const std::string& vehicle) {
	const bool held = vehicles.count(vehicle) > 0;
	trace.push_back("has_vehicle " + vehicle + " -> " + (held ? "true" : "false"));
	return held;
}

bool RecordingHost::in_party(const std::string& member) {
	const bool in = std::find(party.begin(), party.end(), member) != party.end();
	trace.push_back("in_party " + member + " -> " + (in ? "true" : "false"));
	return in;
}

int RecordingHost::item_count(const std::string& item) {
	const auto held = items.find(item);
	const int count = held == items.end() ? 0 : held->second;
	trace.push_back("item_count " + item + " -> " + std::to_string(count));
	return count;
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

Handle RecordingHost::open_shop(const std::string& shop) {
	trace.push_back("shop " + shop);
	return open(0, 0, "shop " + shop);
}

Handle RecordingHost::rest_party(const RestRequest& request) {
	std::string description = describe(request);
	trace.push_back(description);
	if (request.instant) {
		instant_calls.push_back(description);
	}
	return open(0, 0, description);
}

Handle RecordingHost::open_save_prompt() {
	trace.push_back("save prompt");
	return open(0, 0, "save prompt");
}

Handle RecordingHost::open_name_entry(const std::string& member) {
	trace.push_back("name entry " + member);
	return open(0, 0, "name entry " + member);
}

void RecordingHost::log_skipped_line(const Line& line) {
	trace.push_back("skipped " + describe(line));
}

// One record per line: "flag name 0|1", "var name n", "item name n",
// "party name", "control 0|1". Names never hold spaces.
std::string RecordingHost::snapshot_world() {
	std::ostringstream out;
	for (const auto& f : flags) {
		out << "flag " << f.first << ' ' << (f.second ? 1 : 0) << '\n';
	}
	for (const auto& v : vars) {
		out << "var " << v.first << ' ' << v.second << '\n';
	}
	for (const auto& it : items) {
		out << "item " << it.first << ' ' << it.second << '\n';
	}
	for (const std::string& member : party) {
		out << "party " << member << '\n';
	}
	for (const auto& v : vehicles) {
		out << "vehicle " << v.first << ' ' << v.second << '\n';
	}
	for (const auto& r : removed) {
		out << "removed " << r.first << ' ' << (r.second ? 1 : 0) << '\n';
	}
	for (const auto& [name, members] : party_snapshots) {
		out << "snapshot " << name;
		for (const std::string& member : members) {
			out << ' ' << member;
		}
		out << '\n';
	}
	out << "control " << (player_control ? 1 : 0) << '\n';
	return out.str();
}

bool RecordingHost::restore_world(const std::string& snapshot) {
	std::map<std::string, bool> new_flags;
	std::map<std::string, std::int64_t> new_vars;
	std::map<std::string, int> new_items;
	std::vector<std::string> new_party;
	std::map<std::string, std::string> new_vehicles;
	std::map<std::string, bool> new_removed;
	std::map<std::string, std::vector<std::string>> new_snapshots;
	bool control = true;
	std::istringstream in(snapshot);
	std::string record;
	while (std::getline(in, record)) {
		std::istringstream fields(record);
		std::string kind;
		std::string name;
		fields >> kind;
		if (kind == "flag") {
			int value = 0;
			if (!(fields >> name >> value)) {
				return false;
			}
			new_flags[name] = value != 0;
		} else if (kind == "var") {
			std::int64_t value = 0;
			if (!(fields >> name >> value)) {
				return false;
			}
			new_vars[name] = value;
		} else if (kind == "item") {
			int value = 0;
			if (!(fields >> name >> value)) {
				return false;
			}
			new_items[name] = value;
		} else if (kind == "party") {
			if (!(fields >> name)) {
				return false;
			}
			new_party.push_back(name);
		} else if (kind == "vehicle") {
			std::string map;
			std::string marker;
			if (!(fields >> name >> map >> marker)) {
				return false;
			}
			new_vehicles[name] = map + " " + marker;
		} else if (kind == "removed") {
			int value = 0;
			if (!(fields >> name >> value)) {
				return false;
			}
			new_removed[name] = value != 0;
		} else if (kind == "snapshot") {
			if (!(fields >> name)) {
				return false;
			}
			std::vector<std::string>& members = new_snapshots[name];
			for (std::string member; fields >> member;) {
				members.push_back(member);
			}
		} else if (kind == "control") {
			int value = 0;
			if (!(fields >> value)) {
				return false;
			}
			control = value != 0;
		} else {
			return false;
		}
	}
	trace.push_back("restore_world");
	flags = new_flags;
	vars = new_vars;
	items = new_items;
	party = new_party;
	vehicles = new_vehicles;
	removed = new_removed;
	party_snapshots = new_snapshots;
	player_control = control;
	return true;
}

}
