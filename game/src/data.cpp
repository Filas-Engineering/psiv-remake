#include "game/data.h"

#include "game/capabilities.h"
#include "game/save.h"

#include "events/compiler.h"
#include "events/glossary.h"
#include "rules/formulas.h"
#include "rules/loader.h"

#include "json_read.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

double PresentationNames::animationLength(const std::string& name) const {
	auto listed = animationSeconds.find(name);
	if (listed != animationSeconds.end()) {
		return listed->second;
	}
	const events::StandardAnimation* standard = events::standard_animation(name);
	return standard ? standard->seconds : 0;
}

bool ActorWander::covers(Cell home, Cell cell) const {
	if (from && to) {
		return cell.x >= from->x && cell.x <= to->x && cell.y >= from->y && cell.y <= to->y;
	}
	return std::abs(cell.x - home.x) + std::abs(cell.y - home.y) <= radius;
}

const Credits* GameConfig::creditsById(const std::string& id) const {
	for (const Credits& c : credits) {
		if (c.id == id) {
			return &c;
		}
	}
	return nullptr;
}

const ActorDef* GameConfig::actorDef(const std::string& id) const {
	for (const ActorDef& a : actorDefs) {
		if (a.id == id) {
			return &a;
		}
	}
	return nullptr;
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
			reader.only(entry, gameKeys("game.json", "presentation.cameras[]"));
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

// An actor is an id, or an object with the id and its figure and home.
void readActors(JsonReader& reader, const JsonValue& json, GameConfig& c) {
	const JsonValue* list = reader.field(json, "actors", JsonValue::Type::Array, false);
	if (!list) {
		return;
	}
	for (const JsonValue& entry : list->items) {
		ActorDef actor;
		actor.line = entry.line;
		if (entry.is(JsonValue::Type::String) && !entry.string.empty()) {
			actor.id = entry.string;
		} else if (entry.is(JsonValue::Type::Object)) {
			reader.only(entry, gameKeys("game.json", "actors[]"));
			reader.text(entry, "id", actor.id, true);
			if (const JsonValue* sprite = reader.field(entry, "sprite", JsonValue::Type::Object, false)) {
				reader.only(*sprite, gameKeys("game.json", "actors[].sprite"));
				reader.text(*sprite, "enemy", actor.spriteEnemy, false);
				reader.text(*sprite, "look", actor.look, false);
				if (actor.spriteEnemy.empty() == actor.look.empty()) {
					reader.fail(*sprite, "data.bad_actor_sprite", {actor.id});
				}
			}
			reader.text(entry, "area", actor.area, false);
			if (const JsonValue* at = entry.find("at")) {
				Cell cell;
				if (at->is(JsonValue::Type::String) && !at->string.empty()) {
					actor.marker = at->string;
				} else if (reader.cell(*at, cell)) {
					actor.at = cell;
				}
			}
			if (actor.area.empty() != (actor.marker.empty() && !actor.at)) {
				reader.fail(entry, "data.actor_home_needs_area", {actor.id});
			}
			std::string facing = "south";
			reader.text(entry, "facing", facing, false);
			if (!parseFacing(facing, actor.facing)) {
				reader.fail(entry, "data.bad_facing", {});
			}
			reader.boolean(entry, "solid", actor.solid, false);
			reader.text(entry, "require_flag", actor.requireFlag, false);
			reader.text(entry, "block_flag", actor.blockFlag, false);
			if (const JsonValue* wander = reader.field(entry, "wander", JsonValue::Type::Object, false)) {
				reader.only(*wander, gameKeys("game.json", "actors[].wander"));
				ActorWander w;
				reader.integer(*wander, "radius", w.radius, false, 1, 99);
				if (wander->find("from") || wander->find("to")) {
					Cell from;
					Cell to;
					reader.cellField(*wander, "from", from, true);
					reader.cellField(*wander, "to", to, true);
					w.from = Cell{std::min(from.x, to.x), std::min(from.y, to.y)};
					w.to = Cell{std::max(from.x, to.x), std::max(from.y, to.y)};
				}
				if ((w.radius > 0) == w.from.has_value()) {
					reader.fail(*wander, "data.wander_radius_or_region", {actor.id});
				}
				reader.integer(*wander, "speed_percent", w.speedPercent, false, 1, 1000);
				if (const JsonValue* pause = reader.field(*wander, "pause_ms", JsonValue::Type::Array, false)) {
					if (pause->items.size() != 2 || !pause->items[0].is(JsonValue::Type::Number) || !pause->items[1].is(JsonValue::Type::Number) ||
						pause->items[0].number < 0 || pause->items[1].number < pause->items[0].number || pause->items[1].number > 600000) {
						reader.fail(*pause, "data.bad_wander_pause", {actor.id});
					} else {
						w.pauseMinMs = static_cast<int>(pause->items[0].number);
						w.pauseMaxMs = static_cast<int>(pause->items[1].number);
					}
				}
				reader.text(*wander, "terrain", w.terrain, false);
				if (w.terrain.empty() || w.terrain.find_first_not_of(kMapTiles) != std::string::npos || w.terrain.find('#') != std::string::npos) {
					reader.fail(*wander, "data.bad_wander_terrain", {actor.id, w.terrain});
				}
				reader.integer(*wander, "stand_percent", w.standPercent, false, 0, 100);
				std::string axis;
				reader.text(*wander, "axis", axis, false);
				if (const int index = axis.empty() ? 0 : gameEnumIndex("wander_axis", axis); index >= 0) {
					w.axis = static_cast<WanderAxis>(index);
				} else {
					reader.fail(*wander, "data.bad_wander_axis", {actor.id, axis});
				}
				if (actor.area.empty()) {
					reader.fail(*wander, "data.wander_needs_home", {actor.id});
				}
				actor.wander = w;
			}
			if (const JsonValue* patrol = reader.field(entry, "patrol", JsonValue::Type::Object, false)) {
				reader.only(*patrol, gameKeys("game.json", "actors[].patrol"));
				ActorPatrol p;
				if (const JsonValue* steps = reader.field(*patrol, "steps", JsonValue::Type::Array, true)) {
					for (const JsonValue& item : steps->items) {
						PatrolStep step;
						step.line = item.line;
						if (!item.is(JsonValue::Type::Object)) {
							reader.fail(item, "data.bad_patrol_step", {actor.id});
							continue;
						}
						reader.only(item, gameKeys("game.json", "actors[].patrol.steps[]"));
						const auto direction = [&](const char* key, std::optional<Facing>& out) {
							std::string word;
							reader.text(item, key, word, false);
							Facing f = Facing::South;
							if (word.empty()) {
								return;
							}
							if (gameEnumIndex("facing", word) < 0 || !parseFacing(word, f)) {
								reader.fail(item, "data.bad_direction", {word});
								return;
							}
							out = f;
						};
						direction("walk", step.walk);
						direction("face", step.face);
						if (const JsonValue* to = item.find("to")) {
							Cell cell;
							if (to->is(JsonValue::Type::String) && !to->string.empty()) {
								step.toMarker = to->string;
							} else if (reader.cell(*to, cell)) {
								step.to = cell;
							}
						}
						step.count = step.walk ? 1 : 0;
						reader.integer(item, "count", step.count, false, 1, 99);
						reader.integer(item, "pause_ms", step.pauseMs, false, 0, 600000);
						const bool toTarget = step.to || !step.toMarker.empty();
						if ((step.walk && toTarget) || (item.find("count") && !step.walk) || (!step.walk && !toTarget && !step.face && !item.find("pause_ms"))) {
							reader.fail(item, "data.bad_patrol_step", {actor.id});
						}
						p.steps.push_back(step);
					}
					if (steps->items.empty()) {
						reader.fail(*steps, "data.bad_patrol_step", {actor.id});
					}
				}
				reader.integer(*patrol, "speed_percent", p.speedPercent, false, 1, 1000);
				reader.text(*patrol, "terrain", p.terrain, false);
				if (p.terrain.empty() || p.terrain.find_first_not_of(kMapTiles) != std::string::npos || p.terrain.find('#') != std::string::npos) {
					reader.fail(*patrol, "data.bad_patrol_terrain", {actor.id, p.terrain});
				}
				actor.patrol = p;
			}
			if (const JsonValue* path = reader.field(entry, "path", JsonValue::Type::Object, false)) {
				reader.only(*path, gameKeys("game.json", "actors[].path"));
				ActorPath p;
				if (const JsonValue* offsets = reader.field(*path, "offsets", JsonValue::Type::Array, true)) {
					for (const JsonValue& o : offsets->items) {
						if (o.items.size() != 2 || !o.items[0].is(JsonValue::Type::Number) || !o.items[1].is(JsonValue::Type::Number) ||
							std::abs(o.items[0].number) > 100000 || std::abs(o.items[1].number) > 100000 || o.items[0].number != std::floor(o.items[0].number) ||
							o.items[1].number != std::floor(o.items[1].number)) {
							reader.fail(o, "data.bad_path", {actor.id});
							continue;
						}
						p.offsets.emplace_back(static_cast<int>(o.items[0].number), static_cast<int>(o.items[1].number));
					}
					if (offsets->items.empty()) {
						reader.fail(*offsets, "data.bad_path", {actor.id});
					}
				}
				reader.integer(*path, "ticks", p.ticks, false, 1, 1000);
				if (const JsonValue* footprint = reader.field(*path, "footprint", JsonValue::Type::Array, false)) {
					Cell size;
					if (!reader.cell(*footprint, size) || size.x < 1 || size.x > 16 || size.y < 1 || size.y > 16) {
						reader.fail(*footprint, "data.bad_footprint", {actor.id});
					} else {
						p.footprintWidth = size.x;
						p.footprintHeight = size.y;
					}
				}
				actor.path = p;
			}
			if ((actor.wander ? 1 : 0) + (actor.patrol ? 1 : 0) + (actor.path ? 1 : 0) > 1) {
				reader.fail(entry, "data.moves_more_than_one_way", {actor.id});
			}
			if ((actor.patrol || actor.path) && actor.area.empty()) {
				reader.fail(entry, "data.moving_actor_needs_home", {actor.id});
			}
		} else {
			reader.fail(entry, "data.actor_shape", {});
			continue;
		}
		c.actors.push_back(actor.id);
		c.actorDefs.push_back(actor);
	}
}

// An animation is a name, or an object with the name and the seconds one play lasts.
void readAnimations(JsonReader& reader, const JsonValue& presentation, PresentationNames& out) {
	const JsonValue* list = reader.field(presentation, "animations", JsonValue::Type::Array, false);
	if (!list) {
		return;
	}
	for (const JsonValue& entry : list->items) {
		std::string id;
		if (entry.is(JsonValue::Type::String) && !entry.string.empty()) {
			id = entry.string;
		} else if (entry.is(JsonValue::Type::Object)) {
			reader.only(entry, gameKeys("game.json", "presentation.animations[]"));
			reader.text(entry, "id", id, true);
			double seconds = 0;
			reader.number(entry, "seconds", seconds, true, 0, 60);
			out.animationSeconds[id] = seconds;
		} else {
			reader.fail(entry, "data.animation_shape", {});
			continue;
		}
		out.animations.push_back(id);
	}
}

void readConfig(JsonReader& reader, const JsonValue& json, GameConfig& c) {
	reader.only(json, gameKeys("game.json", ""));
	reader.text(json, "title", c.title, true);
	reader.text(json, "core_data", c.coreData, true);
	reader.texts(json, "areas", c.areaFiles, true);
	reader.texts(json, "events", c.eventFiles, true);
	reader.text(json, "glossary", c.glossaryFile, true);
	if (const JsonValue* list = reader.field(json, "languages", JsonValue::Type::Array, true)) {
		for (const JsonValue& l : list->items) {
			reader.only(l, gameKeys("game.json", "languages[]"));
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
	readActors(reader, json, c);
	reader.texts(json, "nameless_actors", c.namelessActors, false);
	if (const JsonValue* p = reader.field(json, "presentation", JsonValue::Type::Object, false)) {
		reader.only(*p, gameKeys("game.json", "presentation"));
		std::string flash;
		reader.text(*p, "flash_default", flash, false);
		if (!flash.empty() && !parseFlashLevel(flash, c.presentation.flashDefault)) {
			reader.fail(*p, "data.bad_flash_default", {flash});
		}
		reader.boolean(*p, "flash_original", c.presentation.flashOriginal, false);
		reader.integer(*p, "map_load_hold_ms", c.presentation.mapLoadHoldMs, false, 0, 60000);
		reader.boolean(*p, "map_load_hold_on_load", c.presentation.mapLoadHoldOnLoad, false);
		reader.texts(*p, "audio", c.presentation.audio, false);
		if (const JsonValue* s = reader.field(*p, "sounds", JsonValue::Type::Object, false)) {
			FieldSounds& sounds = c.presentation.sounds;
			reader.only(*s, gameKeys("game.json", "presentation.sounds"));
			reader.text(*s, "chest", sounds.chest, false);
			if (const JsonValue* t = reader.field(*s, "travel", JsonValue::Type::Object, false)) {
				reader.only(*t, gameKeys("game.json", "presentation.sounds.travel"));
				reader.text(*t, "town", sounds.town, false);
				reader.text(*t, "dungeon_exit", sounds.dungeonExit, false);
			}
			for (const char* key : {"chests", "travel_things"}) {
				const JsonValue* m = reader.field(*s, key, JsonValue::Type::Object, false);
				for (std::size_t i = 0; m && i < m->keys.size(); ++i) {
					if (!m->values[i].is(JsonValue::Type::String) || m->values[i].string.empty()) {
						reader.fail(m->values[i], "data.sound_not_text", {m->keys[i]});
						continue;
					}
					(std::string(key) == "chests" ? sounds.chests : sounds.travelThings)[m->keys[i]] = m->values[i].string;
				}
			}
			const JsonValue* v = reader.field(*s, "vehicles", JsonValue::Type::Object, false);
			for (std::size_t i = 0; v && i < v->keys.size(); ++i) {
				if (!v->values[i].is(JsonValue::Type::Object)) {
					reader.fail(v->values[i], "data.sound_not_text", {v->keys[i]});
					continue;
				}
				VehicleSounds& vs = sounds.vehicles[v->keys[i]];
				reader.only(v->values[i], gameKeys("game.json", "presentation.sounds.vehicles entry"));
				reader.text(v->values[i], "move", vs.move, false);
				reader.integer(v->values[i], "move_ms", vs.moveMs, false, 0, 60000);
				reader.text(v->values[i], "board", vs.board, false);
				reader.text(v->values[i], "leave", vs.leave, false);
			}
		}
		readCameras(reader, *p, c.presentation.cameras);
		reader.texts(*p, "layouts", c.presentation.layouts, false);
		readAnimations(reader, *p, c.presentation);
		reader.texts(*p, "portraits", c.presentation.portraits, false);
		reader.texts(*p, "pictures", c.presentation.pictures, false);
		if (const JsonValue* m = reader.field(*p, "music", JsonValue::Type::Object, false)) {
			reader.only(*m, gameKeys("game.json", "presentation.music"));
			reader.integer(*m, "fade_ms", c.presentation.music.fadeMs, false, 0, 60000);
			reader.text(*m, "title", c.presentation.music.title, false);
			reader.text(*m, "victory", c.presentation.music.victory, false);
			reader.text(*m, "defeat", c.presentation.music.defeat, false);
			for (const char* key : {"encounters", "vehicles"}) {
				const JsonValue* e = reader.field(*m, key, JsonValue::Type::Object, false);
				for (std::size_t i = 0; e && i < e->keys.size(); ++i) {
					if (std::string(key) == "vehicles" && e->values[i].is(JsonValue::Type::Object)) {
						const JsonValue& v = e->values[i];
						reader.only(v, gameKeys("game.json", "presentation.music.vehicles entry"));
						std::string battle;
						std::string field;
						reader.text(v, "battle", battle, false);
						reader.text(v, "field", field, false);
						if (battle.empty() && field.empty()) {
							reader.fail(v, "data.music_not_text", {e->keys[i]});
						}
						if (!battle.empty()) {
							c.presentation.music.vehicles[e->keys[i]] = battle;
						}
						if (!field.empty()) {
							c.presentation.music.vehiclesField[e->keys[i]] = field;
						}
						continue;
					}
					if (!e->values[i].is(JsonValue::Type::String) || e->values[i].string.empty()) {
						reader.fail(e->values[i], "data.music_not_text", {e->keys[i]});
						continue;
					}
					(std::string(key) == "encounters" ? c.presentation.music.encounters : c.presentation.music.vehicles)[e->keys[i]] = e->values[i].string;
				}
			}
			if (const JsonValue* a = reader.field(*m, "areas", JsonValue::Type::Object, false)) {
				for (std::size_t i = 0; i < a->keys.size(); ++i) {
					const JsonValue& value = a->values[i];
					std::vector<const JsonValue*> entries;
					if (value.is(JsonValue::Type::Array)) {
						for (const JsonValue& item : value.items) {
							entries.push_back(&item);
						}
					} else {
						entries.push_back(&value);
					}
					std::vector<AreaTrack>& tracks = c.presentation.music.areas[a->keys[i]];
					for (const JsonValue* entry : entries) {
						AreaTrack t;
						if (entry->is(JsonValue::Type::Object)) {
							reader.only(*entry, gameKeys("game.json", "presentation.music.areas entry"));
							reader.text(*entry, "flag", t.flag, true);
							reader.text(*entry, "track", t.track, true);
						} else if (entry->is(JsonValue::Type::String) && !entry->string.empty()) {
							t.track = entry->string;
						} else {
							reader.fail(*entry, "data.music_not_text", {a->keys[i]});
						}
						if (!t.track.empty()) {
							tracks.push_back(t);
						}
					}
				}
			}
		}
		if (const JsonValue* list = reader.field(*p, "durations", JsonValue::Type::Array, false)) {
			for (const JsonValue& d : list->items) {
				reader.only(d, gameKeys("game.json", "presentation.durations[]"));
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
	if (const JsonValue* flags = reader.field(json, "flags", JsonValue::Type::Array, false)) {
		for (const JsonValue& f : flags->items) {
			if (f.is(JsonValue::Type::String) && !f.string.empty()) {
				c.flags.push_back(f.string);
			} else if (f.is(JsonValue::Type::Object)) {
				reader.only(f, gameKeys("game.json", "flags[]"));
				std::string id;
				std::string reset;
				reader.text(f, "id", id, true);
				reader.text(f, "reset", reset, true);
				if (gameEnumIndex("flag_reset", reset) < 0) {
					reader.fail(f, "data.bad_flag_reset", {id, reset});
				}
				c.flags.push_back(id);
				c.areaFlags.insert(id);
			} else {
				reader.fail(f, "data.flag_shape", {});
			}
		}
	}
	reader.counts(json, "vars", c.vars, false, -999999, 999999);
	reader.text(json, "clear_flag", c.clearFlag, false);
	if (const JsonValue* start = reader.field(json, "start", JsonValue::Type::Object, true)) {
		reader.only(*start, gameKeys("game.json", "start"));
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
	reader.integer(json, "autosave_slots", c.autosaveSlots, false, 0, 9);
	reader.boolean(json, "autosave", c.autosave, false);
	reader.integer(json, "bag_slots", c.bagSlots, false, 0, 9999);
	std::string stacking;
	reader.text(json, "bag_stacking", stacking, false);
	if (const int index = stacking.empty() ? 0 : gameEnumIndex("bag_stacking", stacking); index >= 0) {
		c.bagSingle = index == 1;
	} else {
		reader.fail(json, "data.bad_bag_stacking", {stacking});
	}
	reader.integer(json, "manual_slots", c.manualSlots, false, 1, 99);
	reader.integer(json, "machine_step_heal", c.machineStepHeal, false, 0, 999);
	// A boolean is the key's first form: true stands machines up.
	if (const JsonValue* fallen = json.find("machine_step_heal_fallen")) {
		const int index = fallen->is(JsonValue::Type::Bool) ? (fallen->boolean ? 2 : 0)
			: fallen->is(JsonValue::Type::String) ? gameEnumIndex("machine_step_heal_fallen", fallen->string) : -1;
		if (index >= 0) {
			c.machineStepHealFallen = static_cast<MachineFallenHeal>(index);
		} else {
			reader.fail(json, "data.bad_machine_step_heal_fallen", {fallen->is(JsonValue::Type::String) ? fallen->string : ""});
		}
	}
	reader.text(json, "outside_experience_until", c.outsideExperienceUntil, false);
	std::string leaving = "keep";
	reader.text(json, "leave_equipment", leaving, false);
	if (const int index = gameEnumIndex("leave_equipment", leaving); index >= 0) {
		c.leaveEquipment = static_cast<LeaveEquipment>(index);
	} else {
		reader.fail(json, "data.bad_leave_equipment", {leaving});
	}
	reader.boolean(json, "random_encounters_everywhere", c.randomEncountersEverywhere, false);
	reader.boolean(json, "grace_after_battle", c.graceAfterBattle, false);
	std::string reach = "faced";
	reader.text(json, "interact", reach, false);
	if (const int index = gameEnumIndex("interact", reach); index >= 0) {
		c.interactReach = static_cast<InteractReach>(index);
	} else {
		reader.fail(json, "data.bad_interact", {reach});
	}
	reader.boolean(json, "interact_across", c.interactAcross, false);
	reader.integer(json, "run_speed_percent", c.runSpeedPercent, false, 101, 1000);
	reader.integer(json, "background_limit", c.backgroundLimit, false, 1, 32);
	reader.integer(json, "cell_pixels", c.cellPixels, false, 1, 1024);
	if (const JsonValue* all = reader.field(json, "credits", JsonValue::Type::Object, false)) {
		for (std::size_t i = 0; i < all->keys.size(); ++i) {
			const JsonValue& o = all->values[i];
			Credits credits;
			credits.id = all->keys[i];
			credits.line = o.line;
			if (!o.is(JsonValue::Type::Object)) {
				reader.fail(o, "data.field_wrong_type", {credits.id, "object"});
				continue;
			}
			reader.only(o, gameKeys("game.json", "credits.<id>"));
			reader.text(o, "music", credits.music, false);
			if (const JsonValue* entries = reader.field(o, "entries", JsonValue::Type::Array, true)) {
				for (const JsonValue& e : entries->items) {
					CreditsEntry entry;
					reader.only(e, gameKeys("game.json", "credits.<id>.entries[]"));
					std::string heading;
					reader.text(e, "heading", heading, false);
					reader.text(e, "line", entry.key, false);
					if (heading.empty() == entry.key.empty()) {
						reader.fail(e, "data.credits_entry_kind", {credits.id});
					} else if (!heading.empty()) {
						entry.kind = CreditsEntryKind::Heading;
						entry.key = heading;
					}
					reader.integer(e, "gap", entry.gap, false, 0, 20);
					reader.integer(e, "pause_ms", entry.pauseMs, false, 0, 60000);
					credits.entries.push_back(entry);
				}
				if (entries->items.empty()) {
					reader.fail(*entries, "data.credits_empty", {credits.id});
				}
			}
			c.credits.push_back(credits);
		}
	}
	const auto opening = [&](const JsonValue& object, const char* key, VehicleOpening& out) {
		std::string text;
		reader.text(object, key, text, false);
		if (text.empty()) {
			return false;
		}
		const int index = gameEnumIndex("vehicle_opening", text);
		if (index < 0) {
			reader.fail(object, "data.bad_vehicle_opening", {text});
			return false;
		}
		out = static_cast<VehicleOpening>(index);
		return true;
	};
	opening(json, "vehicle_opening", c.vehicleOpening);
	std::string arrive;
	reader.text(json, "arrive_notice", arrive, false);
	if (const int index = arrive.empty() ? 0 : gameEnumIndex("arrive_notice", arrive); index >= 0) {
		c.arriveNotice = static_cast<ArriveNotice>(index);
	} else {
		reader.fail(json, "data.bad_arrive_notice", {arrive});
	}
	reader.boolean(json, "party_talk_ambient", c.partyTalkAmbient, false);
	const auto arrival = [&](const char* key, FollowersArrive& out) {
		std::string arriving;
		reader.text(json, key, arriving, false);
		if (const int index = arriving.empty() ? 0 : gameEnumIndex("followers_arrive", arriving); index >= 0) {
			out = static_cast<FollowersArrive>(index);
		} else {
			reader.fail(json, "data.bad_followers_arrive", {arriving});
		}
	};
	arrival("followers_arrive", c.followersArrive);
	arrival("followers_arrive_other", c.followersArriveOther);
	reader.boolean(json, "followers_on_exits", c.followersOnExits, false);
	reader.texts(json, "start_towns", c.startTowns, false);
	std::string talkAt;
	reader.text(json, "talk_at", talkAt, false);
	if (const int index = talkAt.empty() ? 0 : gameEnumIndex("talk_at", talkAt); index >= 0) {
		c.talkAt = static_cast<TalkAt>(index);
	} else {
		reader.fail(json, "data.bad_talk_at", {talkAt});
	}
	std::string fires;
	reader.text(json, "exit_fires", fires, false);
	if (const int index = fires.empty() ? 0 : gameEnumIndex("exit_fires", fires); index >= 0) {
		c.exitFires = static_cast<ExitFires>(index);
	} else {
		reader.fail(json, "data.bad_exit_fires", {fires});
	}
	std::string timing;
	reader.text(json, "step_timing", timing, false);
	if (const int index = timing.empty() ? 0 : gameEnumIndex("step_timing", timing); index >= 0) {
		c.exactSteps = index == 1;
	} else {
		reader.fail(json, "data.bad_step_timing", {timing});
	}
	if (const JsonValue* bosses = reader.field(json, "bosses", JsonValue::Type::Array, false)) {
		for (const JsonValue& b : bosses->items) {
			reader.only(b, gameKeys("game.json", "bosses[]"));
			Boss boss;
			reader.text(b, "encounter", boss.encounter, true);
			reader.text(b, "flag", boss.flag, true);
			c.bosses.push_back(boss);
		}
	}
	if (const JsonValue* vehicles = reader.field(json, "vehicles", JsonValue::Type::Array, false)) {
		for (const JsonValue& v : vehicles->items) {
			reader.only(v, gameKeys("game.json", "vehicles[]"));
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
			VehicleOpening own = VehicleOpening::Roll;
			if (opening(v, "opening", own)) {
				vehicle.opening = own;
			}
			if (const JsonValue* table = reader.field(v, "encounters", JsonValue::Type::Array, true)) {
				for (const JsonValue& t : table->items) {
					if (!t.is(JsonValue::Type::Object)) {
						reader.fail(t, "data.table_entry_object", {});
						continue;
					}
					reader.only(t, gameKeys("game.json", "vehicles[].encounters[]"));
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
	reader.only(json, gameKeys("journal.json", ""));
	if (const JsonValue* list = reader.field(json, "objectives", JsonValue::Type::Array, false)) {
		for (const JsonValue& o : list->items) {
			reader.only(o, gameKeys("journal.json", "objectives[]"));
			Objective objective;
			reader.text(o, "id", objective.id, true);
			reader.text(o, "text", objective.text, true);
			readRange(reader, o, objective.range);
			j.objectives.push_back(objective);
		}
	}
	if (const JsonValue* list = reader.field(json, "jobs", JsonValue::Type::Array, false)) {
		for (const JsonValue& o : list->items) {
			reader.only(o, gameKeys("journal.json", "jobs[]"));
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
			reader.only(o, gameKeys("journal.json", "encyclopedia[]"));
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
			reader.only(o, gameKeys("journal.json", "party_talk[]"));
			TalkSet set;
			reader.text(o, "id", set.id, true);
			reader.text(o, "area", set.area, false);
			readRange(reader, o, set.range);
			if (const JsonValue* lines = reader.field(o, "lines", JsonValue::Type::Array, true)) {
				for (const JsonValue& l : lines->items) {
					reader.only(l, gameKeys("journal.json", "party_talk[].lines[]"));
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
	for (const Credits& n : c.credits) r.add(events::NameKind::credits, n.id);
	for (const std::string& n : c.presentation.pictures) r.add(events::NameKind::picture, n);
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
		for (const Zone& z : a.zones) {
			for (int n = 1; n <= z.visible.cap; ++n) {
				r.add(events::NameKind::actor, visibleEnemyActor(z.id, n));
			}
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
	auto registrableTown = [&](const std::string& id) {
		const Area* a = data.area(id);
		return a && a->kind == AreaKind::Town && (!a->arrival.empty() || a->returnTo) && !a->region.empty();
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
	for (const std::string& id : c.startTowns) {
		if (!registrableTown(id)) {
			problems.add("game.json", "data.town_not_registrable", {id});
		}
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
	for (const ActorDef& a : c.actorDefs) {
		flagDeclared("game.json actor '" + a.id + "'", a.requireFlag);
		flagDeclared("game.json actor '" + a.id + "'", a.blockFlag);
	}
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
	const FieldSounds& sounds = shown.sounds;
	auto soundListed = [&](const std::string& where, const std::string& cue) {
		if (!cue.empty() && std::find(shown.audio.begin(), shown.audio.end(), cue) == shown.audio.end()) {
			problems.add(where, "data.sound_not_audio", {cue});
		}
	};
	soundListed("game.json sounds chest", sounds.chest);
	soundListed("game.json sounds travel", sounds.town);
	soundListed("game.json sounds travel", sounds.dungeonExit);
	for (const auto& entry : sounds.chests) {
		const std::string& chest = entry.first;
		const bool known = std::any_of(data.areas.begin(), data.areas.end(),
			[&](const auto& a) { return std::any_of(a.second.chests.begin(), a.second.chests.end(), [&](const Chest& x) { return x.id == chest; }); });
		if (!known) {
			problems.add("game.json sounds chests", "data.sound_unknown_chest", {chest});
		}
		soundListed("game.json sounds chest '" + chest + "'", entry.second);
	}
	for (const auto& [thing, cue] : sounds.travelThings) {
		const std::vector<rules::Effect>* effects = db.item(thing) ? &db.item(thing)->effects : db.ability(thing) ? &db.ability(thing)->effects : nullptr;
		if (!effects || !rules::travelEffect(*effects)) {
			problems.add("game.json sounds travel_things", "data.sound_not_travel", {thing});
		}
		soundListed("game.json sounds travel '" + thing + "'", cue);
	}
	for (const auto& [vehicle, vs] : sounds.vehicles) {
		const std::string& id = vehicle;
		if (std::none_of(c.vehicles.begin(), c.vehicles.end(), [&](const FieldVehicle& f) { return f.id == id; })) {
			problems.add("game.json sounds vehicles", "data.sound_unknown_vehicle", {vehicle});
		}
		for (const std::string* cue : {&vs.move, &vs.board, &vs.leave}) {
			soundListed("game.json sounds vehicle '" + vehicle + "'", *cue);
		}
	}
	musicListed("game.json music title", shown.music.title);
	musicListed("game.json music victory", shown.music.victory);
	musicListed("game.json music defeat", shown.music.defeat);
	for (const auto& [encounter, track] : shown.music.encounters) {
		if (!db.encounter(encounter)) {
			problems.add("game.json music", "data.music_unknown_encounter", {encounter});
		}
		musicListed("game.json music encounter '" + encounter + "'", track);
	}
	for (const auto& [id, character] : db.characters) {
		for (const rules::ProfileRow& row : character.profile) {
			for (const rules::ProfileText* side : {&row.key, &row.value}) {
				if (!side->number) {
					textExists("party.json character '" + id + "' profile", side->text);
				}
			}
		}
	}
	for (const Credits& credits : c.credits) {
		musicListed("game.json credits '" + credits.id + "'", credits.music);
		for (const CreditsEntry& entry : credits.entries) {
			textExists("game.json credits '" + credits.id + "'", entry.key);
		}
	}
	for (const auto& [vehicle, track] : shown.music.vehicles) {
		if (!data.vehicle(vehicle)) {
			problems.add("game.json music", "data.music_unknown_vehicle", {vehicle});
		}
		musicListed("game.json music vehicle '" + vehicle + "'", track);
	}
	for (const auto& [vehicle, track] : shown.music.vehiclesField) {
		if (!data.vehicle(vehicle)) {
			problems.add("game.json music", "data.music_unknown_vehicle", {vehicle});
		}
		musicListed("game.json music vehicle '" + vehicle + "'", track);
	}
	for (const auto& [area, tracks] : shown.music.areas) {
		const std::string where = "game.json music area '" + area + "'";
		if (!data.area(area)) {
			problems.add("game.json music", "data.music_unknown_area", {area});
		}
		for (std::size_t i = 0; i < tracks.size(); ++i) {
			flagDeclared(where, tracks[i].flag);
			musicListed(where, tracks[i].track);
			if (i > 0 && tracks[i - 1].flag.empty()) {
				problems.add(where, "data.music_after_default", {tracks[i].track});
			}
		}
	}
	for (const std::string& actor : c.actors) {
		if (db.character(actor)) {
			problems.add("game.json", "data.actor_is_character", {actor});
		}
		if (!c.nameless(actor)) {
			stringExists("game.json actor", "speaker." + actor);
		}
	}
	for (const ActorDef& actor : c.actorDefs) {
		const std::string where = "game.json actor '" + actor.id + "'";
		if (!actor.spriteEnemy.empty() && !db.enemy(actor.spriteEnemy)) {
			problems.add(where, "data.actor_sprite_unknown_enemy", {actor.id, actor.spriteEnemy});
		}
		if (actor.area.empty()) {
			continue;
		}
		const Area* home = data.area(actor.area);
		if (!home) {
			problems.add(where, "data.actor_home_unknown_area", {actor.id, actor.area});
		} else if (!actor.marker.empty() && !home->markers.count(actor.marker)) {
			problems.add(where, "data.actor_home_marker_missing", {actor.id, actor.area, actor.marker});
		} else if (actor.at && !home->inside(*actor.at)) {
			problems.add(where, "data.not_on_map", {"actor '" + actor.id + "'"});
		} else if (std::any_of(home->hooks.begin(), home->hooks.end(), [&](const Hook& h) {
					   return h.trigger == HookTrigger::Interact && h.id == actor.id && (h.actorAt || !h.cells.empty() || h.talkAt.value_or(c.talkAt) != TalkAt::Actor);
				   })) {
			problems.add(where, "data.actor_home_twice", {actor.id, actor.area});
		} else if (actor.wander) {
			const Cell cell = actor.at ? *actor.at : home->markers.at(actor.marker);
			if (!actor.wander->covers(cell, cell)) {
				problems.add(where, "data.wander_home_outside", {actor.id});
			}
		} else if (actor.patrol) {
			// One round walked on the map as written, so a step off its terrain or a round that drifts shows at load.
			const Cell start = actor.at ? *actor.at : home->markers.at(actor.marker);
			const auto standable = [&](Cell c) { return home->inside(c) && actor.patrol->terrain.find(home->rows[c.y][c.x]) != std::string::npos; };
			Cell cell = start;
			bool blocked = false;
			for (const PatrolStep& step : actor.patrol->steps) {
				std::optional<Cell> goal = step.to;
				if (!step.toMarker.empty()) {
					const auto marker = home->markers.find(step.toMarker);
					if (marker == home->markers.end()) {
						problems.add(where, "data.patrol_marker_missing", {actor.id, step.toMarker, actor.area});
						blocked = true;
						break;
					}
					goal = marker->second;
				}
				for (int i = 0; !blocked && i < (step.walk ? step.count : 0); ++i) {
					cell = stepFrom(cell, *step.walk);
					blocked = !standable(cell);
				}
				while (!blocked && goal && cell != *goal) {
					cell = cell.x != goal->x ? Cell{cell.x + (goal->x > cell.x ? 1 : -1), cell.y} : Cell{cell.x, cell.y + (goal->y > cell.y ? 1 : -1)};
					blocked = !standable(cell);
				}
				if (blocked) {
					problems.add(where, "data.patrol_blocked", {actor.id, std::to_string(cell.x) + " " + std::to_string(cell.y)});
					break;
				}
			}
			if (!blocked && cell != start) {
				problems.add(where, "data.patrol_not_a_loop", {actor.id, std::to_string(cell.x) + " " + std::to_string(cell.y)});
			}
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
		for (const Area::NameVariant& v : area.names) {
			textExists(c.languages.front().text, v.key);
			flagAt(v.line, v.flag);
			if (!v.from.empty() && !data.area(v.from)) {
				at(v.line, "data.unknown_area", {v.from});
			}
		}
		for (const Area::NoticeRule& rule : area.arriveNotices) {
			for (const std::string& from : rule.from) {
				if (!data.area(from)) {
					at(rule.line, "data.unknown_area", {from});
				}
			}
		}
		for (const std::vector<std::string>* list : {&area.arrivalFrom, &area.arrivalNotFrom}) {
			for (const std::string& from : *list) {
				if (!data.area(from)) {
					at(0, "data.unknown_area", {from});
				}
			}
		}
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
		if (const std::optional<DungeonExitTarget>& way = area.returnTo) {
			const Area* to = data.area(way->area);
			if (!to) {
				at(way->line, "data.return_unknown_area", {way->area});
			} else if (to == &area) {
				at(way->line, "data.return_into_own_area", {});
			} else if (!way->marker.empty() && !to->markers.count(way->marker)) {
				at(way->line, "data.return_missing_marker", {way->area, way->marker});
			} else if (const Cell cell = way->marker.empty() ? way->at : to->markers.at(way->marker); !to->walkable(cell)) {
				at(way->line, "data.return_not_walkable", {way->area, "(" + std::to_string(cell.x) + "," + std::to_string(cell.y) + ")"});
			}
		}
		if (const std::optional<DungeonExitTarget>& way = area.dungeonExit) {
			const Area* to = data.area(way->area);
			if (!to) {
				at(way->line, "data.dungeon_exit_unknown_area", {way->area});
			} else if (to->kind == AreaKind::Dungeon) {
				at(way->line, "data.dungeon_exit_into_dungeon", {way->area});
			} else if (!way->marker.empty() && !to->markers.count(way->marker)) {
				at(way->line, "data.dungeon_exit_missing_marker", {way->area, way->marker});
			} else if (way->marker.empty() && !to->walkable(way->at)) {
				at(way->line, "data.dungeon_exit_not_walkable", {way->area, "(" + std::to_string(way->at.x) + "," + std::to_string(way->at.y) + ")"});
			}
		}
		for (const Platform& platform : area.platforms) {
			flagAt(platform.line, platform.requireFlag);
			flagAt(platform.line, platform.blockFlag);
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
			if (h.actorAt && std::find(c.actors.begin(), c.actors.end(), h.id) == c.actors.end()) {
				at(h.line, "data.actor_at_not_actor", {h.id});
			}
			if (h.trigger == HookTrigger::Touch && std::find(c.actors.begin(), c.actors.end(), h.actor) == c.actors.end()) {
				at(h.line, "data.touch_unknown_actor", {h.id, h.actor});
			}
			const bool actorHook = h.trigger == HookTrigger::Interact && std::find(c.actors.begin(), c.actors.end(), h.id) != c.actors.end();
			if (h.talkAt && !actorHook) {
				at(h.line, "data.talk_at_interact_hooks_only", {h.id});
			}
			if (h.turns && h.trigger == HookTrigger::Interact && !actorHook) {
				at(h.line, "data.turns_actor_hooks_only", {h.id});
			}
		}
		const auto keeperKnown = [&](int line, const std::string& point, const std::string& keeper) {
			if (!keeper.empty() && std::find(c.actors.begin(), c.actors.end(), keeper) == c.actors.end()) {
				at(line, "data.keeper_not_actor", {point, keeper});
			}
		};
		const auto voiceUsed = [&](int line, const std::string& point, const std::string& voice) {
			const auto has = [&](const char* line) { return data.languages.front().text.count(voice + line) > 0; };
			if (!voice.empty() && !has(".welcome") && !has(".confirm") && !has(".thanks") && !has(".cannot_afford")) {
				at(line, "data.voice_unused", {point, voice});
			}
		};
		for (const Exit& e : area.exits) {
			if (!e.sound.empty() && std::find(shown.audio.begin(), shown.audio.end(), e.sound) == shown.audio.end()) {
				at(e.line, "data.exit_sound_not_audio", {e.sound});
			}
		}
		for (const RestPoint& r : area.rests) {
			keeperKnown(r.line, r.id, r.keeper);
			voiceUsed(r.line, r.id, r.voice);
			if (!r.name.empty()) {
				textExists(c.languages.front().text, r.name);
			}
		}
		for (const Shop& s : area.shops) {
			keeperKnown(s.line, s.id, s.keeper);
			voiceUsed(s.line, s.id, s.voice);
			if (!s.name.empty()) {
				textExists(c.languages.front().text, s.name);
			}
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
		if (in.op == events::Op::register_town && !registrableTown(in.name)) {
			problems.add({data.program.files[in.file], in.source_line, "", {"data.town_not_registrable", {in.name}}});
		}
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
	std::uint64_t hash = hashJson(1469598103934665603ULL, *json.value, {"credits", "presentation"});

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

std::string visibleEnemyActor(const std::string& zone, int n) {
	return "enemy." + zone + "." + std::to_string(n);
}

bool parseVisibleEnemyActor(const std::string& actor, std::string& zone, int& n) {
	const std::size_t dot = actor.rfind('.');
	if (actor.rfind("enemy.", 0) != 0 || dot == std::string::npos || dot <= 6 || dot + 1 >= actor.size() || dot + 4 < actor.size() ||
		actor.find_first_not_of("0123456789", dot + 1) != std::string::npos) {
		return false;
	}
	zone = actor.substr(6, dot - 6);
	n = std::stoi(actor.substr(dot + 1));
	return n >= 1;
}

std::string registryText(const events::Registry& registry) {
	std::string out = "# Built from the game data by game_cli --registry.\n";
	const std::pair<events::NameKind, const char*> kinds[] = {
		{events::NameKind::actor, "actor"}, {events::NameKind::marker, "marker"}, {events::NameKind::camera, "camera"},
		{events::NameKind::layout, "layout"}, {events::NameKind::audio, "audio"}, {events::NameKind::animation, "animation"},
		{events::NameKind::portrait, "portrait"}, {events::NameKind::item, "item"}, {events::NameKind::member, "member"},
		{events::NameKind::encounter, "encounter"}, {events::NameKind::map, "map"}, {events::NameKind::shop, "shop"},
		{events::NameKind::rest_point, "rest"}, {events::NameKind::enemy, "enemy"}, {events::NameKind::vehicle, "vehicle"}, {events::NameKind::ability, "ability"},
		{events::NameKind::credits, "credits"}, {events::NameKind::picture, "picture"},
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
