#include "game/data.h"

#include "game/save.h"

#include "events/compiler.h"
#include "events/glossary.h"
#include "rules/formulas.h"
#include "rules/loader.h"

#include "json_read.h"

#include <algorithm>
#include <set>

namespace game {

using rules::JsonValue;

const LanguageTables& GameData::language(const std::string& code) const {
	for (const LanguageTables& l : languages) {
		if (l.code == code) {
			return l;
		}
	}
	return languages.front();
}

const Area* GameData::area(const std::string& id) const {
	auto it = areas.find(id);
	return it == areas.end() ? nullptr : &it->second;
}

const Boss* GameData::boss(const std::string& encounter) const {
	for (const Boss& b : config.bosses) {
		if (b.encounter == encounter) {
			return &b;
		}
	}
	return nullptr;
}

int GameData::price(const std::string& id) const {
	auto it = config.prices.find(id);
	return it == config.prices.end() ? -1 : it->second;
}

const Shop* GameData::shop(const std::string& id) const {
	for (const auto& [areaId, a] : areas) {
		if (const Shop* s = a.shop(id)) {
			return s;
		}
	}
	return nullptr;
}

const RestPoint* GameData::rest(const std::string& id) const {
	for (const auto& [areaId, a] : areas) {
		if (const RestPoint* r = a.rest(id)) {
			return r;
		}
	}
	return nullptr;
}

const Port* GameData::port(const std::string& id, const Area** area) const {
	for (const auto& [areaId, a] : areas) {
		if (const Port* p = a.port(id)) {
			if (area) {
				*area = &a;
			}
			return p;
		}
	}
	return nullptr;
}

const FieldVehicle* GameData::vehicle(const std::string& id) const {
	for (const FieldVehicle& v : config.vehicles) {
		if (v.id == id) {
			return &v;
		}
	}
	return nullptr;
}

const char* toString(CameraTarget target) {
	switch (target) {
	case CameraTarget::None: return "none";
	case CameraTarget::Marker: return "marker";
	case CameraTarget::Follow: return "follow";
	case CameraTarget::Leader: return "leader";
	}
	return "";
}

const CameraDef* PresentationNames::camera(const std::string& id) const {
	for (const CameraDef& c : cameras) {
		if (c.id == id) {
			return &c;
		}
	}
	return nullptr;
}

const char* toString(DescriptionKind kind) {
	switch (kind) {
	case DescriptionKind::Item: return "item";
	case DescriptionKind::Equipment: return "equipment";
	case DescriptionKind::Ability: return "ability";
	case DescriptionKind::Status: return "status";
	case DescriptionKind::Enemy: return "enemy";
	case DescriptionKind::Difficulty: return "difficulty";
	}
	return "";
}

std::string descriptionKey(DescriptionKind kind, const std::string& id) {
	return std::string(kDescriptionPrefix) + toString(kind) + "." + id;
}

bool describes(const rules::Database& db, DescriptionKind kind, const std::string& id) {
	switch (kind) {
	case DescriptionKind::Item: return db.item(id) != nullptr;
	case DescriptionKind::Equipment: return db.equipmentPiece(id) != nullptr;
	case DescriptionKind::Ability: return db.ability(id) != nullptr;
	case DescriptionKind::Status: return db.status(id) != nullptr;
	case DescriptionKind::Enemy: return db.enemy(id) != nullptr;
	case DescriptionKind::Difficulty: return db.difficulty(id) != nullptr;
	}
	return false;
}

bool namesKnownThing(const rules::Database& db, const std::string& key) {
	const std::size_t dot = key.find('.', std::char_traits<char>::length(kNamePrefix));
	if (key.compare(0, std::char_traits<char>::length(kNamePrefix), kNamePrefix) != 0 || dot == std::string::npos) {
		return false;
	}
	const std::string kind = key.substr(std::char_traits<char>::length(kNamePrefix), dot - std::char_traits<char>::length(kNamePrefix));
	const std::string id = key.substr(dot + 1);
	return (kind == "ability" && db.ability(id)) || (kind == "character" && db.character(id)) || (kind == "difficulty" && db.difficulty(id)) ||
		(kind == "enemy" && db.enemy(id)) || (kind == "equipment" && db.equipmentPiece(id)) || (kind == "item" && db.item(id)) ||
		(kind == "status" && db.status(id)) || (kind == "vehicle" && db.vehicle(id)) || (kind == "element" && db.hasElement(id));
}

bool GameConfig::nameless(const std::string& actor) const {
	return std::find(namelessActors.begin(), namelessActors.end(), actor) != namelessActors.end();
}

namespace {

// Descriptions and names are optional in every language.
bool isDescription(const std::string& key) {
	return key.compare(0, std::char_traits<char>::length(kDescriptionPrefix), kDescriptionPrefix) == 0 ||
		key.compare(0, std::char_traits<char>::length(kNamePrefix), kNamePrefix) == 0;
}

std::uint64_t fnv(std::uint64_t hash, const std::string& text) {
	for (unsigned char c : text) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	return hash;
}

// The content hash covers what saves and records depend on, not presentation: it reads the parsed JSON and leaves out
// the members named in `skipped` at the top level, so camera, music and timing data can change without changing it.
std::uint64_t hashJson(std::uint64_t hash, const JsonValue& value, const std::set<std::string>& skipped = {}) {
	hash = fnv(hash, std::string(1, static_cast<char>('0' + static_cast<int>(value.type))));
	switch (value.type) {
	case JsonValue::Type::Null: break;
	case JsonValue::Type::Bool: hash = fnv(hash, value.boolean ? "t" : "f"); break;
	case JsonValue::Type::Number: hash = fnv(hash, std::to_string(value.number)); break;
	case JsonValue::Type::String: hash = fnv(hash, value.string); break;
	case JsonValue::Type::Array:
		for (const JsonValue& item : value.items) {
			hash = hashJson(hash, item);
		}
		break;
	case JsonValue::Type::Object:
		for (std::size_t i = 0; i < value.keys.size(); ++i) {
			if (!skipped.count(value.keys[i])) {
				hash = hashJson(fnv(hash, value.keys[i]), value.values[i]);
			}
		}
		break;
	}
	return hash;
}

std::string join(const std::string& directory, const std::string& file) {
	return directory.empty() ? file : directory + "/" + file;
}

// Every load problem with its English line. The lines are kept as each library rendered them, so a script
// diagnostic keeps its "error:" or "warning:" and the command it is about.
class Problems {
public:
	// `where` is a file or the part of the data the problem is in; empty when the text says it all.
	void add(const std::string& where, const char* key, std::vector<std::string> params) { add({where, 0, "", {key, std::move(params)}}); }

	void add(const rules::Problem& problem) {
		list_.push_back(problem);
		lines_.push_back(rules::formatProblem(problem, problemText()));
	}

	void add(const std::vector<rules::Problem>& problems, const std::string& file = "") {
		for (rules::Problem problem : problems) {
			if (!file.empty()) {
				problem.file = file;
			}
			add(problem);
		}
	}

	void add(const std::vector<events::Diagnostic>& diagnostics) {
		for (const events::Diagnostic& d : diagnostics) {
			list_.push_back({d.file, d.line, d.severity == events::Severity::error ? "error" : "warning", {d.key, d.params}});
			lines_.push_back(events::format_diagnostic(d));
		}
	}

	bool empty() const { return list_.empty(); }
	const std::vector<rules::Problem>& list() const { return list_; }

	rules::Result<GameData> failure() const {
		std::string text;
		for (const std::string& line : lines_) {
			text += (text.empty() ? "" : "\n") + line;
		}
		return rules::Result<GameData>::failure(list_, text);
	}

private:
	std::vector<rules::Problem> list_;
	std::vector<std::string> lines_;
};

// A camera is a name, or an object with an id, an optional target (a marker in an area, or `follow` an actor, a
// character or "leader") and an optional zoom.
void readCameras(JsonReader& reader, const JsonValue& presentation, std::vector<CameraDef>& out) {
	const JsonValue* list = reader.field(presentation, "cameras", JsonValue::Type::Array, false);
	if (!list) {
		return;
	}
	for (const JsonValue& entry : list->items) {
		CameraDef camera;
		if (entry.is(JsonValue::Type::String) && !entry.string.empty()) {
			camera.id = entry.string;
		} else if (entry.is(JsonValue::Type::Object)) {
			reader.only(entry, {"id", "area", "marker", "follow", "zoom"});
			reader.text(entry, "id", camera.id, true);
			reader.text(entry, "area", camera.area, false);
			reader.text(entry, "marker", camera.marker, false);
			reader.text(entry, "follow", camera.follow, false);
			reader.number(entry, "zoom", camera.zoom, false, 0.25, 4);
			if (!camera.follow.empty() && (!camera.marker.empty() || !camera.area.empty())) {
				reader.fail(entry, "data.camera_two_targets", {camera.id});
			} else if (camera.marker.empty() != camera.area.empty()) {
				reader.fail(entry, "data.camera_marker_needs_area", {camera.id});
			}
			camera.target = !camera.marker.empty() ? CameraTarget::Marker : camera.follow == "leader" ? CameraTarget::Leader
				: !camera.follow.empty() ? CameraTarget::Follow : CameraTarget::None;
			if (camera.target == CameraTarget::Leader) {
				camera.follow.clear();
			}
		} else {
			reader.fail(entry, "data.camera_shape", {});
			continue;
		}
		for (const CameraDef& other : out) {
			if (other.id == camera.id) {
				reader.fail(entry, "data.camera_listed_twice", {camera.id});
			}
		}
		out.push_back(camera);
	}
}

void readConfig(JsonReader& reader, const JsonValue& json, GameConfig& c) {
	reader.only(json, {"title", "core_data", "areas", "events", "glossary", "languages", "journal", "actors", "presentation", "flags",
		"vars", "clear_flag", "start", "join_levels", "prices", "item_cap", "money_cap", "tick_ms", "grace_ms", "round_ms",
		"autosave_slots", "manual_slots", "bosses", "vehicles", "machine_step_heal", "outside_experience_until", "leave_equipment", "random_encounters_everywhere",
		"nameless_actors"});
	reader.text(json, "title", c.title, true);
	reader.text(json, "core_data", c.coreData, true);
	reader.texts(json, "areas", c.areaFiles, true);
	reader.texts(json, "events", c.eventFiles, true);
	reader.text(json, "glossary", c.glossaryFile, true);
	if (const JsonValue* list = reader.field(json, "languages", JsonValue::Type::Array, true)) {
		for (const JsonValue& l : list->items) {
			reader.only(l, {"code", "strings", "text"});
			LanguageFiles files;
			reader.text(l, "code", files.code, true);
			reader.text(l, "strings", files.strings, true);
			reader.text(l, "text", files.text, true);
			for (const LanguageFiles& other : c.languages) {
				if (other.code == files.code) {
					reader.fail(l, "data.language_listed_twice", {files.code});
				}
			}
			c.languages.push_back(files);
		}
		if (list->items.empty()) {
			reader.fail(*list, "data.list_at_least_one_language", {});
		}
	}
	reader.text(json, "journal", c.journalFile, true);
	reader.texts(json, "actors", c.actors, false);
	reader.texts(json, "nameless_actors", c.namelessActors, false);
	if (const JsonValue* p = reader.field(json, "presentation", JsonValue::Type::Object, false)) {
		reader.only(*p, {"audio", "cameras", "layouts", "animations", "portraits", "music", "durations"});
		reader.texts(*p, "audio", c.presentation.audio, false);
		readCameras(reader, *p, c.presentation.cameras);
		reader.texts(*p, "layouts", c.presentation.layouts, false);
		reader.texts(*p, "animations", c.presentation.animations, false);
		reader.texts(*p, "portraits", c.presentation.portraits, false);
		if (const JsonValue* m = reader.field(*p, "music", JsonValue::Type::Object, false)) {
			reader.only(*m, {"title", "victory", "defeat", "encounters"});
			reader.text(*m, "title", c.presentation.music.title, false);
			reader.text(*m, "victory", c.presentation.music.victory, false);
			reader.text(*m, "defeat", c.presentation.music.defeat, false);
			if (const JsonValue* e = reader.field(*m, "encounters", JsonValue::Type::Object, false)) {
				for (std::size_t i = 0; i < e->keys.size(); ++i) {
					if (!e->values[i].is(JsonValue::Type::String) || e->values[i].string.empty()) {
						reader.fail(e->values[i], "data.music_not_text", {e->keys[i]});
						continue;
					}
					c.presentation.music.encounters[e->keys[i]] = e->values[i].string;
				}
			}
		}
		if (const JsonValue* list = reader.field(*p, "durations", JsonValue::Type::Array, false)) {
			for (const JsonValue& d : list->items) {
				reader.only(d, {"action", "subject", "seconds"});
				std::string action;
				std::string subject;
				double seconds = 0;
				reader.text(d, "action", action, true);
				reader.text(d, "subject", subject, false);
				reader.number(d, "seconds", seconds, true, 0, 60);
				events::ActionType type = events::ActionType::move;
				if (!action.empty() && !events::parse_action_type(action, type)) {
					reader.fail(d, "data.unknown_action", {action});
				} else if (subject.empty()) {
					c.presentation.durations[type] = seconds;
				} else {
					c.presentation.subjectDurations[{type, subject}] = seconds;
				}
			}
		}
	}
	reader.texts(json, "flags", c.flags, false);
	reader.counts(json, "vars", c.vars, false, -999999, 999999);
	reader.text(json, "clear_flag", c.clearFlag, false);
	if (const JsonValue* start = reader.field(json, "start", JsonValue::Type::Object, true)) {
		reader.only(*start, {"area", "marker", "facing", "lineup", "level", "money", "items"});
		reader.text(*start, "area", c.startArea, true);
		reader.text(*start, "marker", c.startMarker, true);
		std::string facing = "south";
		reader.text(*start, "facing", facing, false);
		if (!parseFacing(facing, c.startFacing)) {
			reader.fail(*start, "data.bad_facing", {});
		}
		reader.texts(*start, "lineup", c.startLineup, true);
		reader.integer(*start, "level", c.startLevel, false, 1, rules::formulas::kMaxLevel);
		reader.integer(*start, "money", c.startMoney, false, 0, 999999);
		reader.counts(*start, "items", c.startItems, false, 1, 99);
		if (c.startLineup.empty() || c.startLineup.size() > 5) {
			reader.fail(*start, "data.start_lineup_size", {});
		}
	}
	reader.counts(json, "join_levels", c.joinLevels, false, 1, rules::formulas::kMaxLevel);
	reader.counts(json, "prices", c.prices, false, 1, 999999);
	reader.integer(json, "item_cap", c.itemCap, false, 1, 9999);
	reader.integer(json, "money_cap", c.moneyCap, false, 1, 99999999);
	reader.integer(json, "tick_ms", c.tickMs, false, 10, 1000);
	reader.integer(json, "grace_ms", c.graceMs, false, 0, 60000);
	reader.integer(json, "round_ms", c.roundMs, false, 0, 60000);
	reader.integer(json, "autosave_slots", c.autosaveSlots, false, 1, 9);
	reader.integer(json, "manual_slots", c.manualSlots, false, 1, 99);
	reader.integer(json, "machine_step_heal", c.machineStepHeal, false, 0, 999);
	reader.text(json, "outside_experience_until", c.outsideExperienceUntil, false);
	std::string leaving = "keep";
	reader.text(json, "leave_equipment", leaving, false);
	if (leaving == "bag") {
		c.leaveEquipment = LeaveEquipment::Bag;
	} else if (leaving != "keep") {
		reader.fail(json, "data.bad_leave_equipment", {leaving});
	}
	reader.boolean(json, "random_encounters_everywhere", c.randomEncountersEverywhere, false);
	if (const JsonValue* bosses = reader.field(json, "bosses", JsonValue::Type::Array, false)) {
		for (const JsonValue& b : bosses->items) {
			reader.only(b, {"encounter", "flag"});
			Boss boss;
			reader.text(b, "encounter", boss.encounter, true);
			reader.text(b, "flag", boss.flag, true);
			c.bosses.push_back(boss);
		}
	}
	if (const JsonValue* vehicles = reader.field(json, "vehicles", JsonValue::Type::Array, false)) {
		for (const JsonValue& v : vehicles->items) {
			reader.only(v, {"id", "crosses", "breaks", "breaks_for_good", "speed_percent", "encounters", "call_item"});
			FieldVehicle vehicle;
			reader.text(v, "id", vehicle.id, true);
			const auto terrains = [&](const char* key, std::string& out, bool required) {
				std::vector<std::string> names;
				reader.texts(v, key, names, required);
				for (const std::string& t : names) {
					if (t == "floor") {
						out += '.';
					} else if (t == "water") {
						out += '~';
					} else if (t == "soft") {
						out += ':';
					} else if (t == "barrier") {
						out += '%';
					} else {
						reader.fail(v, "data.unknown_terrain", {t});
					}
				}
				return names;
			};
			const std::vector<std::string> terrain = terrains("crosses", vehicle.crosses, true);
			terrains("breaks", vehicle.breaks, false);
			reader.boolean(v, "breaks_for_good", vehicle.breaksForGood, false);
			reader.text(v, "call_item", vehicle.callItem, false);
			if (terrain.empty()) {
				reader.fail(v, "data.vehicle_needs_terrain", {vehicle.id});
			}
			reader.integer(v, "speed_percent", vehicle.speedPercent, false, 10, 1000);
			if (const JsonValue* table = reader.field(v, "encounters", JsonValue::Type::Array, true)) {
				for (const JsonValue& t : table->items) {
					if (!t.is(JsonValue::Type::Object)) {
						reader.fail(t, "data.table_entry_object", {});
						continue;
					}
					reader.only(t, {"encounter", "weight"});
					TableEntry entry;
					reader.text(t, "encounter", entry.encounter, true);
					reader.integer(t, "weight", entry.weight, false, 1, 1000);
					vehicle.encounters.push_back(entry);
				}
			}
			for (const FieldVehicle& other : c.vehicles) {
				if (other.id == vehicle.id) {
					reader.fail(v, "data.vehicle_listed_twice", {vehicle.id});
				}
			}
			c.vehicles.push_back(vehicle);
		}
	}
}

void readRange(JsonReader& reader, const JsonValue& object, FlagRange& range) {
	reader.text(object, "after", range.after, false);
	reader.text(object, "until", range.until, false);
}

void readJournal(JsonReader& reader, const JsonValue& json, Journal& j) {
	reader.only(json, {"objectives", "jobs", "encyclopedia", "party_talk"});
	if (const JsonValue* list = reader.field(json, "objectives", JsonValue::Type::Array, false)) {
		for (const JsonValue& o : list->items) {
			reader.only(o, {"id", "text", "after", "until"});
			Objective objective;
			reader.text(o, "id", objective.id, true);
			reader.text(o, "text", objective.text, true);
			readRange(reader, o, objective.range);
			j.objectives.push_back(objective);
		}
	}
	if (const JsonValue* list = reader.field(json, "jobs", JsonValue::Type::Array, false)) {
		for (const JsonValue& o : list->items) {
			reader.only(o, {"id", "title", "available", "accepted", "completed", "missed"});
			Job job;
			reader.text(o, "id", job.id, true);
			reader.text(o, "title", job.title, true);
			reader.text(o, "available", job.availableFlag, false);
			reader.text(o, "accepted", job.acceptedFlag, true);
			reader.text(o, "completed", job.completedFlag, true);
			reader.text(o, "missed", job.missedFlag, false);
			j.jobs.push_back(job);
		}
	}
	if (const JsonValue* list = reader.field(json, "encyclopedia", JsonValue::Type::Array, false)) {
		for (const JsonValue& o : list->items) {
			reader.only(o, {"id", "title", "text", "flag"});
			EncyclopediaEntry entry;
			reader.text(o, "id", entry.id, true);
			reader.text(o, "title", entry.title, true);
			reader.text(o, "text", entry.text, true);
			reader.text(o, "flag", entry.flag, true);
			j.encyclopedia.push_back(entry);
		}
	}
	if (const JsonValue* list = reader.field(json, "party_talk", JsonValue::Type::Array, false)) {
		for (const JsonValue& o : list->items) {
			reader.only(o, {"id", "area", "after", "until", "lines"});
			TalkSet set;
			reader.text(o, "id", set.id, true);
			reader.text(o, "area", set.area, false);
			readRange(reader, o, set.range);
			if (const JsonValue* lines = reader.field(o, "lines", JsonValue::Type::Array, true)) {
				for (const JsonValue& l : lines->items) {
					reader.only(l, {"speaker", "text"});
					TalkLine line;
					reader.text(l, "speaker", line.speaker, true);
					reader.text(l, "text", line.text, true);
					set.lines.push_back(line);
				}
				if (lines->items.empty()) {
					reader.fail(*lines, "data.talk_set_needs_line", {set.id});
				}
			}
			j.partyTalk.push_back(set);
		}
	}
}

bool knownThing(const rules::Database& db, const std::string& id) {
	return db.item(id) || db.equipmentPiece(id);
}

events::Registry buildRegistry(const GameData& data, Problems& problems) {
	events::Registry r;
	r.file = "the game data";
	const GameConfig& c = data.config;
	for (const std::string& a : c.actors) {
		r.add(events::NameKind::actor, a);
	}
	for (const auto& [id, character] : data.db.characters) {
		r.add(events::NameKind::actor, id);
		r.add(events::NameKind::member, id);
	}
	for (const auto& [id, item] : data.db.items) {
		r.add(events::NameKind::item, id);
	}
	for (const auto& [id, piece] : data.db.equipment) {
		r.add(events::NameKind::item, id);
	}
	for (const auto& [id, encounter] : data.db.encounters) {
		r.add(events::NameKind::encounter, id);
	}
	for (const auto& [id, enemy] : data.db.enemies) {
		r.add(events::NameKind::enemy, id);
	}
	for (const auto& [id, vehicle] : data.db.vehicles) {
		r.add(events::NameKind::vehicle, id);
	}
	for (const auto& [id, ability] : data.db.abilities) {
		if (ability.kind == rules::AbilityKind::Technique || ability.kind == rules::AbilityKind::Skill) {
			r.add(events::NameKind::ability, id);
		}
	}
	for (const std::string& n : c.presentation.audio) r.add(events::NameKind::audio, n);
	for (const CameraDef& n : c.presentation.cameras) r.add(events::NameKind::camera, n.id);
	for (const std::string& n : c.presentation.layouts) r.add(events::NameKind::layout, n);
	for (const std::string& n : c.presentation.animations) r.add(events::NameKind::animation, n);
	for (const std::string& n : c.presentation.portraits) r.add(events::NameKind::portrait, n);
	std::set<std::string> flags;
	auto declareFlag = [&](const std::string& name, const std::string& where) {
		if (!flags.insert(name).second) {
			problems.add(where, "data.flag_declared_twice", {name});
			return;
		}
		r.declare_flag(name);
	};
	for (const std::string& f : c.flags) {
		declareFlag(f, "game.json");
	}
	for (const auto& [name, initial] : c.vars) {
		if (flags.count(name)) {
			problems.add("game.json", "data.declared_as_flag_and_variable", {name});
			continue;
		}
		r.declare_var(name, false, initial);
	}
	std::set<std::string> shops;
	std::set<std::string> rests;
	std::set<std::string> ports;
	for (const auto& [areaId, a] : data.areas) {
		for (const Port& p : a.ports) {
			if (!ports.insert(p.id).second) {
				problems.add({a.file, p.line, "", {"data.port_id_taken", {p.id}}});
			}
		}
		r.add(events::NameKind::map, areaId);
		for (const auto& [marker, cell] : a.markers) {
			r.add_marker(areaId, marker);
		}
		for (const Hook& h : a.hooks) {
			r.add_event_map(areaId, h.event);
		}
		for (const Shop& s : a.shops) {
			if (!shops.insert(s.id).second) {
				problems.add("area " + areaId, "data.shop_id_taken", {s.id});
			}
			r.add(events::NameKind::shop, s.id);
		}
		for (const RestPoint& p : a.rests) {
			if (!rests.insert(p.id).second) {
				problems.add("area " + areaId, "data.rest_point_id_taken", {p.id});
			}
			r.add(events::NameKind::rest_point, p.id);
		}
		for (const Chest& ch : a.chests) {
			declareFlag(ch.flag, "area " + areaId + " chest '" + ch.id + "'");
		}
	}
	return r;
}

events::GameStrings buildGameStrings(const GameData& data) {
	events::GameStrings strings;
	strings.file = "the game data";
	strings.ids.emplace(kChoiceYes, 0);
	strings.ids.emplace(kChoiceNo, 0);
	for (const std::string& actor : data.config.actors) {
		if (!data.config.nameless(actor)) {
			strings.ids.emplace("speaker." + actor, 0);
		}
	}
	const Journal& j = data.journal;
	for (const Objective& o : j.objectives) {
		strings.ids.emplace(o.text, 0);
	}
	for (const Job& job : j.jobs) {
		strings.ids.emplace(job.title, 0);
	}
	for (const EncyclopediaEntry& e : j.encyclopedia) {
		strings.ids.emplace(e.title, 0);
		strings.ids.emplace(e.text, 0);
	}
	for (const TalkSet& t : j.partyTalk) {
		for (const TalkLine& line : t.lines) {
			strings.ids.emplace(line.text, 0);
		}
	}
	return strings;
}

void checkReferences(const GameData& data, Problems& problems) {
	const rules::Database& db = data.db;
	const GameConfig& c = data.config;
	auto markerExists = [&](const std::string& area, const std::string& marker) {
		const Area* a = data.area(area);
		return a && a->markers.count(marker);
	};
	auto flagDeclared = [&](const std::string& where, const std::string& flag) {
		const events::Declaration* d = data.registry.find(flag);
		if (!flag.empty() && (!d || !d->is_flag)) {
			problems.add(where, "data.flag_not_declared", {flag});
		}
	};
	const LanguageTables& source = data.languages.front();
	auto textExists = [&](const std::string& where, const std::string& key) {
		if (!source.text.count(key)) {
			problems.add(where, "data.text_not_in_table", {key});
		}
	};
	auto stringExists = [&](const std::string& where, const std::string& id) {
		if (!source.strings.find(id)) {
			problems.add(where, "data.string_not_in_dialogue_table", {id});
		}
	};
	if (!markerExists(c.startArea, c.startMarker)) {
		problems.add("game.json", "data.start_marker_missing", {c.startArea, c.startMarker});
	}
	for (const std::string& id : c.startLineup) {
		if (!db.character(id)) {
			problems.add("game.json", "data.unknown_character_starting_lineup", {id});
		}
	}
	for (const auto& [id, level] : c.joinLevels) {
		if (!db.character(id)) {
			problems.add("game.json", "data.unknown_character_join_levels", {id});
		}
	}
	for (const auto& [id, count] : c.startItems) {
		if (!knownThing(db, id)) {
			problems.add("game.json", "data.unknown_item_starting_items", {id});
		}
	}
	for (const auto& [id, price] : c.prices) {
		if (!knownThing(db, id)) {
			problems.add("game.json", "data.unknown_item_prices", {id});
		}
	}
	for (const Boss& b : c.bosses) {
		if (!db.encounter(b.encounter)) {
			problems.add("game.json", "data.unknown_boss_encounter", {b.encounter});
		}
		flagDeclared("game.json boss", b.flag);
	}
	flagDeclared("game.json clear_flag", c.clearFlag);
	for (const FieldVehicle& v : c.vehicles) {
		if (!db.vehicle(v.id)) {
			problems.add("game.json vehicles", "data.vehicle_unknown", {v.id});
		}
		const rules::ItemDef* call = v.callItem.empty() ? nullptr : db.item(v.callItem);
		if (!v.callItem.empty() && (!call || !call->effects.empty())) {
			problems.add("game.json vehicle '" + v.id + "'", "data.call_item_unknown", {v.id, v.callItem});
		}
		for (const TableEntry& t : v.encounters) {
			if (!db.encounter(t.encounter)) {
				problems.add("game.json vehicle '" + v.id + "'", "data.zone_lists_unknown_encounter", {v.id, t.encounter});
			}
		}
	}
	const PresentationNames& shown = c.presentation;
	for (const CameraDef& camera : shown.cameras) {
		if (camera.target == CameraTarget::Marker && !markerExists(camera.area, camera.marker)) {
			problems.add("game.json camera '" + camera.id + "'", "data.camera_marker_missing", {camera.area, camera.marker});
		}
		if (camera.target == CameraTarget::Follow && !db.character(camera.follow) &&
			std::find(c.actors.begin(), c.actors.end(), camera.follow) == c.actors.end()) {
			problems.add("game.json camera '" + camera.id + "'", "data.camera_follows_unknown", {camera.follow});
		}
	}
	auto musicListed = [&](const std::string& where, const std::string& track) {
		if (!track.empty() && std::find(shown.audio.begin(), shown.audio.end(), track) == shown.audio.end()) {
			problems.add(where, "data.music_not_audio", {track});
		}
	};
	musicListed("game.json music title", shown.music.title);
	musicListed("game.json music victory", shown.music.victory);
	musicListed("game.json music defeat", shown.music.defeat);
	for (const auto& [encounter, track] : shown.music.encounters) {
		if (!db.encounter(encounter)) {
			problems.add("game.json music", "data.music_unknown_encounter", {encounter});
		}
		musicListed("game.json music encounter '" + encounter + "'", track);
	}
	for (const std::string& actor : c.actors) {
		if (db.character(actor)) {
			problems.add("game.json", "data.actor_is_character", {actor});
		}
		if (!c.nameless(actor)) {
			stringExists("game.json actor", "speaker." + actor);
		}
	}
	for (const std::string& actor : c.namelessActors) {
		if (std::find(c.actors.begin(), c.actors.end(), actor) == c.actors.end()) {
			problems.add("game.json", "data.nameless_not_actor", {actor});
		}
		for (const LanguageTables& language : data.languages) {
			if (language.strings.find("speaker." + actor)) {
				problems.add("game.json", "data.nameless_has_name", {actor, language.code});
			}
		}
	}
	for (const auto& [areaId, areaData] : data.areas) {
		const Area& area = areaData;
		auto at = [&](int line, const char* key, std::vector<std::string> params) { problems.add({area.file, line, "", {key, std::move(params)}}); };
		auto flagAt = [&](int line, const std::string& flag) {
			const events::Declaration* d = data.registry.find(flag);
			if (!flag.empty() && (!d || !d->is_flag)) {
				at(line, "data.flag_not_declared", {flag});
			}
		};
		textExists(c.languages.front().text, "area." + areaId);
		if (!area.music.empty() && std::find(shown.audio.begin(), shown.audio.end(), area.music) == shown.audio.end()) {
			at(0, "data.music_not_audio", {area.music});
		}
		for (const Exit& e : area.exits) {
			if (!data.area(e.toArea)) {
				at(e.line, "data.exit_leads_unknown_area", {e.toArea});
			} else if (!markerExists(e.toArea, e.toMarker)) {
				at(e.line, "data.exit_leads_missing_marker", {e.toArea, e.toMarker});
			}
			flagAt(e.line, e.requireFlag);
			flagAt(e.line, e.blockFlag);
		}
		for (const Conveyor& conveyor : area.conveyors) {
			flagAt(conveyor.line, conveyor.requireFlag);
			flagAt(conveyor.line, conveyor.blockFlag);
		}
		for (const Hook& h : area.hooks) {
			if (data.program.find_event(h.event) < 0) {
				at(h.line, "data.hook_runs_unknown_event", {h.id, h.event});
			}
			flagAt(h.line, h.requireFlag);
			flagAt(h.line, h.blockFlag);
		}
		for (const Shop& s : area.shops) {
			for (const StockEntry& entry : s.stock) {
				if (!knownThing(db, entry.id) || data.price(entry.id) < 0) {
					at(s.line, "data.shop_stock_unknown", {s.id, entry.id});
				}
				flagAt(s.line, entry.requireFlag);
				flagAt(s.line, entry.blockFlag);
			}
			flagAt(s.line, s.requireFlag);
			flagAt(s.line, s.blockFlag);
		}
		for (const CellChange& change : area.changes) {
			flagAt(change.line, change.flag);
		}
		for (const Chest& ch : area.chests) {
			for (const auto& [id, n] : ch.items) {
				if (!db.item(id)) {
					at(ch.line, "data.chest_holds_unknown_item", {ch.id, id});
				}
			}
			for (const auto& [id, n] : ch.equipment) {
				if (!db.equipmentPiece(id)) {
					at(ch.line, "data.chest_holds_unknown_equipment", {ch.id, id});
				}
			}
		}
		for (const Zone& z : area.zones) {
			for (const TableEntry& t : z.table) {
				if (!db.encounter(t.encounter)) {
					at(z.line, "data.zone_lists_unknown_encounter", {z.id, t.encounter});
				}
			}
		}
		for (const Port& p : area.ports) {
			flagAt(p.line, p.flag);
			flagAt(p.line, p.blockFlag);
			const Area* ship = p.ship.empty() ? nullptr : data.area(p.ship);
			if (!p.ship.empty() && (!ship || ship->kind != AreaKind::Vessel || ship->arrival.empty())) {
				at(p.line, "data.port_ship_not_vessel", {p.id, p.ship});
			}
		}
		if (!area.arrival.empty() && area.kind != AreaKind::Town && area.kind != AreaKind::Vessel) {
			at(0, "data.arrival_not_town", {});
		}
	}
	const Journal& j = data.journal;
	for (const Objective& o : j.objectives) {
		stringExists("journal objective '" + o.id + "'", o.text);
		flagDeclared("journal objective '" + o.id + "'", o.range.after);
		flagDeclared("journal objective '" + o.id + "'", o.range.until);
	}
	for (const Job& job : j.jobs) {
		stringExists("journal job '" + job.id + "'", job.title);
		flagDeclared("journal job '" + job.id + "'", job.availableFlag);
		flagDeclared("journal job '" + job.id + "'", job.acceptedFlag);
		flagDeclared("journal job '" + job.id + "'", job.completedFlag);
		flagDeclared("journal job '" + job.id + "'", job.missedFlag);
	}
	for (const EncyclopediaEntry& e : j.encyclopedia) {
		stringExists("journal entry '" + e.id + "'", e.title);
		stringExists("journal entry '" + e.id + "'", e.text);
		flagDeclared("journal entry '" + e.id + "'", e.flag);
	}
	for (const TalkSet& t : j.partyTalk) {
		const std::string where = "journal party talk '" + t.id + "'";
		if (!t.area.empty() && !data.area(t.area)) {
			problems.add(where, "data.unknown_area", {t.area});
		}
		flagDeclared(where, t.range.after);
		flagDeclared(where, t.range.until);
		for (const TalkLine& line : t.lines) {
			if (!db.character(line.speaker)) {
				problems.add(where, "data.speaker_not_character", {line.speaker});
			}
			stringExists(where, line.text);
		}
	}
	for (const std::string& key : sessionTextKeys()) {
		textExists("the session", key);
	}
	for (const events::Instruction& in : data.program.code) {
		if (in.op != events::Op::vehicle_give && in.op != events::Op::vehicle_place) {
			continue;
		}
		const Area* where = data.area(in.action.subject);
		if (!data.vehicle(in.name)) {
			problems.add({data.program.files[in.file], in.source_line, "", {"data.vehicle_not_field", {in.name}}});
		} else if (where && where->kind != AreaKind::Field) {
			problems.add({data.program.files[in.file], in.source_line, "", {"data.vehicle_not_outdoors", {in.name, where->id}}});
		}
	}
	for (const events::Instruction& in : data.program.code) {
		const events::BattleRequest& b = in.battle;
		if (in.op != events::Op::battle || b.end != events::BattleEndKind::enemy_defeated) {
			continue;
		}
		const rules::Encounter* e = db.encounter(b.encounter);
		const bool inGroup = e && std::any_of(e->enemies.begin(), e->enemies.end(), [&](const rules::FormationSlot& s) { return s.enemy == b.end_enemy; });
		if (!inGroup) {
			problems.add({data.program.files[in.file], in.source_line, "", {"data.scripted_end_enemy_not_in_group", {b.end_enemy, b.encounter}}});
		}
	}
}

// Loads into `data` as far as it can. A game.json problem stops at once; a battle data problem skips core's
// text table but still reads the game's own files, so one run reports as much as it can. The scripts are
// compiled, and the cross-references checked, only when everything before them loaded.
void loadInto(const DataDirectories& directories, GameData& data, Problems& problems, bool& referencesChecked) {
	referencesChecked = false;
	const std::string& directory = directories.game;
	std::string text;
	const std::string configPath = join(directory, "game.json");
	if (!readFile(configPath, text)) {
		problems.add("", "data.cannot_read", {configPath});
		return;
	}
	rules::Result<JsonValue> json = rules::parseJson(text);
	if (!json.ok()) {
		problems.add(json.problems, configPath);
		return;
	}
	JsonReader reader(configPath);
	readConfig(reader, *json.value, data.config);
	if (!reader.ok()) {
		problems.add(reader.problems());
		return;
	}
	std::uint64_t hash = hashJson(1469598103934665603ULL, *json.value, {"presentation"});

	const std::string coreDirectory = directories.core.empty() ? join(directory, data.config.coreData) : directories.core;
	rules::Result<rules::Database> db = rules::loadDatabaseFromDirectory(coreDirectory);
	const bool coreLoaded = db.ok();
	if (!coreLoaded) {
		problems.add(db.problems);
	} else {
		data.db = std::move(*db.value);
	}

	rules::TextTable coreTable;
	rules::Result<rules::TextTable> coreText = coreLoaded ? rules::loadTextTable(join(coreDirectory, "text_en.json")) : rules::Result<rules::TextTable>::success({});
	if (!coreText.ok()) {
		problems.add(coreText.problems);
	} else if (coreLoaded) {
		coreTable = std::move(*coreText.value);
		for (const std::string& key : rules::messageKeys(data.db)) {
			if (!coreTable.count(key)) {
				problems.add(join(coreDirectory, "text_en.json"), "data.missing_message_key", {key});
			}
		}
	}
	std::vector<events::Diagnostic> diagnostics;
	for (const LanguageFiles& files : data.config.languages) {
		LanguageTables tables;
		tables.code = files.code;
		tables.text = coreTable;
		const std::string textPath = join(directory, files.text);
		if (!readFile(textPath, text)) {
			problems.add("", "data.cannot_read", {textPath});
		} else {
			rules::Result<rules::TextTable> own = rules::parseTextTable(text);
			if (!own.ok()) {
				problems.add(own.problems, files.text);
			} else {
				for (const auto& [key, value] : *own.value) {
					if (!isDescription(key)) {
						hash = fnv(fnv(hash, key), value);
					}
					if (!tables.text.emplace(key, value).second) {
						problems.add(files.text, "data.key_already_in_core", {key});
					}
				}
			}
		}
		const std::string stringsPath = join(directory, files.strings);
		if (!readFile(stringsPath, text)) {
			problems.add("", "data.cannot_read", {stringsPath});
		} else {
			hash = fnv(hash, text);
			if (events::load_string_table(files.strings, text, tables.strings, diagnostics) && tables.strings.language != files.code) {
				problems.add(files.strings, "data.table_language_mismatch", {tables.strings.language, files.code});
			}
		}
		data.languages.push_back(std::move(tables));
	}
	// Descriptions and names are optional in every language; the other keys must match the source language's.
	for (std::size_t i = 1; i < data.languages.size(); ++i) {
		const rules::TextTable& first = data.languages.front().text;
		const rules::TextTable& other = data.languages[i].text;
		for (const auto& [key, value] : first) {
			if (!other.count(key) && !isDescription(key)) {
				problems.add(data.config.languages[i].text, "data.key_missing_from_table", {key, std::string(data.languages.front().code)});
			}
		}
		for (const auto& [key, value] : other) {
			if (!first.count(key) && !isDescription(key)) {
				problems.add(data.config.languages[i].text, "data.key_not_in_source_table", {key, std::string(data.languages.front().code)});
			}
		}
	}
	if (coreLoaded) {
		for (std::size_t i = 0; i < data.languages.size(); ++i) {
			for (const std::string& element : data.db.battleRules.elements) {
				if (!data.languages[i].text.count(std::string(kNamePrefix) + "element." + element)) {
					problems.add(data.config.languages[i].text, "data.element_name_missing", {element});
				}
			}
			for (const auto& [key, value] : data.languages[i].text) {
				if (!isDescription(key)) {
					continue;
				}
				if (key.compare(0, std::char_traits<char>::length(kNamePrefix), kNamePrefix) == 0) {
					if (!namesKnownThing(data.db, key)) {
						problems.add(data.config.languages[i].text, "data.name_unknown", {key});
					}
					continue;
				}
				bool known = false;
				for (int k = 0; k <= static_cast<int>(DescriptionKind::Difficulty) && !known; ++k) {
					const DescriptionKind kind = static_cast<DescriptionKind>(k);
					const std::string prefix = descriptionKey(kind, "");
					known = key.compare(0, prefix.size(), prefix) == 0 && describes(data.db, kind, key.substr(prefix.size()));
				}
				if (!known) {
					problems.add(data.config.languages[i].text, "data.description_unknown", {key});
				}
			}
		}
	}

	for (const std::string& file : data.config.areaFiles) {
		const std::string path = join(directory, file);
		if (!readFile(path, text)) {
			problems.add("", "data.cannot_read", {path});
			continue;
		}
		rules::Result<Area> area = parseArea(file, text);
		if (!area.ok()) {
			problems.add(area.problems);
			continue;
		}
		if (data.areas.count(area.value->id)) {
			problems.add(file, "data.duplicate_area_id", {area.value->id});
			continue;
		}
		hash = hashJson(hash, *rules::parseJson(text).value, {"music"});
		data.areas[area.value->id] = std::move(*area.value);
	}

	const std::string journalPath = join(directory, data.config.journalFile);
	if (!readFile(journalPath, text)) {
		problems.add("", "data.cannot_read", {journalPath});
	} else {
		hash = fnv(hash, text);
		rules::Result<JsonValue> parsed = rules::parseJson(text);
		JsonReader journal(data.config.journalFile);
		if (!parsed.ok()) {
			problems.add(parsed.problems, data.config.journalFile);
		} else {
			readJournal(journal, *parsed.value, data.journal);
			if (!journal.ok()) {
				problems.add(journal.problems());
			}
		}
	}

	const std::string glossaryPath = join(directory, data.config.glossaryFile);
	if (!readFile(glossaryPath, text)) {
		problems.add("", "data.cannot_read", {glossaryPath});
	} else {
		events::load_glossary(data.config.glossaryFile, text, data.glossary, diagnostics);
	}
	problems.add(diagnostics);
	if (!problems.empty()) {
		return;
	}

	data.registry = buildRegistry(data, problems);
	data.gameStrings = buildGameStrings(data);
	std::vector<events::Source> sources;
	for (const std::string& file : data.config.eventFiles) {
		const std::string path = join(directory, file);
		if (!readFile(path, text)) {
			problems.add("", "data.cannot_read", {path});
			continue;
		}
		sources.push_back({file, text});
	}
	events::CompileOptions options;
	options.registry = &data.registry;
	events::CompileResult compiled = events::compile(sources, options);
	// The strict check of FORMAT.md: warnings fail too.
	problems.add(compiled.diagnostics);
	if (compiled.ok()) {
		data.program = std::move(compiled.program);
		problems.add(events::check_inline_text(data.program));
		std::vector<events::StringTable> tables;
		for (const LanguageTables& l : data.languages) {
			tables.push_back(l.strings);
		}
		problems.add(events::check_strings(data.program, tables, data.gameStrings));
		problems.add(events::check_names(data.program, data.glossary, tables));
	}

	if (problems.empty()) {
		checkReferences(data, problems);
		referencesChecked = true;
	}
	hash ^= data.db.contentHash;
	hash = hash * 1099511628211ULL ^ data.program.fingerprint;
	data.contentHash = hash;
}

}

rules::Result<GameData> loadGameData(const std::string& directory) {
	return loadGameData(DataDirectories{directory, ""});
}

rules::Result<GameData> loadGameData(const DataDirectories& directories) {
	GameData data;
	Problems problems;
	bool referencesChecked = false;
	loadInto(directories, data, problems, referencesChecked);
	if (!problems.empty()) {
		return problems.failure();
	}
	return rules::Result<GameData>::success(std::move(data));
}

std::vector<rules::Problem> loadGameDataPartly(const DataDirectories& directories, GameData& out, bool* referencesChecked) {
	out = GameData();
	Problems problems;
	bool checked = false;
	loadInto(directories, out, problems, checked);
	if (referencesChecked) {
		*referencesChecked = checked;
	}
	return problems.list();
}

std::string registryText(const events::Registry& registry) {
	std::string out = "# Built from the game data by game_cli --registry.\n";
	const std::pair<events::NameKind, const char*> kinds[] = {
		{events::NameKind::actor, "actor"}, {events::NameKind::marker, "marker"}, {events::NameKind::camera, "camera"},
		{events::NameKind::layout, "layout"}, {events::NameKind::audio, "audio"}, {events::NameKind::animation, "animation"},
		{events::NameKind::portrait, "portrait"}, {events::NameKind::item, "item"}, {events::NameKind::member, "member"},
		{events::NameKind::encounter, "encounter"}, {events::NameKind::map, "map"}, {events::NameKind::shop, "shop"},
		{events::NameKind::rest_point, "rest"}, {events::NameKind::enemy, "enemy"}, {events::NameKind::vehicle, "vehicle"}, {events::NameKind::ability, "ability"},
	};
	for (const auto& [kind, word] : kinds) {
		auto it = registry.names.find(kind);
		if (it == registry.names.end() || it->second.empty()) {
			continue;
		}
		std::string names;
		for (const std::string& name : it->second) {
			// Markers with a map are written on map_markers lines below.
			if (kind != events::NameKind::marker || !registry.marker_maps.count(name)) {
				names += " " + name;
			}
		}
		if (!names.empty()) {
			out += word + names + "\n";
		}
	}
	std::map<std::string, std::vector<std::string>> byMap;
	for (const auto& [marker, maps] : registry.marker_maps) {
		for (const std::string& map : maps) {
			byMap[map].push_back(marker);
		}
	}
	for (const auto& [map, markers] : byMap) {
		out += "map_markers " + map;
		for (const std::string& marker : markers) {
			out += " " + marker;
		}
		out += "\n";
	}
	byMap.clear();
	for (const auto& [event, maps] : registry.event_maps) {
		for (const std::string& map : maps) {
			byMap[map].push_back(event);
		}
	}
	for (const auto& [map, list] : byMap) {
		out += "event_map " + map;
		for (const std::string& event : list) {
			out += " " + event;
		}
		out += "\n";
	}
	for (const events::Declaration& d : registry.declarations) {
		out += std::string(d.is_flag ? "flag " : "var ") + d.name;
		if (d.initial != 0) {
			out += d.is_flag ? " default true" : " default " + std::to_string(d.initial);
		}
		out += "\n";
	}
	return out;
}

std::string gameStringsText(const events::GameStrings& strings) {
	std::string out = "# String IDs the game shows itself, built from the game data by game_cli --used.\n";
	for (const auto& [id, line] : strings.ids) {
		out += id + "\n";
	}
	return out;
}

}
