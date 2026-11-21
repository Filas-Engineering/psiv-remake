#include "psiv_session.h"

#include "game/data.h"
#include "game/save.h"
#include "game/session.h"

#include "events/glossary.h"
#include "events/strings.h"
#include "rules/event.h"
#include "rules/names.h"
#include "rules/progression.h"
#include "rules/record.h"
#include "rules/text.h"
#include "rules/version.h"

#include <algorithm>
#include <cstddef>
#include <ctime>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#define PSIV_BINDING_VERSION "0.9.0"

static_assert(PSIV_MODE_SHOP == static_cast<int>(game::Mode::Shop), "psiv_mode");
static_assert(PSIV_MENU_PORT == static_cast<int>(game::MenuKind::Port), "psiv_menu_kind");
static_assert(PSIV_TRAVEL_DUNGEON_EXIT == static_cast<int>(game::TravelKind::DungeonExit), "psiv_travel_kind");
static_assert(PSIV_DIRECTION_WEST == static_cast<int>(game::Facing::West), "psiv_direction");
static_assert(PSIV_NOTICE_AMBIENT == static_cast<int>(game::OutputKind::Ambient), "psiv_notice_kind");
static_assert(PSIV_AREA_DUNGEON == static_cast<int>(game::AreaKind::Dungeon), "psiv_area_kind");
static_assert(PSIV_ENCOUNTERS_RANDOM == static_cast<int>(game::EncounterMode::Random), "psiv_encounter_mode");
static_assert(PSIV_ENEMY_RETURNING == static_cast<int>(game::EnemyState::Returning), "psiv_enemy_state");
static_assert(PSIV_JOB_COMPLETED == static_cast<int>(game::JobState::Completed), "psiv_job_state");
static_assert(PSIV_TEXT_INSTANT == static_cast<int>(game::TextSpeed::Instant), "psiv_text_speed");
static_assert(PSIV_BATTLE_X3 == static_cast<int>(game::BattleSpeed::X3), "psiv_battle_speed");
static_assert(PSIV_REWARD_X2 == static_cast<int>(rules::RewardMultiplier::X2), "psiv_reward_multiplier");
static_assert(PSIV_BATTLE_EVENT == static_cast<int>(game::BattleSource::Event), "psiv_battle_source");
static_assert(PSIV_SIDE_ENEMY == static_cast<int>(rules::Side::Enemy), "psiv_side");
static_assert(PSIV_COMMAND_ESCAPE == static_cast<int>(rules::CommandKind::Escape), "psiv_command_kind");
static_assert(PSIV_ABILITY_COMBO == static_cast<int>(rules::AbilityKind::Combo), "psiv_ability_kind");
static_assert(PSIV_TARGET_RANDOM_ENEMY == static_cast<int>(rules::Targeting::RandomEnemy), "psiv_targeting");
static_assert(PSIV_SLOT_ACCESSORY == static_cast<int>(rules::EquipSlot::Accessory), "psiv_equip_slot");
static_assert(PSIV_OUTCOME_SCRIPTED_END == static_cast<int>(rules::Outcome::ScriptedEnd), "psiv_outcome");
static_assert(PSIV_EVENT_BATTLE_ENDED == static_cast<int>(rules::EventType::BattleEnded), "psiv_event_type");
static_assert(PSIV_CANCEL_NO_TARGET == static_cast<int>(rules::CancelReason::NoTarget), "psiv_cancel_reason");
static_assert(PSIV_EFFECT_IMMUNE == static_cast<int>(rules::Effectiveness::Immune), "psiv_effectiveness");
static_assert(PSIV_STATUS_END_DEFEATED == static_cast<int>(rules::StatusEnd::Defeated), "psiv_status_end");
static_assert(PSIV_RESOURCE_ITEM == static_cast<int>(rules::ResourceKind::Item), "psiv_resource_kind");
static_assert(PSIV_ELEMENT_GALE == static_cast<int>(rules::Element::Gale), "psiv_element");
static_assert(PSIV_STAT_DEFENSE == static_cast<int>(rules::Stat::Defense), "psiv_stat");
static_assert(PSIV_OPENING_ENEMY_FIRST == static_cast<int>(rules::Opening::EnemyFirst), "psiv_opening");
static_assert(PSIV_AUTO_STOP_BATTLE_ENDED == static_cast<int>(rules::AutoStop::BattleEnded), "psiv_auto_stop");
static_assert(PSIV_MACRO_SKIPPED == static_cast<int>(rules::MacroStepStatus::Skipped), "psiv_macro_step_status");
static_assert(PSIV_ISSUE_NO_TARGET == static_cast<int>(rules::MacroIssue::NoTarget), "psiv_macro_issue");
static_assert(PSIV_MACRO_TARGET_ALL == static_cast<int>(rules::MacroTargetKind::All), "psiv_macro_target_kind");
static_assert(PSIV_ACTION_WARP == static_cast<int>(events::ActionType::warp), "psiv_action_type");
static_assert(PSIV_ACTION_CANCEL == static_cast<int>(game::ActionPhase::Cancel), "psiv_action_phase");
static_assert(PSIV_PRESENTATION_WAIT == static_cast<int>(game::Presentation::Queued), "psiv_presentation");
static_assert(PSIV_WALK_TOO_LONG == static_cast<int>(game::WalkStatus::TooLong), "psiv_walk_status");
static_assert(PSIV_CAMERA_LEADER == static_cast<int>(game::CameraTarget::Leader), "psiv_camera_target");
static_assert(PSIV_SORT_COUNT == static_cast<int>(game::ItemSort::Count), "psiv_item_sort");
static_assert(PSIV_DESCRIPTION_DIFFICULTY == static_cast<int>(game::DescriptionKind::Difficulty), "psiv_description_kind");
static_assert(PSIV_AREA_DUNGEON == static_cast<int>(game::AreaKind::Dungeon), "psiv_area_kind");
static_assert(PSIV_DEV_CHARACTERS == static_cast<int>(game::DevList::Characters), "psiv_dev_list");

namespace {

// Storage for everything a query hands out. Cleared by MUTATES functions, so pointers stay put in between.
class Arena {
public:
	const char* str(std::string text) {
		strings_.push_back(std::move(text));
		return strings_.back().c_str();
	}

	template <class T>
	T* array(std::size_t count) {
		std::shared_ptr<T> block(new T[count == 0 ? 1 : count](), std::default_delete<T[]>());
		blocks_.push_back(block);
		return block.get();
	}

	const char* const* strs(const std::vector<std::string>& values) {
		const char** out = array<const char*>(values.size());
		for (std::size_t i = 0; i < values.size(); ++i) {
			out[i] = str(values[i]);
		}
		return out;
	}

	void clear() {
		strings_.clear();
		blocks_.clear();
	}

private:
	std::deque<std::string> strings_;
	std::vector<std::shared_ptr<void>> blocks_;
};

struct ErrorRecord {
	psiv_status status = PSIV_OK;
	std::string key;
	std::vector<std::string> params;
	std::string text;
	std::vector<const char*> paramPointers;

	void set(psiv_status s, std::string k, std::vector<std::string> p, std::string t) {
		status = s;
		key = std::move(k);
		params = std::move(p);
		text = std::move(t);
		paramPointers.clear();
		for (const std::string& param : params) {
			paramPointers.push_back(param.c_str());
		}
	}
};

thread_local ErrorRecord createError;

struct ProblemRecords {
	std::vector<rules::Problem> problems;
	std::vector<std::string> texts;
	std::vector<std::vector<const char*>> params;
	std::vector<psiv_problem> views;

	void set(const std::vector<rules::Problem>& list) {
		problems = list;
		texts.clear();
		params.clear();
		views.clear();
		for (const rules::Problem& p : problems) {
			texts.push_back(rules::formatProblem(p, game::problemText()));
			std::vector<const char*> pointers;
			for (const std::string& param : p.message.params) {
				pointers.push_back(param.c_str());
			}
			params.push_back(std::move(pointers));
		}
		for (std::size_t i = 0; i < problems.size(); ++i) {
			const rules::Problem& p = problems[i];
			views.push_back({p.file.c_str(), p.line, p.entry.c_str(), p.message.key.c_str(), static_cast<int32_t>(p.message.params.size()),
				params[i].empty() ? nullptr : params[i].data(), texts[i].c_str()});
		}
	}
};

thread_local ProblemRecords createProblems;

void fillAction(const game::PresentedAction& r, psiv_action& out, Arena& arena) {
	out = psiv_action();
	out.handle = r.handle;
	out.phase = static_cast<int32_t>(r.phase);
	out.type = static_cast<int32_t>(r.action.type);
	out.subject = arena.str(r.action.subject);
	out.target = arena.str(r.action.target);
	out.slot = arena.str(r.action.slot);
	out.has_position = r.action.has_position;
	out.x = r.action.position.x;
	out.y = r.action.position.y;
	out.z = r.action.position.z;
	out.seconds = r.action.seconds;
	out.speed = r.action.speed;
	out.strength = r.action.strength;
	out.loop = r.action.loop;
	out.instant = r.action.instant;
	out.description = arena.str(r.description);
	out.duration = r.seconds;
}

std::string str(const char* text) {
	return text ? std::string(text) : std::string();
}

}

struct psiv_session {
	std::unique_ptr<game::GameData> data;
	std::unique_ptr<game::Session> session;
	// What psiv_session_reload builds the next data and session from.
	game::DataDirectories directories;
	game::SessionOptions options;
	Arena arena;
	ErrorRecord error;
	psiv_action_callback callback = nullptr;
	void* user = nullptr;
	std::vector<rules::Event> lastEvents;
	std::optional<game::PathWalk> walk;
};

namespace {

using Session = psiv_session;

psiv_status fail(Session* s, psiv_status status, std::string key, std::vector<std::string> params, std::string text) {
	(s ? s->error : createError).set(status, std::move(key), std::move(params), std::move(text));
	return status;
}

psiv_status nullArgument(Session* s, const char* name) {
	return fail(s, PSIV_INVALID_ARGUMENT, "binding.null_argument", {name}, std::string("argument '") + name + "' is NULL");
}

psiv_status outOfRange(Session* s, const char* name, long long value) {
	return fail(s, PSIV_INVALID_ARGUMENT, "binding.out_of_range", {name, std::to_string(value)}, std::string("argument '") + name + "' is out of range: " + std::to_string(value));
}

psiv_status notFound(Session* s, const char* what) {
	return fail(s, PSIV_NOT_FOUND, std::string("binding.no_") + what, {}, std::string("there is no ") + what + " now");
}

psiv_status refused(Session* s, const rules::Message& message) {
	if (message.empty()) {
		return PSIV_OK;
	}
	return fail(s, PSIV_REFUSED, message.key, message.params, s->session->render(message));
}

psiv_status bindingRefusal(Session* s, const char* key, const std::string& text) {
	return fail(s, PSIV_REFUSED, std::string("binding.") + key, {}, text);
}

// Every entry point runs inside this: no exception crosses the C boundary. A MUTATES function clears the
// arena first, so inputs must already be copied out of any pointer the library handed out.
template <class F>
psiv_status guard(Session* s, bool mutates, F&& body) {
	if (!s) {
		return fail(nullptr, PSIV_INVALID_ARGUMENT, "binding.null_argument", {"session"}, "argument 'session' is NULL");
	}
	try {
		if (mutates) {
			s->arena.clear();
		}
		return body();
	} catch (const std::bad_alloc&) {
		return fail(s, PSIV_INTERNAL_ERROR, "binding.out_of_memory", {}, "out of memory");
	} catch (const std::exception& e) {
		return fail(s, PSIV_INTERNAL_ERROR, "binding.internal", {e.what()}, std::string("internal error: ") + e.what());
	} catch (...) {
		return fail(s, PSIV_INTERNAL_ERROR, "binding.internal", {"unknown"}, "internal error");
	}
}

bool validDirection(int32_t direction) {
	return direction >= PSIV_DIRECTION_NONE && direction <= PSIV_DIRECTION_WEST;
}

// The battle combatant and event reads describe: the live one, else the ended one the game keeps.
const rules::Battle* shownBattle(const Session& s) {
	if (const rules::Battle* b = s.session->battle()) {
		return b;
	}
	const game::EndedBattle* ended = s.session->endedBattle();
	return ended ? &ended->battle : nullptr;
}

// Combatant names, with A, B, ... added when several enemies share one.
std::vector<std::string> labels(const rules::Battle* b) {
	std::vector<std::string> out;
	if (!b) {
		return out;
	}
	std::map<std::string, int> total;
	std::map<std::string, int> seen;
	for (const rules::Combatant& c : b->combatants()) {
		total[c.name]++;
	}
	for (int i = 0; i < static_cast<int>(b->combatants().size()); ++i) {
		const rules::Combatant& c = b->combatant(i);
		std::string name = c.name;
		if (!b->isParty(i) && total[c.name] > 1) {
			name += " " + std::string(1, static_cast<char>('A' + seen[c.name]++));
		}
		out.push_back(std::move(name));
	}
	return out;
}

std::string label(const std::vector<std::string>& names, int index) {
	return index >= 0 && index < static_cast<int>(names.size()) ? names[index] : std::string("?");
}

void fillStatuses(Session& s, const std::vector<rules::ActiveStatus>& statuses, int32_t& count, const psiv_active_status*& out) {
	psiv_active_status* list = s.arena.array<psiv_active_status>(statuses.size());
	for (std::size_t i = 0; i < statuses.size(); ++i) {
		list[i].id = s.arena.str(statuses[i].id);
		list[i].name = s.arena.str(s.session->displayName(statuses[i].id));
		list[i].remaining = statuses[i].remaining;
	}
	count = static_cast<int32_t>(statuses.size());
	out = list;
}

std::string eventText(const Session& s, const std::vector<std::string>& names, const rules::Event& e) {
	const rules::TextTable& table = s.session->language().text;
	auto name = [&](const std::string& id) { return s.session->displayName(id); };
	switch (e.type) {
	case rules::EventType::ActionCancelled:
		return rules::formatText(table, std::string("cancel.") + rules::toString(e.cancel),
			{label(names, e.actor), e.cancel == rules::CancelReason::ActorUnable ? name(e.status) : name(e.ability)});
	case rules::EventType::StatusApplied:
	case rules::EventType::StatusResisted:
	case rules::EventType::StatusRemoved:
		return rules::formatText(table, e.message, {label(names, e.target), name(e.status)});
	case rules::EventType::Message:
		return s.session->render({e.message, e.params});
	default:
		return "";
	}
}

void fillEvent(Session& s, const std::vector<std::string>& names, const rules::Event& e, psiv_battle_event& out) {
	Arena& a = s.arena;
	out = psiv_battle_event();
	out.type = static_cast<int32_t>(e.type);
	out.round = e.round;
	out.actor = e.actor;
	out.target = e.target;
	out.previous_target = e.previousTarget;
	out.ability = a.str(e.ability);
	out.combo = a.str(e.combo);
	out.status = a.str(e.status);
	out.message = a.str(e.message);
	out.param_count = static_cast<int32_t>(e.params.size());
	out.params = a.strs(e.params);
	out.text = a.str(eventText(s, names, e));
	out.amount = e.amount;
	out.hp = e.hp;
	out.duration = e.duration;
	out.command = static_cast<int32_t>(e.command);
	out.element = static_cast<int32_t>(e.element);
	out.effectiveness = static_cast<int32_t>(e.effectiveness);
	out.stat = static_cast<int32_t>(e.stat);
	out.cancel = static_cast<int32_t>(e.cancel);
	out.status_end = static_cast<int32_t>(e.statusEnd);
	out.outcome = static_cast<int32_t>(e.outcome);
	out.resource = static_cast<int32_t>(e.resource);
	out.opening = static_cast<int32_t>(e.opening);
	out.succeeded = e.succeeded;
	out.game_over = e.gameOver;
	out.experience = e.experience;
	out.currency = e.currency;
	psiv_item_count* items = a.array<psiv_item_count>(e.items.size());
	int n = 0;
	for (const auto& [id, count] : e.items) {
		items[n].id = a.str(id);
		items[n].count = count;
		++n;
	}
	out.item_count = n;
	out.items = items;
	out.macro = a.str(e.macro);
	int32_t* participants = a.array<int32_t>(e.participants.size());
	std::copy(e.participants.begin(), e.participants.end(), participants);
	out.participant_count = static_cast<int32_t>(e.participants.size());
	out.participants = participants;
	int32_t* order = a.array<int32_t>(e.order.size());
	std::copy(e.order.begin(), e.order.end(), order);
	out.order_count = static_cast<int32_t>(e.order.size());
	out.order = order;
	out.id_count = static_cast<int32_t>(e.ids.size());
	out.ids = a.strs(e.ids);
	std::vector<std::string> groups = e.groups;
	groups.resize(e.ids.size());
	out.groups = a.strs(groups);
	out.party_count = e.partyCount;
	out.escape_allowed = e.flags.escapeAllowed;
	out.defeat_has_branch = e.flags.defeatHasBranch;
	out.vehicle_battle = e.flags.vehicleBattle;
	psiv_recorded_command* commands = a.array<psiv_recorded_command>(e.commands.size());
	for (std::size_t i = 0; i < e.commands.size(); ++i) {
		commands[i].actor = e.commands[i].actor;
		commands[i].kind = static_cast<int32_t>(e.commands[i].command.kind);
		commands[i].id = a.str(e.commands[i].command.id);
		commands[i].target = e.commands[i].command.target;
	}
	out.command_count = static_cast<int32_t>(e.commands.size());
	out.commands = commands;
}

const psiv_battle_event* fillEvents(Session& s, const std::vector<rules::Event>& events, const std::vector<std::string>& names, int32_t* count) {
	psiv_battle_event* list = s.arena.array<psiv_battle_event>(events.size());
	for (std::size_t i = 0; i < events.size(); ++i) {
		fillEvent(s, names, events[i], list[i]);
	}
	*count = static_cast<int32_t>(events.size());
	return list;
}

const psiv_notice* fillNotices(Session& s, const std::vector<game::Output>& outputs, int32_t* count) {
	psiv_notice* list = s.arena.array<psiv_notice>(outputs.size());
	for (std::size_t i = 0; i < outputs.size(); ++i) {
		list[i].kind = static_cast<int32_t>(outputs[i].kind);
		list[i].key = s.arena.str(outputs[i].message.key);
		list[i].param_count = static_cast<int32_t>(outputs[i].message.params.size());
		list[i].params = s.arena.strs(outputs[i].message.params);
		list[i].text = s.arena.str(outputs[i].text);
	}
	*count = static_cast<int32_t>(outputs.size());
	return list;
}

// A new game, load or the title leaves nothing of the last round or walk.
void forgetRun(Session& s) {
	s.lastEvents.clear();
	s.walk.reset();
}

const game::Area* currentArea(Session* s) {
	return s->session->area();
}

const char* fromList(const char* const* names, int count, int32_t value) {
	return value >= 0 && value < count ? names[value] : "";
}

}

namespace {

// The travel kind a travel effect's target goes with, or -1.
int32_t travelKind(const std::vector<rules::Effect>& effects) {
	const rules::Effect* travel = rules::travelEffect(effects);
	if (!travel) {
		return -1;
	}
	return travel->travel == rules::TravelTarget::Town ? PSIV_TRAVEL_TOWN : PSIV_TRAVEL_DUNGEON_EXIT;
}

psiv_inventory_entry entryFor(Session& s, const std::string& id, int count) {
	const rules::Database& db = s.data->db;
	psiv_inventory_entry e = {};
	e.id = s.arena.str(id);
	e.name = s.arena.str(s.session->displayName(id));
	e.count = count;
	if (const rules::ItemDef* item = db.item(id)) {
		e.equipment = 0;
		e.field = item->field;
		e.targeting_or_slot = static_cast<int32_t>(item->targeting);
	} else if (const rules::EquipmentDef* piece = db.equipmentPiece(id)) {
		e.equipment = 1;
		e.targeting_or_slot = static_cast<int32_t>(piece->slot);
	}
	e.price = s.data->price(id);
	e.travel = db.item(id) ? travelKind(db.item(id)->effects) : -1;
	return e;
}

}

extern "C" {

uint32_t psiv_abi_version(void) {
	return PSIV_ABI_VERSION;
}

const char* psiv_version(void) {
	static const std::string version = std::string("psiv-bindings " PSIV_BINDING_VERSION " (game ") + game::kGameBuild + ", rules " + std::to_string(rules::kRulesVersion) + ")";
	return version.c_str();
}

uint32_t psiv_sizeof(const char* struct_name) {
#define PSIV_SIZE(name) {#name, static_cast<uint32_t>(sizeof(name))}
	static const std::map<std::string, uint32_t> sizes = {
		PSIV_SIZE(psiv_ability_info), PSIV_SIZE(psiv_action), PSIV_SIZE(psiv_actor), PSIV_SIZE(psiv_active_status), PSIV_SIZE(psiv_battle_event),
		PSIV_SIZE(psiv_battle_info), PSIV_SIZE(psiv_battle_option), PSIV_SIZE(psiv_choice), PSIV_SIZE(psiv_codex_entry),
		PSIV_SIZE(psiv_combatant), PSIV_SIZE(psiv_enemy), PSIV_SIZE(psiv_error), PSIV_SIZE(psiv_exit), PSIV_SIZE(psiv_field_info),
		PSIV_SIZE(psiv_game_info), PSIV_SIZE(psiv_glossary_entry), PSIV_SIZE(psiv_inn_info), PSIV_SIZE(psiv_inventory_entry),
		PSIV_SIZE(psiv_item_count), PSIV_SIZE(psiv_job), PSIV_SIZE(psiv_line), PSIV_SIZE(psiv_macro_step_result), PSIV_SIZE(psiv_marker),
		PSIV_SIZE(psiv_member), PSIV_SIZE(psiv_notice), PSIV_SIZE(psiv_point), PSIV_SIZE(psiv_profile), PSIV_SIZE(psiv_quest_log),
		PSIV_SIZE(psiv_recorded_command), PSIV_SIZE(psiv_save_summary), PSIV_SIZE(psiv_session_options), PSIV_SIZE(psiv_settings),
		PSIV_SIZE(psiv_shop_info), PSIV_SIZE(psiv_world_info), PSIV_SIZE(psiv_problem), PSIV_SIZE(psiv_level_up), PSIV_SIZE(psiv_battle_result),
		PSIV_SIZE(psiv_auto_round), PSIV_SIZE(psiv_auto_battle), PSIV_SIZE(psiv_pacing_area), PSIV_SIZE(psiv_pacing_boss),
		PSIV_SIZE(psiv_macro_step), PSIV_SIZE(psiv_macro), PSIV_SIZE(psiv_stat_bonus), PSIV_SIZE(psiv_affinity), PSIV_SIZE(psiv_status_resist),
		PSIV_SIZE(psiv_member_detail), PSIV_SIZE(psiv_combo_entry), PSIV_SIZE(psiv_combat_view), PSIV_SIZE(psiv_equip_preview), PSIV_SIZE(psiv_difficulty), PSIV_SIZE(psiv_camera),
		PSIV_SIZE(psiv_vehicle), PSIV_SIZE(psiv_travel_destination), PSIV_SIZE(psiv_event_position), PSIV_SIZE(psiv_zone),
	};
#undef PSIV_SIZE
	if (!struct_name) {
		return 0;
	}
	auto it = sizes.find(struct_name);
	return it == sizes.end() ? 0 : it->second;
}

psiv_status psiv_last_error(const psiv_session* session, psiv_error* out) {
	if (!out) {
		return PSIV_INVALID_ARGUMENT;
	}
	const ErrorRecord& record = session ? session->error : createError;
	out->status = record.status;
	out->key = record.key.c_str();
	out->param_count = static_cast<int32_t>(record.params.size());
	out->params = record.paramPointers.empty() ? nullptr : record.paramPointers.data();
	out->text = record.text.c_str();
	return record.status == PSIV_OK ? PSIV_NOT_FOUND : PSIV_OK;
}

const char* psiv_name_of(int32_t kind, int32_t value) {
	static const char* const pointKinds[] = {"talk", "step", "chest", "shop", "rest", "port"};
	switch (kind) {
	case PSIV_NAME_MODE:
		return value >= 0 && value <= PSIV_MODE_SHOP ? game::toString(static_cast<game::Mode>(value)) : "";
	case PSIV_NAME_MENU_KIND:
		return value >= 0 && value <= PSIV_MENU_PORT ? game::toString(static_cast<game::MenuKind>(value)) : "";
	case PSIV_NAME_DIRECTION:
		return value == PSIV_DIRECTION_NONE ? "none" : value >= 0 && value <= PSIV_DIRECTION_WEST ? game::toString(static_cast<game::Facing>(value)) : "";
	case PSIV_NAME_NOTICE_KIND:
		return value >= 0 && value <= PSIV_NOTICE_AMBIENT ? game::toString(static_cast<game::OutputKind>(value)) : "";
	case PSIV_NAME_AREA_KIND:
		return value >= 0 && value <= PSIV_AREA_DUNGEON ? game::toString(static_cast<game::AreaKind>(value)) : "";
	case PSIV_NAME_ENCOUNTER_MODE:
		return value >= 0 && value <= PSIV_ENCOUNTERS_RANDOM ? game::toString(static_cast<game::EncounterMode>(value)) : "";
	case PSIV_NAME_ENEMY_STATE:
		return value >= 0 && value <= PSIV_ENEMY_RETURNING ? game::toString(static_cast<game::EnemyState>(value)) : "";
	case PSIV_NAME_POINT_KIND:
		return fromList(pointKinds, 6, value);
	case PSIV_NAME_JOB_STATE:
		return value >= 0 && value <= PSIV_JOB_MISSED ? game::toString(static_cast<game::JobState>(value)) : "";
	case PSIV_NAME_TEXT_SPEED:
		return value >= 0 && value <= PSIV_TEXT_INSTANT ? game::toString(static_cast<game::TextSpeed>(value)) : "";
	case PSIV_NAME_BATTLE_SPEED:
		return value >= 0 && value <= PSIV_BATTLE_X3 ? game::toString(static_cast<game::BattleSpeed>(value)) : "";
	case PSIV_NAME_REWARD_MULTIPLIER:
		return value >= 0 && value <= PSIV_REWARD_X2 ? rules::toString(static_cast<rules::RewardMultiplier>(value)) : "";
	case PSIV_NAME_BATTLE_SOURCE:
		return value >= 0 && value <= PSIV_BATTLE_EVENT ? game::toString(static_cast<game::BattleSource>(value)) : "";
	case PSIV_NAME_SIDE:
		return value >= 0 && value <= PSIV_SIDE_ENEMY ? rules::toString(static_cast<rules::Side>(value)) : "";
	case PSIV_NAME_COMMAND_KIND:
		return value >= 0 && value <= PSIV_COMMAND_ESCAPE ? rules::toString(static_cast<rules::CommandKind>(value)) : "";
	case PSIV_NAME_ABILITY_KIND:
		return value >= 0 && value <= PSIV_ABILITY_COMBO ? rules::toString(static_cast<rules::AbilityKind>(value)) : "";
	case PSIV_NAME_TARGETING:
		return value >= 0 && value <= PSIV_TARGET_RANDOM_ENEMY ? rules::toString(static_cast<rules::Targeting>(value)) : "";
	case PSIV_NAME_EQUIP_SLOT:
		return value >= 0 && value <= PSIV_SLOT_ACCESSORY ? rules::toString(static_cast<rules::EquipSlot>(value)) : "";
	case PSIV_NAME_OUTCOME:
		return value >= 0 && value <= PSIV_OUTCOME_SCRIPTED_END ? rules::toString(static_cast<rules::Outcome>(value)) : "";
	case PSIV_NAME_EVENT_TYPE:
		return value >= 0 && value <= PSIV_EVENT_ENEMY_TRANSFORMED ? rules::toString(static_cast<rules::EventType>(value)) : "";
	case PSIV_NAME_CANCEL_REASON:
		return value >= 0 && value <= PSIV_CANCEL_NO_TARGET ? rules::toString(static_cast<rules::CancelReason>(value)) : "";
	case PSIV_NAME_EFFECTIVENESS:
		return value >= 0 && value <= PSIV_EFFECT_IMMUNE ? rules::toString(static_cast<rules::Effectiveness>(value)) : "";
	case PSIV_NAME_STATUS_END:
		return value >= 0 && value <= PSIV_STATUS_END_DEFEATED ? rules::toString(static_cast<rules::StatusEnd>(value)) : "";
	case PSIV_NAME_RESOURCE_KIND:
		return value >= 0 && value <= PSIV_RESOURCE_ITEM ? rules::toString(static_cast<rules::ResourceKind>(value)) : "";
	case PSIV_NAME_ELEMENT:
		return value >= 0 && value <= PSIV_ELEMENT_HOLY ? rules::toString(static_cast<rules::Element>(value)) : "";
	case PSIV_NAME_STAT:
		return value >= 0 && value <= PSIV_STAT_DEFENSE ? rules::toString(static_cast<rules::Stat>(value)) : "";
	case PSIV_NAME_OPENING:
		return value >= 0 && value <= PSIV_OPENING_ENEMY_FIRST ? rules::toString(static_cast<rules::Opening>(value)) : "";
	case PSIV_NAME_AUTO_STOP:
		return value >= 0 && value <= PSIV_AUTO_STOP_BATTLE_ENDED ? rules::toString(static_cast<rules::AutoStop>(value)) : "";
	case PSIV_NAME_MACRO_STEP_STATUS:
		return value >= 0 && value <= PSIV_MACRO_SKIPPED ? rules::toString(static_cast<rules::MacroStepStatus>(value)) : "";
	case PSIV_NAME_MACRO_ISSUE:
		return value >= 0 && value <= PSIV_ISSUE_NO_TARGET ? rules::toString(static_cast<rules::MacroIssue>(value)) : "";
	case PSIV_NAME_ACTION_TYPE:
		return value >= 0 && value <= PSIV_ACTION_WARP ? events::action_type_name(static_cast<events::ActionType>(value)) : "";
	case PSIV_NAME_MACRO_TARGET_KIND:
		return value >= 0 && value <= PSIV_MACRO_TARGET_ALL ? rules::toString(static_cast<rules::MacroTargetKind>(value)) : "";
	case PSIV_NAME_CAMERA_TARGET:
		return value >= 0 && value <= PSIV_CAMERA_LEADER ? game::toString(static_cast<game::CameraTarget>(value)) : "";
	case PSIV_NAME_ITEM_SORT:
		return value >= 0 && value <= PSIV_SORT_COUNT ? game::toString(static_cast<game::ItemSort>(value)) : "";
	case PSIV_NAME_DESCRIPTION_KIND:
		return value >= 0 && value <= PSIV_DESCRIPTION_DIFFICULTY ? game::toString(static_cast<game::DescriptionKind>(value)) : "";
	case PSIV_NAME_TRAVEL_KIND:
		return value >= 0 && value <= PSIV_TRAVEL_DUNGEON_EXIT ? game::toString(static_cast<game::TravelKind>(value)) : "";
	case PSIV_NAME_DEV_LIST:
		return value >= 0 && value <= PSIV_DEV_CHARACTERS ? game::toString(static_cast<game::DevList>(value)) : "";
	case PSIV_NAME_CREATURE_KIND:
		return value >= 0 && value <= PSIV_CREATURE_MACHINE ? rules::toString(static_cast<rules::CreatureKind>(value)) : "";
	default:
		return "";
	}
}

psiv_status psiv_parse_name(int32_t kind, const char* text, int32_t* out) {
	if (!text || !out) {
		return PSIV_INVALID_ARGUMENT;
	}
	const std::string name = text;
	if (kind == PSIV_NAME_REWARD_MULTIPLIER) {
		rules::RewardMultiplier value;
		if (rules::parseRewardMultiplier(name, value)) {
			*out = static_cast<int32_t>(value);
			return PSIV_OK;
		}
		return PSIV_NOT_FOUND;
	}
	if (kind == PSIV_NAME_EVENT_TYPE) {
		rules::EventType value;
		if (rules::parseEventType(name, value)) {
			*out = static_cast<int32_t>(value);
			return PSIV_OK;
		}
		return PSIV_NOT_FOUND;
	}
	for (int32_t value = kind == PSIV_NAME_DIRECTION ? PSIV_DIRECTION_NONE : 0; value < 64; ++value) {
		const char* candidate = psiv_name_of(kind, value);
		if (!*candidate && value >= 0) {
			break;
		}
		if (name == candidate) {
			*out = value;
			return PSIV_OK;
		}
	}
	return PSIV_NOT_FOUND;
}

psiv_status psiv_data_problems(const psiv_problem** out, int32_t* count) {
	if (!out || !count) {
		return PSIV_INVALID_ARGUMENT;
	}
	*out = createProblems.views.empty() ? nullptr : createProblems.views.data();
	*count = static_cast<int32_t>(createProblems.views.size());
	return PSIV_OK;
}

void psiv_session_options_init(psiv_session_options* options) {
	if (!options) {
		return;
	}
	*options = psiv_session_options();
	options->struct_size = sizeof(psiv_session_options);
	options->seed = 1;
	options->encounter_mode = -1;
	options->presentation = PSIV_PRESENTATION_INSTANT;
}

psiv_status psiv_session_create(const psiv_session_options* options, psiv_session** out) {
	if (!out) {
		return nullArgument(nullptr, "out");
	}
	*out = nullptr;
	if (!options) {
		return nullArgument(nullptr, "options");
	}
	if (options->struct_size < sizeof(psiv_session_options)) {
		return fail(nullptr, PSIV_INVALID_ARGUMENT, "binding.struct_size", {std::to_string(options->struct_size)}, "psiv_session_options.struct_size is too small; call psiv_session_options_init");
	}
	if (!options->data_dir || !*options->data_dir) {
		return nullArgument(nullptr, "options->data_dir");
	}
	if (options->encounter_mode < -1 || options->encounter_mode > PSIV_ENCOUNTERS_RANDOM) {
		return outOfRange(nullptr, "options->encounter_mode", options->encounter_mode);
	}
	if (options->presentation != PSIV_PRESENTATION_INSTANT && options->presentation != PSIV_PRESENTATION_WAIT) {
		return outOfRange(nullptr, "options->presentation", options->presentation);
	}
	try {
		createProblems.set({});
		rules::Result<game::GameData> loaded = game::loadGameData(game::DataDirectories{options->data_dir, str(options->core_data_dir)});
		if (!loaded.ok()) {
			createProblems.set(loaded.problems);
			return fail(nullptr, PSIV_DATA_ERROR, "binding.data_error", {loaded.error}, loaded.error);
		}
		auto s = std::make_unique<psiv_session>();
		s->data = std::make_unique<game::GameData>(std::move(*loaded.value));
		game::SessionOptions sessionOptions;
		sessionOptions.presentation = static_cast<game::Presentation>(options->presentation);
		psiv_session* raw = s.get();
		sessionOptions.onAction = [raw](const game::PresentedAction& action) {
			if (raw->callback) {
				Arena scratch;
				psiv_action view;
				fillAction(action, view, scratch);
				raw->callback(raw->user, &view);
			}
		};
		sessionOptions.saveDirectory = str(options->save_dir);
		sessionOptions.seed = options->seed;
		if (options->encounter_mode >= 0) {
			sessionOptions.encounterMode = static_cast<game::EncounterMode>(options->encounter_mode);
		}
		if (options->wall_clock) {
			sessionOptions.clock = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
		}
		s->directories = game::DataDirectories{options->data_dir, str(options->core_data_dir)};
		s->options = sessionOptions;
		s->session = std::make_unique<game::Session>(*s->data, std::move(sessionOptions));
		*out = s.release();
		return PSIV_OK;
	} catch (const std::bad_alloc&) {
		return fail(nullptr, PSIV_INTERNAL_ERROR, "binding.out_of_memory", {}, "out of memory");
	} catch (const std::exception& e) {
		return fail(nullptr, PSIV_INTERNAL_ERROR, "binding.internal", {e.what()}, std::string("internal error: ") + e.what());
	} catch (...) {
		return fail(nullptr, PSIV_INTERNAL_ERROR, "binding.internal", {"unknown"}, "internal error");
	}
}

void psiv_session_destroy(psiv_session* session) {
	if (!session) {
		return;
	}
	try {
		session->session.reset();
		delete session;
	} catch (...) {
	}
}

psiv_status psiv_game_info_get(psiv_session* session, psiv_game_info* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const game::GameConfig& c = session->data->config;
		*out = psiv_game_info();
		out->title = session->arena.str(c.title);
		out->game_build = game::kGameBuild;
		out->rules_version = rules::kRulesVersion;
		out->tick_ms = c.tickMs;
		out->round_ms = c.roundMs;
		out->manual_slots = c.manualSlots;
		out->autosave_slots = c.autosaveSlots;
		out->item_cap = c.itemCap;
		out->money_cap = c.moneyCap;
		std::vector<std::string> codes;
		for (const game::LanguageTables& l : session->data->languages) {
			codes.push_back(l.code);
		}
		out->language_count = static_cast<int32_t>(codes.size());
		out->languages = session->arena.strs(codes);
		out->title_music = session->arena.str(c.presentation.music.title);
		out->victory_music = session->arena.str(c.presentation.music.victory);
		out->defeat_music = session->arena.str(c.presentation.music.defeat);
		return PSIV_OK;
	});
}

// Profile and settings

psiv_status psiv_profile_get(psiv_session* session, psiv_profile* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const game::Profile& p = session->session->profile();
		*out = psiv_profile();
		out->text_speed = static_cast<int32_t>(p.textSpeed);
		out->auto_advance = p.autoAdvance;
		out->show_shipped_names = p.showShippedNames;
		out->text_language = session->arena.str(p.textLanguage);
		out->text_size_percent = p.textSizePercent;
		out->battle_speed = static_cast<int32_t>(p.battleSpeed);
		out->ambient_talk = p.ambientTalk;
		out->reduced_flash = p.reducedFlash;
		out->music_volume = p.musicVolume;
		out->ambient_volume = p.ambientVolume;
		out->effects_volume = p.effectsVolume;
		return PSIV_OK;
	});
}

psiv_status psiv_profile_set(psiv_session* session, const psiv_profile* profile) {
	const std::string language = profile ? str(profile->text_language) : std::string();
	const psiv_profile in = profile ? *profile : psiv_profile();
	return guard(session, true, [&] {
		if (!profile) {
			return nullArgument(session, "profile");
		}
		if (in.text_speed < 0 || in.text_speed > PSIV_TEXT_INSTANT) {
			return outOfRange(session, "profile->text_speed", in.text_speed);
		}
		if (in.battle_speed < 0 || in.battle_speed > PSIV_BATTLE_X3) {
			return outOfRange(session, "profile->battle_speed", in.battle_speed);
		}
		game::Profile p = session->session->profile();
		p.textSpeed = static_cast<game::TextSpeed>(in.text_speed);
		p.autoAdvance = in.auto_advance != 0;
		p.showShippedNames = in.show_shipped_names != 0;
		p.textLanguage = language;
		p.textSizePercent = in.text_size_percent;
		p.battleSpeed = static_cast<game::BattleSpeed>(in.battle_speed);
		p.ambientTalk = in.ambient_talk != 0;
		p.reducedFlash = in.reduced_flash != 0;
		p.musicVolume = in.music_volume;
		p.ambientVolume = in.ambient_volume;
		p.effectsVolume = in.effects_volume;
		return refused(session, session->session->setProfile(p));
	});
}

psiv_status psiv_profile_set_value(psiv_session* session, const char* key, const char* value) {
	const std::string k = str(key);
	const std::string v = str(value);
	return guard(session, true, [&] {
		if (!key) {
			return nullArgument(session, "key");
		}
		if (!value) {
			return nullArgument(session, "value");
		}
		game::Profile p = session->session->profile();
		const std::string problem = game::setProfileValue(p, k, v);
		if (!problem.empty()) {
			return fail(session, PSIV_REFUSED, "binding.profile_value", {k, v}, problem);
		}
		return refused(session, session->session->setProfile(p));
	});
}

psiv_status psiv_settings_get(psiv_session* session, psiv_settings* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const game::World& w = session->session->world();
		out->difficulty = session->arena.str(w.settings.difficulty);
		out->reward_multiplier = static_cast<int32_t>(w.settings.rewardMultiplier);
		out->auto_macro = session->arena.str(w.autoMacro);
		return PSIV_OK;
	});
}

psiv_status psiv_settings_set(psiv_session* session, const char* difficulty, int32_t reward_multiplier) {
	const std::string d = str(difficulty);
	return guard(session, true, [&] {
		if (!difficulty) {
			return nullArgument(session, "difficulty");
		}
		if (reward_multiplier < 0 || reward_multiplier > PSIV_REWARD_X2) {
			return outOfRange(session, "reward_multiplier", reward_multiplier);
		}
		game::Settings settings;
		settings.difficulty = d;
		settings.rewardMultiplier = static_cast<rules::RewardMultiplier>(reward_multiplier);
		return refused(session, session->session->setSettings(settings));
	});
}

psiv_status psiv_difficulties(psiv_session* session, const psiv_difficulty** out, int32_t* count, int32_t* can_change) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<game::DifficultyView> presets = session->session->difficulties();
		Arena& a = session->arena;
		psiv_difficulty* list = a.array<psiv_difficulty>(presets.size());
		for (std::size_t i = 0; i < presets.size(); ++i) {
			list[i] = {a.str(presets[i].id), a.str(presets[i].name), a.str(presets[i].description), presets[i].rewardPercent};
		}
		*out = list;
		*count = static_cast<int32_t>(presets.size());
		if (can_change) {
			*can_change = session->session->canChangeSettings();
		}
		return PSIV_OK;
	});
}

psiv_status psiv_settings_set_auto_macro(psiv_session* session, const char* macro) {
	const std::string name = str(macro);
	return guard(session, true, [&] {
		return refused(session, session->session->setAutoMacro(name));
	});
}

psiv_status psiv_macros(psiv_session* session, const char* const** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		std::vector<std::string> names;
		for (const rules::Macro& m : session->session->world().macros) {
			names.push_back(m.name);
		}
		*out = session->arena.strs(names);
		*count = static_cast<int32_t>(names.size());
		return PSIV_OK;
	});
}

psiv_status psiv_macro_list(psiv_session* session, const psiv_macro** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<game::MacroView> views = session->session->macroViews();
		Arena& a = session->arena;
		psiv_macro* list = a.array<psiv_macro>(views.size());
		for (std::size_t i = 0; i < views.size(); ++i) {
			const game::MacroView& view = views[i];
			psiv_macro_step* steps = a.array<psiv_macro_step>(view.steps.size());
			for (std::size_t j = 0; j < view.steps.size(); ++j) {
				const game::MacroStepView& v = view.steps[j];
				psiv_macro_step& o = steps[j];
				o.member = a.str(v.step.member);
				o.member_name = a.str(v.memberName);
				o.command_kind = static_cast<int32_t>(v.step.kind);
				o.id = a.str(v.step.id);
				o.id_name = a.str(v.commandName);
				o.target_kind = static_cast<int32_t>(v.step.target.kind);
				o.target_member = a.str(v.step.target.member);
				o.target_member_name = a.str(v.targetName);
				o.target_enemy = v.step.target.enemySlot;
				o.issue = static_cast<int32_t>(v.issue);
			}
			list[i].name = a.str(view.name);
			list[i].step_count = static_cast<int32_t>(view.steps.size());
			list[i].steps = steps;
		}
		*out = list;
		*count = static_cast<int32_t>(views.size());
		return PSIV_OK;
	});
}

psiv_status psiv_macro_create(psiv_session* session, const char* name) {
	const std::string text = str(name);
	return guard(session, true, [&] {
		if (!name) {
			return nullArgument(session, "name");
		}
		return refused(session, session->session->createMacro(text));
	});
}

psiv_status psiv_macro_rename(psiv_session* session, const char* name, const char* new_name) {
	const std::string from = str(name);
	const std::string to = str(new_name);
	return guard(session, true, [&] {
		if (!name || !new_name) {
			return nullArgument(session, name ? "new_name" : "name");
		}
		return refused(session, session->session->renameMacro(from, to));
	});
}

psiv_status psiv_macro_delete(psiv_session* session, const char* name) {
	const std::string text = str(name);
	return guard(session, true, [&] {
		if (!name) {
			return nullArgument(session, "name");
		}
		return refused(session, session->session->deleteMacro(text));
	});
}

psiv_status psiv_macro_move(psiv_session* session, const char* name, int32_t position) {
	const std::string text = str(name);
	return guard(session, true, [&] {
		if (!name) {
			return nullArgument(session, "name");
		}
		return refused(session, session->session->moveMacro(text, position));
	});
}

psiv_status psiv_macro_set_step(psiv_session* session, const char* macro, int32_t index, const char* member, int32_t command_kind,
	const char* id, int32_t target_kind, const char* target_member, int32_t target_enemy) {
	const std::string name = str(macro);
	rules::MacroStep step;
	step.member = str(member);
	step.id = str(id);
	step.target.member = str(target_member);
	step.target.enemySlot = target_enemy;
	return guard(session, true, [&] {
		if (!macro || !member) {
			return nullArgument(session, macro ? "member" : "macro");
		}
		if (command_kind < PSIV_COMMAND_NONE || command_kind > PSIV_COMMAND_ESCAPE) {
			return outOfRange(session, "command_kind", command_kind);
		}
		if (target_kind < PSIV_MACRO_TARGET_DEFAULT || target_kind > PSIV_MACRO_TARGET_ALL) {
			return outOfRange(session, "target_kind", target_kind);
		}
		step.kind = static_cast<rules::CommandKind>(command_kind);
		step.target.kind = static_cast<rules::MacroTargetKind>(target_kind);
		return refused(session, session->session->setMacroStep(name, index, step));
	});
}

psiv_status psiv_macro_remove_step(psiv_session* session, const char* macro, int32_t index) {
	const std::string name = str(macro);
	return guard(session, true, [&] {
		if (!macro) {
			return nullArgument(session, "macro");
		}
		return refused(session, session->session->removeMacroStep(name, index));
	});
}

psiv_status psiv_macro_move_step(psiv_session* session, const char* macro, int32_t from, int32_t to) {
	const std::string name = str(macro);
	return guard(session, true, [&] {
		if (!macro) {
			return nullArgument(session, "macro");
		}
		return refused(session, session->session->moveMacroStep(name, from, to));
	});
}

psiv_status psiv_macro_from_combo(psiv_session* session, const char* recipe, const char* name) {
	const std::string r = str(recipe);
	const std::string n = str(name);
	return guard(session, true, [&] {
		if (!recipe || !name) {
			return nullArgument(session, recipe ? "name" : "recipe");
		}
		return refused(session, session->session->macroFromCombo(r, n));
	});
}

// Title and saves

psiv_status psiv_new_game(psiv_session* session) {
	return guard(session, true, [&] {
		const psiv_status status = refused(session, session->session->newGame());
		if (status == PSIV_OK) {
			forgetRun(*session);
		}
		return status;
	});
}

psiv_status psiv_continue_game(psiv_session* session) {
	return guard(session, true, [&] {
		const psiv_status status = refused(session, session->session->continueGame());
		if (status == PSIV_OK) {
			forgetRun(*session);
		}
		return status;
	});
}

psiv_status psiv_load(psiv_session* session, const char* slot) {
	const std::string name = str(slot);
	return guard(session, true, [&] {
		if (!slot) {
			return nullArgument(session, "slot");
		}
		const psiv_status status = refused(session, session->session->load(name));
		if (status == PSIV_OK) {
			forgetRun(*session);
		}
		return status;
	});
}

psiv_status psiv_retry_battle(psiv_session* session) {
	return guard(session, true, [&] {
		const psiv_status status = refused(session, session->session->retryBattle());
		if (status == PSIV_OK) {
			forgetRun(*session);
		}
		return status;
	});
}

psiv_status psiv_quit_to_title(psiv_session* session) {
	return guard(session, true, [&] {
		session->session->quitToTitle();
		forgetRun(*session);
		return PSIV_OK;
	});
}

psiv_status psiv_save_manual(psiv_session* session, const char* slot) {
	const std::string name = str(slot);
	return guard(session, true, [&] {
		if (!slot) {
			return nullArgument(session, "slot");
		}
		return refused(session, session->session->saveManual(name));
	});
}

psiv_status psiv_saves(psiv_session* session, const psiv_save_summary** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const game::SaveStore* store = session->session->store();
		if (!store) {
			return refused(session, {"game.refuse.saving_disabled", {}});
		}
		const std::vector<game::SaveSummary> list = store->list();
		psiv_save_summary* items = session->arena.array<psiv_save_summary>(list.size());
		for (std::size_t i = 0; i < list.size(); ++i) {
			const game::SaveSummary& s = list[i];
			items[i].slot = session->arena.str(s.slot);
			items[i].area = session->arena.str(s.area);
			items[i].play_time_ms = s.playTimeMs;
			items[i].party_count = static_cast<int32_t>(s.party.size());
			items[i].party = session->arena.strs(s.party);
			items[i].difficulty = session->arena.str(s.difficulty);
			items[i].write_time = s.writeTime;
			items[i].sequence = s.sequence;
			items[i].autosave = s.autosave;
			items[i].damaged = s.damaged;
			items[i].error = session->arena.str(s.error);
			items[i].dev = s.dev;
		}
		*out = items;
		*count = static_cast<int32_t>(list.size());
		return PSIV_OK;
	});
}

int32_t psiv_mode_get(const psiv_session* session) {
	return session ? static_cast<int32_t>(session->session->mode()) : -1;
}

int32_t psiv_menu_kind_get(const psiv_session* session) {
	return session ? static_cast<int32_t>(session->session->menuKind()) : -1;
}

psiv_status psiv_world_info_get(psiv_session* session, psiv_world_info* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const game::Session& g = *session->session;
		*out = psiv_world_info();
		out->mode = static_cast<int32_t>(g.mode());
		out->menu_kind = static_cast<int32_t>(g.menuKind());
		out->money = g.world().money;
		out->play_time_ms = g.world().playTimeMs;
		out->autosave_count = g.autosaveCount();
		out->game_over = g.gameOver();
		out->can_retry = g.canRetry();
		out->player_control = g.world().playerControl;
		out->dev = g.devMarked();
		out->encounters = g.encountersOn();
		return PSIV_OK;
	});
}

int32_t psiv_flag(const psiv_session* session, const char* name) {
	if (!session || !name) {
		return 0;
	}
	try {
		return session->session->world().flag(name);
	} catch (...) {
		return 0;
	}
}

// Frame loop and field

psiv_status psiv_update(psiv_session* session, int32_t dt_ms, int32_t direction) {
	return guard(session, true, [&] {
		if (dt_ms < 0) {
			return outOfRange(session, "dt_ms", dt_ms);
		}
		if (!validDirection(direction)) {
			return outOfRange(session, "direction", direction);
		}
		game::FieldInput input;
		input.held = direction != PSIV_DIRECTION_NONE;
		input.direction = input.held ? static_cast<game::Facing>(direction) : game::Facing::South;
		session->session->update(dt_ms, input);
		return PSIV_OK;
	});
}

psiv_status psiv_walk(psiv_session* session, int32_t direction) {
	return guard(session, true, [&] {
		if (direction < PSIV_DIRECTION_NORTH || direction > PSIV_DIRECTION_WEST) {
			return outOfRange(session, "direction", direction);
		}
		return refused(session, session->session->walk(static_cast<game::Facing>(direction)));
	});
}

psiv_status psiv_face(psiv_session* session, int32_t direction) {
	return guard(session, true, [&] {
		if (direction < PSIV_DIRECTION_NORTH || direction > PSIV_DIRECTION_WEST) {
			return outOfRange(session, "direction", direction);
		}
		return refused(session, session->session->face(static_cast<game::Facing>(direction)));
	});
}

psiv_status psiv_walk_to(psiv_session* session, int32_t x, int32_t y) {
	return guard(session, true, [&] {
		if (session->session->mode() != game::Mode::Field) {
			return refused(session, {"game.refuse.walk_field_only", {}});
		}
		session->walk = session->session->beginWalk({x, y});
		return PSIV_OK;
	});
}

psiv_status psiv_walk_step(psiv_session* session, int32_t* status) {
	return guard(session, true, [&] {
		if (!status) {
			return nullArgument(session, "status");
		}
		if (!session->walk) {
			return notFound(session, "walk");
		}
		const game::WalkStatus result = session->session->walkStep(*session->walk);
		if (result != game::WalkStatus::Walking) {
			session->walk.reset();
		}
		*status = static_cast<int32_t>(result);
		return PSIV_OK;
	});
}

psiv_status psiv_interact(psiv_session* session) {
	return guard(session, true, [&] { return refused(session, session->session->interact()); });
}

psiv_status psiv_board(psiv_session* session) {
	return guard(session, true, [&] { return refused(session, session->session->board()); });
}

psiv_status psiv_leave_vehicle(psiv_session* session) {
	return guard(session, true, [&] { return refused(session, session->session->leaveVehicle()); });
}

psiv_status psiv_party_talk(psiv_session* session) {
	return guard(session, true, [&] { return refused(session, session->session->partyTalk()); });
}

psiv_status psiv_field_info_get(psiv_session* session, psiv_field_info* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const game::Area* a = currentArea(session);
		if (!a) {
			return notFound(session, "area");
		}
		const game::World& w = session->session->world();
		*out = psiv_field_info();
		out->area_id = session->arena.str(a->id);
		out->area_name = session->arena.str(session->session->displayName(a->id));
		out->area_kind = static_cast<int32_t>(a->kind);
		out->encounter_mode = static_cast<int32_t>(session->session->encounterMode());
		out->width = a->width();
		out->height = a->height();
		out->step_ms = a->stepMs;
		out->leader_x = w.position.x;
		out->leader_y = w.position.y;
		out->facing = static_cast<int32_t>(w.facing);
		out->player_control = w.playerControl;
		out->grace_ms = w.graceMs;
		out->music = session->arena.str(a->music);
		out->region = session->arena.str(a->region);
		out->aboard = session->arena.str(w.aboard);
		const game::FieldVehicle* vehicle = session->session->riding();
		out->leader_step_ms = vehicle ? std::max(1, a->stepMs * 100 / vehicle->speedPercent) : a->stepMs;
		return PSIV_OK;
	});
}

psiv_status psiv_field_grid(psiv_session* session, const uint8_t** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const game::Area* a = currentArea(session);
		if (!a) {
			return notFound(session, "area");
		}
		const int width = a->width();
		const int height = a->height();
		uint8_t* cells = session->arena.array<uint8_t>(static_cast<std::size_t>(width) * height);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const char c = x < static_cast<int>(a->rows[y].size()) ? a->rows[y][x] : '#';
				uint8_t cell = c == '.' ? PSIV_CELL_FLOOR : c == '~' ? PSIV_CELL_WATER : PSIV_CELL_WALL;
				if (a->walkable({x, y})) {
					cell |= PSIV_CELL_WALKABLE;
				}
				cells[y * width + x] = cell;
			}
		}
		*out = cells;
		*count = width * height;
		return PSIV_OK;
	});
}

psiv_status psiv_field_rows(psiv_session* session, const char* const** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const game::Area* a = currentArea(session);
		if (!a) {
			return notFound(session, "area");
		}
		*out = session->arena.strs(a->rows);
		*count = static_cast<int32_t>(a->rows.size());
		return PSIV_OK;
	});
}

psiv_status psiv_field_enemies(psiv_session* session, const psiv_enemy** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<game::VisibleEnemy>& enemies = session->session->world().enemies;
		psiv_enemy* list = session->arena.array<psiv_enemy>(enemies.size());
		for (std::size_t i = 0; i < enemies.size(); ++i) {
			const game::VisibleEnemy& e = enemies[i];
			list[i].id = e.id;
			list[i].encounter = session->arena.str(e.encounter);
			list[i].zone = session->arena.str(e.zone);
			list[i].x = e.position.x;
			list[i].y = e.position.y;
			list[i].facing = static_cast<int32_t>(e.facing);
			list[i].state = static_cast<int32_t>(e.state);
		}
		*out = list;
		*count = static_cast<int32_t>(enemies.size());
		return PSIV_OK;
	});
}

psiv_status psiv_field_markers(psiv_session* session, const psiv_marker** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const game::Area* a = currentArea(session);
		if (!a) {
			return notFound(session, "area");
		}
		psiv_marker* list = session->arena.array<psiv_marker>(a->markers.size());
		int n = 0;
		for (const auto& [name, cell] : a->markers) {
			list[n].name = session->arena.str(name);
			list[n].x = cell.x;
			list[n].y = cell.y;
			++n;
		}
		*out = list;
		*count = n;
		return PSIV_OK;
	});
}

psiv_status psiv_field_exits(psiv_session* session, const psiv_exit** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const game::Area* a = currentArea(session);
		if (!a) {
			return notFound(session, "area");
		}
		psiv_exit* list = session->arena.array<psiv_exit>(a->exits.size());
		for (std::size_t i = 0; i < a->exits.size(); ++i) {
			list[i].x = a->exits[i].at.x;
			list[i].y = a->exits[i].at.y;
			list[i].to_area = session->arena.str(a->exits[i].toArea);
			list[i].to_marker = session->arena.str(a->exits[i].toMarker);
		}
		*out = list;
		*count = static_cast<int32_t>(a->exits.size());
		return PSIV_OK;
	});
}

psiv_status psiv_field_points(psiv_session* session, const psiv_point** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const game::Area* a = currentArea(session);
		if (!a) {
			return notFound(session, "area");
		}
		const game::World& w = session->session->world();
		std::vector<psiv_point> points;
		Arena& arena = session->arena;
		for (const game::Hook& h : a->hooks) {
			if (h.trigger != game::HookTrigger::Interact && h.trigger != game::HookTrigger::Step) {
				continue;
			}
			const bool holds = (h.requireFlag.empty() || w.flag(h.requireFlag)) && (h.blockFlag.empty() || !w.flag(h.blockFlag));
			points.push_back({h.trigger == game::HookTrigger::Interact ? PSIV_POINT_TALK : PSIV_POINT_STEP, arena.str(h.id), h.at.x, h.at.y, arena.str(h.event), holds, 0});
		}
		for (const game::Chest& c : a->chests) {
			points.push_back({PSIV_POINT_CHEST, arena.str(c.id), c.at.x, c.at.y, "", !w.flag(c.flag), 0});
		}
		for (const game::Shop& shop : a->shops) {
			if (shop.at) {
				points.push_back({PSIV_POINT_SHOP, arena.str(shop.id), shop.at->x, shop.at->y, "", 1, 0});
			}
		}
		for (const game::RestPoint& r : a->rests) {
			if (r.at) {
				points.push_back({PSIV_POINT_REST, arena.str(r.id), r.at->x, r.at->y, "", 1, r.cost});
			}
		}
		for (const game::Port& p : a->ports) {
			points.push_back({PSIV_POINT_PORT, arena.str(p.id), p.at.x, p.at.y, "", p.flag.empty() || w.flag(p.flag), 0});
		}
		psiv_point* list = arena.array<psiv_point>(points.size());
		std::copy(points.begin(), points.end(), list);
		*out = list;
		*count = static_cast<int32_t>(points.size());
		return PSIV_OK;
	});
}

psiv_status psiv_field_vehicles(psiv_session* session, const psiv_vehicle** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		if (!currentArea(session)) {
			return notFound(session, "area");
		}
		const std::vector<game::VehicleView> vehicles = session->session->vehiclesHere();
		Arena& arena = session->arena;
		psiv_vehicle* list = arena.array<psiv_vehicle>(vehicles.size());
		for (std::size_t i = 0; i < vehicles.size(); ++i) {
			const game::VehicleView& v = vehicles[i];
			list[i] = {arena.str(v.id), arena.str(session->session->displayName(v.id)), v.cell.x, v.cell.y, static_cast<int32_t>(v.facing), v.aboard};
		}
		*out = list;
		*count = static_cast<int32_t>(vehicles.size());
		return PSIV_OK;
	});
}

psiv_status psiv_travel_destinations(psiv_session* session, int32_t kind, const psiv_travel_destination** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		if (kind < PSIV_TRAVEL_PORT || kind > PSIV_TRAVEL_DUNGEON_EXIT) {
			return outOfRange(session, "kind", kind);
		}
		const std::vector<game::TravelDestination> places = session->session->travelDestinations(static_cast<game::TravelKind>(kind));
		Arena& arena = session->arena;
		psiv_travel_destination* list = arena.array<psiv_travel_destination>(places.size());
		for (std::size_t i = 0; i < places.size(); ++i) {
			const game::TravelDestination& d = places[i];
			list[i] = {arena.str(d.id), arena.str(d.area), arena.str(session->session->displayName(d.area)), arena.str(d.region), d.cell.x, d.cell.y};
		}
		*out = list;
		*count = static_cast<int32_t>(places.size());
		return PSIV_OK;
	});
}

psiv_status psiv_travel(psiv_session* session, int32_t kind, const char* destination, const char* user, const char* thing) {
	const std::string to = str(destination);
	const std::string who = str(user);
	const std::string what = str(thing);
	return guard(session, true, [&] {
		if (kind < PSIV_TRAVEL_PORT || kind > PSIV_TRAVEL_DUNGEON_EXIT) {
			return outOfRange(session, "kind", kind);
		}
		session->walk.reset();
		return refused(session, session->session->travel(static_cast<game::TravelKind>(kind), to, who, what));
	});
}

psiv_status psiv_field_actors(psiv_session* session, const psiv_actor** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		if (!currentArea(session)) {
			return notFound(session, "area");
		}
		const std::vector<game::ActorView> actors = session->session->actorsHere();
		Arena& arena = session->arena;
		psiv_actor* list = arena.array<psiv_actor>(actors.size());
		for (std::size_t i = 0; i < actors.size(); ++i) {
			const game::ActorView& a = actors[i];
			list[i] = {arena.str(a.id), arena.str(session->session->displayName(a.id)), a.cell.x, a.cell.y, static_cast<int32_t>(a.facing), a.visible, a.scripted};
		}
		*out = list;
		*count = static_cast<int32_t>(actors.size());
		return PSIV_OK;
	});
}

psiv_status psiv_actor_ids(psiv_session* session, const char* const** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<std::string>& actors = session->data->config.actors;
		*out = session->arena.strs(actors);
		*count = static_cast<int32_t>(actors.size());
		return PSIV_OK;
	});
}

// Events

int32_t psiv_line_pending(const psiv_session* session) {
	return session && session->session->linePending();
}

psiv_status psiv_line_get(psiv_session* session, psiv_line* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		if (!session->session->linePending()) {
			return notFound(session, "line");
		}
		const game::LineView line = *session->session->line();
		*out = psiv_line();
		out->speaker_id = session->arena.str(line.speakerId);
		out->speaker_name = session->arena.str(line.speakerName);
		out->text_id = session->arena.str(line.textId);
		out->text = session->arena.str(line.text);
		out->portrait = session->arena.str(line.portrait);
		out->display = session->arena.str(session->session->lineText());
		return PSIV_OK;
	});
}

psiv_status psiv_line_confirm(psiv_session* session) {
	return guard(session, true, [&] {
		if (!session->session->linePending()) {
			return notFound(session, "line");
		}
		session->session->confirmLine();
		return PSIV_OK;
	});
}

int32_t psiv_choice_pending(const psiv_session* session) {
	return session && session->session->choice() != nullptr;
}

psiv_status psiv_choice_get(psiv_session* session, psiv_choice* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const std::optional<game::ChoiceView> c = session->session->choiceView();
		if (!c) {
			return notFound(session, "choice");
		}
		*out = psiv_choice();
		out->prompt = session->arena.str(c->prompt);
		out->yes_no = c->yesNo;
		out->option_count = static_cast<int32_t>(c->options.size());
		out->options = session->arena.strs(c->options);
		return PSIV_OK;
	});
}

psiv_status psiv_choose(psiv_session* session, int32_t index) {
	return guard(session, true, [&] { return refused(session, session->session->choose(index)); });
}

int32_t psiv_can_skip(const psiv_session* session) {
	return session && session->session->canSkip();
}

psiv_status psiv_skip(psiv_session* session) {
	return guard(session, true, [&] {
		if (!session->session->skip()) {
			return bindingRefusal(session, "cannot_skip", "this scene cannot be skipped now");
		}
		return PSIV_OK;
	});
}

// Presentation actions

psiv_status psiv_cameras(psiv_session* session, const psiv_camera** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<game::CameraDef>& cameras = session->data->config.presentation.cameras;
		Arena& a = session->arena;
		psiv_camera* list = a.array<psiv_camera>(cameras.size());
		for (std::size_t i = 0; i < cameras.size(); ++i) {
			const game::CameraDef& c = cameras[i];
			list[i] = {a.str(c.id), static_cast<int32_t>(c.target), a.str(c.area), a.str(c.marker), a.str(c.follow), c.zoom};
		}
		*out = list;
		*count = static_cast<int32_t>(cameras.size());
		return PSIV_OK;
	});
}

psiv_status psiv_actions_poll(psiv_session* session, const psiv_action** out, int32_t* count) {
	return guard(session, true, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<game::PresentedAction> records = session->session->pollActions();
		psiv_action* list = session->arena.array<psiv_action>(records.size());
		for (std::size_t i = 0; i < records.size(); ++i) {
			fillAction(records[i], list[i], session->arena);
		}
		*out = list;
		*count = static_cast<int32_t>(records.size());
		return PSIV_OK;
	});
}

psiv_status psiv_action_finish(psiv_session* session, uint64_t handle) {
	return guard(session, true, [&] {
		session->session->finishAction(handle);
		return PSIV_OK;
	});
}

psiv_status psiv_actions_set_callback(psiv_session* session, psiv_action_callback callback, void* user) {
	return guard(session, false, [&] {
		session->callback = callback;
		session->user = user;
		return PSIV_OK;
	});
}

// Notices

psiv_status psiv_notices_drain(psiv_session* session, const psiv_notice** out, int32_t* count) {
	return guard(session, true, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		*out = fillNotices(*session, session->session->drainOutput(), count);
		return PSIV_OK;
	});
}

// Battle

psiv_status psiv_battle_info_get(psiv_session* session, psiv_battle_info* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const game::Session& g = *session->session;
		const rules::Battle* b = shownBattle(*session);
		if (!b) {
			return notFound(session, "battle");
		}
		const game::EndedBattle* ended = g.battle() ? nullptr : g.endedBattle();
		*out = psiv_battle_info();
		out->live = ended == nullptr;
		out->encounter = session->arena.str(ended ? ended->encounter : g.battleEncounter());
		out->source = static_cast<int32_t>(ended ? ended->source : g.battleSource());
		out->round = b->round();
		out->party_count = b->partyCount();
		out->combatant_count = static_cast<int32_t>(b->combatants().size());
		out->outcome = static_cast<int32_t>(b->outcome());
		out->escape_allowed = b->escapeAllowed();
		out->auto_active = !ended && b->autoActive();
		out->party_acts_next_round = !ended && b->sideActsNextRound(rules::Side::Party);
		out->game_over = b->gameOver();
		const std::map<std::string, std::string>& tracks = session->data->config.presentation.music.encounters;
		auto track = tracks.find(ended ? ended->encounter : g.battleEncounter());
		out->music = session->arena.str(track == tracks.end() ? std::string() : track->second);
		out->vehicle = session->arena.str(b->setup().flags.vehicleBattle ? b->setup().vehicle : std::string());
		return PSIV_OK;
	});
}

psiv_status psiv_battle_combatants(psiv_session* session, const psiv_combatant** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const rules::Battle* b = shownBattle(*session);
		if (!b) {
			return notFound(session, "battle");
		}
		const bool live = session->session->battle() != nullptr;
		const std::vector<std::string> names = labels(b);
		Arena& a = session->arena;
		psiv_combatant* items = a.array<psiv_combatant>(names.size());
		for (std::size_t i = 0; i < names.size(); ++i) {
			const rules::Combatant& c = b->combatant(static_cast<int>(i));
			const rules::Command command = live ? b->command(static_cast<int>(i)) : rules::Command();
			psiv_combatant& o = items[i];
			o.index = static_cast<int32_t>(i);
			o.id = a.str(c.defId);
			o.name = a.str(c.name);
			o.label = a.str(names[i]);
			o.side = static_cast<int32_t>(c.side);
			o.group = a.str(c.group);
			o.hp = c.hp;
			o.max_hp = c.stats.maxHp;
			o.tp = c.tp;
			o.max_tp = c.stats.maxTp;
			o.alive = c.hp > 0;
			o.defending = c.defending;
			fillStatuses(*session, c.statuses, o.status_count, o.statuses);
			o.command_kind = static_cast<int32_t>(command.kind);
			o.command_id = a.str(command.id);
			o.command_target = command.target;
		}
		*out = items;
		*count = static_cast<int32_t>(names.size());
		return PSIV_OK;
	});
}

psiv_status psiv_battle_result_get(psiv_session* session, psiv_battle_result* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const game::EndedBattle* ended = session->session->endedBattle();
		if (!ended) {
			return notFound(session, "ended_battle");
		}
		Arena& a = session->arena;
		*out = psiv_battle_result();
		out->encounter = a.str(ended->encounter);
		out->source = static_cast<int32_t>(ended->source);
		out->outcome = static_cast<int32_t>(ended->outcome);
		out->game_over = ended->gameOver;
		out->experience = ended->rewards.experience;
		out->money = ended->rewards.currency;
		psiv_item_count* drops = a.array<psiv_item_count>(ended->rewards.items.size());
		int n = 0;
		for (const auto& [id, count] : ended->rewards.items) {
			drops[n].id = a.str(id);
			drops[n].count = count;
			++n;
		}
		out->drop_count = n;
		out->drops = drops;
		psiv_level_up* ups = a.array<psiv_level_up>(ended->levelUps.size());
		for (std::size_t i = 0; i < ended->levelUps.size(); ++i) {
			const game::LevelUpView& up = ended->levelUps[i];
			ups[i].member = a.str(up.member);
			ups[i].name = a.str(session->session->displayName(up.member));
			ups[i].level = up.level;
			ups[i].learned_count = static_cast<int32_t>(up.learned.size());
			ups[i].learned = a.strs(up.learned);
		}
		out->level_up_count = static_cast<int32_t>(ended->levelUps.size());
		out->level_ups = ups;
		return PSIV_OK;
	});
}

psiv_status psiv_battle_dismiss(psiv_session* session) {
	return guard(session, true, [&] {
		session->session->dismissBattle();
		return PSIV_OK;
	});
}

psiv_status psiv_battle_options(psiv_session* session, int32_t actor, const psiv_battle_option** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const rules::Battle* b = session->session->battle();
		if (!b) {
			return notFound(session, "battle");
		}
		if (actor < 0 || actor >= b->partyCount()) {
			return outOfRange(session, "actor", actor);
		}
		const rules::Combatant& c = b->combatant(actor);
		const rules::Database& db = b->database();
		Arena& a = session->arena;
		std::vector<psiv_battle_option> options;
		for (const std::string& id : c.abilities) {
			const rules::AbilityDef* def = db.ability(id);
			if (!def) {
				continue;
			}
			psiv_battle_option o = {};
			o.id = a.str(id);
			o.name = a.str(def->name);
			o.command = PSIV_COMMAND_ABILITY;
			o.ability_kind = static_cast<int32_t>(def->kind);
			o.targeting = static_cast<int32_t>(def->targeting);
			o.cost = def->cost;
			auto uses = c.skillUses.find(id);
			o.uses = def->uses > 0 ? (uses == c.skillUses.end() ? 0 : uses->second) : -1;
			o.unusable = static_cast<int32_t>(b->checkUsable(actor, *def));
			options.push_back(o);
		}
		for (const auto& [id, n] : b->inventory()) {
			const rules::ItemDef* def = db.item(id);
			if (!def || n <= 0) {
				continue;
			}
			psiv_battle_option o = {};
			o.id = a.str(id);
			o.name = a.str(def->name);
			o.command = PSIV_COMMAND_ITEM;
			o.ability_kind = -1;
			o.targeting = static_cast<int32_t>(def->targeting);
			o.uses = n;
			o.unusable = PSIV_CANCEL_NONE;
			options.push_back(o);
		}
		psiv_battle_option* list = a.array<psiv_battle_option>(options.size());
		std::copy(options.begin(), options.end(), list);
		*out = list;
		*count = static_cast<int32_t>(options.size());
		return PSIV_OK;
	});
}

psiv_status psiv_battle_set_command(psiv_session* session, int32_t actor, int32_t kind, const char* id, int32_t target) {
	const std::string name = str(id);
	return guard(session, true, [&] {
		if (kind < PSIV_COMMAND_NONE || kind > PSIV_COMMAND_ESCAPE) {
			return outOfRange(session, "kind", kind);
		}
		const rules::Battle* b = session->session->battle();
		if (!b) {
			return refused(session, {"game.refuse.no_battle", {}});
		}
		if (actor < 0 || actor >= static_cast<int>(b->combatants().size())) {
			return outOfRange(session, "actor", actor);
		}
		if (target < -1 || target >= static_cast<int>(b->combatants().size())) {
			return outOfRange(session, "target", target);
		}
		rules::Command command;
		command.kind = static_cast<rules::CommandKind>(kind);
		command.id = name;
		command.target = target;
		return refused(session, session->session->setCommand(actor, command));
	});
}

psiv_status psiv_battle_apply_macro(psiv_session* session, const char* macro, const psiv_macro_step_result** out, int32_t* count) {
	const std::string name = str(macro);
	return guard(session, true, [&] {
		if (!macro) {
			return nullArgument(session, "macro");
		}
		std::vector<rules::MacroStepResult> results;
		const psiv_status status = refused(session, session->session->applyMacro(name, &results));
		Arena& a = session->arena;
		psiv_macro_step_result* list = a.array<psiv_macro_step_result>(results.size());
		for (std::size_t i = 0; i < results.size(); ++i) {
			list[i].member = a.str(results[i].member);
			list[i].actor = results[i].actor;
			list[i].status = static_cast<int32_t>(results[i].status);
			list[i].issue = static_cast<int32_t>(results[i].issue);
			list[i].command_kind = static_cast<int32_t>(results[i].command.kind);
			list[i].command_id = a.str(results[i].command.id);
			list[i].command_target = results[i].command.target;
		}
		if (out) {
			*out = list;
		}
		if (count) {
			*count = static_cast<int32_t>(results.size());
		}
		return status;
	});
}

psiv_status psiv_battle_start_auto(psiv_session* session) {
	return guard(session, true, [&] { return refused(session, session->session->startAuto()); });
}

psiv_status psiv_battle_cancel_auto(psiv_session* session) {
	return guard(session, true, [&] {
		if (!session->session->battle()) {
			return refused(session, {"game.refuse.no_battle", {}});
		}
		session->session->cancelAuto();
		return PSIV_OK;
	});
}

psiv_status psiv_battle_fill_from_auto(psiv_session* session, int32_t* filled) {
	return guard(session, true, [&] {
		if (!filled) {
			return nullArgument(session, "filled");
		}
		*filled = session->session->fillFromAuto();
		return PSIV_OK;
	});
}

psiv_status psiv_battle_fill_from_policy(psiv_session* session) {
	return guard(session, true, [&] {
		if (!session->session->battle()) {
			return refused(session, {"game.refuse.no_battle", {}});
		}
		session->session->fillFromPolicy();
		return PSIV_OK;
	});
}

psiv_status psiv_battle_fill_auto_round(psiv_session* session) {
	return guard(session, true, [&] {
		if (!session->session->battle()) {
			return refused(session, {"game.refuse.no_battle", {}});
		}
		return refused(session, session->session->fillAutoRound());
	});
}

psiv_status psiv_battle_resolve_round(psiv_session* session, const psiv_battle_event** out, int32_t* count) {
	return guard(session, true, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		if (!session->session->battle()) {
			return refused(session, {"game.refuse.no_battle", {}});
		}
		session->lastEvents = session->session->resolveRound();
		*out = fillEvents(*session, session->lastEvents, labels(shownBattle(*session)), count);
		return PSIV_OK;
	});
}

psiv_status psiv_battle_last_events(psiv_session* session, const psiv_battle_event** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		*out = fillEvents(*session, session->lastEvents, labels(shownBattle(*session)), count);
		return PSIV_OK;
	});
}

psiv_status psiv_battle_play(psiv_session* session, int32_t max_rounds, psiv_auto_battle* out) {
	return guard(session, true, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		if (!session->session->battle()) {
			return refused(session, {"game.refuse.no_battle", {}});
		}
		const std::vector<std::string> names = labels(session->session->battle());
		const game::AutoBattle played = session->session->playBattle(max_rounds > 0 ? max_rounds : 60);
		Arena& a = session->arena;
		*out = psiv_auto_battle();
		out->refusal_key = a.str(played.refusal.key);
		out->refusal_param_count = static_cast<int32_t>(played.refusal.params.size());
		out->refusal_params = a.strs(played.refusal.params);
		out->refusal_text = a.str(played.refusal.empty() ? "" : session->session->render(played.refusal));
		psiv_auto_round* rounds = a.array<psiv_auto_round>(played.rounds.size());
		for (std::size_t i = 0; i < played.rounds.size(); ++i) {
			rounds[i].notices = fillNotices(*session, played.rounds[i].output, &rounds[i].notice_count);
			rounds[i].events = fillEvents(*session, played.rounds[i].events, names, &rounds[i].event_count);
		}
		out->round_count = static_cast<int32_t>(played.rounds.size());
		out->rounds = rounds;
		out->ended = played.ended;
		if (!played.rounds.empty()) {
			session->lastEvents = played.rounds.back().events;
		}
		return PSIV_OK;
	});
}

psiv_status psiv_battle_record(psiv_session* session, const char** out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const rules::Battle* b = shownBattle(*session);
		if (!b) {
			return notFound(session, "battle");
		}
		*out = session->arena.str(rules::serializeRecord(b->record()));
		return PSIV_OK;
	});
}

psiv_status psiv_battle_replay(psiv_session* session, const char* record, const psiv_battle_event** events, int32_t* count, const char** event_text) {
	const std::string text = str(record);
	return guard(session, false, [&] {
		if (!record) {
			return nullArgument(session, "record");
		}
		if (!events || !count) {
			return nullArgument(session, events ? "count" : "events");
		}
		const rules::Result<rules::BattleRecord> parsed = rules::parseRecord(text);
		if (!parsed.ok()) {
			return fail(session, PSIV_DATA_ERROR, "binding.bad_record", {parsed.error}, parsed.error);
		}
		rules::ComboLog log;
		std::vector<rules::Event> replayed;
		const rules::Result<rules::Battle> battle = rules::replayBattle(session->data->db, *parsed.value, log, replayed);
		if (!battle.ok()) {
			return fail(session, PSIV_DATA_ERROR, "binding.bad_record", {battle.error}, battle.error);
		}
		*events = fillEvents(*session, replayed, labels(&*battle.value), count);
		if (event_text) {
			*event_text = session->arena.str(rules::serializeEvents(replayed));
		}
		return PSIV_OK;
	});
}

// Party, inventory, menus and shops

psiv_status psiv_party(psiv_session* session, const psiv_member** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const game::World& w = session->session->world();
		const rules::Database& db = session->data->db;
		std::vector<const rules::PartyMember*> order;
		for (const std::string& id : w.lineup) {
			if (const rules::PartyMember* m = w.member(id)) {
				order.push_back(m);
			}
		}
		for (const rules::PartyMember& m : w.roster) {
			if (std::find(w.lineup.begin(), w.lineup.end(), m.characterId) == w.lineup.end()) {
				order.push_back(&m);
			}
		}
		Arena& a = session->arena;
		psiv_member* list = a.array<psiv_member>(order.size());
		for (std::size_t i = 0; i < order.size(); ++i) {
			const rules::PartyMember& m = *order[i];
			const rules::Stats stats = rules::memberStats(db, m);
			psiv_member& o = list[i];
			o.id = a.str(m.characterId);
			o.name = a.str(session->session->displayName(m.characterId));
			o.in_lineup = std::find(w.lineup.begin(), w.lineup.end(), m.characterId) != w.lineup.end();
			o.level = m.level;
			o.experience = m.experience;
			o.hp = m.hp;
			o.max_hp = stats.maxHp;
			o.tp = m.tp;
			o.max_tp = stats.maxTp;
			o.strength = stats.strength;
			o.mental = stats.mental;
			o.agility = stats.agility;
			o.dexterity = stats.dexterity;
			o.defense = stats.defense;
			fillStatuses(*session, m.statuses, o.status_count, o.statuses);
			for (int slot = 0; slot < PSIV_SLOT_COUNT; ++slot) {
				auto worn = m.equipment.find(static_cast<rules::EquipSlot>(slot));
				const std::string piece = worn == m.equipment.end() ? "" : worn->second;
				o.equipment[slot] = a.str(piece);
				o.equipment_names[slot] = a.str(piece.empty() ? "" : session->session->displayName(piece));
			}
		}
		*out = list;
		*count = static_cast<int32_t>(order.size());
		return PSIV_OK;
	});
}

psiv_status psiv_member_abilities(psiv_session* session, const char* member, const psiv_ability_info** out, int32_t* count) {
	const std::string id = str(member);
	return guard(session, false, [&] {
		if (!member) {
			return nullArgument(session, "member");
		}
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const rules::PartyMember* m = session->session->world().member(id);
		if (!m) {
			return fail(session, PSIV_NOT_FOUND, "field.unknown_member", {id}, session->session->render({"field.unknown_member", {id}}));
		}
		const rules::Database& db = session->data->db;
		std::vector<psiv_ability_info> abilities;
		Arena& a = session->arena;
		for (const std::string& abilityId : rules::knownAbilities(db, *m)) {
			const rules::AbilityDef* def = db.ability(abilityId);
			if (!def) {
				continue;
			}
			int32_t uses = -1;
			int32_t maxUses = -1;
			if (def->kind == rules::AbilityKind::Skill) {
				auto left = m->skillUses.find(abilityId);
				uses = left == m->skillUses.end() ? 0 : left->second;
				maxUses = def->uses;
			}
			abilities.push_back({a.str(abilityId), a.str(def->name), static_cast<int32_t>(def->kind), static_cast<int32_t>(def->targeting), def->cost, def->field,
				uses, maxUses, travelKind(def->effects)});
		}
		psiv_ability_info* list = a.array<psiv_ability_info>(abilities.size());
		std::copy(abilities.begin(), abilities.end(), list);
		*out = list;
		*count = static_cast<int32_t>(abilities.size());
		return PSIV_OK;
	});
}

psiv_status psiv_party_swap(psiv_session* session, int32_t a, int32_t b) {
	return guard(session, true, [&] { return refused(session, session->session->swapLineup(a, b)); });
}

psiv_status psiv_member_detail_get(psiv_session* session, const char* member, psiv_member_detail* out) {
	const std::string id = str(member);
	return guard(session, false, [&] {
		if (!member) {
			return nullArgument(session, "member");
		}
		if (!out) {
			return nullArgument(session, "out");
		}
		const std::optional<game::MemberDetail> detail = session->session->memberDetail(id);
		if (!detail) {
			return fail(session, PSIV_NOT_FOUND, "field.unknown_member", {id}, session->session->render({"field.unknown_member", {id}}));
		}
		Arena& a = session->arena;
		*out = psiv_member_detail();
		out->id = a.str(detail->member);
		out->experience_to_next = detail->experienceToNext;
		out->critical = detail->stats.critical;
		out->evasion = detail->stats.evasion;
		out->attack_element = static_cast<int32_t>(detail->attackElement);
		for (const auto& [slot, b] : detail->equipmentBonus) {
			out->equipment_bonus[static_cast<int>(slot)] = {b.maxHp, b.maxTp, b.strength, b.mental, b.agility, b.dexterity, b.defense, b.critical, b.evasion};
		}
		psiv_affinity* affinities = a.array<psiv_affinity>(detail->affinities.size());
		int n = 0;
		for (const auto& [element, percent] : detail->affinities) {
			affinities[n++] = {static_cast<int32_t>(element), percent};
		}
		out->affinity_count = n;
		out->affinities = affinities;
		psiv_status_resist* resists = a.array<psiv_status_resist>(detail->statusResist.size());
		n = 0;
		for (const auto& [status, percent] : detail->statusResist) {
			resists[n++] = {a.str(status), a.str(session->session->displayName(status)), percent};
		}
		out->status_resist_count = n;
		out->status_resists = resists;
		return PSIV_OK;
	});
}

psiv_status psiv_equip_preview_get(psiv_session* session, const char* member, const char* piece, psiv_equip_preview* out) {
	const std::string memberId = str(member);
	const std::string pieceId = str(piece);
	return guard(session, false, [&] {
		if (!member || !piece) {
			return nullArgument(session, member ? "piece" : "member");
		}
		if (!out) {
			return nullArgument(session, "out");
		}
		const std::optional<game::EquipPreview> preview = session->session->equipPreview(memberId, pieceId);
		if (!preview) {
			return fail(session, PSIV_NOT_FOUND, "binding.no_preview", {memberId, pieceId}, "'" + memberId + "' is not in the roster or '" + pieceId + "' is not equipment");
		}
		Arena& a = session->arena;
		game::Session& g = *session->session;
		auto names = [&](const std::vector<std::string>& ids) {
			std::vector<std::string> shown;
			for (const std::string& id : ids) {
				shown.push_back(g.displayName(id));
			}
			return a.strs(shown);
		};
		auto resists = [&](const rules::StatusResists& list, int32_t& count, const psiv_status_resist*& outList) {
			psiv_status_resist* entries = a.array<psiv_status_resist>(list.size());
			int n = 0;
			for (const auto& [status, percent] : list) {
				entries[n++] = {a.str(status), a.str(g.displayName(status)), percent};
			}
			count = n;
			outList = entries;
		};
		auto bonus = [](const rules::Stats& b) { return psiv_stat_bonus{b.maxHp, b.maxTp, b.strength, b.mental, b.agility, b.dexterity, b.defense, b.critical, b.evasion}; };
		auto combat = [&](const game::CombatView& v, psiv_combat_view& o) {
			o.stats = bonus(v.stats);
			o.basic_attack = a.str(v.basicAttack);
			o.basic_attack_name = a.str(g.displayName(v.basicAttack));
			o.attack_element = static_cast<int32_t>(v.attackElement);
			o.ability_count = static_cast<int32_t>(v.abilities.size());
			o.abilities = a.strs(v.abilities);
			o.ability_names = names(v.abilities);
			resists(v.statusResist, o.status_resist_count, o.status_resists);
		};
		const rules::EquipmentDef& def = preview->piece;
		*out = psiv_equip_preview();
		out->member = a.str(preview->member);
		out->piece = a.str(def.id);
		out->piece_name = a.str(g.displayName(def.id));
		out->slot = static_cast<int32_t>(def.slot);
		out->category = a.str(def.category);
		out->bonus = bonus(def.bonus);
		out->element = static_cast<int32_t>(def.element);
		out->attack = a.str(def.attack);
		out->attack_name = a.str(def.attack.empty() ? "" : g.displayName(def.attack));
		out->granted_count = static_cast<int32_t>(def.abilities.size());
		out->granted = a.strs(def.abilities);
		out->granted_names = names(def.abilities);
		resists(def.statusResist, out->piece_resist_count, out->piece_resists);
		out->replaces = a.str(preview->replaces);
		out->replaces_name = a.str(preview->replaces.empty() ? "" : g.displayName(preview->replaces));
		out->can_wear = preview->refusal.empty();
		out->refusal_key = a.str(preview->refusal.key);
		out->refusal_text = a.str(preview->refusal.empty() ? "" : g.render(preview->refusal));
		out->wearer_count = static_cast<int32_t>(preview->wearers.size());
		out->wearers = a.strs(preview->wearers);
		out->wearer_names = names(preview->wearers);
		combat(preview->before, out->before);
		combat(preview->after, out->after);
		return PSIV_OK;
	});
}

psiv_status psiv_combo_log(psiv_session* session, const psiv_combo_entry** out, int32_t* count, int32_t* total) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<game::ComboView> log = session->session->comboLog();
		Arena& a = session->arena;
		auto names = [&](const std::vector<std::string>& ids) {
			std::vector<std::string> shown;
			for (const std::string& id : ids) {
				shown.push_back(session->session->displayName(id));
			}
			return a.strs(shown);
		};
		psiv_combo_entry* list = a.array<psiv_combo_entry>(log.size());
		for (std::size_t i = 0; i < log.size(); ++i) {
			const game::ComboView& v = log[i];
			psiv_combo_entry& o = list[i];
			o.recipe = a.str(v.recipe);
			o.result = a.str(v.result);
			o.result_name = a.str(v.resultName);
			o.play_time_ms = v.playTimeMs;
			o.participant_count = static_cast<int32_t>(v.characters.size());
			o.participants = a.strs(v.characters);
			o.participant_names = names(v.characters);
			o.ability_count = static_cast<int32_t>(v.abilities.size());
			o.abilities = a.strs(v.abilities);
			o.ability_names = names(v.abilities);
		}
		*out = list;
		*count = static_cast<int32_t>(log.size());
		if (total) {
			*total = static_cast<int32_t>(session->data->db.combos.size());
		}
		return PSIV_OK;
	});
}

psiv_status psiv_inventory_sorted(psiv_session* session, int32_t order, const psiv_inventory_entry** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		if (order < 0 || order > PSIV_SORT_COUNT) {
			return outOfRange(session, "order", order);
		}
		const std::vector<game::BagEntry> bag = session->session->bag(static_cast<game::ItemSort>(order));
		psiv_inventory_entry* list = session->arena.array<psiv_inventory_entry>(bag.size());
		for (std::size_t i = 0; i < bag.size(); ++i) {
			list[i] = entryFor(*session, bag[i].id, bag[i].count);
		}
		*out = list;
		*count = static_cast<int32_t>(bag.size());
		return PSIV_OK;
	});
}

psiv_status psiv_inventory(psiv_session* session, const psiv_inventory_entry** out, int32_t* count) {
	return psiv_inventory_sorted(session, PSIV_SORT_ID, out, count);
}

psiv_status psiv_shop_get(psiv_session* session, psiv_shop_info* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const game::Shop* shop = session->session->shop();
		if (!shop) {
			return notFound(session, "shop");
		}
		const game::World& w = session->session->world();
		psiv_inventory_entry* stock = session->arena.array<psiv_inventory_entry>(shop->stock.size());
		for (std::size_t i = 0; i < shop->stock.size(); ++i) {
			const std::string& id = shop->stock[i];
			auto held = w.items.find(id);
			int n = held == w.items.end() ? 0 : held->second;
			if (auto piece = w.equipmentBag.find(id); piece != w.equipmentBag.end()) {
				n = piece->second;
			}
			stock[i] = entryFor(*session, id, n);
		}
		out->id = session->arena.str(shop->id);
		out->sell_percent = shop->sellPercent;
		out->stock_count = static_cast<int32_t>(shop->stock.size());
		out->stock = stock;
		return PSIV_OK;
	});
}

psiv_status psiv_inn_get(psiv_session* session, psiv_inn_info* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const game::RestPoint* inn = session->session->inn();
		if (!inn) {
			return notFound(session, "inn");
		}
		out->id = session->arena.str(inn->id);
		out->cost = inn->cost;
		return PSIV_OK;
	});
}

psiv_status psiv_open_menu(psiv_session* session) {
	return guard(session, true, [&] { return refused(session, session->session->openMenu()); });
}

psiv_status psiv_close_menu(psiv_session* session) {
	return guard(session, true, [&] { return refused(session, session->session->closeMenu()); });
}

psiv_status psiv_buy(psiv_session* session, const char* id, int32_t count) {
	const std::string name = str(id);
	return guard(session, true, [&] {
		if (!id) {
			return nullArgument(session, "id");
		}
		return refused(session, session->session->buy(name, count));
	});
}

psiv_status psiv_sell(psiv_session* session, const char* id, int32_t count) {
	const std::string name = str(id);
	return guard(session, true, [&] {
		if (!id) {
			return nullArgument(session, "id");
		}
		return refused(session, session->session->sell(name, count));
	});
}

psiv_status psiv_rest(psiv_session* session) {
	return guard(session, true, [&] { return refused(session, session->session->rest()); });
}

psiv_status psiv_use_item(psiv_session* session, const char* item, const char* member) {
	const std::string itemId = str(item);
	const std::string memberId = str(member);
	return guard(session, true, [&] {
		if (!item) {
			return nullArgument(session, "item");
		}
		return refused(session, session->session->useItem(itemId, memberId));
	});
}

psiv_status psiv_use_ability(psiv_session* session, const char* user, const char* ability, const char* member) {
	const std::string userId = str(user);
	const std::string abilityId = str(ability);
	const std::string memberId = str(member);
	return guard(session, true, [&] {
		if (!user) {
			return nullArgument(session, "user");
		}
		if (!ability) {
			return nullArgument(session, "ability");
		}
		return refused(session, session->session->useAbility(userId, abilityId, memberId));
	});
}

psiv_status psiv_equip(psiv_session* session, const char* member, const char* equipment) {
	const std::string memberId = str(member);
	const std::string piece = str(equipment);
	return guard(session, true, [&] {
		if (!member) {
			return nullArgument(session, "member");
		}
		if (!equipment) {
			return nullArgument(session, "equipment");
		}
		return refused(session, session->session->equip(memberId, piece));
	});
}

psiv_status psiv_unequip(psiv_session* session, const char* member, int32_t slot) {
	const std::string memberId = str(member);
	return guard(session, true, [&] {
		if (!member) {
			return nullArgument(session, "member");
		}
		if (slot < 0 || slot > PSIV_SLOT_ACCESSORY) {
			return outOfRange(session, "slot", slot);
		}
		return refused(session, session->session->unequip(memberId, static_cast<rules::EquipSlot>(slot)));
	});
}

const char* psiv_name_entry_member(psiv_session* session) {
	if (!session) {
		return "";
	}
	try {
		return session->arena.str(session->session->menuKind() == game::MenuKind::NameEntry ? session->session->nameEntryMember() : "");
	} catch (...) {
		return "";
	}
}

psiv_status psiv_enter_name(psiv_session* session, const char* name) {
	const std::string text = str(name);
	return guard(session, true, [&] {
		if (!name) {
			return nullArgument(session, "name");
		}
		return refused(session, session->session->enterName(text));
	});
}

// Journal, text log and text

psiv_status psiv_quest_log_get(psiv_session* session, psiv_quest_log* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const game::QuestLog log = session->session->questLog();
		psiv_job* jobs = session->arena.array<psiv_job>(log.jobs.size());
		for (std::size_t i = 0; i < log.jobs.size(); ++i) {
			jobs[i].id = session->arena.str(log.jobs[i].id);
			jobs[i].title = session->arena.str(log.jobs[i].title);
			jobs[i].state = static_cast<int32_t>(log.jobs[i].state);
		}
		out->objective = session->arena.str(log.objective);
		out->job_count = static_cast<int32_t>(log.jobs.size());
		out->jobs = jobs;
		return PSIV_OK;
	});
}

psiv_status psiv_encyclopedia(psiv_session* session, const psiv_codex_entry** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<game::EncyclopediaView> entries = session->session->encyclopedia();
		psiv_codex_entry* list = session->arena.array<psiv_codex_entry>(entries.size());
		for (std::size_t i = 0; i < entries.size(); ++i) {
			list[i].id = session->arena.str(entries[i].id);
			list[i].title = session->arena.str(entries[i].title);
			list[i].text = session->arena.str(entries[i].text);
		}
		*out = list;
		*count = static_cast<int32_t>(entries.size());
		return PSIV_OK;
	});
}

psiv_status psiv_glossary(psiv_session* session, const psiv_glossary_entry** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<events::GlossaryEntry>& entries = session->data->glossary.entries;
		psiv_glossary_entry* list = session->arena.array<psiv_glossary_entry>(entries.size());
		for (std::size_t i = 0; i < entries.size(); ++i) {
			list[i].id = session->arena.str(entries[i].id);
			list[i].restored = session->arena.str(entries[i].restored);
			list[i].shipped = session->arena.str(entries[i].shipped);
		}
		*out = list;
		*count = static_cast<int32_t>(entries.size());
		return PSIV_OK;
	});
}

psiv_status psiv_text_log(psiv_session* session, const char* const** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<std::string> pages = session->session->textLog();
		*out = session->arena.strs(pages);
		*count = static_cast<int32_t>(pages.size());
		return PSIV_OK;
	});
}

psiv_status psiv_pacing_table(psiv_session* session, const char** out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		*out = session->arena.str(session->session->pacing().table());
		return PSIV_OK;
	});
}

psiv_status psiv_pacing_areas(psiv_session* session, const psiv_pacing_area** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const game::PacingRecorder& pacing = session->session->pacing();
		Arena& a = session->arena;
		const std::vector<std::string>& order = pacing.areaOrder();
		psiv_pacing_area* list = a.array<psiv_pacing_area>(order.size());
		for (std::size_t i = 0; i < order.size(); ++i) {
			const game::AreaPacing& p = pacing.areas().at(order[i]);
			psiv_pacing_area& o = list[i];
			o.area = a.str(order[i]);
			o.visits = p.visits;
			o.battles = p.battles;
			o.steps = p.steps;
			o.field_ms = p.fieldMs;
			o.battle_ms = p.battleMs;
			o.event_ms = p.eventMs;
			o.experience = p.experience;
			o.money = p.money;
			o.gap_count = static_cast<int32_t>(p.msBetweenBattles.size());
			int64_t* gapMs = a.array<int64_t>(p.msBetweenBattles.size());
			std::copy(p.msBetweenBattles.begin(), p.msBetweenBattles.end(), gapMs);
			int32_t* gapSteps = a.array<int32_t>(p.stepsBetweenBattles.size());
			std::copy(p.stepsBetweenBattles.begin(), p.stepsBetweenBattles.end(), gapSteps);
			o.gap_ms = gapMs;
			o.gap_steps = gapSteps;
		}
		*out = list;
		*count = static_cast<int32_t>(order.size());
		return PSIV_OK;
	});
}

psiv_status psiv_pacing_bosses(psiv_session* session, const psiv_pacing_boss** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<game::BossPacing>& bosses = session->session->pacing().bosses();
		Arena& a = session->arena;
		psiv_pacing_boss* list = a.array<psiv_pacing_boss>(bosses.size());
		for (std::size_t i = 0; i < bosses.size(); ++i) {
			list[i].flag = a.str(bosses[i].flag);
			list[i].encounter = a.str(bosses[i].encounter);
			list[i].area = a.str(bosses[i].area);
			list[i].average_level_x100 = bosses[i].averageLevelX100;
			list[i].dungeon_ms = bosses[i].dungeonMs;
		}
		*out = list;
		*count = static_cast<int32_t>(bosses.size());
		return PSIV_OK;
	});
}

psiv_status psiv_render(psiv_session* session, const char* key, const char* const* params, int32_t param_count, const char** out) {
	return guard(session, false, [&] {
		if (!key) {
			return nullArgument(session, "key");
		}
		if (!out) {
			return nullArgument(session, "out");
		}
		if (param_count < 0 || (param_count > 0 && !params)) {
			return outOfRange(session, "param_count", param_count);
		}
		rules::Message message{key, {}};
		for (int32_t i = 0; i < param_count; ++i) {
			message.params.push_back(str(params[i]));
		}
		*out = session->arena.str(session->session->render(message));
		return PSIV_OK;
	});
}

psiv_status psiv_format_text(psiv_session* session, const char* key, const char* const* params, int32_t param_count, const char** out) {
	return guard(session, false, [&] {
		if (!key) {
			return nullArgument(session, "key");
		}
		if (!out) {
			return nullArgument(session, "out");
		}
		if (param_count < 0 || (param_count > 0 && !params)) {
			return outOfRange(session, "param_count", param_count);
		}
		std::vector<std::string> values;
		for (int32_t i = 0; i < param_count; ++i) {
			values.push_back(str(params[i]));
		}
		*out = session->arena.str(rules::formatText(session->session->language().text, key, values));
		return PSIV_OK;
	});
}

psiv_status psiv_display_name(psiv_session* session, const char* id, const char** out) {
	return guard(session, false, [&] {
		if (!id) {
			return nullArgument(session, "id");
		}
		if (!out) {
			return nullArgument(session, "out");
		}
		*out = session->arena.str(session->session->displayName(id));
		return PSIV_OK;
	});
}

psiv_status psiv_string_text(psiv_session* session, const char* id, const char** out) {
	return guard(session, false, [&] {
		if (!id) {
			return nullArgument(session, "id");
		}
		if (!out) {
			return nullArgument(session, "out");
		}
		*out = session->arena.str(session->session->stringText(id));
		return PSIV_OK;
	});
}

psiv_status psiv_description(psiv_session* session, int32_t kind, const char* id, const char** out) {
	const std::string thing = str(id);
	return guard(session, false, [&] {
		if (!id) {
			return nullArgument(session, "id");
		}
		if (!out) {
			return nullArgument(session, "out");
		}
		if (kind < 0 || kind > PSIV_DESCRIPTION_DIFFICULTY) {
			return outOfRange(session, "kind", kind);
		}
		const game::DescriptionKind k = static_cast<game::DescriptionKind>(kind);
		if (!game::describes(session->data->db, k, thing)) {
			return fail(session, PSIV_NOT_FOUND, "binding.no_such_thing", {game::toString(k), thing}, std::string("there is no ") + game::toString(k) + " '" + thing + "'");
		}
		*out = session->arena.str(session->session->description(k, thing));
		return PSIV_OK;
	});
}

}

// Developer tools

psiv_status psiv_dev_ids(psiv_session* session, int32_t list, const char* const** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		if (list < 0 || list > PSIV_DEV_CHARACTERS) {
			return outOfRange(session, "list", list);
		}
		const std::vector<std::string> ids = session->session->devList(static_cast<game::DevList>(list));
		*out = session->arena.strs(ids);
		*count = static_cast<int32_t>(ids.size());
		return PSIV_OK;
	});
}

psiv_status psiv_dev_warp(psiv_session* session, const char* area, const char* marker) {
	const std::string a = str(area);
	const std::string m = str(marker);
	return guard(session, true, [&] {
		if (!area || !marker) {
			return nullArgument(session, area ? "marker" : "area");
		}
		return refused(session, session->session->devWarp(a, m));
	});
}

psiv_status psiv_dev_warp_cell(psiv_session* session, const char* area, int32_t x, int32_t y) {
	const std::string a = str(area);
	return guard(session, true, [&] {
		if (!area) {
			return nullArgument(session, "area");
		}
		return refused(session, session->session->devWarpCell(a, {x, y}));
	});
}

psiv_status psiv_dev_set_flag(psiv_session* session, const char* flag, int32_t value) {
	const std::string f = str(flag);
	return guard(session, true, [&] {
		if (!flag) {
			return nullArgument(session, "flag");
		}
		return refused(session, session->session->devSetFlag(f, value != 0));
	});
}

psiv_status psiv_dev_give(psiv_session* session, const char* id, int32_t count) {
	const std::string thing = str(id);
	return guard(session, true, [&] {
		if (!id) {
			return nullArgument(session, "id");
		}
		return refused(session, session->session->devGive(thing, count));
	});
}

psiv_status psiv_dev_money(psiv_session* session, int32_t amount) {
	return guard(session, true, [&] { return refused(session, session->session->devMoney(amount)); });
}

psiv_status psiv_dev_set_level(psiv_session* session, const char* member, int32_t level) {
	const std::string m = str(member);
	return guard(session, true, [&] {
		if (!member) {
			return nullArgument(session, "member");
		}
		return refused(session, session->session->devSetLevel(m, level));
	});
}

psiv_status psiv_dev_battle(psiv_session* session, const char* encounter) {
	const std::string e = str(encounter);
	return guard(session, true, [&] {
		if (!encounter) {
			return nullArgument(session, "encounter");
		}
		const psiv_status status = refused(session, session->session->devBattle(e));
		if (status == PSIV_OK) {
			session->lastEvents.clear();
		}
		return status;
	});
}

psiv_status psiv_dev_end_battle(psiv_session* session, int32_t victory, const psiv_battle_event** out, int32_t* count) {
	return guard(session, true, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		std::vector<rules::Event> events;
		const psiv_status status = refused(session, session->session->devEndBattle(victory != 0, &events));
		if (status != PSIV_OK) {
			return status;
		}
		session->lastEvents = std::move(events);
		*out = fillEvents(*session, session->lastEvents, labels(shownBattle(*session)), count);
		return PSIV_OK;
	});
}

psiv_status psiv_dev_vehicle(psiv_session* session, const char* vehicle, const char* area, const char* marker) {
	const std::string v = str(vehicle);
	const std::string a = str(area);
	const std::string m = str(marker);
	return guard(session, true, [&] {
		if (!vehicle || !area || !marker) {
			return nullArgument(session, !vehicle ? "vehicle" : !area ? "area" : "marker");
		}
		return refused(session, session->session->devVehicle(v, a, m));
	});
}

psiv_status psiv_dev_encounters(psiv_session* session, int32_t on) {
	return guard(session, true, [&] { return refused(session, session->session->devEncounters(on != 0)); });
}

psiv_status psiv_dev_run_event(psiv_session* session, const char* event) {
	const std::string e = str(event);
	return guard(session, true, [&] {
		if (!event) {
			return nullArgument(session, "event");
		}
		return refused(session, session->session->devRunEvent(e));
	});
}

psiv_status psiv_dev_mark(psiv_session* session) {
	return guard(session, true, [&] {
		if (session->session->mode() != game::Mode::Title) {
			session->session->devMark();
		}
		return PSIV_OK;
	});
}

psiv_status psiv_event_position_get(psiv_session* session, psiv_event_position* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		const std::optional<game::EventPosition> at = session->session->eventPosition();
		if (!at) {
			return notFound(session, "event");
		}
		*out = psiv_event_position();
		out->event = session->arena.str(at->event);
		out->file = session->arena.str(at->file);
		out->line = at->line;
		return PSIV_OK;
	});
}

psiv_status psiv_field_zones(psiv_session* session, const psiv_zone** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const game::Area* a = currentArea(session);
		if (!a) {
			return notFound(session, "area");
		}
		psiv_zone* zones = session->arena.array<psiv_zone>(a->zones.size());
		for (std::size_t i = 0; i < a->zones.size(); ++i) {
			const game::Zone& z = a->zones[i];
			psiv_zone& view = zones[i];
			view.id = session->arena.str(z.id);
			view.from_x = z.from.x;
			view.from_y = z.from.y;
			view.to_x = z.to.x;
			view.to_y = z.to.y;
			std::vector<std::string> encounters;
			int32_t* weights = session->arena.array<int32_t>(z.table.size());
			for (std::size_t k = 0; k < z.table.size(); ++k) {
				encounters.push_back(z.table[k].encounter);
				weights[k] = z.table[k].weight;
			}
			view.entry_count = static_cast<int32_t>(z.table.size());
			view.encounters = session->arena.strs(encounters);
			view.weights = weights;
			int32_t* spawns = session->arena.array<int32_t>(z.visible.spawns.size() * 2);
			for (std::size_t k = 0; k < z.visible.spawns.size(); ++k) {
				spawns[2 * k] = z.visible.spawns[k].x;
				spawns[2 * k + 1] = z.visible.spawns[k].y;
			}
			view.spawn_count = static_cast<int32_t>(z.visible.spawns.size());
			view.spawns = spawns;
		}
		*out = zones;
		*count = static_cast<int32_t>(a->zones.size());
		return PSIV_OK;
	});
}

psiv_status psiv_save_text(psiv_session* session, const char** out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		std::string text;
		const psiv_status status = refused(session, session->session->saveText(text));
		if (status == PSIV_OK) {
			*out = session->arena.str(std::move(text));
		}
		return status;
	});
}

psiv_status psiv_load_text(psiv_session* session, const char* text) {
	const std::string t = str(text);
	return guard(session, true, [&] {
		if (!text) {
			return nullArgument(session, "text");
		}
		const psiv_status status = refused(session, session->session->loadSaveText(t));
		if (status == PSIV_OK) {
			forgetRun(*session);
		}
		return status;
	});
}

psiv_status psiv_session_reload(psiv_session* session) {
	return guard(session, true, [&] {
		createProblems.set({});
		rules::Result<game::GameData> loaded = game::loadGameData(session->directories);
		if (!loaded.ok()) {
			createProblems.set(loaded.problems);
			return fail(session, PSIV_DATA_ERROR, "binding.data_error", {loaded.error}, loaded.error);
		}
		game::Session& old = *session->session;
		std::string text;
		const bool playing = old.mode() != game::Mode::Title && old.saveText(text).empty();
		auto data = std::make_unique<game::GameData>(std::move(*loaded.value));
		auto next = std::make_unique<game::Session>(*data, session->options);
		next->setProfile(old.profile());
		next->drainOutput();
		if (playing) {
			const rules::Message refusal = next->loadSaveText(text);
			if (!refusal.empty()) {
				return fail(session, PSIV_REFUSED, refusal.key, refusal.params, next->render(refusal));
			}
			if (!old.encountersOn()) {
				next->devEncounters(false);
			}
			next->devMark();
		}
		session->session = std::move(next);
		session->data = std::move(data);
		forgetRun(*session);
		return PSIV_OK;
	});
}

// Followers, kinds, vehicle skill uses, the clear-data record and the audio list

psiv_status psiv_follower(psiv_session* session, const char** out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		*out = session->arena.str(session->session->world().follower);
		return PSIV_OK;
	});
}

psiv_status psiv_character_kind(psiv_session* session, const char* character, int32_t* out) {
	return guard(session, false, [&] {
		if (!character || !out) {
			return nullArgument(session, character ? "out" : "character");
		}
		const rules::CharacterDef* def = session->data->db.character(character);
		if (!def) {
			return notFound(session, "character");
		}
		*out = static_cast<int32_t>(def->kind);
		return PSIV_OK;
	});
}

psiv_status psiv_vehicle_skill_uses(psiv_session* session, const char* vehicle, const char* skill, int32_t* out) {
	return guard(session, false, [&] {
		if (!vehicle || !skill || !out) {
			return nullArgument(session, !vehicle ? "vehicle" : (!skill ? "skill" : "out"));
		}
		const game::World& world = session->session->world();
		const auto place = world.vehicles.find(vehicle);
		const rules::VehicleDef* def = session->data->db.vehicle(vehicle);
		const rules::AbilityDef* ability = session->data->db.ability(skill);
		if (place == world.vehicles.end() || !def || !ability || std::find(def->abilities.begin(), def->abilities.end(), skill) == def->abilities.end()) {
			return notFound(session, "vehicle skill");
		}
		const auto left = place->second.skillUses.find(skill);
		*out = left == place->second.skillUses.end() ? ability->uses : left->second;
		return PSIV_OK;
	});
}

psiv_status psiv_clear_record_exists(psiv_session* session, int32_t* out) {
	return guard(session, false, [&] {
		if (!out) {
			return nullArgument(session, "out");
		}
		*out = 0;
		if (const game::SaveStore* store = session->session->store()) {
			std::string text;
			*out = game::readFile(store->clearRecordPath(), text) && game::parseClearRecord(text).ok() ? 1 : 0;
		}
		return PSIV_OK;
	});
}

psiv_status psiv_audio_ids(psiv_session* session, const char* const** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<std::string>& audio = session->data->config.presentation.audio;
		*out = session->arena.strs(audio);
		*count = static_cast<int32_t>(audio.size());
		return PSIV_OK;
	});
}
