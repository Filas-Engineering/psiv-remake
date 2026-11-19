#include "psiv_session.h"

#include "game/data.h"
#include "game/save.h"
#include "game/session.h"

#include "events/glossary.h"
#include "events/strings.h"
#include "rules/auto_policy.h"
#include "rules/names.h"
#include "rules/progression.h"
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
#include <set>
#include <string>
#include <utility>
#include <vector>

#define PSIV_BINDING_VERSION "0.1.0"

static_assert(PSIV_MODE_SHOP == static_cast<int>(game::Mode::Shop), "psiv_mode");
static_assert(PSIV_MENU_NAME_ENTRY == static_cast<int>(game::MenuKind::NameEntry), "psiv_menu_kind");
static_assert(PSIV_DIRECTION_WEST == static_cast<int>(game::Facing::West), "psiv_direction");
static_assert(PSIV_NOTICE_AUTOSAVE == static_cast<int>(game::OutputKind::Autosave), "psiv_notice_kind");
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
static_assert(PSIV_ACTION_WARP == static_cast<int>(events::ActionType::warp), "psiv_action_type");

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

struct ActionRecord {
	std::uint64_t handle = 0;
	psiv_action_phase phase = PSIV_ACTION_BEGIN;
	events::Action action;
	std::string description;
};

void fillAction(const ActionRecord& r, psiv_action& out, Arena& arena) {
	out = psiv_action();
	out.handle = r.handle;
	out.phase = r.phase;
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
}

class BindingPresenter : public game::Presenter {
public:
	bool wait = false;
	psiv_action_callback callback = nullptr;
	void* user = nullptr;
	std::vector<ActionRecord> queue;
	std::set<std::uint64_t> running;

	bool begin(std::uint64_t handle, const events::Action& action) override {
		push({handle, PSIV_ACTION_BEGIN, action, events::describe(action)});
		if (!wait || action.instant) {
			return true;
		}
		running.insert(handle);
		return false;
	}

	bool finished(std::uint64_t handle) override { return !running.count(handle); }

	void cancel(std::uint64_t handle) override {
		running.erase(handle);
		push({handle, PSIV_ACTION_CANCEL, events::Action(), ""});
	}

private:
	void push(ActionRecord record) {
		queue.push_back(std::move(record));
		if (callback) {
			Arena scratch;
			psiv_action view;
			fillAction(queue.back(), view, scratch);
			callback(user, &view);
		}
	}
};

// A say instruction of the program. Session shows a line only as "Speaker: text", so the binding finds the
// instruction it came from to give the speaker id and portrait separately.
struct CatalogLine {
	events::Line line;
};

struct StatusSnap {
	std::string id;
	int remaining = 0;
};

struct CombatantSnap {
	std::string id;
	std::string name;
	std::string label;
	rules::Side side = rules::Side::Party;
	std::string group;
	int hp = 0;
	int maxHp = 0;
	int tp = 0;
	int maxTp = 0;
	bool defending = false;
	std::vector<StatusSnap> statuses;
	rules::Command command;
};

struct BattleSnap {
	bool present = false;
	std::string encounter;
	game::BattleSource source = game::BattleSource::Visible;
	int round = 0;
	int partyCount = 0;
	rules::Outcome outcome = rules::Outcome::Ongoing;
	bool escapeAllowed = false;
	bool gameOver = false;
	std::vector<CombatantSnap> combatants;
};

std::string str(const char* text) {
	return text ? std::string(text) : std::string();
}

}

struct psiv_session {
	std::unique_ptr<game::GameData> data;
	BindingPresenter presenter;
	std::unique_ptr<game::Session> session;
	Arena arena;
	ErrorRecord error;
	std::vector<CatalogLine> catalog;
	BattleSnap lastBattle;
	std::vector<rules::Event> lastEvents;
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

std::string label(const std::vector<CombatantSnap>& list, int index) {
	return index >= 0 && index < static_cast<int>(list.size()) ? list[index].label : std::string("?");
}

std::vector<CombatantSnap> snapshot(const Session& s, const rules::Battle& b) {
	std::vector<CombatantSnap> out;
	std::map<std::string, int> total;
	std::map<std::string, int> seen;
	for (const rules::Combatant& c : b.combatants()) {
		total[c.name]++;
	}
	for (int i = 0; i < static_cast<int>(b.combatants().size()); ++i) {
		const rules::Combatant& c = b.combatant(i);
		CombatantSnap snap;
		snap.id = c.defId;
		snap.name = b.isParty(i) ? s.session->displayName(c.defId) : c.name;
		snap.label = c.name;
		if (!b.isParty(i) && total[c.name] > 1) {
			snap.label += " " + std::string(1, static_cast<char>('A' + seen[c.name]++));
		}
		snap.side = c.side;
		snap.group = c.group;
		snap.hp = c.hp;
		snap.maxHp = c.stats.maxHp;
		snap.tp = c.tp;
		snap.maxTp = c.stats.maxTp;
		snap.defending = c.defending;
		for (const rules::ActiveStatus& st : c.statuses) {
			snap.statuses.push_back({st.id, st.remaining});
		}
		snap.command = b.command(i);
		out.push_back(std::move(snap));
	}
	return out;
}

// The battle ends inside Session::resolveRound, which drops it, so the last round's effects are applied to
// the snapshot taken before the round.
void applyEvents(BattleSnap& snap, const std::vector<rules::Event>& events) {
	auto at = [&](int index) -> CombatantSnap* { return index >= 0 && index < static_cast<int>(snap.combatants.size()) ? &snap.combatants[index] : nullptr; };
	for (const rules::Event& e : events) {
		if (e.round > snap.round) {
			snap.round = e.round;
		}
		CombatantSnap* target = at(e.target);
		switch (e.type) {
		case rules::EventType::DamageDealt:
		case rules::EventType::Healed:
		case rules::EventType::Revived:
		case rules::EventType::StatusTicked:
			if (target && e.hp >= 0) {
				target->hp = e.hp;
			}
			break;
		case rules::EventType::CombatantDefeated:
			if (target) {
				target->hp = 0;
			}
			break;
		case rules::EventType::StatusApplied:
			if (target) {
				target->statuses.erase(std::remove_if(target->statuses.begin(), target->statuses.end(), [&](const StatusSnap& st) { return st.id == e.status; }), target->statuses.end());
				target->statuses.push_back({e.status, e.duration});
			}
			break;
		case rules::EventType::StatusRemoved:
			if (target) {
				target->statuses.erase(std::remove_if(target->statuses.begin(), target->statuses.end(), [&](const StatusSnap& st) { return st.id == e.status; }), target->statuses.end());
			}
			break;
		case rules::EventType::ResourceSpent:
			if (CombatantSnap* actor = at(e.actor); actor && e.resource == rules::ResourceKind::Tp) {
				actor->tp = std::max(0, actor->tp - e.amount);
			}
			break;
		case rules::EventType::BattleEnded:
			snap.outcome = e.outcome;
			snap.gameOver = e.gameOver;
			break;
		default:
			break;
		}
	}
	for (CombatantSnap& c : snap.combatants) {
		c.command = rules::Command();
		c.defending = false;
	}
}

void fillStatuses(Session& s, const std::vector<StatusSnap>& statuses, int32_t& count, const psiv_active_status*& out) {
	psiv_active_status* list = s.arena.array<psiv_active_status>(statuses.size());
	for (std::size_t i = 0; i < statuses.size(); ++i) {
		list[i].id = s.arena.str(statuses[i].id);
		list[i].name = s.arena.str(s.session->displayName(statuses[i].id));
		list[i].remaining = statuses[i].remaining;
	}
	count = static_cast<int32_t>(statuses.size());
	out = list;
}

std::string eventText(const Session& s, const std::vector<CombatantSnap>& names, const rules::Event& e) {
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

void fillEvent(Session& s, const std::vector<CombatantSnap>& names, const rules::Event& e, psiv_battle_event& out) {
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

const psiv_battle_event* fillEvents(Session& s, int32_t* count) {
	const std::vector<CombatantSnap>& names = s.lastBattle.combatants;
	psiv_battle_event* list = s.arena.array<psiv_battle_event>(s.lastEvents.size());
	for (std::size_t i = 0; i < s.lastEvents.size(); ++i) {
		fillEvent(s, names, s.lastEvents[i], list[i]);
	}
	*count = static_cast<int32_t>(s.lastEvents.size());
	return list;
}

std::string lineDisplay(const Session& s, const events::Line& line) {
	const std::string text = s.session->scriptText(line.text);
	return line.speaker.empty() ? text : s.session->displayName(line.speaker) + ": " + text;
}

void forgetBattle(Session& s) {
	s.lastBattle = BattleSnap();
	s.lastEvents.clear();
}

const game::Area* currentArea(Session* s) {
	return s->session->area();
}

const char* fromList(const char* const* names, int count, int32_t value) {
	return value >= 0 && value < count ? names[value] : "";
}

}

namespace {

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
		PSIV_SIZE(psiv_ability_info), PSIV_SIZE(psiv_action), PSIV_SIZE(psiv_active_status), PSIV_SIZE(psiv_battle_event),
		PSIV_SIZE(psiv_battle_info), PSIV_SIZE(psiv_battle_option), PSIV_SIZE(psiv_choice), PSIV_SIZE(psiv_codex_entry),
		PSIV_SIZE(psiv_combatant), PSIV_SIZE(psiv_enemy), PSIV_SIZE(psiv_error), PSIV_SIZE(psiv_exit), PSIV_SIZE(psiv_field_info),
		PSIV_SIZE(psiv_game_info), PSIV_SIZE(psiv_glossary_entry), PSIV_SIZE(psiv_inn_info), PSIV_SIZE(psiv_inventory_entry),
		PSIV_SIZE(psiv_item_count), PSIV_SIZE(psiv_job), PSIV_SIZE(psiv_line), PSIV_SIZE(psiv_macro_step_result), PSIV_SIZE(psiv_marker),
		PSIV_SIZE(psiv_member), PSIV_SIZE(psiv_notice), PSIV_SIZE(psiv_point), PSIV_SIZE(psiv_profile), PSIV_SIZE(psiv_quest_log),
		PSIV_SIZE(psiv_recorded_command), PSIV_SIZE(psiv_save_summary), PSIV_SIZE(psiv_session_options), PSIV_SIZE(psiv_settings),
		PSIV_SIZE(psiv_shop_info), PSIV_SIZE(psiv_world_info),
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
	static const char* const menuKinds[] = {"none", "field", "inn", "save_prompt", "name_entry"};
	static const char* const noticeKinds[] = {"line", "choice", "action", "notice", "warning", "autosave"};
	static const char* const areaKinds[] = {"town", "field", "dungeon"};
	static const char* const pointKinds[] = {"talk", "step", "chest", "shop", "rest"};
	static const char* const battleSources[] = {"visible", "random", "event"};
	switch (kind) {
	case PSIV_NAME_MODE:
		return value >= 0 && value <= PSIV_MODE_SHOP ? game::toString(static_cast<game::Mode>(value)) : "";
	case PSIV_NAME_MENU_KIND:
		return fromList(menuKinds, 5, value);
	case PSIV_NAME_DIRECTION:
		return value == PSIV_DIRECTION_NONE ? "none" : value >= 0 && value <= PSIV_DIRECTION_WEST ? game::toString(static_cast<game::Facing>(value)) : "";
	case PSIV_NAME_NOTICE_KIND:
		return fromList(noticeKinds, 6, value);
	case PSIV_NAME_AREA_KIND:
		return fromList(areaKinds, 3, value);
	case PSIV_NAME_ENCOUNTER_MODE:
		return value >= 0 && value <= PSIV_ENCOUNTERS_RANDOM ? game::toString(static_cast<game::EncounterMode>(value)) : "";
	case PSIV_NAME_ENEMY_STATE:
		return value >= 0 && value <= PSIV_ENEMY_RETURNING ? game::toString(static_cast<game::EnemyState>(value)) : "";
	case PSIV_NAME_POINT_KIND:
		return fromList(pointKinds, 5, value);
	case PSIV_NAME_JOB_STATE:
		return value >= 0 && value <= PSIV_JOB_COMPLETED ? game::toString(static_cast<game::JobState>(value)) : "";
	case PSIV_NAME_TEXT_SPEED:
		return value >= 0 && value <= PSIV_TEXT_INSTANT ? game::toString(static_cast<game::TextSpeed>(value)) : "";
	case PSIV_NAME_BATTLE_SPEED:
		return value >= 0 && value <= PSIV_BATTLE_X3 ? game::toString(static_cast<game::BattleSpeed>(value)) : "";
	case PSIV_NAME_REWARD_MULTIPLIER:
		return value >= 0 && value <= PSIV_REWARD_X2 ? rules::toString(static_cast<rules::RewardMultiplier>(value)) : "";
	case PSIV_NAME_BATTLE_SOURCE:
		return fromList(battleSources, 3, value);
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
		return value >= 0 && value <= PSIV_EVENT_BATTLE_ENDED ? rules::toString(static_cast<rules::EventType>(value)) : "";
	case PSIV_NAME_CANCEL_REASON:
		return value >= 0 && value <= PSIV_CANCEL_NO_TARGET ? rules::toString(static_cast<rules::CancelReason>(value)) : "";
	case PSIV_NAME_EFFECTIVENESS:
		return value >= 0 && value <= PSIV_EFFECT_IMMUNE ? rules::toString(static_cast<rules::Effectiveness>(value)) : "";
	case PSIV_NAME_STATUS_END:
		return value >= 0 && value <= PSIV_STATUS_END_DEFEATED ? rules::toString(static_cast<rules::StatusEnd>(value)) : "";
	case PSIV_NAME_RESOURCE_KIND:
		return value >= 0 && value <= PSIV_RESOURCE_ITEM ? rules::toString(static_cast<rules::ResourceKind>(value)) : "";
	case PSIV_NAME_ELEMENT:
		return value >= 0 && value <= PSIV_ELEMENT_GALE ? rules::toString(static_cast<rules::Element>(value)) : "";
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
	default:
		return "";
	}
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
		rules::Result<game::GameData> loaded = game::loadGameData(options->data_dir);
		if (!loaded.ok()) {
			return fail(nullptr, PSIV_DATA_ERROR, "binding.data_error", {loaded.error}, loaded.error);
		}
		auto s = std::make_unique<psiv_session>();
		s->data = std::make_unique<game::GameData>(std::move(*loaded.value));
		s->presenter.wait = options->presentation == PSIV_PRESENTATION_WAIT;
		game::SessionOptions sessionOptions;
		sessionOptions.saveDirectory = str(options->save_dir);
		sessionOptions.seed = options->seed;
		if (options->encounter_mode >= 0) {
			sessionOptions.encounterMode = static_cast<game::EncounterMode>(options->encounter_mode);
		}
		if (options->wall_clock) {
			sessionOptions.clock = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
		}
		sessionOptions.presenter = &s->presenter;
		s->session = std::make_unique<game::Session>(*s->data, std::move(sessionOptions));
		for (const events::Instruction& instruction : s->data->program.code) {
			if (instruction.op == events::Op::say) {
				s->catalog.push_back({instruction.dialogue});
			}
		}
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

psiv_status psiv_settings_set_auto_macro(psiv_session* session, const char* macro) {
	const std::string name = str(macro);
	return guard(session, true, [&] {
		// Session has no setter: the auto macro is world state the terminal front end writes directly.
		game::World& w = session->session->world();
		if (!name.empty() && std::none_of(w.macros.begin(), w.macros.end(), [&](const rules::Macro& m) { return m.name == name; })) {
			return fail(session, PSIV_NOT_FOUND, "game.refuse.no_macro", {name}, session->session->render({"game.refuse.no_macro", {name}}));
		}
		w.autoMacro = name;
		return PSIV_OK;
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

// Title and saves

psiv_status psiv_new_game(psiv_session* session) {
	return guard(session, true, [&] {
		const psiv_status status = refused(session, session->session->newGame());
		if (status == PSIV_OK) {
			forgetBattle(*session);
		}
		return status;
	});
}

psiv_status psiv_continue_game(psiv_session* session) {
	return guard(session, true, [&] {
		const psiv_status status = refused(session, session->session->continueGame());
		if (status == PSIV_OK) {
			forgetBattle(*session);
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
			forgetBattle(*session);
		}
		return status;
	});
}

psiv_status psiv_retry_battle(psiv_session* session) {
	return guard(session, true, [&] {
		const psiv_status status = refused(session, session->session->retryBattle());
		if (status == PSIV_OK) {
			forgetBattle(*session);
		}
		return status;
	});
}

psiv_status psiv_quit_to_title(psiv_session* session) {
	return guard(session, true, [&] {
		session->session->quitToTitle();
		forgetBattle(*session);
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
		if (session->session->mode() != game::Mode::Field) {
			return refused(session, {"game.refuse.walk_field_only", {}});
		}
		// Session has no turn call; facing is world state, as the terminal front end sets it.
		session->session->world().facing = static_cast<game::Facing>(direction);
		return PSIV_OK;
	});
}

psiv_status psiv_interact(psiv_session* session) {
	return guard(session, true, [&] { return refused(session, session->session->interact()); });
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
		psiv_point* list = arena.array<psiv_point>(points.size());
		std::copy(points.begin(), points.end(), list);
		*out = list;
		*count = static_cast<int32_t>(points.size());
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
		const std::string display = session->session->lineText();
		*out = psiv_line();
		out->display = session->arena.str(display);
		for (const CatalogLine& entry : session->catalog) {
			if (lineDisplay(*session, entry.line) == display) {
				out->speaker_id = session->arena.str(entry.line.speaker);
				out->speaker_name = session->arena.str(entry.line.speaker.empty() ? "" : session->session->displayName(entry.line.speaker));
				out->text = session->arena.str(session->session->scriptText(entry.line.text));
				out->portrait = session->arena.str(entry.line.portrait);
				return PSIV_OK;
			}
		}
		out->speaker_id = "";
		out->speaker_name = "";
		out->text = out->display;
		out->portrait = "";
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
		const events::ChoiceRequest* c = session->session->choice();
		if (!c) {
			return notFound(session, "choice");
		}
		*out = psiv_choice();
		out->prompt = session->arena.str(c->prompt.value.empty() ? "" : session->session->scriptText(c->prompt));
		out->yes_no = c->yes_no;
		std::vector<std::string> options;
		if (c->yes_no) {
			// The game's text tables have no yes/no strings, so these are the terminal's own words.
			options = {"yes", "no"};
		} else {
			for (const events::Text& t : c->options) {
				options.push_back(session->session->scriptText(t));
			}
		}
		out->option_count = static_cast<int32_t>(options.size());
		out->options = session->arena.strs(options);
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

psiv_status psiv_actions_poll(psiv_session* session, const psiv_action** out, int32_t* count) {
	return guard(session, true, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		std::vector<ActionRecord> records;
		records.swap(session->presenter.queue);
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
		session->presenter.running.erase(handle);
		return PSIV_OK;
	});
}

psiv_status psiv_actions_set_callback(psiv_session* session, psiv_action_callback callback, void* user) {
	return guard(session, false, [&] {
		session->presenter.callback = callback;
		session->presenter.user = user;
		return PSIV_OK;
	});
}

// Notices

psiv_status psiv_notices_drain(psiv_session* session, const psiv_notice** out, int32_t* count) {
	return guard(session, true, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const std::vector<game::Output> outputs = session->session->drainOutput();
		psiv_notice* list = session->arena.array<psiv_notice>(outputs.size());
		for (std::size_t i = 0; i < outputs.size(); ++i) {
			list[i].kind = static_cast<int32_t>(outputs[i].kind);
			list[i].key = session->arena.str(outputs[i].message.key);
			list[i].param_count = static_cast<int32_t>(outputs[i].message.params.size());
			list[i].params = session->arena.strs(outputs[i].message.params);
			list[i].text = session->arena.str(outputs[i].text);
		}
		*out = list;
		*count = static_cast<int32_t>(outputs.size());
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
		*out = psiv_battle_info();
		if (const rules::Battle* b = g.battle()) {
			out->live = 1;
			out->encounter = session->arena.str(g.battleEncounter());
			out->source = static_cast<int32_t>(g.battleSource());
			out->round = b->round();
			out->party_count = b->partyCount();
			out->combatant_count = static_cast<int32_t>(b->combatants().size());
			out->outcome = static_cast<int32_t>(b->outcome());
			out->escape_allowed = b->escapeAllowed();
			out->auto_active = b->autoActive();
			out->party_acts_next_round = b->sideActsNextRound(rules::Side::Party);
			out->game_over = b->gameOver();
			return PSIV_OK;
		}
		const BattleSnap& snap = session->lastBattle;
		if (!snap.present) {
			return notFound(session, "battle");
		}
		out->live = 0;
		out->encounter = session->arena.str(snap.encounter);
		out->source = static_cast<int32_t>(snap.source);
		out->round = snap.round;
		out->party_count = snap.partyCount;
		out->combatant_count = static_cast<int32_t>(snap.combatants.size());
		out->outcome = static_cast<int32_t>(snap.outcome);
		out->escape_allowed = snap.escapeAllowed;
		out->game_over = snap.gameOver;
		return PSIV_OK;
	});
}

psiv_status psiv_battle_combatants(psiv_session* session, const psiv_combatant** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		std::vector<CombatantSnap> list;
		if (const rules::Battle* b = session->session->battle()) {
			list = snapshot(*session, *b);
		} else if (session->lastBattle.present) {
			list = session->lastBattle.combatants;
		} else {
			return notFound(session, "battle");
		}
		Arena& a = session->arena;
		psiv_combatant* items = a.array<psiv_combatant>(list.size());
		for (std::size_t i = 0; i < list.size(); ++i) {
			const CombatantSnap& c = list[i];
			psiv_combatant& o = items[i];
			o.index = static_cast<int32_t>(i);
			o.id = a.str(c.id);
			o.name = a.str(c.name);
			o.label = a.str(c.label);
			o.side = static_cast<int32_t>(c.side);
			o.group = a.str(c.group);
			o.hp = c.hp;
			o.max_hp = c.maxHp;
			o.tp = c.tp;
			o.max_tp = c.maxTp;
			o.alive = c.hp > 0;
			o.defending = c.defending;
			fillStatuses(*session, c.statuses, o.status_count, o.statuses);
			o.command_kind = static_cast<int32_t>(c.command.kind);
			o.command_id = a.str(c.command.id);
			o.command_target = c.command.target;
		}
		*out = items;
		*count = static_cast<int32_t>(list.size());
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

psiv_status psiv_battle_resolve_round(psiv_session* session, const psiv_battle_event** out, int32_t* count) {
	return guard(session, true, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		game::Session& g = *session->session;
		const rules::Battle* b = g.battle();
		if (!b) {
			return refused(session, {"game.refuse.no_battle", {}});
		}
		BattleSnap snap;
		snap.present = true;
		snap.encounter = g.battleEncounter();
		snap.source = g.battleSource();
		snap.round = b->round();
		snap.partyCount = b->partyCount();
		snap.outcome = b->outcome();
		snap.escapeAllowed = b->escapeAllowed();
		snap.combatants = snapshot(*session, *b);
		session->lastEvents = g.resolveRound();
		applyEvents(snap, session->lastEvents);
		session->lastBattle = std::move(snap);
		*out = fillEvents(*session, count);
		return PSIV_OK;
	});
}

psiv_status psiv_battle_last_events(psiv_session* session, const psiv_battle_event** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		*out = fillEvents(*session, count);
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
			std::vector<StatusSnap> statuses;
			for (const rules::ActiveStatus& st : m.statuses) {
				statuses.push_back({st.id, st.remaining});
			}
			fillStatuses(*session, statuses, o.status_count, o.statuses);
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
			abilities.push_back({a.str(abilityId), a.str(def->name), static_cast<int32_t>(def->kind), static_cast<int32_t>(def->targeting), def->cost, def->field});
		}
		psiv_ability_info* list = a.array<psiv_ability_info>(abilities.size());
		std::copy(abilities.begin(), abilities.end(), list);
		*out = list;
		*count = static_cast<int32_t>(abilities.size());
		return PSIV_OK;
	});
}


psiv_status psiv_inventory(psiv_session* session, const psiv_inventory_entry** out, int32_t* count) {
	return guard(session, false, [&] {
		if (!out || !count) {
			return nullArgument(session, out ? "count" : "out");
		}
		const game::World& w = session->session->world();
		std::vector<psiv_inventory_entry> entries;
		for (const auto& [id, n] : w.items) {
			entries.push_back(entryFor(*session, id, n));
		}
		for (const auto& [id, n] : w.equipmentBag) {
			entries.push_back(entryFor(*session, id, n));
		}
		psiv_inventory_entry* list = session->arena.array<psiv_inventory_entry>(entries.size());
		std::copy(entries.begin(), entries.end(), list);
		*out = list;
		*count = static_cast<int32_t>(entries.size());
		return PSIV_OK;
	});
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

}
