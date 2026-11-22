#include "game/capabilities.h"

#include "game/data.h"
#include "game/profile.h"
#include "game/save.h"
#include "game/world.h"

#include "events/host.h"
#include "events/language.h"
#include "events/save_state.h"
#include "rules/version.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>

namespace game {

const std::vector<rules::DataKeySet>& gameDataKeys() {
	static const std::vector<rules::DataKeySet> sets = {
		rules::DataKeySet("area", "", {"id", "kind", "encounter_mode", "step_ms", "music", "region", "arrival", "dungeon_exit", "map", "markers", "exits", "hooks", "shops", "rests", "chests", "ports", "zones", "helms", "gangway", "changes", "conveyors", "arrive_notice", "counters", "platforms", "field_status", "name", "field_poison", "field_paralysis", "arrival_from", "arrival_not_from"}),
		rules::DataKeySet("area", "arrive_notice[]", {"from", "notice"}),
		rules::DataKeySet("area", "name[]", {"from", "flag", "key"}),
		rules::DataKeySet("area", "changes[]", {"flag", "tile", "cells"}),
		rules::DataKeySet("area", "chests[]", {"id", "at", "flag", "items", "equipment", "money"}),
		rules::DataKeySet("area", "conveyors[]", {"cells", "direction", "step_ms", "require_flag", "block_flag"}),
		rules::DataKeySet("area", "dungeon_exit", {"area", "at", "marker", "facing"}),
		rules::DataKeySet("area", "exits[]", {"at", "to", "marker", "fall", "require_flag", "block_flag", "closed_tile", "sound", "fires", "followers"}),
		rules::DataKeySet("area", "helms[]", {"id", "at"}),
		rules::DataKeySet("area", "hooks[]", {"id", "at", "trigger", "event", "require_flag", "block_flag", "once", "tile", "solid", "actor_at", "across", "cells", "actor", "talk_at", "turns", "touch_from"}),
		rules::DataKeySet("area", "platforms[]", {"id", "from", "to", "step_ms", "pause_ms", "mode", "require_flag", "block_flag"}),
		rules::DataKeySet("area", "ports[]", {"id", "at", "marker", "flag", "ship", "block_flag"}),
		rules::DataKeySet("area", "rests[]", {"id", "at", "cost", "keeper", "per_member", "voice", "name"}),
		rules::DataKeySet("area", "shops[]", {"id", "at", "kind", "stock", "sell_percent", "require_flag", "block_flag", "keeper", "voice", "name"}),
		rules::DataKeySet("area", "shops[].stock[]", {"id", "require_flag", "block_flag"}),
		rules::DataKeySet("area", "zones[]", {"id", "from", "to", "table", "visible", "random"}),
		rules::DataKeySet("area", "zones[].random", {"min_steps", "max_steps", "step_distance", "grace_steps", "step_chance", "vehicle_step_chance", "safe_near_exits", "grace_after_battle"}),
		rules::DataKeySet("area", "zones[].table[]", {"encounter", "weight"}),
		rules::DataKeySet("area", "zones[].visible", {"spawns", "cap", "detection_radius", "leash_radius", "chase_speed_percent", "chase_ms", "wander_speed_percent", "wander_percent"}),
		rules::DataKeySet("game.json", "", {"title", "core_data", "areas", "events", "glossary", "languages", "journal", "actors", "presentation", "flags", "vars", "clear_flag", "start", "join_levels", "prices", "item_cap", "money_cap", "tick_ms", "grace_ms", "round_ms", "autosave_slots", "manual_slots", "bosses", "vehicles", "machine_step_heal", "outside_experience_until", "leave_equipment", "random_encounters_everywhere", "nameless_actors", "grace_after_battle", "interact", "interact_across", "run_speed_percent", "vehicle_opening", "background_limit", "credits", "arrive_notice", "step_timing", "party_talk_ambient", "cell_pixels", "machine_step_heal_fallen", "exit_fires", "followers_arrive", "autosave", "bag_slots", "bag_stacking", "followers_arrive_other", "talk_at"}),
		rules::DataKeySet("game.json", "actors[]", {"id", "sprite", "area", "at", "facing", "solid", "wander", "patrol", "path"}),
		rules::DataKeySet("game.json", "actors[].path", {"offsets", "ticks", "footprint"}),
		rules::DataKeySet("game.json", "actors[].patrol", {"steps", "speed_percent", "terrain"}),
		rules::DataKeySet("game.json", "actors[].patrol.steps[]", {"walk", "count", "to", "face", "pause_ms"}),
		rules::DataKeySet("game.json", "actors[].sprite", {"enemy", "look"}),
		rules::DataKeySet("game.json", "actors[].wander", {"radius", "from", "to", "speed_percent", "pause_ms", "terrain", "stand_percent", "axis"}),
		rules::DataKeySet("game.json", "bosses[]", {"encounter", "flag"}),
		rules::DataKeySet("game.json", "credits.<id>", {"music", "entries"}),
		rules::DataKeySet("game.json", "credits.<id>.entries[]", {"heading", "line", "gap", "pause_ms"}),
		rules::DataKeySet("game.json", "flags[]", {"id", "reset"}),
		rules::DataKeySet("game.json", "languages[]", {"code", "strings", "text"}),
		rules::DataKeySet("game.json", "presentation", {"audio", "cameras", "layouts", "animations", "portraits", "pictures", "music", "durations", "flash_default", "flash_original"}),
		rules::DataKeySet("game.json", "presentation.animations[]", {"id", "seconds"}),
		rules::DataKeySet("game.json", "presentation.cameras[]", {"id", "area", "marker", "follow", "zoom"}),
		rules::DataKeySet("game.json", "presentation.durations[]", {"action", "subject", "seconds"}),
		rules::DataKeySet("game.json", "presentation.music", {"title", "victory", "defeat", "encounters", "vehicles", "areas", "fade_ms"}),
		rules::DataKeySet("game.json", "presentation.music.areas entry", {"flag", "track"}),
		rules::DataKeySet("game.json", "start", {"area", "marker", "facing", "lineup", "level", "money", "items"}),
		rules::DataKeySet("game.json", "vehicles[]", {"id", "crosses", "breaks", "breaks_for_good", "speed_percent", "encounters", "call_item", "opening"}),
		rules::DataKeySet("game.json", "vehicles[].encounters[]", {"encounter", "weight"}),
		rules::DataKeySet("journal.json", "", {"objectives", "jobs", "encyclopedia", "party_talk"}),
		rules::DataKeySet("journal.json", "encyclopedia[]", {"id", "title", "text", "flag"}),
		rules::DataKeySet("journal.json", "jobs[]", {"id", "title", "available", "accepted", "completed", "missed"}),
		rules::DataKeySet("journal.json", "objectives[]", {"id", "text", "after", "until"}),
		rules::DataKeySet("journal.json", "party_talk[]", {"id", "area", "after", "until", "lines"}),
		rules::DataKeySet("journal.json", "party_talk[].lines[]", {"speaker", "text"}),

	};
	return sets;
}

const rules::DataKeySet& gameKeys(const std::string& file, const std::string& object) {
	for (const rules::DataKeySet& set : gameDataKeys()) {
		if (set.file == file && set.object == object) {
			return set;
		}
	}
	std::fprintf(stderr, "no data key set %s %s\n", file.c_str(), object.c_str());
	std::abort();
}

const std::vector<rules::DataEnum>& gameDataEnums() {
	static const std::vector<rules::DataEnum> enums = [] {
		std::vector<rules::DataEnum> list = {
			{"area_kind", {"town", "field", "dungeon", "vessel"}},
			{"bag_stacking", {"stacks", "single"}},
			{"arrive_notice", {"always", "walked", "never"}},
			{"encounter_mode", {"visible", "random"}},
			{"exit_fires", {"from_floor", "always"}},
			{"flag_reset", {"area"}},
			{"followers_arrive", {"stacked", "behind", "up", "down", "left", "right"}},
			{"hook_solid", {"always", "while_active"}},
			{"hook_trigger", {"interact", "step", "load", "flag", "touch"}},
			{"interact", {"faced", "nearby"}},
			{"leave_equipment", {"keep", "bag"}},
			{"machine_step_heal_fallen", {"off", "heal", "stand"}},
			{"platform_mode", {"loop", "one_way", "triggered"}},
			{"shop_kind", {"general", "weapon", "armor", "tool", "gift"}},
			{"step_timing", {"ticks", "exact"}},
			{"talk_at", {"point", "actor"}},
			{"touch_from", {"any", "keys"}},
			{"vehicle_opening", {"roll", "normal"}},
			{"wander_axis", {"both", "x", "y"}},
		};
		rules::DataEnum facing{"facing", {}};
		for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
			facing.values.push_back(toString(f));
		}
		rules::DataEnum flash{"flash_level", {}};
		for (FlashLevel f : {FlashLevel::Reduced, FlashLevel::Standard, FlashLevel::Original}) {
			flash.values.push_back(toString(f));
		}
		list.push_back(facing);
		list.push_back(flash);
		return list;
	}();
	return enums;
}

int gameEnumIndex(const std::string& name, const std::string& word) {
	for (const rules::DataEnum& e : gameDataEnums()) {
		if (e.name != name) {
			continue;
		}
		for (std::size_t i = 0; i < e.values.size(); ++i) {
			if (e.values[i] == word) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
	std::fprintf(stderr, "no data enum %s\n", name.c_str());
	std::abort();
}

namespace {

// Keys that take one of the named word sets, for the report. Each names a set and a key of the tables above.
const std::vector<std::pair<std::pair<std::string, std::string>, std::pair<std::string, std::string>>>& enumKeys() {
	static const std::vector<std::pair<std::pair<std::string, std::string>, std::pair<std::string, std::string>>> keys = {
		{{"abilities.json", "abilities[]"}, {"kind", "ability_kind"}},
		{{"abilities.json", "abilities[]"}, {"target", "targeting"}},
		{{"area", ""}, {"encounter_mode", "encounter_mode"}},
		{{"area", ""}, {"kind", "area_kind"}},
		{{"area", "dungeon_exit"}, {"facing", "facing"}},
		{{"area", "exits[]"}, {"fires", "exit_fires"}},
		{{"area", "exits[]"}, {"followers", "followers_arrive"}},
		{{"area", "hooks[]"}, {"solid", "hook_solid"}},
		{{"area", "platforms[]"}, {"mode", "platform_mode"}},
		{{"area", "hooks[]"}, {"trigger", "hook_trigger"}},
		{{"area", "hooks[]"}, {"talk_at", "talk_at"}},
		{{"area", "hooks[]"}, {"touch_from", "touch_from"}},
		{{"area", "shops[]"}, {"kind", "shop_kind"}},
		{{"equipment.json", "equipment[]"}, {"slot", "equip_slot"}},
		{{"game.json", ""}, {"arrive_notice", "arrive_notice"}},
		{{"game.json", ""}, {"bag_stacking", "bag_stacking"}},
		{{"game.json", ""}, {"exit_fires", "exit_fires"}},
		{{"game.json", ""}, {"followers_arrive", "followers_arrive"}},
		{{"game.json", ""}, {"followers_arrive_other", "followers_arrive"}},
		{{"game.json", "flags[]"}, {"reset", "flag_reset"}},
		{{"game.json", ""}, {"interact", "interact"}},
		{{"game.json", ""}, {"leave_equipment", "leave_equipment"}},
		{{"game.json", ""}, {"machine_step_heal_fallen", "machine_step_heal_fallen"}},
		{{"game.json", ""}, {"step_timing", "step_timing"}},
		{{"game.json", ""}, {"talk_at", "talk_at"}},
		{{"game.json", ""}, {"vehicle_opening", "vehicle_opening"}},
		{{"game.json", "actors[]"}, {"facing", "facing"}},
		{{"game.json", "actors[].patrol.steps[]"}, {"face", "facing"}},
		{{"game.json", "actors[].patrol.steps[]"}, {"walk", "facing"}},
		{{"game.json", "actors[].wander"}, {"axis", "wander_axis"}},
		{{"game.json", "presentation"}, {"flash_default", "flash_level"}},
		{{"game.json", "start"}, {"facing", "facing"}},
		{{"game.json", "vehicles[]"}, {"opening", "vehicle_opening"}},
		{{"party.json", "macros[]"}, {"on_trouble", "macro_on_trouble"}},
		{{"party.json", "macros[]"}, {"policy", "macro_policy"}},
		{{"party.json", "macros[]"}, {"recover_who", "macro_recover_who"}},
		{{"party.json", "macros[]"}, {"unnamed", "macro_unnamed"}},
		{{"party.json", "macros[].steps[]"}, {"command", "macro_command"}},
		{{"rules.json", ""}, {"auto_macro_on_trouble", "macro_on_trouble"}},
		{{"rules.json", ""}, {"macro_recover_who", "macro_recover_who"}},
		{{"rules.json", ""}, {"macro_unnamed", "macro_unnamed"}},
		{{"rules.json", ""}, {"named_macro_on_trouble", "macro_on_trouble"}},
		{{"statuses.json", "statuses[]"}, {"action", "status_action"}},
		{{"statuses.json", "statuses[]"}, {"forced_target", "forced_target"}},
	};
	return keys;
}

class Json {
public:
	std::string str;

	void text(const std::string& value) {
		str += '"';
		for (char c : value) {
			if (c == '"' || c == '\\') {
				str += '\\';
			}
			str += c;
		}
		str += '"';
	}
	void key(const std::string& name) {
		comma();
		text(name);
		str += ": ";
		first_ = true;
	}
	void open(char bracket) {
		comma();
		str += bracket;
		first_ = true;
	}
	void close(char bracket) {
		str += bracket;
		first_ = false;
	}
	void value(const std::string& v) {
		comma();
		text(v);
		first_ = false;
	}
	void number(double v) {
		comma();
		char buffer[32];
		std::snprintf(buffer, sizeof buffer, "%g", v);
		str += buffer;
		first_ = false;
	}
	void boolean(bool v) {
		comma();
		str += v ? "true" : "false";
		first_ = false;
	}
	void strings(const std::vector<std::string>& values) {
		open('[');
		for (const std::string& v : values) {
			value(v);
		}
		close(']');
	}

private:
	void comma() {
		if (!first_) {
			str += ", ";
		}
		first_ = false;
	}
	// After a key or an opening bracket: no comma before what comes next.
	bool first_ = true;
};

void dataSet(Json& j, const rules::DataKeySet& set) {
	j.open('{');
	j.key("file");
	j.value(set.file);
	j.key("object");
	j.value(set.object);
	j.key("keys");
	j.strings(set.keys);
	j.key("enums");
	j.open('{');
	for (const auto& [where, keyEnum] : enumKeys()) {
		if (where.first == set.file && where.second == set.object) {
			j.key(keyEnum.first);
			j.value(keyEnum.second);
		}
	}
	j.close('}');
	j.close('}');
}

}

std::string capabilitiesJson() {
	Json j;
	j.open('{');
	j.key("capabilities_schema");
	j.number(kCapabilitiesSchema);
	j.key("versions");
	j.open('{');
	j.key("library");
	j.value(kLibraryVersion);
	j.key("abi");
	j.number(kAbiVersion);
	j.key("game_build");
	j.value(kGameBuild);
	j.key("rules");
	j.number(rules::kRulesVersion);
	j.key("event_schema");
	j.number(rules::kEventSchemaVersion);
	j.key("record_format");
	j.number(rules::kRecordFormatVersion);
	j.key("save_schema");
	j.number(kSaveSchemaVersion);
	j.key("events_save");
	j.number(events::kSaveTextVersion);
	j.close('}');

	j.key("data");
	j.open('[');
	std::vector<rules::DataKeySet> sets = rules::coreDataKeys();
	sets.insert(sets.end(), gameDataKeys().begin(), gameDataKeys().end());
	for (const rules::DataKeySet& set : sets) {
		dataSet(j, set);
	}
	j.close(']');

	j.key("enums");
	j.open('{');
	std::map<std::string, std::vector<std::string>> enums;
	for (const rules::DataEnum& e : rules::coreDataEnums()) {
		enums[e.name] = e.values;
	}
	for (const rules::DataEnum& e : gameDataEnums()) {
		enums[e.name] = e.values;
	}
	for (const auto& [name, values] : enums) {
		j.key(name);
		j.strings(values);
	}
	j.close('}');

	j.key("script");
	j.open('{');
	j.key("commands");
	j.open('[');
	for (const events::CommandInfo& c : events::script_commands()) {
		j.open('{');
		j.key("name");
		j.value(c.name);
		j.key("forms");
		j.strings(c.forms);
		j.key("background");
		j.boolean(c.background);
		j.key("parallel");
		j.boolean(c.parallel);
		j.close('}');
	}
	j.close(']');
	j.key("conditions");
	j.open('[');
	for (const events::ConditionInfo& c : events::condition_tests()) {
		j.value(c.form);
	}
	j.close(']');
	j.key("standard_animations");
	j.open('[');
	for (const events::StandardAnimation& a : events::standard_animations()) {
		j.open('{');
		j.key("name");
		j.value(a.name);
		j.key("seconds");
		j.number(a.seconds);
		j.key("leaves");
		j.value(a.end == events::AnimationEnd::shown ? "shown" : a.end == events::AnimationEnd::hidden ? "hidden" : "as_it_was");
		j.close('}');
	}
	j.close(']');
	j.key("action_types");
	j.open('[');
	for (int i = 0; i <= static_cast<int>(events::ActionType::camera_reset); ++i) {
		j.value(events::action_type_name(static_cast<events::ActionType>(i)));
	}
	j.close(']');
	j.close('}');

	j.key("text_keys");
	j.open('[');
	std::vector<std::pair<std::string, std::string>> families = {
		{"area.<area>", "an area's display name, every language"},
		{"speaker.<actor>", "an actor's name in dialogue (not for nameless_actors), dialogue tables"},
		{"choice.yes", "the yes answer of every yes/no question, dialogue tables"},
		{"choice.no", "the no answer of every yes/no question, dialogue tables"},
		{"credits.*", "headings and lines game.json's credits name"},
		{"<voice>.welcome|confirm|thanks|cannot_afford", "a shop's or rest point's keeper lines; at least one per voice"},
		{"<name>", "a shop's or rest point's display name (its `name`), every language"},
		{"game.*", "the game's notices"},
		{"game.refuse.*", "the game's refusals"},
		{"ui.*", "a host's own words; ui.shop.* and ui.inn.* are the keepers' default lines"},
		{"battle.*", "core's battle messages (core's text_en.json)"},
		{"cancel.*", "core's cancelled-action messages (core's text_en.json)"},
		{"name.element.<element>", "each declared element's name, every language"},
	};
	for (const char* kind : {"character", "ability", "item", "equipment", "status", "enemy", "vehicle", "difficulty"}) {
		families.push_back({std::string("name.") + kind + ".<id>", "optional: a thing's name in that language"});
	}
	for (DescriptionKind kind : {DescriptionKind::Item, DescriptionKind::Equipment, DescriptionKind::Ability, DescriptionKind::Status, DescriptionKind::Enemy,
			 DescriptionKind::Difficulty}) {
		families.push_back({std::string("desc.") + toString(kind) + ".<id>", "optional: a description"});
	}
	for (const auto& [family, meaning] : families) {
		j.open('{');
		j.key("family");
		j.value(family);
		j.key("meaning");
		j.value(meaning);
		j.close('}');
	}
	j.close(']');
	j.close('}');
	return j.str + "\n";
}

}
