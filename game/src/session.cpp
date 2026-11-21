#include "game/session.h"

#include "game/encounters.h"

#include "events/glossary.h"
#include "events/save_state.h"
#include "events/strings.h"
#include "rules/auto_policy.h"
#include "rules/field.h"
#include "rules/formulas.h"
#include "rules/names.h"
#include "rules/progression.h"
#include "rules/setup.h"
#include "rules/text.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>

namespace game {

namespace {

const char* kAreaEntry = "area_entry";
const char* kEventEnd = "event_end";
const char* kBeforeBattle = "before_battle";
const char* kAfterRest = "after_rest";
const char* kVehicle = "vehicle";

constexpr std::size_t kTextLogPages = 500;
constexpr std::size_t kMaxNameLength = 16;
constexpr std::size_t kInstantActionRecords = 256;
constexpr int kMaxWalkSteps = 4000;

// A save is plain text a player can edit, so everything in it that names data or indexes a map is checked against the
// data before the world is used. Returns what is wrong, in English, or empty.
std::string invalidWorld(const GameData& data, const World& w) {
	const rules::Database& db = data.db;
	auto cellIn = [&](const std::string& areaId, Cell cell) {
		const Area* a = data.area(areaId);
		return a && a->inside(cell);
	};
	auto named = [](const std::string& what, const std::string& id) { return what + " '" + id + "'"; };
	if (!cellIn(w.area, w.position)) {
		return "the party stands outside area '" + w.area + "'";
	}
	if (w.lineup.empty() || w.lineup.size() > static_cast<std::size_t>(rules::kMaxPartySize)) {
		return "the lineup needs 1 to " + std::to_string(rules::kMaxPartySize) + " members";
	}
	std::set<std::string> seen;
	for (const rules::PartyMember& m : w.roster) {
		if (!db.character(m.characterId) || !seen.insert(m.characterId).second) {
			return named("unknown or repeated member", m.characterId);
		}
		if (m.level < 1 || m.level > rules::formulas::kMaxLevel || m.experience < 0 || m.hp < 0 || m.tp < 0) {
			return named("out-of-range level, experience or points for", m.characterId);
		}
		for (const auto& [slot, piece] : m.equipment) {
			const rules::EquipmentDef* def = db.equipmentPiece(piece);
			if (!def || !rules::fitsPosition(*def, slot)) {
				return named("unknown or misplaced piece", piece);
			}
		}
		for (const auto& [ability, uses] : m.skillUses) {
			if (!db.ability(ability) || uses < 0) {
				return named("bad skill uses for", ability);
			}
		}
		for (const rules::ActiveStatus& status : m.statuses) {
			const rules::StatusDef* def = db.status(status.id);
			if (!def || !def->persistsOutsideBattle || status.remaining < 0) {
				return named("bad status", status.id);
			}
		}
		for (const std::string& ability : m.taught) {
			if (!db.ability(ability)) {
				return named("unknown taught ability", ability);
			}
		}
	}
	std::set<std::string> inLineup;
	for (const std::string& id : w.lineup) {
		if (!seen.count(id) || !inLineup.insert(id).second) {
			return named("lineup member not in the roster or repeated", id);
		}
	}
	for (const auto& [id, name] : w.memberNames) {
		if (!db.character(id)) {
			return named("name for unknown character", id);
		}
	}
	for (const auto& [id, count] : w.items) {
		if (!db.item(id) || count < 1 || count > data.config.itemCap) {
			return named("unknown item or count out of range", id);
		}
	}
	for (const auto& [id, count] : w.equipmentBag) {
		if (!db.equipmentPiece(id) || count < 1 || count > data.config.itemCap) {
			return named("unknown piece or count out of range", id);
		}
	}
	if (w.money < 0 || w.money > data.config.moneyCap) {
		return "money out of range";
	}
	for (const auto& [name, value] : w.flags) {
		const events::Declaration* d = data.registry.find(name);
		if (!d || !d->is_flag || d->local) {
			return named("undeclared flag", name);
		}
	}
	for (const auto& [name, value] : w.vars) {
		const events::Declaration* d = data.registry.find(name);
		if (!d || d->is_flag || d->local) {
			return named("undeclared variable", name);
		}
	}
	if (w.macros.size() > static_cast<std::size_t>(kMaxMacros)) {
		return "too many macros";
	}
	bool autoFound = w.autoMacro.empty();
	for (const rules::Macro& macro : w.macros) {
		autoFound = autoFound || macro.name == w.autoMacro;
		if (macro.steps.size() > static_cast<std::size_t>(rules::kMaxPartySize)) {
			return named("too many steps in macro", macro.name);
		}
		for (const rules::MacroStep& step : macro.steps) {
			const bool badTarget = step.target.kind == rules::MacroTargetKind::Enemy && (step.target.enemySlot < 0 || step.target.enemySlot >= rules::kMaxEnemies);
			const bool badMember = step.target.kind == rules::MacroTargetKind::Member && !db.character(step.target.member);
			const bool badId = (step.kind == rules::CommandKind::Ability && !db.ability(step.id)) || (step.kind == rules::CommandKind::Item && !db.item(step.id));
			if (!db.character(step.member) || badTarget || badMember || badId) {
				return named("bad step in macro", macro.name);
			}
		}
	}
	if (!autoFound) {
		return named("unknown auto macro", w.autoMacro);
	}
	for (const rules::ComboLogEntry& entry : w.comboLog.entries()) {
		const bool known = std::any_of(db.combos.begin(), db.combos.end(), [&](const rules::ComboRecipe& r) { return r.id == entry.recipe; });
		if (!known || entry.playTimeMs < 0) {
			return named("unknown combination", entry.recipe);
		}
	}
	for (const std::string& id : w.visitedAreas) {
		if (!data.area(id)) {
			return named("unknown visited area", id);
		}
	}
	const Area& here = *data.area(w.area);
	for (const VisibleEnemy& e : w.enemies) {
		const bool zoneHere = std::any_of(here.zones.begin(), here.zones.end(), [&](const Zone& z) { return z.id == e.zone; });
		if (!zoneHere || !db.encounter(e.encounter) || !here.inside(e.spawn) || !here.inside(e.position) || e.id < 1 || e.id >= w.nextEnemyId || e.stateMs < 0 || e.moveProgress < 0) {
			return "bad visible enemy " + std::to_string(e.id);
		}
	}
	for (const auto& [id, place] : w.actors) {
		if (std::find(data.config.actors.begin(), data.config.actors.end(), id) == data.config.actors.end() || (!place.area.empty() && !cellIn(place.area, place.cell))) {
			return named("unknown actor or cell outside its area for", id);
		}
	}
	for (const auto& [id, place] : w.vehicles) {
		const FieldVehicle* field = data.vehicle(id);
		const rules::VehicleDef* def = db.vehicle(id);
		if (!field || !def || !cellIn(place.area, place.cell) || !cellIn(place.homeArea, place.home)) {
			return named("unknown vehicle or cell outside its area for", id);
		}
		for (const auto& [skill, uses] : place.skillUses) {
			if (std::find(def->abilities.begin(), def->abilities.end(), skill) == def->abilities.end() || uses < 0) {
				return named("bad skill uses for vehicle", id);
			}
		}
	}
	if (!w.aboard.empty()) {
		const auto ridden = w.vehicles.find(w.aboard);
		if (ridden == w.vehicles.end() || ridden->second.area != w.area || ridden->second.cell != w.position) {
			return named("aboard a vehicle the party does not have where it stands", w.aboard);
		}
	}
	if (!w.follower.empty() && std::find(data.config.actors.begin(), data.config.actors.end(), w.follower) == data.config.actors.end()) {
		return named("unknown follower", w.follower);
	}
	if (!w.dungeonExitArea.empty() && !cellIn(w.dungeonExitArea, w.dungeonExitCell)) {
		return "the way out of the dungeon is outside its area";
	}
	if (!w.shipPort.empty() && !data.port(w.shipPort)) {
		return named("unknown ship port", w.shipPort);
	}
	for (const auto& [areaId, cells] : w.brokenCells) {
		for (Cell c : cells) {
			if (!cellIn(areaId, c)) {
				return named("broken barrier outside its area", areaId);
			}
		}
	}
	for (Cell c : w.brokenHere) {
		if (!here.inside(c)) {
			return "broken barrier outside the current area";
		}
	}
	if (w.graceMs < 0 || w.randomCounter < 0 || w.randomThreshold < 0 || w.playTimeMs < 0) {
		return "negative grace, random counter or play time";
	}
	for (const auto& job : w.jobs) {
		if (std::none_of(data.journal.jobs.begin(), data.journal.jobs.end(), [&](const Job& j) { return j.id == job.first; })) {
			return named("unknown job", job.first);
		}
	}
	for (const std::string& id : w.unlocked) {
		if (std::none_of(data.journal.encyclopedia.begin(), data.journal.encyclopedia.end(), [&](const EncyclopediaEntry& e) { return e.id == id; })) {
			return named("unknown encyclopedia entry", id);
		}
	}
	if (!db.difficulty(w.settings.difficulty)) {
		return named("unknown difficulty", w.settings.difficulty);
	}
	return "";
}

bool hookHolds(const Hook& hook, const World& world) {
	return (hook.requireFlag.empty() || world.flag(hook.requireFlag)) && (hook.blockFlag.empty() || !world.flag(hook.blockFlag));
}

bool rangeHolds(const FlagRange& range, const World& world) {
	return (range.after.empty() || world.flag(range.after)) && (range.until.empty() || !world.flag(range.until));
}

rules::Message refusal(const char* key, std::vector<std::string> params = {}) {
	return {std::string("game.refuse.") + key, std::move(params)};
}

// Spaces at the ends dropped; 1 to kMaxNameLength bytes with no control characters, else empty.
std::string cleanName(const std::string& name) {
	const std::size_t first = name.find_first_not_of(' ');
	const std::size_t last = name.find_last_not_of(' ');
	const std::string trimmed = first == std::string::npos ? "" : name.substr(first, last - first + 1);
	if (trimmed.size() > kMaxNameLength || std::any_of(trimmed.begin(), trimmed.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F; })) {
		return "";
	}
	return trimmed;
}

std::vector<rules::Macro>::iterator findMacro(std::vector<rules::Macro>& macros, const std::string& name) {
	return std::find_if(macros.begin(), macros.end(), [&](const rules::Macro& m) { return m.name == name; });
}

// The macro editor's mark for a step (UI-005): the first reason the battle would skip or cancel it, judged on the
// member as it stands outside battle.
rules::MacroIssue stepIssue(const rules::Database& db, const World& world, const rules::MacroStep& step) {
	const rules::PartyMember* m = world.member(step.member);
	if (!m || std::find(world.lineup.begin(), world.lineup.end(), step.member) == world.lineup.end()) {
		return rules::MacroIssue::NotInParty;
	}
	if (m->hp <= 0) {
		return rules::MacroIssue::ActorDefeated;
	}
	if (step.kind == rules::CommandKind::Ability) {
		const std::vector<std::string> known = rules::knownAbilities(db, *m);
		const rules::AbilityDef* ability = db.ability(step.id);
		if (!ability || std::find(known.begin(), known.end(), step.id) == known.end()) {
			return rules::MacroIssue::UnknownAbility;
		}
		if (ability->kind == rules::AbilityKind::Technique) {
			const bool silenced = std::any_of(m->statuses.begin(), m->statuses.end(), [&](const rules::ActiveStatus& active) {
				const rules::StatusDef* status = db.status(active.id);
				return status && status->blocksTechniques;
			});
			if (silenced) {
				return rules::MacroIssue::Silenced;
			}
			if (m->tp < ability->cost) {
				return rules::MacroIssue::NotEnoughTp;
			}
		}
		if (ability->kind == rules::AbilityKind::Skill) {
			auto uses = m->skillUses.find(step.id);
			if (uses == m->skillUses.end() || uses->second <= 0) {
				return rules::MacroIssue::NoUsesLeft;
			}
		}
	}
	if (step.kind == rules::CommandKind::Item) {
		auto held = world.items.find(step.id);
		if (held == world.items.end() || held->second <= 0) {
			return rules::MacroIssue::NoItem;
		}
	}
	return rules::MacroIssue::None;
}

// The data check makes sure an enemy_defeated end names an enemy of the encounter; its first position counts.
rules::ScriptedEnd scriptedEnd(const events::BattleRequest& request, const rules::Database& db) {
	rules::ScriptedEnd end;
	if (request.end == events::BattleEndKind::survive_rounds) {
		end.kind = rules::ScriptedEndKind::SurviveRounds;
		end.rounds = request.end_rounds;
	} else if (request.end == events::BattleEndKind::enemy_defeated) {
		const rules::Encounter* encounter = db.encounter(request.encounter);
		for (std::size_t i = 0; encounter && i < encounter->enemies.size(); ++i) {
			if (encounter->enemies[i].enemy == request.end_enemy) {
				end.kind = rules::ScriptedEndKind::EnemyDefeated;
				end.enemySlot = static_cast<int>(i);
				break;
			}
		}
	}
	return end;
}

// The talk point named after an actor: where the actor stands in that area until a script moves it.
const Hook* actorHome(const Area* area, const std::string& actor) {
	if (!area) {
		return nullptr;
	}
	for (const Hook& hook : area->hooks) {
		if (hook.trigger == HookTrigger::Interact && hook.id == actor) {
			return &hook;
		}
	}
	return nullptr;
}

// Toward `to` along the longer axis; a tie turns north or south. Nothing when the cells are the same.
std::optional<Facing> facingToward(Cell from, Cell to) {
	const int dx = to.x - from.x;
	const int dy = to.y - from.y;
	if (dx == 0 && dy == 0) {
		return std::nullopt;
	}
	if (std::abs(dx) > std::abs(dy)) {
		return dx > 0 ? Facing::East : Facing::West;
	}
	return dy > 0 ? Facing::South : Facing::North;
}

rules::Opening toOpening(events::BattleOpening opening) {
	switch (opening) {
	case events::BattleOpening::party_first: return rules::Opening::PartyFirst;
	case events::BattleOpening::enemy_first: return rules::Opening::EnemyFirst;
	case events::BattleOpening::normal: break;
	}
	return rules::Opening::Normal;
}

}

const char* toString(OutputKind kind) {
	switch (kind) {
	case OutputKind::Line: return "line";
	case OutputKind::Choice: return "choice";
	case OutputKind::Action: return "action";
	case OutputKind::Notice: return "notice";
	case OutputKind::Warning: return "warning";
	case OutputKind::Autosave: return "autosave";
	case OutputKind::Ambient: return "ambient";
	}
	return "notice";
}

const char* toString(MenuKind kind) {
	switch (kind) {
	case MenuKind::None: return "none";
	case MenuKind::Field: return "field";
	case MenuKind::Inn: return "inn";
	case MenuKind::SavePrompt: return "save_prompt";
	case MenuKind::NameEntry: return "name_entry";
	case MenuKind::Port: return "port";
	}
	return "none";
}

const char* toString(TravelKind kind) {
	switch (kind) {
	case TravelKind::Port: return "port";
	case TravelKind::Town: return "town";
	case TravelKind::DungeonExit: return "dungeon_exit";
	}
	return "port";
}

bool parseTravelKind(const std::string& text, TravelKind& out) {
	for (TravelKind kind : {TravelKind::Port, TravelKind::Town, TravelKind::DungeonExit}) {
		if (text == toString(kind)) {
			out = kind;
			return true;
		}
	}
	return false;
}

const char* toString(ItemSort sort) {
	switch (sort) {
	case ItemSort::Id: return "id";
	case ItemSort::Kind: return "kind";
	case ItemSort::Name: return "name";
	case ItemSort::Count: return "count";
	}
	return "";
}

const char* toString(BattleSource source) {
	switch (source) {
	case BattleSource::Visible: return "visible";
	case BattleSource::Random: return "random";
	case BattleSource::Event: return "event";
	}
	return "visible";
}

class Session::ActionQueue : public Presenter {
public:
	ActionQueue(const PresentationNames& names, Presentation presentation, std::function<void(const PresentedAction&)> onAction)
		: names_(names), queued_(presentation == Presentation::Queued), onAction_(std::move(onAction)) {}

	bool begin(std::uint64_t handle, const events::Action& action) override {
		push({handle, ActionPhase::Begin, action, events::describe(action), events::action_seconds(action, names_.durations, names_.subjectDurations)});
		if (!queued_ || action.instant) {
			return true;
		}
		running_.insert(handle);
		return false;
	}

	bool finished(std::uint64_t handle) override { return !running_.count(handle); }

	void cancel(std::uint64_t handle) override {
		running_.erase(handle);
		push({handle, ActionPhase::Cancel, events::Action(), "", 0});
	}

	void finish(std::uint64_t handle) { running_.erase(handle); }

	std::vector<PresentedAction> poll() {
		std::vector<PresentedAction> out(records_.begin(), records_.end());
		records_.clear();
		return out;
	}

private:
	void push(PresentedAction record) {
		if (!queued_ && records_.size() == kInstantActionRecords) {
			records_.pop_front();
		}
		records_.push_back(std::move(record));
		if (onAction_) {
			onAction_(records_.back());
		}
	}

	const PresentationNames& names_;
	bool queued_;
	std::function<void(const PresentedAction&)> onAction_;
	std::deque<PresentedAction> records_;
	std::set<std::uint64_t> running_;
};

const std::vector<std::string>& sessionTextKeys() {
	static const std::vector<std::string> keys = {
		"game.arrive",
		"game.auto_macro_cleared",
		"game.auto_on",
		"game.autosave_failed",
		"game.autosave_point",
		"game.autosaved",
		"game.barrier_broken",
		"game.battle_start",
		"game.boarded",
		"game.boarded_ship",
		"game.bought",
		"game.chest_empty",
		"game.clear_not_recorded",
		"game.clear_recorded",
		"game.dev_marked",
		"game.dropped",
		"game.equipment_returned",
		"game.equipped",
		"game.event_cannot_resume",
		"game.event_damaged",
		"game.fallen",
		"game.fell",
		"game.follower_joined",
		"game.follower_left",
		"game.found",
		"game.found_money",
		"game.game_over",
		"game.got_away",
		"game.healed",
		"game.helm_open",
		"game.inn_offer",
		"game.job.accepted",
		"game.job.available",
		"game.job.completed",
		"game.job.missed",
		"game.joins",
		"game.learned",
		"game.left_ship",
		"game.left_vehicle",
		"game.leaves",
		"game.level_up",
		"game.loaded",
		"game.loaded_text",
		"game.money_given",
		"game.money_taken",
		"game.named",
		"game.no_effect",
		"game.port_open",
		"game.reason.after_rest",
		"game.reason.area_entry",
		"game.reason.before_battle",
		"game.reason.event_end",
		"game.reason.vehicle",
		"game.refuse.aboard",
		"game.refuse.blocked",
		"game.refuse.buy_one",
		"game.refuse.call_outdoors",
		"game.refuse.combo_no_member",
		"game.refuse.combo_not_found",
		"game.refuse.carry_cap",
		"game.refuse.dev_bad_cell",
		"game.refuse.dev_bad_count",
		"game.refuse.dev_bad_level",
		"game.refuse.dev_field_only",
		"game.refuse.dev_in_battle",
		"game.refuse.dev_unknown_area",
		"game.refuse.dev_unknown_encounter",
		"game.refuse.dev_unknown_event",
		"game.refuse.dev_unknown_flag",
		"game.refuse.dev_unknown_marker",
		"game.refuse.dev_unknown_member",
		"game.refuse.dev_unknown_thing",
		"game.refuse.dev_unknown_vehicle",
		"game.refuse.dev_vehicle_outdoors",
		"game.refuse.equip_menu_only",
		"game.refuse.in_dungeon",
		"game.refuse.in_the_way",
		"game.refuse.interact_field_only",
		"game.refuse.items_menu_only",
		"game.refuse.load_title",
		"game.refuse.macro_bad_command",
		"game.refuse.macro_member_twice",
		"game.refuse.macro_menu_only",
		"game.refuse.macro_name_taken",
		"game.refuse.macro_no_position",
		"game.refuse.macro_no_step",
		"game.refuse.macro_slots_full",
		"game.refuse.macro_steps_full",
		"game.refuse.macro_target",
		"game.refuse.macro_unknown_ability",
		"game.refuse.macro_unknown_item",
		"game.refuse.macro_unknown_member",
		"game.refuse.menu_cannot_open",
		"game.refuse.menu_field_only",
		"game.refuse.name_length",
		"game.refuse.new_game_title",
		"game.refuse.no_answer",
		"game.refuse.no_auto_macro",
		"game.refuse.no_battle",
		"game.refuse.no_choice",
		"game.refuse.no_control_act",
		"game.refuse.no_control_move",
		"game.refuse.no_destination",
		"game.refuse.no_game",
		"game.refuse.no_macro",
		"game.refuse.no_menu",
		"game.refuse.no_money",
		"game.refuse.no_name_entry",
		"game.refuse.no_port",
		"game.refuse.no_retry",
		"game.refuse.no_save",
		"game.refuse.no_shop",
		"game.refuse.no_slot",
		"game.refuse.no_town",
		"game.refuse.no_way_out",
		"game.refuse.nobody_talks",
		"game.refuse.not_aboard",
		"game.refuse.not_bought",
		"game.refuse.not_dungeon",
		"game.refuse.not_enough_held",
		"game.refuse.not_sold",
		"game.refuse.not_travel",
		"game.refuse.nothing_there",
		"game.refuse.nothing_worn",
		"game.refuse.nowhere_leave",
		"game.refuse.nowhere_rest",
		"game.refuse.on_ship",
		"game.refuse.party_menu_only",
		"game.refuse.party_no_position",
		"game.refuse.pick_option",
		"game.refuse.port_closed",
		"game.refuse.profile_not_written",
		"game.refuse.save_damaged",
		"game.refuse.save_failed",
		"game.refuse.save_in_event",
		"game.refuse.save_invalid",
		"game.refuse.save_unknown_area",
		"game.refuse.save_where",
		"game.refuse.saving_disabled",
		"game.refuse.sell_one",
		"game.refuse.settings_in_battle",
		"game.refuse.settings_no_game",
		"game.refuse.shop_closed",
		"game.refuse.slot_unreadable",
		"game.refuse.talk_field_only",
		"game.refuse.tech_menu_only",
		"game.refuse.travel_item",
		"game.refuse.travel_menu_only",
		"game.refuse.unknown_difficulty",
		"game.refuse.unknown_language",
		"game.refuse.vehicle_not_owned",
		"game.refuse.walk_field_only",
		"game.rest_no_money",
		"game.rested",
		"game.revived",
		"game.saved",
		"game.scripted_end",
		"game.shop_open",
		"game.skipped",
		"game.sold",
		"game.unequipped",
		"game.unlocked",
		"game.used",
		"game.used_ability",
		"game.vehicle_acquired",
		"game.vehicle_called",
		"game.vehicle_parked",
		"game.vehicle_returned",
		"game.victory",
	};
	return keys;
}

class Session::GameHost : public events::Host {
public:
	explicit GameHost(Session& session) : s_(session) {}

	events::Handle start_action(const events::Action& action) override {
		if (action.type == events::ActionType::warp) {
			s_.leaveVehicleBehind();
			s_.enterArea(action.subject, action.target);
		} else {
			s_.applyActorAction(action);
		}
		HandleState state;
		state.kind = HandleKind::Action;
		const events::Handle handle = s_.newHandle(state);
		const bool finished = s_.presenter()->begin(handle, action);
		// An instant action has its end state on screen once begin returns, whatever the presenter says.
		s_.handles_[handle].done = finished || action.instant;
		s_.output_.push_back({OutputKind::Action, {}, events::describe(action) + (action.instant ? " (instant)" : "")});
		return handle;
	}

	events::Handle show_line(const events::Line& line) override {
		HandleState state;
		state.kind = HandleKind::Line;
		state.line = line;
		s_.lineHandle_ = s_.newHandle(state);
		s_.addLog(line, false);
		return s_.lineHandle_;
	}

	void log_skipped_line(const events::Line& line) override { s_.addLog(line, true); }

	events::Handle present_choice(const events::ChoiceRequest& request) override {
		HandleState state;
		state.kind = HandleKind::Choice;
		state.choice = request;
		s_.choiceHandle_ = s_.newHandle(state);
		return s_.choiceHandle_;
	}

	events::Handle start_battle(const events::BattleRequest& request) override {
		HandleState state;
		state.kind = HandleKind::Battle;
		const events::Handle handle = s_.newHandle(state);
		s_.battleSource_ = BattleSource::Event;
		s_.battleHandle_ = handle;
		s_.battleEnemyId_ = 0;
		const std::string error = s_.startBattle(request.encounter, toOpening(request.opening), request.escape_allowed, request.defeat_handled, scriptedEnd(request, s_.data_.db), request.vehicle);
		if (!error.empty()) {
			// Encounters are checked when the data loads, so this only happens if the party is empty.
			s_.warn("battle '" + request.encounter + "' could not start: " + error);
			s_.handles_[handle].done = true;
			s_.handles_[handle].result = static_cast<int>(events::BattleResult::win);
		} else if (!s_.resumedSlot_.empty()) {
			// Set up again from its own autosave: that save is already the one to retry from.
			s_.pendingRetrySlot_ = s_.resumedSlot_;
			s_.resumedSlot_.clear();
		} else {
			s_.autosave(kBeforeBattle);
		}
		return handle;
	}

	events::Handle open_shop(const std::string& shop) override {
		if (!s_.data_.shop(shop)) {
			return 0;
		}
		const events::Handle handle = menuHandle();
		s_.shopId_ = shop;
		s_.openScriptMenu(Mode::Shop, MenuKind::None, handle);
		s_.emit(OutputKind::Notice, "game.shop_open");
		return handle;
	}

	events::Handle rest_party(const events::RestRequest& request) override {
		const RestPoint* point = request.point.empty() ? nullptr : s_.data_.rest(request.point);
		if (!request.point.empty() && !point) {
			return 0;
		}
		HandleState state;
		state.kind = HandleKind::Menu;
		state.done = true;
		const events::Handle handle = s_.newHandle(state);
		const rules::Message problem = s_.payRest(point);
		if (!problem.empty()) {
			s_.emit(OutputKind::Notice, "game.rest_no_money");
		}
		return handle;
	}

	events::Handle open_save_prompt() override {
		const events::Handle handle = menuHandle();
		s_.openScriptMenu(Mode::Menu, MenuKind::SavePrompt, handle);
		return handle;
	}

	events::Handle open_name_entry(const std::string& member) override {
		if (!s_.data_.db.character(member)) {
			return 0;
		}
		const events::Handle handle = menuHandle();
		s_.nameMember_ = member;
		s_.openScriptMenu(Mode::Menu, MenuKind::NameEntry, handle);
		return handle;
	}

	bool is_done(events::Handle handle) override {
		auto it = s_.handles_.find(handle);
		if (it == s_.handles_.end()) {
			return true;
		}
		if (!it->second.done && it->second.kind == HandleKind::Action) {
			it->second.done = s_.presenter()->finished(handle);
		}
		return it->second.done;
	}

	int result(events::Handle handle) override {
		auto it = s_.handles_.find(handle);
		return it == s_.handles_.end() ? 0 : it->second.result;
	}

	void cancel(events::Handle handle) override {
		auto it = s_.handles_.find(handle);
		if (it != s_.handles_.end() && it->second.kind == HandleKind::Action) {
			s_.presenter()->cancel(handle);
		}
		forget(handle);
	}

	void release(events::Handle handle) override { forget(handle); }

	bool get_flag(const std::string& name) override { return s_.world_.flag(name); }

	void set_flag(const std::string& name, bool value) override {
		const bool before = s_.world_.flag(name);
		if (before != value) {
			s_.eventChangedFlag_ = true;
		}
		s_.world_.flags[name] = value;
		if (value && !before && name == s_.data_.config.clearFlag) {
			s_.clearPending_ = true;
		}
		s_.refreshJournal(true);
	}

	std::int64_t get_var(const std::string& name) override {
		auto it = s_.world_.vars.find(name);
		return it == s_.world_.vars.end() ? 0 : it->second;
	}

	void set_var(const std::string& name, std::int64_t value) override { s_.world_.vars[name] = value; }

	void give_item(const std::string& item, int count) override { s_.giveThing(item, count); }

	void take_item(const std::string& item, int count) override {
		std::map<std::string, int>& bag = s_.data_.db.item(item) ? s_.world_.items : s_.world_.equipmentBag;
		auto it = bag.find(item);
		if (it == bag.end()) {
			return;
		}
		it->second -= count;
		if (it->second <= 0) {
			bag.erase(it);
		}
	}

	void add_party_member(const std::string& member) override {
		World& w = s_.world_;
		if (!w.member(member)) {
			auto level = s_.data_.config.joinLevels.find(member);
			w.roster.push_back(rules::makePartyMember(s_.data_.db, *s_.data_.db.character(member),
				level == s_.data_.config.joinLevels.end() ? s_.data_.config.startLevel : level->second));
		}
		if (std::find(w.lineup.begin(), w.lineup.end(), member) == w.lineup.end() && w.lineup.size() < static_cast<std::size_t>(rules::kMaxPartySize)) {
			w.lineup.push_back(member);
			s_.emit(OutputKind::Notice, "game.joins", {member});
		}
	}

	void remove_party_member(const std::string& member) override {
		World& w = s_.world_;
		auto it = std::find(w.lineup.begin(), w.lineup.end(), member);
		if (it == w.lineup.end()) {
			return;
		}
		if (w.lineup.size() == 1) {
			s_.warn("the last active member cannot leave the party");
			return;
		}
		w.lineup.erase(it);
		s_.emit(OutputKind::Notice, "game.leaves", {member});
		rules::PartyMember* leaving = w.member(member);
		if (leaving && s_.data_.config.leaveEquipment == LeaveEquipment::Bag && !leaving->equipment.empty()) {
			while (!leaving->equipment.empty()) {
				rules::unequip(*leaving, s_.data_.db, w.equipmentBag, leaving->equipment.begin()->first);
			}
			s_.emit(OutputKind::Notice, "game.equipment_returned", {member});
		}
	}

	void set_player_control(bool enabled) override { s_.world_.playerControl = enabled; }

	void teach_ability(const std::string& member, const std::string& ability) override {
		rules::PartyMember* m = s_.world_.member(member);
		if (!m) {
			s_.warn("'" + member + "' cannot learn '" + ability + "' before joining");
			return;
		}
		const std::vector<std::string> known = rules::knownAbilities(s_.data_.db, *m);
		if (std::find(known.begin(), known.end(), ability) != known.end()) {
			return;
		}
		m->taught.push_back(ability);
		const rules::AbilityDef* def = s_.data_.db.ability(ability);
		if (def && def->kind == rules::AbilityKind::Skill && !m->skillUses.count(ability)) {
			m->skillUses[ability] = def->uses;
		}
		s_.emit(OutputKind::Notice, "game.learned", {member, ability});
	}

	std::int64_t money() override { return s_.world_.money; }

	void change_money(std::int64_t delta) override {
		const int before = s_.world_.money;
		s_.world_.money = static_cast<int>(std::clamp<std::int64_t>(before + delta, 0, s_.data_.config.moneyCap));
		const int moved = std::abs(s_.world_.money - before);
		s_.emit(OutputKind::Notice, s_.world_.money >= before ? "game.money_given" : "game.money_taken", {std::to_string(moved)});
	}

	void equip_piece(const std::string& member, const std::string& piece) override {
		rules::PartyMember* m = s_.world_.member(member);
		if (!m) {
			s_.warn("cannot equip " + piece + " on " + member + ": not in the roster");
			return;
		}
		const rules::Message refused = rules::equip(*m, s_.data_.db, s_.world_.equipmentBag, piece);
		if (!refused.empty()) {
			s_.warn("cannot equip " + piece + " on " + member + ": " + s_.render(refused));
			return;
		}
		s_.emit(OutputKind::Notice, "game.equipped", {member, piece});
	}

	void unequip_piece(const std::string& member, const std::string& piece) override {
		rules::PartyMember* m = s_.world_.member(member);
		std::optional<rules::EquipSlot> slot;
		for (const auto& [position, worn] : m ? m->equipment : std::map<rules::EquipSlot, std::string>()) {
			if (worn == piece && !slot) {
				slot = position;
			}
		}
		if (!slot) {
			s_.warn("cannot unequip " + piece + " from " + member + ": not worn");
			return;
		}
		rules::unequip(*m, s_.data_.db, s_.world_.equipmentBag, *slot);
		s_.emit(OutputKind::Notice, "game.unequipped", {member, piece});
	}

	void set_area_music(const std::string& track) override { s_.world_.areaMusic = track; }

	void set_follower(const std::string& actor) override {
		if (s_.world_.follower == actor) {
			return;
		}
		s_.world_.follower = actor;
		s_.emit(OutputKind::Notice, actor.empty() ? "game.follower_left" : "game.follower_joined", {actor.empty() ? std::string("-") : actor});
	}

	void give_vehicle(const std::string& vehicle, const std::string& map, const std::string& marker) override {
		const bool had = s_.world_.vehicles.count(vehicle) > 0;
		if (!put(vehicle, map, marker)) {
			return;
		}
		if (!had) {
			s_.emit(OutputKind::Notice, "game.vehicle_acquired", {vehicle});
			s_.autosaveReason_ = kVehicle;
		}
	}

	void place_vehicle(const std::string& vehicle, const std::string& map, const std::string& marker) override {
		if (!s_.world_.vehicles.count(vehicle)) {
			s_.warn("vehicle '" + vehicle + "' cannot be placed: the party does not have it");
			return;
		}
		put(vehicle, map, marker);
	}

	void remove_vehicle(const std::string& vehicle) override {
		if (s_.world_.aboard == vehicle) {
			s_.dismount();
		}
		s_.world_.vehicles.erase(vehicle);
	}

	bool has_vehicle(const std::string& vehicle) override { return s_.world_.vehicles.count(vehicle) > 0; }

	bool aboard_vehicle(const std::string& vehicle) override { return s_.world_.aboard == vehicle; }

	bool in_party(const std::string& member) override {
		return std::find(s_.world_.lineup.begin(), s_.world_.lineup.end(), member) != s_.world_.lineup.end();
	}

	int item_count(const std::string& item) override {
		const std::map<std::string, int>& bag = s_.data_.db.item(item) ? s_.world_.items : s_.world_.equipmentBag;
		const auto held = bag.find(item);
		return held == bag.end() ? 0 : held->second;
	}

	std::string snapshot_world() override { return serializeWorldChunks(s_.world_); }

	bool restore_world(const std::string& snapshot) override {
		std::string error;
		const bool marked = s_.world_.devMarked;
		World restored = s_.world_;
		if (!parseWorldChunks(snapshot, restored, error)) {
			s_.warn("cannot restore the event-start world: " + error);
			return false;
		}
		if (!s_.data_.area(restored.area)) {
			error = "unknown area '" + restored.area + "'";
		} else {
			error = invalidWorld(s_.data_, restored);
		}
		if (!error.empty()) {
			s_.warn("cannot restore the event-start world: " + error);
			return false;
		}
		s_.world_ = std::move(restored);
		s_.world_.devMarked = s_.world_.devMarked || marked;
		return true;
	}

private:
	bool put(const std::string& vehicle, const std::string& map, const std::string& marker) {
		const Area* a = s_.data_.area(map);
		if (!s_.data_.vehicle(vehicle) || !a || !a->markers.count(marker)) {
			s_.warn("vehicle '" + vehicle + "' cannot be put at " + map + " " + marker);
			return false;
		}
		if (s_.world_.aboard == vehicle) {
			s_.dismount();
		}
		const Cell cell = a->markers.at(marker);
		s_.world_.vehicles[vehicle] = {map, cell, Facing::South, map, cell};
		return true;
	}

	events::Handle menuHandle() {
		HandleState state;
		state.kind = HandleKind::Menu;
		return s_.newHandle(state);
	}

	void forget(events::Handle handle) {
		s_.handles_.erase(handle);
		if (handle == s_.lineHandle_) {
			s_.lineHandle_ = 0;
		}
		if (handle == s_.choiceHandle_) {
			s_.choiceHandle_ = 0;
		}
		if (handle == s_.battleHandle_) {
			s_.battleHandle_ = 0;
		}
		if (handle == s_.menuHandle_) {
			s_.menuHandle_ = 0;
		}
	}

	Session& s_;
};

Session::Session(const GameData& data, SessionOptions options) : data_(data), options_(std::move(options)) {
	if (!options_.presenter) {
		actions_ = std::make_unique<ActionQueue>(data_.config.presentation, options_.presentation, options_.onAction);
	}
	if (!options_.saveDirectory.empty()) {
		store_.emplace(options_.saveDirectory, data_.config.manualSlots, data_.config.autosaveSlots);
		std::string text;
		if (readFile(store_->profilePath(), text)) {
			rules::Result<Profile> loaded = parseProfile(text);
			if (loaded.ok()) {
				profile_ = *loaded.value;
			} else {
				warn("the profile cannot be read, so the default settings apply: " + loaded.error);
			}
		}
	}
	host_ = std::make_unique<GameHost>(*this);
}

Session::~Session() = default;

Presenter* Session::presenter() const {
	return options_.presenter ? options_.presenter : actions_.get();
}

std::vector<PresentedAction> Session::pollActions() {
	return actions_ ? actions_->poll() : std::vector<PresentedAction>();
}

void Session::finishAction(std::uint64_t handle) {
	if (actions_) {
		actions_->finish(handle);
	}
}

std::optional<Cell> Session::actorCell(const std::string& actor) const {
	auto it = world_.actors.find(actor);
	if (it != world_.actors.end()) {
		return it->second.area == world_.area && !world_.area.empty() ? std::optional<Cell>(it->second.cell) : std::nullopt;
	}
	const Hook* home = actorHome(area(), actor);
	return home ? std::optional<Cell>(home->at) : std::nullopt;
}

// The end state of a move, face, show or hide of one of the game's actors. Party members and anything else a script
// names are left to the front end. A move walks along x, then along y, so the actor ends facing its last leg.
void Session::applyActorAction(const events::Action& action) {
	using events::ActionType;
	if (action.type != ActionType::move && action.type != ActionType::face && action.type != ActionType::show_actor &&
		action.type != ActionType::hide_actor) {
		return;
	}
	const std::vector<std::string>& actors = data_.config.actors;
	if (std::find(actors.begin(), actors.end(), action.subject) == actors.end()) {
		return;
	}
	const Area* here = area();
	auto it = world_.actors.find(action.subject);
	if (it == world_.actors.end()) {
		ActorPlace place;
		if (const Hook* home = actorHome(here, action.subject)) {
			place.area = world_.area;
			place.cell = home->at;
		}
		it = world_.actors.emplace(action.subject, place).first;
	}
	ActorPlace& place = it->second;
	switch (action.type) {
	case ActionType::move: {
		Cell goal;
		if (action.has_position) {
			goal = {static_cast<int>(std::lround(action.position.x)), static_cast<int>(std::lround(action.position.z))};
		} else {
			auto marker = here ? here->markers.find(action.target) : std::map<std::string, Cell>::const_iterator();
			if (!here || marker == here->markers.end()) {
				return;
			}
			goal = marker->second;
		}
		if (place.area == world_.area && goal != place.cell) {
			place.facing = goal.y != place.cell.y ? (goal.y > place.cell.y ? Facing::South : Facing::North)
				: (goal.x > place.cell.x ? Facing::East : Facing::West);
		}
		place.area = world_.area;
		place.cell = goal;
		break;
	}
	case ActionType::face: {
		if (place.area.empty() || place.area != world_.area) {
			return;
		}
		std::optional<Cell> target;
		if (here && here->markers.count(action.target)) {
			target = here->markers.at(action.target);
		} else if (data_.db.character(action.target)) {
			// Party members walk with the leader.
			target = world_.position;
		} else if (action.target != action.subject) {
			target = actorCell(action.target);
		}
		if (std::optional<Facing> facing = target ? facingToward(place.cell, *target) : std::nullopt) {
			place.facing = *facing;
		}
		break;
	}
	case ActionType::show_actor: place.visible = true; break;
	case ActionType::hide_actor: place.visible = false; break;
	default: break;
	}
}

std::vector<ActorView> Session::actorsHere() const {
	std::vector<ActorView> out;
	const Area* here = area();
	if (!here) {
		return out;
	}
	for (const std::string& id : data_.config.actors) {
		auto it = world_.actors.find(id);
		if (it != world_.actors.end()) {
			if (it->second.area == world_.area) {
				out.push_back({id, it->second.cell, it->second.facing, it->second.visible, true});
			}
		} else if (const Hook* home = actorHome(here, id)) {
			out.push_back({id, home->at, Facing::South, !home->solidWhileActive || hookHolds(*home, world_), false});
		}
	}
	return out;
}

const Area* Session::area() const {
	return areaAt(world_.area);
}

std::string Session::elementName(const rules::Element& element) const {
	const auto named = language().text.find(std::string(kNamePrefix) + "element." + element.id);
	return named == language().text.end() ? element.id : named->second;
}

std::string Session::areaMusic() const {
	const Area* here = area();
	return world_.areaMusic.empty() && here ? here->music : world_.areaMusic;
}

// The copy keeps its hooks, shops and the rest in place, so pointers into it stay valid while only its rows and passable
// cells change.
const Area* Session::areaAt(const std::string& id) const {
	const Area* base = data_.area(id);
	if (!base) {
		return nullptr;
	}
	const auto broken = world_.brokenCells.find(id);
	const bool here = id == world_.area && !world_.brokenHere.empty();
	const bool gatedExits = std::any_of(base->exits.begin(), base->exits.end(), [](const Exit& e) { return !e.requireFlag.empty() || !e.blockFlag.empty(); });
	const bool gatedHooks = std::any_of(base->hooks.begin(), base->hooks.end(), [](const Hook& h) { return h.solidWhileActive; });
	if (base->changes.empty() && broken == world_.brokenCells.end() && !here && !gatedExits && !gatedHooks) {
		return base;
	}
	// A hook that is solid only while active stands while its flags hold and, for an actor's talk point, the actor shows.
	auto hookStands = [&](const Hook& h) {
		const auto actor = world_.actors.find(h.id);
		return hookHolds(h, world_) && (actor == world_.actors.end() || actor->second.visible);
	};
	std::set<Cell> open = broken == world_.brokenCells.end() ? std::set<Cell>() : broken->second;
	if (here) {
		open.insert(world_.brokenHere.begin(), world_.brokenHere.end());
	}
	std::string key;
	for (const CellChange& change : base->changes) {
		key += world_.flag(change.flag) ? '1' : '0';
	}
	for (Cell c : open) {
		key += ";" + std::to_string(c.x) + "," + std::to_string(c.y);
	}
	key += "|";
	for (const Exit& e : base->exits) {
		key += exitOpen(e, world_) ? '1' : '0';
	}
	for (const Hook& h : base->hooks) {
		key += !h.solidWhileActive || hookStands(h) ? '1' : '0';
	}
	auto view = areaViews_.find(id);
	if (view == areaViews_.end()) {
		view = areaViews_.emplace(id, std::make_pair(std::string("-"), *base)).first;
	}
	if (view->second.first != key) {
		std::vector<std::string>& rows = view->second.second.rows;
		rows = base->rows;
		for (const CellChange& change : base->changes) {
			if (world_.flag(change.flag)) {
				for (Cell c : change.cells) {
					rows[c.y][c.x] = change.tile;
				}
			}
		}
		for (Cell c : open) {
			if (base->inside(c)) {
				rows[c.y][c.x] = '.';
			}
		}
		for (const Exit& e : base->exits) {
			if (!exitOpen(e, world_)) {
				rows[e.at.y][e.at.x] = '#';
			}
		}
		std::set<Cell>& passable = view->second.second.passable;
		passable.clear();
		for (const Hook& h : base->hooks) {
			if (h.solidWhileActive && !hookStands(h)) {
				passable.insert(h.at);
			}
		}
		view->second.first = key;
	}
	return &view->second.second;
}

EncounterMode Session::encounterMode() const {
	if (options_.encounterMode) {
		return *options_.encounterMode;
	}
	if (data_.config.randomEncountersEverywhere) {
		return EncounterMode::Random;
	}
	const Area* a = area();
	return a ? a->mode : EncounterMode::Visible;
}

std::vector<Output> Session::drainOutput() {
	std::vector<Output> out;
	out.swap(output_);
	return out;
}

void Session::emit(OutputKind kind, const std::string& key, std::vector<std::string> params) {
	rules::Message message{key, std::move(params)};
	std::string rendered = render(message);
	output_.push_back({kind, std::move(message), std::move(rendered)});
}

void Session::warn(const std::string& text) {
	output_.push_back({OutputKind::Warning, {}, text});
}

std::string Session::text(const std::string& key, std::vector<std::string> params) const {
	return render({key, std::move(params)});
}

const LanguageTables& Session::language() const {
	return data_.language(profile_.textLanguage);
}

std::string Session::render(const rules::Message& message) const {
	std::vector<std::string> params;
	for (const std::string& p : message.params) {
		params.push_back(displayName(p));
	}
	return events::render_names(rules::formatText(language().text, message.key, params), data_.glossary,
		events::name_set_for(profile_.showShippedNames, language().code));
}

std::string Session::description(DescriptionKind kind, const std::string& id) const {
	return language().text.count(descriptionKey(kind, id)) ? render({descriptionKey(kind, id), {}}) : std::string();
}

std::string Session::scriptText(const events::Text& text) const {
	std::string out;
	events::resolve_text(text, language().strings, out);
	return events::render_names(out, data_.glossary, events::name_set_for(profile_.showShippedNames, language().code));
}

std::string Session::stringText(const std::string& id) const {
	return scriptText({true, id});
}

std::string Session::displayName(const std::string& id) const {
	const rules::Database& db = data_.db;
	auto named = world_.memberNames.find(id);
	if (named != world_.memberNames.end()) {
		return named->second;
	}
	const auto inLanguage = [&](const char* kind, const std::string& dataName) {
		const auto named = language().text.find(std::string(kNamePrefix) + kind + "." + id);
		return named == language().text.end() ? dataName : named->second;
	};
	if (const rules::CharacterDef* c = db.character(id)) {
		return inLanguage("character", c->name);
	}
	if (language().strings.find("speaker." + id)) {
		return stringText("speaker." + id);
	}
	if (const rules::AbilityDef* a = db.ability(id)) {
		return inLanguage("ability", a->name);
	}
	if (const rules::ItemDef* i = db.item(id)) {
		return inLanguage("item", i->name);
	}
	if (const rules::StatusDef* s = db.status(id)) {
		return inLanguage("status", s->name);
	}
	if (const rules::EnemyDef* e = db.enemy(id)) {
		return inLanguage("enemy", e->name);
	}
	if (const rules::EquipmentDef* e = db.equipmentPiece(id)) {
		return inLanguage("equipment", e->name);
	}
	if (const rules::VehicleDef* v = db.vehicle(id)) {
		return inLanguage("vehicle", v->name);
	}
	if (data_.area(id)) {
		return text("area." + id);
	}
	return id;
}

rules::Message Session::setProfile(const Profile& profile) {
	if (std::none_of(data_.languages.begin(), data_.languages.end(), [&](const LanguageTables& l) { return l.code == profile.textLanguage; })) {
		return refusal("unknown_language", {profile.textLanguage});
	}
	profile_ = profile;
	if (!store_) {
		return {};
	}
	std::string error;
	if (!writeFileAtomic(store_->profilePath(), serializeProfile(profile_), error)) {
		return refusal("profile_not_written", {error});
	}
	return {};
}

events::Handle Session::newHandle(HandleState state) {
	const events::Handle handle = nextHandle_++;
	handles_[handle] = std::move(state);
	return handle;
}

std::vector<rules::PartyMember> Session::activeParty() const {
	std::vector<rules::PartyMember> party;
	for (const std::string& id : world_.lineup) {
		if (const rules::PartyMember* m = world_.member(id)) {
			party.push_back(*m);
		}
	}
	return party;
}

int Session::averageLevelX100() const {
	if (world_.lineup.empty()) {
		return 0;
	}
	int total = 0;
	for (const rules::PartyMember& m : activeParty()) {
		total += m.level;
	}
	return total * 100 / static_cast<int>(world_.lineup.size());
}

void Session::addLog(const events::Line& line, bool skipped) {
	textLog_.push_back({line.speaker, line.text, skipped});
	if (textLog_.size() > kTextLogPages) {
		textLog_.pop_front();
	}
}

std::vector<std::string> Session::textLog() const {
	std::vector<std::string> out;
	for (const LogEntry& e : textLog_) {
		const std::string line = (e.speaker.empty() || data_.config.nameless(e.speaker) ? "" : displayName(e.speaker) + ": ") + scriptText(e.text);
		out.push_back(e.skipped ? text("game.skipped", {line}) : line);
	}
	return out;
}

// Jobs and unlocks follow the flags, and only ever move forward.
void Session::refreshJournal(bool announce) {
	for (const Job& job : data_.journal.jobs) {
		std::optional<JobState> next;
		if (world_.flag(job.completedFlag)) {
			next = JobState::Completed;
		} else if (world_.flag(job.acceptedFlag)) {
			next = JobState::Accepted;
		} else if (!job.missedFlag.empty() && world_.flag(job.missedFlag)) {
			next = JobState::Missed;
		} else if (job.availableFlag.empty() || world_.flag(job.availableFlag)) {
			next = JobState::Available;
		}
		auto current = world_.jobs.find(job.id);
		if (!next || (current != world_.jobs.end() && current->second >= *next)) {
			continue;
		}
		world_.jobs[job.id] = *next;
		if (announce) {
			emit(OutputKind::Notice, std::string("game.job.") + toString(*next), {stringText(job.title)});
		}
	}
	for (const EncyclopediaEntry& entry : data_.journal.encyclopedia) {
		if (world_.flag(entry.flag) && world_.unlocked.insert(entry.id).second && announce) {
			emit(OutputKind::Notice, "game.unlocked", {stringText(entry.title)});
		}
	}
}

QuestLog Session::questLog() const {
	QuestLog log;
	for (const Objective& o : data_.journal.objectives) {
		if (rangeHolds(o.range, world_)) {
			log.objective = stringText(o.text);
		}
	}
	for (const Job& job : data_.journal.jobs) {
		auto state = world_.jobs.find(job.id);
		if (state != world_.jobs.end()) {
			log.jobs.push_back({job.id, stringText(job.title), state->second});
		}
	}
	return log;
}

std::vector<EncyclopediaView> Session::encyclopedia() const {
	std::vector<EncyclopediaView> out;
	for (const EncyclopediaEntry& entry : data_.journal.encyclopedia) {
		if (world_.unlocked.count(entry.id)) {
			out.push_back({entry.id, stringText(entry.title), stringText(entry.text)});
		}
	}
	return out;
}

// Title

rules::Message Session::newGame() {
	if (mode() != Mode::Title) {
		return refusal("new_game_title");
	}
	const GameConfig& c = data_.config;
	world_ = World();
	world_.encounterStream.restore(options_.seed);
	for (const events::Declaration& d : data_.program.declarations) {
		if (!d.local && d.initial != 0) {
			if (d.is_flag) {
				world_.flags[d.name] = true;
			} else {
				world_.vars[d.name] = d.initial;
			}
		}
	}
	for (const std::string& id : c.startLineup) {
		world_.roster.push_back(rules::makePartyMember(data_.db, *data_.db.character(id), c.startLevel));
		world_.lineup.push_back(id);
	}
	world_.money = c.startMoney;
	for (const auto& [id, n] : c.startItems) {
		giveThing(id, n);
	}
	world_.macros = data_.db.macros;
	refreshJournal(false);
	dropEvent();
	battle_.reset();
	ended_.reset();
	latchedDirection_.reset();
	textLog_.clear();
	gameOver_ = false;
	retrySlot_.clear();
	pendingRetrySlot_.clear();
	retryCount_ = 0;
	areasSinceAutosave_.clear();
	autosaveReason_.clear();
	clearPending_ = false;
	modes_.transition(Mode::Field);
	enterArea(c.startArea, c.startMarker);
	world_.facing = c.startFacing;
	settleField();
	return {};
}

rules::Message Session::continueGame() {
	if (!store_) {
		return refusal("saving_disabled");
	}
	const std::string slot = store_->latest();
	if (slot.empty()) {
		return refusal("no_save");
	}
	return load(slot);
}

rules::Message Session::load(const std::string& slot) {
	retryCount_ = 0;
	retrySlot_.clear();
	std::string text;
	if (!store_ || !store_->read(slot, text)) {
		return refusal("slot_unreadable", {slot});
	}
	return loadText(text, slot);
}

rules::Message Session::retryBattle() {
	if (!canRetry()) {
		return refusal("no_retry");
	}
	const std::string slot = retrySlot_;
	std::string text;
	if (!store_->read(slot, text)) {
		return refusal("slot_unreadable", {slot});
	}
	++retryCount_;
	return loadText(text, slot);
}

rules::Message Session::loadText(const std::string& text, const std::string& slot) {
	if (mode() != Mode::Title) {
		return refusal("load_title");
	}
	rules::Result<SaveFile> save = parseSave(text);
	if (!save.ok()) {
		return refusal("save_damaged", {slot, save.error});
	}
	if (!data_.area(save.value->world.area)) {
		return refusal("save_unknown_area", {slot, save.value->world.area});
	}
	const std::string invalid = invalidWorld(data_, save.value->world);
	if (!invalid.empty()) {
		return refusal("save_invalid", {slot, invalid});
	}
	dropEvent();
	battle_.reset();
	ended_.reset();
	latchedDirection_.reset();
	world_ = std::move(save.value->world);
	// Data that changed since the save may have lowered a maximum.
	for (rules::PartyMember& m : world_.roster) {
		const rules::Stats stats = rules::memberStats(data_.db, m);
		m.hp = std::min(m.hp, stats.maxHp);
		m.tp = std::min(m.tp, stats.maxTp);
	}
	refreshJournal(false);
	textLog_.clear();
	gameOver_ = false;
	pendingRetrySlot_.clear();
	areasSinceAutosave_ = {world_.area};
	autosaveReason_.clear();
	clearPending_ = false;
	loadHooksPending_ = false;
	loadHooksFired_.clear();
	stepProgressMs_ = 0;
	tickRemainderMs_ = 0;
	modes_.transition(Mode::Field);
	const Area* a = area();
	if (pacingOn()) {
		pacing_.enterArea(a->id, a->kind == AreaKind::Dungeon);
	}
	if (slot.empty()) {
		emit(OutputKind::Notice, "game.loaded_text");
	} else {
		emit(OutputKind::Notice, "game.loaded", {slot});
	}
	if (save.value->midEvent.empty()) {
		return {};
	}
	events::SaveState state;
	std::string error;
	if (!events::save_state_from_text(save.value->midEvent, state)) {
		emit(OutputKind::Warning, "game.event_damaged");
		return {};
	}
	modes_.transition(Mode::Event);
	interpreter_ = std::make_unique<events::Interpreter>(data_.program, *host_);
	eventChangedFlag_ = false;
	if (!interpreter_->restore(state, error)) {
		emit(OutputKind::Warning, "game.event_cannot_resume");
		warn(error);
		dropEvent();
		modes_.transition(Mode::Field);
		return {};
	}
	resumedSlot_ = slot;
	pumpEvent(0);
	resumedSlot_.clear();
	return {};
}

void Session::quitToTitle() {
	dropEvent();
	battle_.reset();
	ended_.reset();
	gameOver_ = false;
	modes_.reset(Mode::Title);
}

// Field

void Session::update(int ms, const FieldInput& input) {
	switch (mode()) {
	case Mode::Field:
		if (input.held) {
			latchedDirection_ = input.direction;
		}
		tickRemainderMs_ += ms;
		while (tickRemainderMs_ >= data_.config.tickMs && mode() == Mode::Field) {
			tickRemainderMs_ -= data_.config.tickMs;
			FieldInput tickInput = input;
			if (!tickInput.held && latchedDirection_) {
				tickInput = {true, *latchedDirection_};
			}
			latchedDirection_.reset();
			tickField(tickInput);
		}
		break;
	case Mode::Event:
		addTime(ms);
		if (pacingOn()) {
			pacing_.eventTime(ms);
		}
		pumpEvent(ms / 1000.0);
		break;
	case Mode::Menu:
	case Mode::Shop:
		addTime(ms);
		break;
	case Mode::Battle:
	case Mode::Title:
		break;
	}
	if (mode() != Mode::Field) {
		latchedDirection_.reset();
	}
}

void Session::addTime(int ms) {
	world_.playTimeMs += ms;
}

void Session::tickField(const FieldInput& input) {
	const int tick = data_.config.tickMs;
	addTime(tick);
	if (pacingOn()) {
		pacing_.fieldTime(tick);
	}
	world_.graceMs = std::max(0, world_.graceMs - tick);
	const Area& a = *area();
	const FieldVehicle* vehicle = riding();
	const Conveyor* belt = nullptr;
	for (const Conveyor& c : a.conveyors) {
		const bool holds = (c.requireFlag.empty() || world_.flag(c.requireFlag)) && (c.blockFlag.empty() || !world_.flag(c.blockFlag));
		if (holds && std::find(c.cells.begin(), c.cells.end(), world_.position) != c.cells.end()) {
			belt = &c;
		}
	}
	if (!belt) {
		conveyorMs_ = 0;
	}
	if (belt && world_.playerControl) {
		// A belt carries the party one cell per step and takes no input while it does.
		stepProgressMs_ = 0;
		conveyorMs_ += tick;
		const Cell target = stepFrom(world_.position, belt->direction);
		const bool blockedByEnemy = std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; });
		if (conveyorMs_ >= belt->stepMs) {
			conveyorMs_ = 0;
			if (canEnter(a, target) && !blockedByEnemy) {
				arrive(target);
				if (mode() != Mode::Field) {
					return;
				}
			}
		}
	} else if (input.held && world_.playerControl) {
		world_.facing = input.direction;
		if (vehicle) {
			world_.vehicles[world_.aboard].facing = input.direction;
		}
		const Cell target = stepFrom(world_.position, input.direction);
		// Aboard, visible enemies are in the way but start nothing: battles aboard come from the vehicle's own table.
		if (!vehicle && encountersOn_) {
			if (std::optional<EncounterStart> contact = leaderBump(world_, target)) {
				startFieldBattle(*contact);
				return;
			}
		}
		const bool blockedByEnemy = std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; });
		const int stepMs = vehicle ? std::max(1, a.stepMs * 100 / vehicle->speedPercent) : a.stepMs;
		if (canEnter(a, target) && !blockedByEnemy) {
			stepProgressMs_ += tick;
			if (stepProgressMs_ >= stepMs) {
				stepProgressMs_ = 0;
				arrive(target);
				if (mode() != Mode::Field) {
					return;
				}
			}
		} else {
			stepProgressMs_ = 0;
		}
	} else {
		stepProgressMs_ = 0;
	}
	if (encounterMode() == EncounterMode::Visible && world_.area == a.id && encountersOn_) {
		if (std::optional<EncounterStart> contact = tickVisibleEnemies(world_, a, tick, a.stepMs, !world_.aboard.empty())) {
			startFieldBattle(*contact);
		}
	}
}

// P6: a machine member still standing regains hit points with each step.
void Session::healMachinesOnStep() {
	if (data_.config.machineStepHeal <= 0) {
		return;
	}
	for (const std::string& id : world_.lineup) {
		rules::PartyMember* m = world_.member(id);
		const rules::CharacterDef* character = data_.db.character(id);
		if (m && character && character->kind == rules::CreatureKind::Machine && m->hp > 0) {
			m->hp = std::min(m->hp + data_.config.machineStepHeal, rules::memberStats(data_.db, *m).maxHp);
		}
	}
}

void Session::arrive(Cell cell) {
	const Cell from = world_.position;
	world_.position = cell;
	int longestGrace = 0;
	for (const Zone& zone : area()->zones) {
		longestGrace = std::max(longestGrace, zone.random.graceSteps);
	}
	if (world_.areaSteps <= longestGrace) {
		++world_.areaSteps;
	}
	if (!world_.aboard.empty()) {
		world_.vehicles[world_.aboard].cell = cell;
	}
	if (pacingOn()) {
		pacing_.step();
	}
	healMachinesOnStep();
	if (const FieldVehicle* vehicle = riding(); vehicle && vehicle->breaks.find(area()->rows[cell.y][cell.x]) != std::string::npos) {
		if (vehicle->breaksForGood) {
			world_.brokenCells[world_.area].insert(cell);
		} else {
			world_.brokenHere.insert(cell);
		}
		emit(OutputKind::Notice, "game.barrier_broken", {vehicle->id});
	}
	const Area& a = *area();
	const Area* shore = nullptr;
	if (const Port* boarded = a.gangwayAt(cell) ? data_.port(world_.shipPort, &shore) : nullptr) {
		const std::string marker = boarded->marker;
		emit(OutputKind::Notice, "game.left_ship");
		enterArea(shore->id, marker, from);
		settleField();
		return;
	}
	if (const Exit* exit = a.exitAt(cell)) {
		const Exit e = *exit;
		if (e.fall) {
			emit(OutputKind::Notice, "game.fell");
		}
		// A vehicle goes on into another field area it can stand in; at a town or a dungeon it stays outside.
		if (const FieldVehicle* vehicle = riding()) {
			const Area* to = areaAt(e.toArea);
			if (!to || to->kind != AreaKind::Field || !vehicle->canCross(to->rows[to->markers.at(e.toMarker).y][to->markers.at(e.toMarker).x])) {
				const std::string id = world_.aboard;
				world_.aboard.clear();
				parkVehicle(id, a.id, from);
				emit(OutputKind::Notice, "game.vehicle_parked", {id});
			}
		}
		enterArea(e.toArea, e.toMarker, from);
		settleField();
		return;
	}
	if (const Hook* hook = a.stepHookAt(cell)) {
		if (hookHolds(*hook, world_)) {
			startEvent(hook->event);
			return;
		}
	}
	if (!encountersOn_) {
		return;
	}
	if (const FieldVehicle* vehicle = riding()) {
		const rules::VehicleDef* fighter = data_.db.vehicle(vehicle->id);
		if (std::optional<EncounterStart> start = vehicleStep(world_, a, cell, vehicle->encounters, data_.db, fighter ? fighter->stats.agility : 0)) {
			startFieldBattle(*start, vehicle->id);
		}
		return;
	}
	if (encounterMode() == EncounterMode::Random) {
		if (std::optional<EncounterStart> start = randomStep(world_, a, cell, data_.db, activeParty())) {
			startFieldBattle(*start);
		}
	}
}

void Session::enterArea(const std::string& areaId, const std::string& marker, std::optional<Cell> standing) {
	const Area* a = data_.area(areaId);
	if (!a) {
		warn("unknown area '" + areaId + "'");
		return;
	}
	enterAreaAt(areaId, a->markers.at(marker), standing);
}

void Session::enterAreaAt(const std::string& areaId, Cell cell, std::optional<Cell> standing) {
	const Area* a = data_.area(areaId);
	if (!a) {
		warn("unknown area '" + areaId + "'");
		return;
	}
	const Area* before = area();
	if (a->kind != AreaKind::Dungeon) {
		world_.dungeonExitArea.clear();
		world_.dungeonExitCell = {};
	} else if (before && before->kind != AreaKind::Dungeon) {
		world_.dungeonExitArea = before->id;
		world_.dungeonExitCell = standing ? *standing : world_.position;
	}
	if (world_.area != a->id) {
		world_.brokenHere.clear();
		world_.areaMusic.clear();
	}
	world_.areaSteps = 0;
	if (a->kind != AreaKind::Vessel) {
		world_.shipPort.clear();
	}
	world_.area = a->id;
	world_.position = cell;
	if (!world_.aboard.empty()) {
		world_.vehicles[world_.aboard].area = a->id;
		world_.vehicles[world_.aboard].cell = cell;
	}
	world_.visitedAreas.insert(a->id);
	stepProgressMs_ = 0;
	if (encounterMode() == EncounterMode::Visible) {
		spawnVisibleEnemies(world_, *a);
	} else {
		world_.enemies.clear();
	}
	if (pacingOn()) {
		pacing_.enterArea(a->id, a->kind == AreaKind::Dungeon);
	}
	loadHooksPending_ = true;
	ambientPending_ = true;
	loadHooksFired_.clear();
	if (!areasSinceAutosave_.count(a->id)) {
		areasSinceAutosave_.insert(a->id);
		if (autosaveReason_.empty()) {
			autosaveReason_ = kAreaEntry;
		}
	}
	emit(OutputKind::Notice, "game.arrive", {a->id});
}

void Session::settleField() {
	if (mode() != Mode::Field || gameOver_) {
		return;
	}
	if (loadHooksPending_) {
		for (const Hook& hook : area()->hooks) {
			if (hook.trigger == HookTrigger::Load && !loadHooksFired_.count(hook.id) && hookHolds(hook, world_)) {
				loadHooksFired_.insert(hook.id);
				startEvent(hook.event);
				return;
			}
		}
		loadHooksPending_ = false;
	}
	for (const Hook& hook : area()->hooks) {
		const std::string onceKey = world_.area + "/" + hook.id;
		if (hook.trigger == HookTrigger::Flag && hookHolds(hook, world_) && !(hook.once && world_.onceHooks.count(onceKey))) {
			if (hook.once) {
				world_.onceHooks.insert(onceKey);
			}
			startEvent(hook.event);
			return;
		}
	}
	if (ambientPending_) {
		ambientPending_ = false;
		const TalkSet* set = profile_.ambientTalk ? talkSet() : nullptr;
		if (set && world_.playerControl && !world_.ambientHeard.count(set->id + "@" + world_.area) && speak(*set, OutputKind::Ambient)) {
			world_.ambientHeard.insert(set->id + "@" + world_.area);
		}
	}
	if (clearPending_) {
		clearPending_ = false;
		if (world_.devMarked) {
			emit(OutputKind::Notice, "game.clear_not_recorded");
		} else {
			writeClearRecord();
		}
	}
	if (!autosaveReason_.empty()) {
		autosave(autosaveReason_);
	}
}

rules::Message Session::walk(Facing direction) {
	if (mode() != Mode::Field) {
		return refusal("walk_field_only");
	}
	if (!world_.playerControl) {
		return refusal("no_control_move");
	}
	const Area& a = *area();
	const Cell start = world_.position;
	const Cell target = stepFrom(start, direction);
	const bool enemyThere = std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; });
	if (!canEnter(a, target) && !enemyThere) {
		face(direction);
		return refusal("blocked");
	}
	if (enemyThere && !world_.aboard.empty()) {
		face(direction);
		return refusal("in_the_way");
	}
	const std::string startArea = world_.area;
	for (int guard = 0; guard < 1000; ++guard) {
		update(data_.config.tickMs, {true, direction});
		if (mode() != Mode::Field || world_.position != start || world_.area != startArea) {
			return {};
		}
		if (world_.graceMs == 0 && std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; }) &&
			(!encountersOn_ || !leaderBump(world_, target))) {
			return refusal("in_the_way");
		}
	}
	return refusal("blocked");
}

rules::Message Session::face(Facing direction) {
	if (mode() != Mode::Field) {
		return refusal("walk_field_only");
	}
	if (!world_.playerControl) {
		return refusal("no_control_move");
	}
	world_.facing = direction;
	if (!world_.aboard.empty()) {
		world_.vehicles[world_.aboard].facing = direction;
	}
	return {};
}

PathWalk Session::beginWalk(Cell goal) const {
	return {goal, world_.area, 0};
}

WalkStatus Session::walkStep(PathWalk& walk) {
	if (walk.steps++ >= kMaxWalkSteps) {
		return WalkStatus::TooLong;
	}
	if (mode() != Mode::Field) {
		return WalkStatus::Stopped;
	}
	if (world_.area != walk.area) {
		return WalkStatus::LeftArea;
	}
	if (world_.position == walk.goal) {
		return WalkStatus::Arrived;
	}
	const Area& a = *area();
	// A belt carries the party wherever it runs, so the path crosses none but the goal.
	std::set<Cell> belts;
	for (const Conveyor& c : a.conveyors) {
		belts.insert(c.cells.begin(), c.cells.end());
	}
	std::map<Cell, Cell> from;
	std::deque<Cell> queue{world_.position};
	from[world_.position] = world_.position;
	while (!queue.empty() && !from.count(walk.goal)) {
		const Cell c = queue.front();
		queue.pop_front();
		for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
			const Cell n = stepFrom(c, f);
			if (from.count(n) || !canEnter(a, n) || ((a.exitAt(n) || belts.count(n)) && n != walk.goal)) {
				continue;
			}
			from[n] = c;
			queue.push_back(n);
		}
	}
	if (!from.count(walk.goal)) {
		return WalkStatus::NoPath;
	}
	Cell next = walk.goal;
	while (from[next] != world_.position) {
		next = from[next];
	}
	Facing direction = Facing::North;
	for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
		if (stepFrom(world_.position, f) == next) {
			direction = f;
		}
	}
	if (!this->walk(direction).empty()) {
		// A visible enemy in the way during the grace period: let the moment pass.
		update(data_.config.tickMs);
	}
	return WalkStatus::Walking;
}

rules::Message Session::interact() {
	if (mode() != Mode::Field) {
		return refusal("interact_field_only");
	}
	if (!world_.playerControl) {
		return refusal("no_control_act");
	}
	if (!world_.aboard.empty()) {
		return refusal("aboard", {world_.aboard});
	}
	const Area& a = *area();
	const Cell target = stepFrom(world_.position, world_.facing);
	if (!vehicleAt(target).empty()) {
		return board();
	}
	if (const Port* port = a.portAt(target)) {
		if (!portOpen(*port, world_)) {
			return refusal("port_closed");
		}
		if (!port->ship.empty()) {
			boardShip(*port);
			return {};
		}
		modes_.transition(Mode::Menu);
		menuKind_ = MenuKind::Port;
		portId_ = port->id;
		emit(OutputKind::Notice, "game.port_open");
		return {};
	}
	if (a.helmAt(target) && data_.port(world_.shipPort)) {
		modes_.transition(Mode::Menu);
		menuKind_ = MenuKind::Port;
		portId_ = world_.shipPort;
		emit(OutputKind::Notice, "game.helm_open");
		return {};
	}
	if (const Hook* hook = a.hookAt(target, HookTrigger::Interact)) {
		if (!hookHolds(*hook, world_)) {
			return refusal("no_answer");
		}
		startEvent(hook->event);
		return {};
	}
	if (const Chest* chest = a.chestAt(target)) {
		openChest(*chest);
		settleField();
		return {};
	}
	if (const Shop* shop = a.shopAt(target)) {
		if (!shopOpen(*shop)) {
			return refusal("shop_closed", {shop->id});
		}
		modes_.transition(Mode::Shop);
		shopId_ = shop->id;
		emit(OutputKind::Notice, "game.shop_open");
		return {};
	}
	if (const RestPoint* rest = a.restAt(target)) {
		modes_.transition(Mode::Menu);
		menuKind_ = MenuKind::Inn;
		innId_ = rest->id;
		emit(OutputKind::Notice, "game.inn_offer", {std::to_string(rest->cost)});
		return {};
	}
	return refusal("nothing_there");
}

// Vehicles (FLD-060 to FLD-063)

const FieldVehicle* Session::riding() const {
	return world_.aboard.empty() ? nullptr : data_.vehicle(world_.aboard);
}

std::string Session::vehicleAt(Cell cell) const {
	for (const auto& [id, v] : world_.vehicles) {
		if (id != world_.aboard && v.area == world_.area && v.cell == cell) {
			return id;
		}
	}
	return "";
}

bool Session::canEnter(const Area& area, Cell cell) const {
	if (!vehicleAt(cell).empty()) {
		return false;
	}
	const FieldVehicle* vehicle = riding();
	if (!vehicle) {
		return area.walkable(cell);
	}
	return area.inside(cell) && vehicle->canCross(area.rows[cell.y][cell.x]) && !area.hookAt(cell, HookTrigger::Interact) && !area.shopAt(cell) &&
		!area.restAt(cell) && !area.chestAt(cell) && !area.portAt(cell);
}

std::vector<VehicleView> Session::vehiclesHere() const {
	std::vector<VehicleView> out;
	for (const auto& [id, v] : world_.vehicles) {
		if (v.area == world_.area) {
			out.push_back({id, v.cell, v.facing, id == world_.aboard});
		}
	}
	return out;
}

rules::Message Session::board() {
	if (mode() != Mode::Field) {
		return refusal("interact_field_only");
	}
	if (!world_.playerControl) {
		return refusal("no_control_act");
	}
	if (!world_.aboard.empty()) {
		return refusal("aboard", {world_.aboard});
	}
	const Cell target = stepFrom(world_.position, world_.facing);
	const std::string id = vehicleAt(target);
	if (id.empty() || area()->kind != AreaKind::Field) {
		return refusal("nothing_there");
	}
	VehiclePlace& place = world_.vehicles[id];
	world_.aboard = id;
	world_.position = place.cell;
	place.facing = world_.facing;
	place.homeArea = place.area;
	place.home = place.cell;
	stepProgressMs_ = 0;
	emit(OutputKind::Notice, "game.boarded", {id});
	return {};
}

rules::Message Session::leaveVehicle() {
	if (mode() != Mode::Field) {
		return refusal("interact_field_only");
	}
	if (!world_.playerControl) {
		return refusal("no_control_act");
	}
	if (world_.aboard.empty()) {
		return refusal("not_aboard");
	}
	const Area& a = *area();
	std::vector<Facing> order{world_.facing};
	for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
		if (f != world_.facing) {
			order.push_back(f);
		}
	}
	for (Facing f : order) {
		const Cell c = stepFrom(world_.position, f);
		const bool enemyThere = std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == c; });
		if (a.walkable(c) && !a.exitAt(c) && !enemyThere && vehicleAt(c).empty()) {
			const std::string id = world_.aboard;
			world_.aboard.clear();
			world_.position = c;
			world_.facing = f;
			stepProgressMs_ = 0;
			emit(OutputKind::Notice, "game.left_vehicle", {id});
			return {};
		}
	}
	return refusal("nowhere_leave");
}

// F3: from the item list anywhere outdoors, a vehicle the party has comes to it, and the party boards it where it stands.
rules::Message Session::callVehicle(const FieldVehicle& vehicle) {
	if (!world_.vehicles.count(vehicle.id)) {
		return refusal("vehicle_not_owned", {vehicle.id});
	}
	if (!world_.aboard.empty()) {
		return refusal("aboard", {world_.aboard});
	}
	const Area& a = *area();
	if (a.kind != AreaKind::Field || !a.inside(world_.position) || !vehicle.canCross(a.rows[world_.position.y][world_.position.x])) {
		return refusal("call_outdoors", {vehicle.callItem});
	}
	VehiclePlace& place = world_.vehicles[vehicle.id];
	place.area = world_.area;
	place.cell = world_.position;
	place.facing = world_.facing;
	place.homeArea = place.area;
	place.home = place.cell;
	world_.aboard = vehicle.id;
	stepProgressMs_ = 0;
	modes_.transition(Mode::Field);
	menuKind_ = MenuKind::None;
	emit(OutputKind::Notice, "game.vehicle_called", {vehicle.id});
	return {};
}

// F2: the party walks aboard the ship at the vessel's arrival marker; its helm lists the open ports of the same ship.
void Session::boardShip(const Port& port) {
	const std::string from = port.id;
	const Area& ship = *data_.area(port.ship);
	emit(OutputKind::Notice, "game.boarded_ship", {ship.id});
	enterArea(ship.id, ship.arrival, world_.position);
	world_.shipPort = from;
	settleField();
}

void Session::leaveVehicleBehind() {
	if (world_.aboard.empty()) {
		return;
	}
	const std::string id = world_.aboard;
	world_.aboard.clear();
	parkVehicle(id, world_.area, world_.position);
}

void Session::parkVehicle(const std::string& id, const std::string& areaId, Cell cell) {
	VehiclePlace& place = world_.vehicles[id];
	const Area* a = areaAt(areaId);
	bool reachable = a && a->walkable(cell);
	for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
		reachable = reachable || (a && a->walkable(stepFrom(cell, f)));
	}
	if (reachable) {
		place.area = areaId;
		place.cell = cell;
		return;
	}
	place.area = place.homeArea;
	place.cell = place.home;
	emit(OutputKind::Notice, "game.vehicle_returned", {id});
}

void Session::dismount() {
	if (world_.aboard.empty()) {
		return;
	}
	world_.aboard.clear();
	const Area* a = area();
	if (!a || (a->walkable(world_.position) && !a->exitAt(world_.position))) {
		return;
	}
	// The nearest cell the party can stand on, searching through every cell of the map.
	std::set<Cell> seen{world_.position};
	std::deque<Cell> queue{world_.position};
	while (!queue.empty()) {
		const Cell c = queue.front();
		queue.pop_front();
		if (a->walkable(c) && !a->exitAt(c) && vehicleAt(c).empty()) {
			world_.position = c;
			return;
		}
		for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
			const Cell n = stepFrom(c, f);
			if (a->inside(n) && seen.insert(n).second) {
				queue.push_back(n);
			}
		}
	}
}

// Travel (FLD-051)

const Port* Session::port() const {
	return menuKind() == MenuKind::Port ? data_.port(portId_) : nullptr;
}

std::vector<TravelDestination> Session::travelDestinations(TravelKind kind) const {
	std::vector<TravelDestination> out;
	const Area* here = area();
	if (!here) {
		return out;
	}
	switch (kind) {
	case TravelKind::Port:
		if (!port()) {
			break;
		}
		for (const auto& [id, a] : data_.areas) {
			for (const Port& p : a.ports) {
				if (p.id != portId_ && p.ship == port()->ship && portOpen(p, world_)) {
					out.push_back({p.id, a.id, a.markers.at(p.marker), a.region});
				}
			}
		}
		break;
	case TravelKind::Town:
		for (const auto& [id, a] : data_.areas) {
			if (a.kind == AreaKind::Town && !a.arrival.empty() && a.region == here->region && id != here->id && world_.visitedAreas.count(id)) {
				out.push_back({id, id, a.markers.at(a.arrival), a.region});
			}
		}
		break;
	case TravelKind::DungeonExit:
		if (here->kind == AreaKind::Dungeon && data_.area(world_.dungeonExitArea)) {
			out.push_back({world_.dungeonExitArea, world_.dungeonExitArea, world_.dungeonExitCell, data_.area(world_.dungeonExitArea)->region});
		}
		break;
	}
	return out;
}

rules::Message Session::travel(TravelKind kind, const std::string& destination, const std::string& user, const std::string& thing) {
	if (kind == TravelKind::Port ? !port() : menuKind() != MenuKind::Field) {
		return refusal(kind == TravelKind::Port ? "no_port" : "travel_menu_only");
	}
	if (kind != TravelKind::Port) {
		const std::vector<rules::Effect>* effects = nullptr;
		if (user.empty() && data_.db.item(thing)) {
			effects = &data_.db.item(thing)->effects;
		} else if (!user.empty() && data_.db.ability(thing)) {
			effects = &data_.db.ability(thing)->effects;
		}
		const rules::Effect* travelEffect = effects ? rules::travelEffect(*effects) : nullptr;
		const rules::TravelTarget wanted = kind == TravelKind::Town ? rules::TravelTarget::Town : rules::TravelTarget::DungeonExit;
		if (!travelEffect || travelEffect->travel != wanted) {
			return refusal("not_travel", {thing});
		}
		const bool inDungeon = area()->kind == AreaKind::Dungeon;
		if (kind == TravelKind::Town && inDungeon) {
			return refusal("in_dungeon", {thing});
		}
		if (area()->kind == AreaKind::Vessel) {
			return refusal("on_ship", {thing});
		}
		if (kind == TravelKind::DungeonExit && !inDungeon) {
			return refusal("not_dungeon", {thing});
		}
	}
	const std::vector<TravelDestination> destinations = travelDestinations(kind);
	if (destinations.empty() && kind != TravelKind::Port) {
		return refusal(kind == TravelKind::Town ? "no_town" : "no_way_out");
	}
	auto chosen = std::find_if(destinations.begin(), destinations.end(), [&](const TravelDestination& d) { return d.id == destination; });
	if (kind == TravelKind::DungeonExit && destination.empty()) {
		chosen = destinations.begin();
	}
	if (chosen == destinations.end()) {
		return refusal("no_destination", {destination});
	}
	const TravelDestination to = *chosen;
	if (kind != TravelKind::Port) {
		std::vector<rules::PartyMember> party = activeParty();
		rules::FieldUse use;
		if (user.empty()) {
			use = rules::useItemOutsideBattle(data_.db, party, world_.items, thing, -1);
		} else {
			auto it = std::find(world_.lineup.begin(), world_.lineup.end(), user);
			if (it == world_.lineup.end()) {
				return {"field.unknown_member", {user}};
			}
			use = rules::useAbilityOutsideBattle(data_.db, party, static_cast<int>(it - world_.lineup.begin()), thing, -1);
		}
		if (!use.rejection.empty()) {
			return use.rejection;
		}
		for (std::size_t i = 0; i < party.size(); ++i) {
			*world_.member(world_.lineup[i]) = party[i];
		}
		if (user.empty()) {
			emit(OutputKind::Notice, "game.used", {thing});
		} else {
			emit(OutputKind::Notice, "game.used_ability", {user, thing});
		}
	}
	leaveVehicleBehind();
	modes_.transition(Mode::Field);
	menuKind_ = MenuKind::None;
	portId_.clear();
	enterAreaAt(to.area, to.cell);
	settleField();
	return {};
}

// Among the sets whose area and flag range hold, an area set beats a chapter set, then more range bounds win, then
// the first listed (FLD-072).
const TalkSet* Session::talkSet() const {
	const TalkSet* best = nullptr;
	int bestScore = -1;
	for (const TalkSet& set : data_.journal.partyTalk) {
		if ((!set.area.empty() && set.area != world_.area) || !rangeHolds(set.range, world_)) {
			continue;
		}
		const int score = (set.area.empty() ? 0 : 4) + (set.range.after.empty() ? 0 : 1) + (set.range.until.empty() ? 0 : 1);
		if (score > bestScore) {
			best = &set;
			bestScore = score;
		}
	}
	return best;
}

bool Session::speak(const TalkSet& set, OutputKind kind) {
	bool spoke = false;
	for (const TalkLine& line : set.lines) {
		if (std::find(world_.lineup.begin(), world_.lineup.end(), line.speaker) == world_.lineup.end()) {
			continue;
		}
		output_.push_back({kind, {line.text, {}}, displayName(line.speaker) + ": " + stringText(line.text)});
		spoke = true;
	}
	return spoke;
}

rules::Message Session::partyTalk() {
	if (mode() != Mode::Field || interpreter_) {
		return refusal("talk_field_only");
	}
	const TalkSet* best = talkSet();
	return best && speak(*best, OutputKind::Line) ? rules::Message() : refusal("nobody_talks");
}

void Session::openChest(const Chest& chest) {
	if (world_.flag(chest.flag)) {
		emit(OutputKind::Notice, "game.chest_empty");
		return;
	}
	for (const auto& [id, n] : chest.items) {
		giveThing(id, n);
		emit(OutputKind::Notice, "game.found", {std::to_string(n), id});
	}
	for (const auto& [id, n] : chest.equipment) {
		giveThing(id, n);
		emit(OutputKind::Notice, "game.found", {std::to_string(n), id});
	}
	if (chest.money > 0) {
		world_.money = std::min(data_.config.moneyCap, world_.money + chest.money);
		emit(OutputKind::Notice, "game.found_money", {std::to_string(chest.money)});
	}
	world_.flags[chest.flag] = true;
	refreshJournal(true);
}

void Session::giveThing(const std::string& id, int count) {
	std::map<std::string, int>& bag = data_.db.item(id) ? world_.items : world_.equipmentBag;
	bag[id] = std::min(data_.config.itemCap, bag[id] + count);
}

// Events

bool Session::startEvent(const std::string& event) {
	if (!modes_.transition(Mode::Event)) {
		return false;
	}
	interpreter_ = std::make_unique<events::Interpreter>(data_.program, *host_);
	eventChangedFlag_ = false;
	if (!interpreter_->start(event)) {
		warn("event '" + event + "' cannot start: " + interpreter_->error());
		dropEvent();
		modes_.transition(Mode::Field);
		return false;
	}
	pumpEvent(0);
	return true;
}

void Session::pumpEvent(double seconds) {
	if (!interpreter_ || mode() != Mode::Event) {
		return;
	}
	const events::Status status = interpreter_->step(seconds);
	if (status == events::Status::error) {
		warn("event error: " + interpreter_->error());
		endEvent();
	} else if (status == events::Status::finished) {
		endEvent();
	}
}

void Session::endEvent() {
	dropEvent();
	if (eventChangedFlag_ && autosaveReason_ != kVehicle) {
		autosaveReason_ = kEventEnd;
	}
	if (mode() == Mode::Event) {
		modes_.transition(Mode::Field);
	}
	settleField();
}

void Session::dropEvent() {
	interpreter_.reset();
	handles_.clear();
	lineHandle_ = 0;
	choiceHandle_ = 0;
	battleHandle_ = 0;
	menuHandle_ = 0;
}

bool Session::linePending() const {
	auto it = handles_.find(lineHandle_);
	return lineHandle_ && it != handles_.end() && !it->second.done;
}

std::optional<LineView> Session::line() const {
	if (!linePending()) {
		return std::nullopt;
	}
	const events::Line& line = handles_.at(lineHandle_).line;
	LineView view;
	view.speakerId = line.speaker;
	view.speakerName = line.speaker.empty() || data_.config.nameless(line.speaker) ? "" : displayName(line.speaker);
	view.textId = line.text.is_id ? line.text.value : "";
	view.text = scriptText(line.text);
	view.portrait = line.portrait;
	return view;
}

std::string Session::lineText() const {
	const std::optional<LineView> view = line();
	if (!view) {
		return "";
	}
	return view->speakerName.empty() ? view->text : view->speakerName + ": " + view->text;
}

void Session::confirmLine() {
	if (!linePending()) {
		return;
	}
	handles_[lineHandle_].done = true;
	pumpEvent(0);
}

const events::ChoiceRequest* Session::choice() const {
	auto it = handles_.find(choiceHandle_);
	if (!choiceHandle_ || it == handles_.end() || it->second.done) {
		return nullptr;
	}
	return &it->second.choice;
}

std::optional<ChoiceView> Session::choiceView() const {
	const events::ChoiceRequest* request = choice();
	if (!request) {
		return std::nullopt;
	}
	ChoiceView view;
	view.prompt = request->prompt.value.empty() ? "" : scriptText(request->prompt);
	view.yesNo = request->yes_no;
	if (request->yes_no) {
		view.options = {stringText(kChoiceYes), stringText(kChoiceNo)};
	} else {
		for (const events::Text& option : request->options) {
			view.options.push_back(scriptText(option));
		}
	}
	return view;
}

rules::Message Session::choose(int index) {
	const events::ChoiceRequest* request = choice();
	if (!request) {
		return refusal("no_choice");
	}
	const int count = request->yes_no ? 2 : static_cast<int>(request->options.size());
	if (index < 0 || index >= count) {
		return refusal("pick_option", {std::to_string(count - 1)});
	}
	HandleState& state = handles_[choiceHandle_];
	state.done = true;
	state.result = index;
	pumpEvent(0);
	return {};
}

bool Session::canSkip() const {
	return interpreter_ && mode() == Mode::Event && interpreter_->can_skip();
}

bool Session::skip() {
	if (!canSkip() || !interpreter_->skip()) {
		return false;
	}
	pumpEvent(0);
	return true;
}

// Menus an event opens

void Session::openScriptMenu(Mode mode, MenuKind kind, events::Handle handle) {
	if (!modes_.transition(mode)) {
		warn(std::string("a menu cannot open from ") + toString(this->mode()));
		handles_[handle].done = true;
		return;
	}
	menuKind_ = kind;
	menuHandle_ = handle;
}

void Session::finishScriptMenu() {
	auto it = handles_.find(menuHandle_);
	if (it != handles_.end()) {
		it->second.done = true;
	}
}

// Battles

void Session::startFieldBattle(const EncounterStart& start, const std::string& vehicle) {
	battleSource_ = start.enemyId ? BattleSource::Visible : BattleSource::Random;
	battleEnemyId_ = start.enemyId;
	battleHandle_ = 0;
	const std::string error = startBattle(start.encounter, start.opening, true, false, rules::ScriptedEnd(), vehicle);
	if (!error.empty()) {
		warn("battle '" + start.encounter + "' could not start: " + error);
	}
}

std::string Session::startBattle(const std::string& encounter, rules::Opening opening, bool escapeAllowed, bool defeatHasBranch, const rules::ScriptedEnd& end,
	const std::string& vehicle) {
	rules::BattleSetup setup;
	setup.party = activeParty();
	for (rules::PartyMember& m : setup.party) {
		auto named = world_.memberNames.find(m.characterId);
		if (named != world_.memberNames.end()) {
			m.name = named->second;
		}
	}
	setup.inventory = world_.items;
	for (const auto& [id, count] : world_.equipmentBag) {
		const rules::EquipmentDef* piece = data_.db.equipmentPiece(id);
		if (piece && piece->useFromBag && count > 0) {
			setup.pieces[id] = count;
		}
	}
	setup.vehicle = vehicle;
	setup.flags.vehicleBattle = !vehicle.empty();
	if (auto place = world_.vehicles.find(vehicle); place != world_.vehicles.end()) {
		setup.vehicleSkillUses = place->second.skillUses;
	}
	const std::string error = rules::useEncounter(setup, data_.db, encounter);
	if (!error.empty()) {
		return error;
	}
	setup.flags.escapeAllowed = setup.flags.escapeAllowed && escapeAllowed;
	setup.flags.defeatHasBranch = defeatHasBranch;
	setup.opening = opening;
	setup.scriptedEnd = end;
	setup.difficulty = world_.settings.difficulty;
	setup.rewardMultiplier = world_.settings.rewardMultiplier;
	streamBeforeBattle_ = world_.encounterStream.state();
	// BAT-134: drawn from the encounter stream; a retry mixes in its counter so it rolls differently.
	setup.seed = world_.encounterStream.next() ^ (static_cast<std::uint64_t>(retryCount_) * 0x9E3779B97F4A7C15ULL);
	rules::Result<rules::Battle> created = rules::Battle::create(data_.db, setup, &world_.comboLog);
	if (!created.ok()) {
		return created.error;
	}
	if (!modes_.transition(Mode::Battle)) {
		return std::string("a battle cannot start from ") + toString(mode());
	}
	battle_.emplace(std::move(*created.value));
	ended_.reset();
	battleEncounter_ = encounter;
	battleRounds_ = 0;
	battleDefeatHandled_ = defeatHasBranch;
	const Boss* boss = data_.boss(encounter);
	battleIsBoss_ = boss != nullptr;
	if (pacingOn()) {
		pacing_.battleStarted();
		if (boss) {
			pacing_.bossStarted(boss->flag, encounter, averageLevelX100());
		}
	}
	emit(OutputKind::Notice, "game.battle_start");
	return "";
}

rules::Message Session::setCommand(int actor, const rules::Command& command) {
	if (!battle_) {
		return refusal("no_battle");
	}
	return battle_->setCommand(actor, command);
}

rules::Message Session::applyMacro(const std::string& name, std::vector<rules::MacroStepResult>* results) {
	if (!battle_) {
		return refusal("no_battle");
	}
	for (const rules::Macro& m : world_.macros) {
		if (m.name == name) {
			std::vector<rules::MacroStepResult> r = battle_->applyMacro(m);
			if (results) {
				*results = std::move(r);
			}
			return {};
		}
	}
	return refusal("no_macro", {name});
}

rules::Message Session::startAuto() {
	if (!battle_) {
		return refusal("no_battle");
	}
	if (world_.autoMacro.empty()) {
		return refusal("no_auto_macro");
	}
	for (const rules::Macro& m : world_.macros) {
		if (m.name == world_.autoMacro) {
			battle_->startAuto(m);
			emit(OutputKind::Notice, "game.auto_on", {m.name});
			return {};
		}
	}
	return refusal("no_macro", {world_.autoMacro});
}

void Session::cancelAuto() {
	if (battle_) {
		battle_->cancelAuto();
	}
}

bool Session::autoActive() const {
	return battle_ && battle_->autoActive();
}

bool Session::fillFromAuto() {
	if (!autoActive()) {
		return false;
	}
	if (battle_->fillFromAuto()) {
		return true;
	}
	emit(OutputKind::Notice, std::string("auto.stopped.") + rules::toString(battle_->autoStop()));
	return false;
}

void Session::fillFromPolicy() {
	if (!battle_ || !battle_->sideActsNextRound(rules::Side::Party)) {
		return;
	}
	for (int i = 0; i < battle_->partyCount(); ++i) {
		if (battle_->combatant(i).alive()) {
			battle_->setCommand(i, rules::autoCommand(*battle_, i));
		}
	}
}

rules::Message Session::fillAutoRound() {
	if (!battle_) {
		return refusal("no_battle");
	}
	rules::Message refused;
	if (!world_.autoMacro.empty() && battle_->round() == 0 && !battle_->autoActive()) {
		refused = startAuto();
	}
	if (!fillFromAuto()) {
		fillFromPolicy();
	}
	return refused;
}

AutoBattle Session::playBattle(int maxRounds) {
	AutoBattle played;
	for (int round = 0; round < maxRounds && battle_; ++round) {
		const rules::Message refused = fillAutoRound();
		if (round == 0) {
			played.refusal = refused;
		}
		AutoRound r;
		r.output = drainOutput();
		r.events = resolveRound();
		played.rounds.push_back(std::move(r));
	}
	played.ended = !battle_;
	return played;
}

void Session::dismissBattle() {
	ended_.reset();
}

std::vector<rules::Event> Session::resolveRound() {
	if (!battle_) {
		return {};
	}
	battle_->setPlayTime(world_.playTimeMs);
	std::vector<rules::Event> events = battle_->resolveRound();
	++battleRounds_;
	addTime(data_.config.roundMs);
	if (battle_->outcome() != rules::Outcome::Ongoing) {
		finishBattle();
	}
	return events;
}

// SAV-032: until the configured flag is set, every member who has joined and is not in the battle gets the share
// each member standing got.
void Session::shareWithMembersOutside(const std::vector<rules::PartyMember>& fought, const rules::Rewards& rewards) {
	if (data_.config.outsideExperienceUntil.empty() || world_.flag(data_.config.outsideExperienceUntil)) {
		return;
	}
	int share = 0;
	for (int amount : rewards.memberExperience) {
		share = std::max(share, amount);
	}
	if (share <= 0) {
		return;
	}
	for (rules::PartyMember& member : world_.roster) {
		const bool inBattle = std::any_of(fought.begin(), fought.end(), [&](const rules::PartyMember& m) { return m.characterId == member.characterId; });
		if (inBattle) {
			continue;
		}
		const rules::LevelUp up = rules::grantExperience(member, data_.db, share);
		if (up.levelsGained > 0) {
			emit(OutputKind::Notice, "game.level_up", {member.characterId, std::to_string(member.level)});
		}
		for (const std::string& ability : up.learned) {
			emit(OutputKind::Notice, "game.learned", {member.characterId, ability});
		}
	}
}

void Session::finishBattle() {
	const rules::Outcome outcome = battle_->outcome();
	const rules::Rewards rewards = battle_->rewards();
	const std::vector<rules::PartyMember> state = battle_->partyState();
	world_.items.clear();
	for (const auto& [id, n] : battle_->inventory()) {
		if (n > 0) {
			world_.items[id] = n;
		}
	}
	const bool gameOver = battle_->gameOver();
	if (auto place = world_.vehicles.find(battle_->setup().vehicle); place != world_.vehicles.end()) {
		place->second.skillUses = battle_->vehicleSkillUses();
	}
	ended_.emplace(EndedBattle{std::move(*battle_), battleEncounter_, battleSource_, outcome, gameOver, rules::Rewards(), {}});
	battle_.reset();
	for (const rules::PartyMember& m : state) {
		if (rules::PartyMember* member = world_.member(m.characterId)) {
			*member = m;
			// The chosen name lives in World::memberNames; the battle only borrowed it.
			member->name.clear();
		}
	}
	int experience = 0;
	int money = 0;
	if (outcome == rules::Outcome::Victory || outcome == rules::Outcome::ScriptedEnd) {
		ended_->rewards = rewards;
		experience = rewards.experience;
		money = rewards.currency;
		world_.money = std::min(data_.config.moneyCap, world_.money + money);
		emit(OutputKind::Notice, outcome == rules::Outcome::Victory ? "game.victory" : "game.scripted_end", {std::to_string(experience), std::to_string(money)});
		for (const rules::Inventory* drops : {&rewards.items, &rewards.equipment}) {
			for (const auto& [id, n] : *drops) {
				giveThing(id, n);
				emit(OutputKind::Notice, "game.dropped", {std::to_string(n), id});
			}
		}
		for (std::size_t i = 0; i < state.size() && i < rewards.memberExperience.size(); ++i) {
			rules::PartyMember* member = world_.member(state[i].characterId);
			const rules::LevelUp up = rules::grantExperience(*member, data_.db, rewards.memberExperience[i]);
			if (up.levelsGained > 0 || !up.learned.empty()) {
				ended_->levelUps.push_back({member->characterId, member->level, up.learned});
			}
			if (up.levelsGained > 0) {
				emit(OutputKind::Notice, "game.level_up", {member->characterId, std::to_string(member->level)});
			}
			for (const std::string& ability : up.learned) {
				emit(OutputKind::Notice, "game.learned", {member->characterId, ability});
			}
		}
		shareWithMembersOutside(state, rewards);
	} else if (outcome == rules::Outcome::Escaped) {
		emit(OutputKind::Notice, "game.got_away");
	} else {
		emit(OutputKind::Notice, "game.fallen");
	}
	if (pacingOn()) {
		pacing_.battleEnded(battleRounds_, data_.config.roundMs, experience, money);
		if (battleIsBoss_ && outcome == rules::Outcome::Victory) {
			pacing_.bossEnded();
		}
	}
	world_.graceMs = data_.config.graceMs;

	if (battleSource_ == BattleSource::Visible) {
		auto it = std::find_if(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.id == battleEnemyId_; });
		if (it != world_.enemies.end()) {
			if (outcome == rules::Outcome::Victory) {
				world_.enemies.erase(it);
			} else {
				it->state = EnemyState::Returning;
				it->stateMs = 0;
			}
		}
	}

	if (outcome == rules::Outcome::Defeat && !(battleSource_ == BattleSource::Event && battleDefeatHandled_)) {
		enterGameOver();
		return;
	}
	retryCount_ = 0;
	pendingRetrySlot_.clear();
	if (battleSource_ == BattleSource::Event) {
		events::BattleResult result = events::BattleResult::win;
		if (outcome == rules::Outcome::Escaped) {
			result = events::BattleResult::flee;
		} else if (outcome == rules::Outcome::Defeat) {
			result = events::BattleResult::lose;
			// The spec does not say what state the party is in after a scripted defeat; fallen members stand
			// back up with 1 hit point so the field stays playable.
			for (const std::string& id : world_.lineup) {
				rules::PartyMember* m = world_.member(id);
				if (m && m->hp <= 0) {
					m->hp = 1;
				}
			}
		}
		auto it = handles_.find(battleHandle_);
		if (it != handles_.end()) {
			it->second.done = true;
			it->second.result = static_cast<int>(result);
		}
		modes_.transition(Mode::Event);
		pumpEvent(0);
		return;
	}
	modes_.transition(Mode::Field);
	settleField();
}

void Session::enterGameOver() {
	emit(OutputKind::Notice, "game.game_over");
	dropEvent();
	battle_.reset();
	modes_.transition(Mode::Title);
	gameOver_ = true;
	retrySlot_ = pendingRetrySlot_;
	pendingRetrySlot_.clear();
}

// Saves

SaveFile Session::makeSave(bool autosave) const {
	SaveFile save;
	save.world = world_;
	save.autosave = autosave;
	save.contentHash = data_.contentHash;
	save.writeTime = options_.clock ? options_.clock() : 0;
	save.sequence = store_ ? store_->nextSequence() : 0;
	return save;
}

void Session::autosave(std::string reason) {
	++autosaveCount_;
	areasSinceAutosave_ = {world_.area};
	autosaveReason_.clear();
	SaveFile save = makeSave(true);
	if (!options_.clock) {
		save.writeTime = ++clockCounter_;
	}
	if (reason == kBeforeBattle && interpreter_) {
		save.midEvent = events::save_state_to_text(interpreter_->save());
		save.world.encounterStream.restore(streamBeforeBattle_);
	}
	const std::string why = text("game.reason." + reason);
	if (!store_) {
		emit(OutputKind::Autosave, "game.autosave_point", {why});
		return;
	}
	const std::string slot = store_->nextAutosaveSlot();
	std::string error;
	if (!store_->write(slot, serializeSave(save), error)) {
		emit(OutputKind::Warning, "game.autosave_failed", {error});
		return;
	}
	if (reason == kBeforeBattle) {
		pendingRetrySlot_ = slot;
	}
	emit(OutputKind::Autosave, "game.autosaved", {slot, why});
}

void Session::writeClearRecord() {
	if (store_) {
		std::string error;
		const std::int64_t time = options_.clock ? options_.clock() : ++clockCounter_;
		if (!writeFileAtomic(store_->clearRecordPath(), serializeClearRecord(world_, data_.contentHash, time), error)) {
			warn("the clear-data record was not written: " + error);
			return;
		}
	}
	emit(OutputKind::Notice, "game.clear_recorded");
}

rules::Message Session::saveManual(const std::string& slot) {
	const bool fieldMenu = mode() == Mode::Menu && menuKind_ == MenuKind::Field && modes_.returnMode() == Mode::Field;
	const bool prompt = mode() == Mode::Menu && menuKind_ == MenuKind::SavePrompt;
	if (!fieldMenu && !prompt) {
		return refusal("save_where");
	}
	if (fieldMenu && interpreter_) {
		return refusal("save_in_event");
	}
	if (!store_) {
		return refusal("saving_disabled");
	}
	if (slot.compare(0, 7, "manual_") != 0 || !store_->validSlot(slot)) {
		return refusal("no_slot", {slot});
	}
	SaveFile save = makeSave(false);
	if (!options_.clock) {
		save.writeTime = ++clockCounter_;
	}
	// A save prompt's save loads into the field (EVT-053), so it must not keep the event's lock on the player.
	save.world.playerControl = true;
	std::string error;
	if (!store_->write(slot, serializeSave(save), error)) {
		return refusal("save_failed", {error});
	}
	emit(OutputKind::Notice, "game.saved", {slot});
	return {};
}

// Menus

rules::Message Session::openMenu() {
	if (mode() != Mode::Field) {
		return refusal("menu_field_only");
	}
	if (!modes_.transition(Mode::Menu)) {
		return refusal("menu_cannot_open");
	}
	menuKind_ = MenuKind::Field;
	innId_.clear();
	shopId_.clear();
	portId_.clear();
	return {};
}

rules::Message Session::closeMenu() {
	if (mode() != Mode::Menu && mode() != Mode::Shop) {
		return refusal("no_menu");
	}
	modes_.transition(modes_.returnMode());
	finishScriptMenu();
	menuKind_ = MenuKind::None;
	innId_.clear();
	shopId_.clear();
	portId_.clear();
	nameMember_.clear();
	settleField();
	if (mode() == Mode::Event) {
		pumpEvent(0);
	}
	return {};
}

const Shop* Session::shop() const {
	return mode() == Mode::Shop ? data_.shop(shopId_) : nullptr;
}

const RestPoint* Session::inn() const {
	return menuKind() == MenuKind::Inn ? data_.rest(innId_) : nullptr;
}

bool Session::shopOpen(const Shop& shop) const {
	return (shop.requireFlag.empty() || world_.flag(shop.requireFlag)) && (shop.blockFlag.empty() || !world_.flag(shop.blockFlag));
}

std::vector<std::string> Session::stock(const Shop& shop) const {
	std::vector<std::string> out;
	for (const StockEntry& entry : shop.stock) {
		if ((entry.requireFlag.empty() || world_.flag(entry.requireFlag)) && (entry.blockFlag.empty() || !world_.flag(entry.blockFlag))) {
			out.push_back(entry.id);
		}
	}
	return out;
}

rules::Message Session::buy(const std::string& id, int count) {
	const Shop* s = shop();
	if (!s) {
		return refusal("no_shop");
	}
	const std::vector<std::string> sold = stock(*s);
	if (std::find(sold.begin(), sold.end(), id) == sold.end()) {
		return refusal("not_sold", {id});
	}
	if (count < 1) {
		return refusal("buy_one");
	}
	const std::int64_t total = static_cast<std::int64_t>(data_.price(id)) * count;
	if (total > world_.money) {
		return refusal("no_money");
	}
	const std::map<std::string, int>& bag = data_.db.item(id) ? world_.items : world_.equipmentBag;
	auto held = bag.find(id);
	if ((held == bag.end() ? 0 : held->second) + count > data_.config.itemCap) {
		return refusal("carry_cap", {std::to_string(data_.config.itemCap)});
	}
	world_.money -= static_cast<int>(total);
	giveThing(id, count);
	emit(OutputKind::Notice, "game.bought", {std::to_string(count), id, std::to_string(total)});
	return {};
}

rules::Message Session::sell(const std::string& id, int count) {
	const Shop* s = shop();
	if (!s) {
		return refusal("no_shop");
	}
	if (data_.price(id) < 0) {
		return refusal("not_bought", {id});
	}
	if (count < 1) {
		return refusal("sell_one");
	}
	std::map<std::string, int>& bag = data_.db.item(id) ? world_.items : world_.equipmentBag;
	auto held = bag.find(id);
	if (held == bag.end() || held->second < count) {
		return refusal("not_enough_held");
	}
	const std::int64_t value = static_cast<std::int64_t>(data_.price(id)) * s->sellPercent / 100 * count;
	held->second -= count;
	if (held->second == 0) {
		bag.erase(held);
	}
	world_.money = static_cast<int>(std::min<std::int64_t>(data_.config.moneyCap, world_.money + value));
	emit(OutputKind::Notice, "game.sold", {std::to_string(count), id, std::to_string(value)});
	return {};
}

// Rest for the whole roster, active or not (FLD-034). A null point is a free rest.
rules::Message Session::payRest(const RestPoint* point) {
	const int cost = point ? point->cost : 0;
	if (world_.money < cost) {
		return refusal("no_money");
	}
	world_.money -= cost;
	for (rules::PartyMember& m : world_.roster) {
		rules::rest(m, data_.db);
	}
	for (auto& [id, place] : world_.vehicles) {
		place.skillUses.clear();
	}
	emit(OutputKind::Notice, "game.rested");
	if (autosaveReason_.empty()) {
		autosaveReason_ = kAfterRest;
	}
	return {};
}

rules::Message Session::rest() {
	const RestPoint* r = inn();
	if (!r) {
		return refusal("nowhere_rest");
	}
	const rules::Message problem = payRest(r);
	if (!problem.empty()) {
		return problem;
	}
	closeMenu();
	return {};
}

void Session::reportFieldUse(const std::vector<rules::Event>& events, const std::vector<rules::PartyMember>& party) {
	auto who = [&](int index) { return index >= 0 && index < static_cast<int>(party.size()) ? party[index].characterId : std::string(); };
	for (const rules::Event& e : events) {
		if (e.type == rules::EventType::Healed) {
			emit(OutputKind::Notice, "game.healed", {who(e.target), std::to_string(e.amount)});
		} else if (e.type == rules::EventType::Revived) {
			emit(OutputKind::Notice, "game.revived", {who(e.target), std::to_string(e.amount)});
		} else if (e.type == rules::EventType::StatusRemoved) {
			emit(OutputKind::Notice, e.message, {who(e.target), e.status});
		}
	}
}

rules::Message Session::useItem(const std::string& item, const std::string& member) {
	if (menuKind() != MenuKind::Field) {
		return refusal("items_menu_only");
	}
	const rules::ItemDef* def = data_.db.item(item);
	if (def && rules::travelEffect(def->effects)) {
		return refusal("travel_item", {item});
	}
	for (const FieldVehicle& vehicle : data_.config.vehicles) {
		if (!vehicle.callItem.empty() && vehicle.callItem == item && world_.items.count(item)) {
			return callVehicle(vehicle);
		}
	}
	std::vector<rules::PartyMember> party = activeParty();
	int target = -1;
	if (!member.empty()) {
		auto it = std::find(world_.lineup.begin(), world_.lineup.end(), member);
		if (it == world_.lineup.end()) {
			return {"field.unknown_member", {member}};
		}
		target = static_cast<int>(it - world_.lineup.begin());
	}
	const rules::FieldUse use = rules::useItemOutsideBattle(data_.db, party, world_.items, item, target);
	if (!use.rejection.empty()) {
		return use.rejection;
	}
	auto held = world_.items.find(item);
	if (held != world_.items.end() && held->second <= 0) {
		world_.items.erase(held);
	}
	if (std::any_of(use.events.begin(), use.events.end(), [](const rules::Event& e) { return e.type == rules::EventType::ItemHadNoEffect; })) {
		return {"game.no_effect", {item}};
	}
	for (std::size_t i = 0; i < party.size(); ++i) {
		*world_.member(world_.lineup[i]) = party[i];
	}
	emit(OutputKind::Notice, "game.used", {item});
	reportFieldUse(use.events, party);
	return {};
}

rules::Message Session::useAbility(const std::string& user, const std::string& ability, const std::string& member) {
	if (menuKind() != MenuKind::Field) {
		return refusal("tech_menu_only");
	}
	const rules::AbilityDef* def = data_.db.ability(ability);
	if (def && rules::travelEffect(def->effects)) {
		return refusal("travel_item", {ability});
	}
	auto index = [&](const std::string& id) {
		auto it = std::find(world_.lineup.begin(), world_.lineup.end(), id);
		return it == world_.lineup.end() ? -1 : static_cast<int>(it - world_.lineup.begin());
	};
	const int userIndex = index(user);
	const int target = member.empty() ? -1 : index(member);
	if (userIndex < 0 || (!member.empty() && target < 0)) {
		return {"field.unknown_member", {userIndex < 0 ? user : member}};
	}
	std::vector<rules::PartyMember> party = activeParty();
	const rules::FieldUse use = rules::useAbilityOutsideBattle(data_.db, party, userIndex, ability, target);
	if (!use.rejection.empty()) {
		return use.rejection;
	}
	for (std::size_t i = 0; i < party.size(); ++i) {
		*world_.member(world_.lineup[i]) = party[i];
	}
	emit(OutputKind::Notice, "game.used_ability", {user, ability});
	reportFieldUse(use.events, party);
	return {};
}

rules::Message Session::equip(const std::string& member, const std::string& equipment) {
	const rules::EquipmentDef* piece = data_.db.equipmentPiece(equipment);
	return equip(member, equipment, piece ? rules::defaultPosition(*piece) : rules::EquipSlot::Weapon);
}

rules::Message Session::equip(const std::string& member, const std::string& equipment, rules::EquipSlot position) {
	if (menuKind() != MenuKind::Field) {
		return refusal("equip_menu_only");
	}
	rules::PartyMember* m = world_.member(member);
	if (!m) {
		return {"equip.unknown_character", {member}};
	}
	const rules::Message refused = rules::equip(*m, data_.db, world_.equipmentBag, equipment, position);
	if (!refused.empty()) {
		return refused;
	}
	emit(OutputKind::Notice, "game.equipped", {member, equipment});
	return {};
}

rules::Message Session::unequip(const std::string& member, rules::EquipSlot slot) {
	if (menuKind() != MenuKind::Field) {
		return refusal("equip_menu_only");
	}
	rules::PartyMember* m = world_.member(member);
	if (!m) {
		return {"equip.unknown_character", {member}};
	}
	auto worn = m->equipment.find(slot);
	if (worn == m->equipment.end()) {
		return refusal("nothing_worn");
	}
	const std::string piece = worn->second;
	rules::unequip(*m, data_.db, world_.equipmentBag, slot);
	emit(OutputKind::Notice, "game.unequipped", {member, piece});
	return {};
}

rules::Message Session::enterName(const std::string& name) {
	if (menuKind() != MenuKind::NameEntry) {
		return refusal("no_name_entry");
	}
	const std::string trimmed = cleanName(name);
	if (trimmed.empty()) {
		return refusal("name_length", {std::to_string(kMaxNameLength)});
	}
	emit(OutputKind::Notice, "game.named", {displayName(nameMember_), trimmed});
	world_.memberNames[nameMember_] = trimmed;
	return closeMenu();
}

rules::Message Session::setAutoMacro(const std::string& macro) {
	if (!macro.empty() && std::none_of(world_.macros.begin(), world_.macros.end(), [&](const rules::Macro& m) { return m.name == macro; })) {
		return refusal("no_macro", {macro});
	}
	world_.autoMacro = macro;
	return {};
}

rules::Message Session::setSettings(const Settings& settings) {
	if (mode() == Mode::Battle) {
		return refusal("settings_in_battle");
	}
	if (mode() == Mode::Title) {
		return refusal("settings_no_game");
	}
	if (!data_.db.difficulty(settings.difficulty)) {
		return refusal("unknown_difficulty", {settings.difficulty});
	}
	world_.settings = settings;
	return {};
}

std::vector<DifficultyView> Session::difficulties() const {
	std::vector<DifficultyView> out;
	for (const auto& [id, d] : data_.db.difficulties) {
		const auto named = language().text.find(std::string(kNamePrefix) + "difficulty." + id);
		out.push_back({id, named == language().text.end() ? d.name : named->second, description(DescriptionKind::Difficulty, id), d.rewardPercent});
	}
	return out;
}

rules::Message Session::swapLineup(int a, int b) {
	if (menuKind() != MenuKind::Field) {
		return refusal("party_menu_only");
	}
	const int size = static_cast<int>(world_.lineup.size());
	for (int position : {a, b}) {
		if (position < 0 || position >= size) {
			return refusal("party_no_position", {std::to_string(position)});
		}
	}
	std::swap(world_.lineup[a], world_.lineup[b]);
	return {};
}

std::vector<MacroView> Session::macroViews() const {
	std::vector<MacroView> out;
	for (const rules::Macro& macro : world_.macros) {
		MacroView view;
		view.name = macro.name;
		for (const rules::MacroStep& step : macro.steps) {
			MacroStepView v;
			v.step = step;
			v.memberName = displayName(step.member);
			if (step.kind == rules::CommandKind::Ability || step.kind == rules::CommandKind::Item) {
				v.commandName = displayName(step.id);
			}
			if (step.target.kind == rules::MacroTargetKind::Member) {
				v.targetName = displayName(step.target.member);
			}
			v.issue = stepIssue(data_.db, world_, step);
			view.steps.push_back(std::move(v));
		}
		out.push_back(std::move(view));
	}
	return out;
}

rules::Message Session::createMacro(const std::string& name) {
	if (menuKind() != MenuKind::Field) {
		return refusal("macro_menu_only");
	}
	const std::string clean = cleanName(name);
	if (clean.empty()) {
		return refusal("name_length", {std::to_string(kMaxNameLength)});
	}
	if (findMacro(world_.macros, clean) != world_.macros.end()) {
		return refusal("macro_name_taken", {clean});
	}
	if (static_cast<int>(world_.macros.size()) >= kMaxMacros) {
		return refusal("macro_slots_full", {std::to_string(kMaxMacros)});
	}
	world_.macros.push_back({clean, {}});
	return {};
}

rules::Message Session::renameMacro(const std::string& name, const std::string& newName) {
	if (menuKind() != MenuKind::Field) {
		return refusal("macro_menu_only");
	}
	auto macro = findMacro(world_.macros, name);
	if (macro == world_.macros.end()) {
		return refusal("no_macro", {name});
	}
	const std::string clean = cleanName(newName);
	if (clean.empty()) {
		return refusal("name_length", {std::to_string(kMaxNameLength)});
	}
	auto other = findMacro(world_.macros, clean);
	if (other != world_.macros.end() && other != macro) {
		return refusal("macro_name_taken", {clean});
	}
	if (world_.autoMacro == name) {
		world_.autoMacro = clean;
	}
	macro->name = clean;
	return {};
}

rules::Message Session::deleteMacro(const std::string& name) {
	if (menuKind() != MenuKind::Field) {
		return refusal("macro_menu_only");
	}
	auto macro = findMacro(world_.macros, name);
	if (macro == world_.macros.end()) {
		return refusal("no_macro", {name});
	}
	world_.macros.erase(macro);
	if (world_.autoMacro == name) {
		world_.autoMacro.clear();
		emit(OutputKind::Notice, "game.auto_macro_cleared", {name});
	}
	return {};
}

rules::Message Session::moveMacro(const std::string& name, int position) {
	if (menuKind() != MenuKind::Field) {
		return refusal("macro_menu_only");
	}
	auto macro = findMacro(world_.macros, name);
	if (macro == world_.macros.end()) {
		return refusal("no_macro", {name});
	}
	if (position < 0 || position >= static_cast<int>(world_.macros.size())) {
		return refusal("macro_no_position", {std::to_string(position)});
	}
	rules::Macro moved = std::move(*macro);
	world_.macros.erase(macro);
	world_.macros.insert(world_.macros.begin() + position, std::move(moved));
	return {};
}

rules::Message Session::setMacroStep(const std::string& name, int index, const rules::MacroStep& step) {
	if (menuKind() != MenuKind::Field) {
		return refusal("macro_menu_only");
	}
	auto macro = findMacro(world_.macros, name);
	if (macro == world_.macros.end()) {
		return refusal("no_macro", {name});
	}
	std::vector<rules::MacroStep>& steps = macro->steps;
	const int count = static_cast<int>(steps.size());
	if (index < 0 || index > count) {
		return refusal("macro_no_step", {std::to_string(index)});
	}
	if (index == count && count >= rules::kMaxPartySize) {
		return refusal("macro_steps_full", {std::to_string(rules::kMaxPartySize)});
	}
	const rules::Database& db = data_.db;
	const rules::PartyMember* member = world_.member(step.member);
	if (!member) {
		return refusal("macro_unknown_member", {step.member});
	}
	for (int i = 0; i < count; ++i) {
		if (i != index && steps[i].member == step.member) {
			return refusal("macro_member_twice", {step.member});
		}
	}
	rules::Targeting targeting = rules::Targeting::OneEnemy;
	switch (step.kind) {
	case rules::CommandKind::Ability: {
		const std::vector<std::string> known = rules::knownAbilities(db, *member);
		const rules::AbilityDef* ability = db.ability(step.id);
		if (!ability || std::find(known.begin(), known.end(), step.id) == known.end()) {
			return refusal("macro_unknown_ability", {step.member, step.id});
		}
		targeting = ability->targeting;
		break;
	}
	case rules::CommandKind::Item: {
		const rules::ItemDef* item = db.item(step.id);
		if (!item) {
			return refusal("macro_unknown_item", {step.id});
		}
		targeting = item->targeting;
		break;
	}
	case rules::CommandKind::Attack:
	case rules::CommandKind::Defend:
	case rules::CommandKind::Escape:
		if (!step.id.empty()) {
			return refusal("macro_bad_command", {rules::toString(step.kind)});
		}
		break;
	case rules::CommandKind::None:
		return refusal("macro_bad_command", {rules::toString(step.kind)});
	}
	const rules::MacroTarget& target = step.target;
	if (!rules::macroTargetFits(target.kind, step.kind, targeting) ||
		(target.kind == rules::MacroTargetKind::Enemy && (target.enemySlot < 0 || target.enemySlot >= rules::kMaxEnemies))) {
		return refusal("macro_target", {rules::toString(target.kind)});
	}
	if (target.kind == rules::MacroTargetKind::Member && !world_.member(target.member)) {
		return refusal("macro_unknown_member", {target.member});
	}
	rules::MacroStep kept = step;
	if (kept.target.kind != rules::MacroTargetKind::Member) {
		kept.target.member.clear();
	}
	if (kept.target.kind != rules::MacroTargetKind::Enemy) {
		kept.target.enemySlot = -1;
	}
	if (index == count) {
		steps.push_back(std::move(kept));
	} else {
		steps[index] = std::move(kept);
	}
	return {};
}

rules::Message Session::removeMacroStep(const std::string& name, int index) {
	if (menuKind() != MenuKind::Field) {
		return refusal("macro_menu_only");
	}
	auto macro = findMacro(world_.macros, name);
	if (macro == world_.macros.end()) {
		return refusal("no_macro", {name});
	}
	if (index < 0 || index >= static_cast<int>(macro->steps.size())) {
		return refusal("macro_no_step", {std::to_string(index)});
	}
	macro->steps.erase(macro->steps.begin() + index);
	return {};
}

rules::Message Session::moveMacroStep(const std::string& name, int from, int to) {
	if (menuKind() != MenuKind::Field) {
		return refusal("macro_menu_only");
	}
	auto macro = findMacro(world_.macros, name);
	if (macro == world_.macros.end()) {
		return refusal("no_macro", {name});
	}
	std::vector<rules::MacroStep>& steps = macro->steps;
	for (int index : {from, to}) {
		if (index < 0 || index >= static_cast<int>(steps.size())) {
			return refusal("macro_no_step", {std::to_string(index)});
		}
	}
	rules::MacroStep moved = std::move(steps[from]);
	steps.erase(steps.begin() + from);
	steps.insert(steps.begin() + to, std::move(moved));
	return {};
}

rules::Message Session::macroFromCombo(const std::string& recipe, const std::string& name) {
	if (menuKind() != MenuKind::Field) {
		return refusal("macro_menu_only");
	}
	const rules::ComboLogEntry* entry = world_.comboLog.entry(recipe);
	auto found = std::find_if(data_.db.combos.begin(), data_.db.combos.end(), [&](const rules::ComboRecipe& r) { return r.id == recipe; });
	if (!entry || found == data_.db.combos.end()) {
		return refusal("combo_not_found", {recipe});
	}
	const rules::ComboRecipe& r = *found;
	const std::string clean = cleanName(name);
	if (clean.empty()) {
		return refusal("name_length", {std::to_string(kMaxNameLength)});
	}
	if (findMacro(world_.macros, clean) != world_.macros.end()) {
		return refusal("macro_name_taken", {clean});
	}
	if (static_cast<int>(world_.macros.size()) >= kMaxMacros) {
		return refusal("macro_slots_full", {std::to_string(kMaxMacros)});
	}
	std::vector<std::string> candidates = entry->characters;
	for (const std::string& id : world_.lineup) {
		candidates.push_back(id);
	}
	auto knows = [&](const std::string& member, std::size_t component) {
		const rules::PartyMember* m = world_.member(member);
		const bool tiedElsewhere = component < r.characters.size() && !r.characters[component].empty() && r.characters[component] != member;
		if (!m || tiedElsewhere || std::find(world_.lineup.begin(), world_.lineup.end(), member) == world_.lineup.end()) {
			return false;
		}
		const std::vector<std::string> known = rules::knownAbilities(data_.db, *m);
		return std::find(known.begin(), known.end(), r.abilities[component]) != known.end();
	};
	std::vector<std::string> chosen(r.abilities.size());
	std::size_t stuck = 0;
	std::function<bool(std::size_t)> assign = [&](std::size_t component) {
		if (component == r.abilities.size()) {
			return true;
		}
		for (const std::string& member : candidates) {
			if (std::find(chosen.begin(), chosen.begin() + component, member) == chosen.begin() + component && knows(member, component)) {
				chosen[component] = member;
				if (assign(component + 1)) {
					return true;
				}
			}
		}
		stuck = std::max(stuck, component);
		return false;
	};
	if (!assign(0)) {
		return refusal("combo_no_member", {r.abilities[stuck]});
	}
	rules::Macro macro{clean, {}};
	for (std::size_t i = 0; i < r.abilities.size(); ++i) {
		macro.steps.push_back({chosen[i], rules::CommandKind::Ability, r.abilities[i], {}});
	}
	world_.macros.push_back(std::move(macro));
	return {};
}

std::vector<ComboView> Session::comboLog() const {
	std::vector<ComboView> out;
	for (const rules::ComboLogEntry& entry : world_.comboLog.entries()) {
		ComboView view;
		view.recipe = entry.recipe;
		view.playTimeMs = entry.playTimeMs;
		view.characters = entry.characters;
		auto recipe = std::find_if(data_.db.combos.begin(), data_.db.combos.end(), [&](const rules::ComboRecipe& r) { return r.id == entry.recipe; });
		if (recipe != data_.db.combos.end()) {
			view.result = recipe->result;
			view.resultName = displayName(recipe->result);
			view.abilities = recipe->abilities;
		}
		out.push_back(std::move(view));
	}
	return out;
}

std::optional<MemberDetail> Session::memberDetail(const std::string& member) const {
	const rules::PartyMember* m = world_.member(member);
	if (!m || !data_.db.character(member)) {
		return std::nullopt;
	}
	const rules::Combatant combatant = rules::memberCombatant(data_.db, *m);
	MemberDetail detail;
	detail.member = member;
	detail.experienceToNext = m->level >= rules::formulas::kMaxLevel ? 0 : std::max(0, rules::experienceToNextLevel(*data_.db.character(member), m->level) - m->experience);
	detail.stats = combatant.stats;
	detail.attackElement = combatant.attackElement;
	detail.statusResist = combatant.statusResist;
	for (const auto& [slot, id] : m->equipment) {
		if (const rules::EquipmentDef* piece = data_.db.equipmentPiece(id)) {
			detail.equipmentBonus[slot] = piece->bonus;
		}
	}
	for (const std::string& element : data_.db.elementIds()) {
		const int affinity = combatant.affinity(element);
		if (affinity != combatant.neutralAffinity) {
			detail.affinities.emplace_back(element, affinity);
		}
	}
	return detail;
}

std::vector<BagEntry> Session::bag(ItemSort order) const {
	std::vector<BagEntry> out;
	for (const auto& [id, count] : world_.items) {
		out.push_back({id, count, false});
	}
	for (const auto& [id, count] : world_.equipmentBag) {
		out.push_back({id, count, true});
	}
	if (order == ItemSort::Id) {
		return out;
	}
	auto kind = [&](const BagEntry& e) {
		if (e.equipment) {
			const rules::EquipmentDef* piece = data_.db.equipmentPiece(e.id);
			return 2 + (piece ? static_cast<int>(piece->slot) : 0);
		}
		const rules::ItemDef* item = data_.db.item(e.id);
		return item && item->field ? 0 : 1;
	};
	std::vector<std::pair<std::string, BagEntry>> named;
	for (BagEntry& e : out) {
		named.emplace_back(displayName(e.id), std::move(e));
	}
	std::stable_sort(named.begin(), named.end(), [&](const auto& a, const auto& b) {
		if (order == ItemSort::Kind && kind(a.second) != kind(b.second)) {
			return kind(a.second) < kind(b.second);
		}
		if (order == ItemSort::Count && a.second.count != b.second.count) {
			return a.second.count > b.second.count;
		}
		return a.first != b.first ? a.first < b.first : a.second.id < b.second.id;
	});
	out.clear();
	for (auto& [name, e] : named) {
		out.push_back(std::move(e));
	}
	return out;
}

std::optional<EquipPreview> Session::equipPreview(const std::string& member, const std::string& piece) const {
	const rules::EquipmentDef* def = data_.db.equipmentPiece(piece);
	return equipPreview(member, piece, def ? rules::defaultPosition(*def) : rules::EquipSlot::Weapon);
}

std::optional<EquipPreview> Session::equipPreview(const std::string& member, const std::string& piece, rules::EquipSlot position) const {
	const rules::PartyMember* m = world_.member(member);
	const rules::EquipmentDef* def = data_.db.equipmentPiece(piece);
	if (!m || !def || !data_.db.character(member)) {
		return std::nullopt;
	}
	auto view = [&](const rules::PartyMember& wearer) {
		const rules::Combatant c = rules::memberCombatant(data_.db, wearer);
		return CombatView{c.stats, c.basicAttack, c.attackElement, c.abilities, c.statusResist};
	};
	EquipPreview preview;
	preview.member = member;
	preview.piece = *def;
	preview.position = position;
	if (auto worn = m->equipment.find(position); worn != m->equipment.end()) {
		preview.replaces = worn->second;
	}
	const auto other = m->equipment.find(position == rules::EquipSlot::Weapon ? rules::EquipSlot::LeftHand : rules::EquipSlot::Weapon);
	if (rules::isHandPiece(*def) && other != m->equipment.end()) {
		const rules::EquipmentDef* held = data_.db.equipmentPiece(other->second);
		if ((def->hands == 2 && position == rules::EquipSlot::Weapon) || (position == rules::EquipSlot::LeftHand && held && held->hands == 2)) {
			preview.alsoReplaces = other->second;
		}
	}
	for (const std::string& id : world_.lineup) {
		if (rules::canEquip(data_.db, id, piece).empty()) {
			preview.wearers.push_back(id);
		}
	}
	preview.before = view(*m);
	rules::PartyMember wearing = *m;
	rules::EquipmentBag bag = world_.equipmentBag;
	bag[piece] += 1;
	preview.refusal = rules::equip(wearing, data_.db, bag, piece, position);
	preview.after = preview.refusal.empty() ? view(wearing) : preview.before;
	return preview;
}

// Developer tools

rules::Message Session::devNeedsGame() const {
	if (mode() == Mode::Title) {
		return refusal("no_game");
	}
	if (mode() == Mode::Battle) {
		return refusal("dev_in_battle");
	}
	return {};
}

rules::Message Session::devNeedsField() const {
	if (mode() == Mode::Title) {
		return refusal("no_game");
	}
	if (mode() != Mode::Field) {
		return refusal("dev_field_only");
	}
	return {};
}

void Session::devMark() {
	if (!world_.devMarked) {
		world_.devMarked = true;
		emit(OutputKind::Notice, "game.dev_marked");
	}
}

std::vector<std::string> Session::devList(DevList list) const {
	std::vector<std::string> out;
	auto keys = [&out](const auto& table) {
		for (const auto& entry : table) {
			out.push_back(entry.first);
		}
	};
	switch (list) {
	case DevList::Areas: keys(data_.areas); break;
	case DevList::Markers:
		for (const auto& [id, a] : data_.areas) {
			for (const auto& marker : a.markers) {
				out.push_back(id + ":" + marker.first);
			}
		}
		break;
	case DevList::Flags:
		for (const events::Declaration& d : data_.program.declarations) {
			if (d.is_flag && !d.local) {
				out.push_back(d.name);
			}
		}
		std::sort(out.begin(), out.end());
		break;
	case DevList::Items: keys(data_.db.items); break;
	case DevList::Equipment: keys(data_.db.equipment); break;
	case DevList::Abilities: keys(data_.db.abilities); break;
	case DevList::Enemies: keys(data_.db.enemies); break;
	case DevList::Encounters: keys(data_.db.encounters); break;
	case DevList::Events:
		for (const events::EventInfo& e : data_.program.events) {
			out.push_back(e.name);
		}
		std::sort(out.begin(), out.end());
		break;
	case DevList::Vehicles:
		for (const FieldVehicle& v : data_.config.vehicles) {
			out.push_back(v.id);
		}
		std::sort(out.begin(), out.end());
		break;
	case DevList::Characters: keys(data_.db.characters); break;
	}
	return out;
}

rules::Message Session::devWarp(const std::string& areaId, const std::string& marker) {
	const Area* a = data_.area(areaId);
	if (!a) {
		return refusal("dev_unknown_area", {areaId});
	}
	auto it = a->markers.find(marker);
	if (it == a->markers.end()) {
		return refusal("dev_unknown_marker", {areaId, marker});
	}
	return devWarpCell(areaId, it->second);
}

rules::Message Session::devWarpCell(const std::string& areaId, Cell cell) {
	if (rules::Message refused = devNeedsField(); !refused.empty()) {
		return refused;
	}
	const Area* a = data_.area(areaId);
	if (!a) {
		return refusal("dev_unknown_area", {areaId});
	}
	if (!a->walkable(cell)) {
		return refusal("dev_bad_cell", {areaId, std::to_string(cell.x), std::to_string(cell.y)});
	}
	devMark();
	leaveVehicleBehind();
	enterAreaAt(areaId, cell);
	settleField();
	return {};
}

rules::Message Session::devSetFlag(const std::string& flag, bool value) {
	if (mode() == Mode::Title) {
		return refusal("no_game");
	}
	const events::Declaration* declared = data_.registry.find(flag);
	if (!declared || !declared->is_flag || declared->local) {
		return refusal("dev_unknown_flag", {flag});
	}
	devMark();
	world_.flags[flag] = value;
	refreshJournal(true);
	settleField();
	return {};
}

rules::Message Session::devGive(const std::string& id, int count) {
	if (rules::Message refused = devNeedsGame(); !refused.empty()) {
		return refused;
	}
	if (!data_.db.item(id) && !data_.db.equipmentPiece(id)) {
		return refusal("dev_unknown_thing", {id});
	}
	if (count == 0) {
		return refusal("dev_bad_count");
	}
	devMark();
	if (count > 0) {
		giveThing(id, count);
	} else {
		host_->take_item(id, -count);
	}
	return {};
}

rules::Message Session::devMoney(int amount) {
	if (rules::Message refused = devNeedsGame(); !refused.empty()) {
		return refused;
	}
	devMark();
	world_.money = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(world_.money) + amount, 0, data_.config.moneyCap));
	return {};
}

rules::Message Session::devSetLevel(const std::string& member, int level) {
	if (rules::Message refused = devNeedsGame(); !refused.empty()) {
		return refused;
	}
	rules::PartyMember* m = world_.member(member);
	if (!m) {
		return refusal("dev_unknown_member", {member});
	}
	if (level < 1 || level > rules::formulas::kMaxLevel) {
		return refusal("dev_bad_level", {std::to_string(level), std::to_string(rules::formulas::kMaxLevel)});
	}
	devMark();
	m->level = level;
	m->experience = 0;
	m->hp = std::max(m->hp, 1);
	rules::rest(*m, data_.db);
	return {};
}

rules::Message Session::devBattle(const std::string& encounter) {
	if (rules::Message refused = devNeedsField(); !refused.empty()) {
		return refused;
	}
	if (!data_.db.encounter(encounter)) {
		return refusal("dev_unknown_encounter", {encounter});
	}
	devMark();
	startFieldBattle(EncounterStart{encounter, rules::Opening::Normal, 0}, world_.aboard);
	return {};
}

rules::Message Session::devEndBattle(bool victory, std::vector<rules::Event>* events) {
	if (!battle_) {
		return refusal("no_battle");
	}
	devMark();
	for (int i = 0; i < static_cast<int>(battle_->combatants().size()); ++i) {
		if (battle_->isParty(i) != victory) {
			battle_->combatant(i).hp = 0;
		}
	}
	std::vector<rules::Event> round = resolveRound();
	if (events) {
		*events = std::move(round);
	}
	return {};
}

rules::Message Session::devVehicle(const std::string& vehicle, const std::string& areaId, const std::string& marker) {
	if (rules::Message refused = devNeedsField(); !refused.empty()) {
		return refused;
	}
	if (!data_.vehicle(vehicle)) {
		return refusal("dev_unknown_vehicle", {vehicle});
	}
	const Area* a = data_.area(areaId);
	if (!a) {
		return refusal("dev_unknown_area", {areaId});
	}
	if (!a->markers.count(marker)) {
		return refusal("dev_unknown_marker", {areaId, marker});
	}
	if (a->kind != AreaKind::Field) {
		return refusal("dev_vehicle_outdoors", {areaId});
	}
	devMark();
	const bool had = world_.vehicles.count(vehicle) > 0;
	if (world_.aboard == vehicle) {
		dismount();
	}
	const Cell cell = a->markers.at(marker);
	world_.vehicles[vehicle] = {areaId, cell, Facing::South, areaId, cell};
	if (!had) {
		emit(OutputKind::Notice, "game.vehicle_acquired", {vehicle});
	}
	return {};
}

rules::Message Session::devEncounters(bool on) {
	if (mode() == Mode::Title) {
		return refusal("no_game");
	}
	devMark();
	encountersOn_ = on;
	return {};
}

rules::Message Session::devRunEvent(const std::string& event) {
	if (rules::Message refused = devNeedsField(); !refused.empty()) {
		return refused;
	}
	if (data_.program.find_event(event) < 0) {
		return refusal("dev_unknown_event", {event});
	}
	devMark();
	startEvent(event);
	return {};
}

std::optional<EventPosition> Session::eventPosition() const {
	if (!interpreter_) {
		return std::nullopt;
	}
	const events::Interpreter::Position at = interpreter_->position();
	if (at.event.empty()) {
		return std::nullopt;
	}
	return EventPosition{at.event, at.file, at.line};
}

rules::Message Session::saveText(std::string& out) const {
	if (mode() == Mode::Title) {
		return refusal("no_game");
	}
	SaveFile save = makeSave(false);
	if (battle_) {
		save.world.encounterStream.restore(streamBeforeBattle_);
	}
	if (interpreter_) {
		save.midEvent = events::save_state_to_text(interpreter_->save());
	}
	out = serializeSave(save);
	return {};
}

rules::Message Session::loadSaveText(const std::string& text) {
	retryCount_ = 0;
	retrySlot_.clear();
	return loadText(text, "");
}

const char* toString(DevList list) {
	switch (list) {
	case DevList::Areas: return "areas";
	case DevList::Markers: return "markers";
	case DevList::Flags: return "flags";
	case DevList::Items: return "items";
	case DevList::Equipment: return "equipment";
	case DevList::Abilities: return "abilities";
	case DevList::Enemies: return "enemies";
	case DevList::Encounters: return "encounters";
	case DevList::Events: return "events";
	case DevList::Vehicles: return "vehicles";
	case DevList::Characters: return "characters";
	}
	return "areas";
}

bool parseDevList(const std::string& text, DevList& out) {
	for (DevList list : {DevList::Areas, DevList::Markers, DevList::Flags, DevList::Items, DevList::Equipment, DevList::Abilities, DevList::Enemies,
			 DevList::Encounters, DevList::Events, DevList::Vehicles, DevList::Characters}) {
		if (text == toString(list)) {
			out = list;
			return true;
		}
	}
	return false;
}

}
