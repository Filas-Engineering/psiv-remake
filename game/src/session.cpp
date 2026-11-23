#include "game/session.h"

#include "game/capabilities.h"
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
#include <chrono>
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
// No view of the actor in Session::TickViews.
constexpr std::size_t kAway = static_cast<std::size_t>(-1);
// Session::AreaIndex::objects bits: something stands on the cell, a platform's end is there, an exit is there.
constexpr std::uint8_t kObject = 1;
constexpr std::uint8_t kPlatformEnd = 2;
constexpr std::uint8_t kExit = 4;

// Adds the time from its making to its end to `total`, in microseconds.
struct Timer {
	explicit Timer(std::uint64_t& total) : total_(total), start_(std::chrono::steady_clock::now()) {}
	~Timer() { total_ += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_).count()); }
	Timer(const Timer&) = delete;
	Timer& operator=(const Timer&) = delete;

private:
	std::uint64_t& total_;
	std::chrono::steady_clock::time_point start_;
};
// Cells of the leader's trail kept: more than a lineup's followers and the follower actor need.
constexpr std::size_t kTrailCells = 8;

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
	for (const auto& [name, snapshot] : w.partySnapshots) {
		std::set<std::string> listed;
		for (const std::vector<std::string>* part : {&snapshot.lineup, &snapshot.reserve}) {
			for (const std::string& id : *part) {
				if (!db.character(id) || !listed.insert(id).second) {
					return named("unknown or repeated member in party snapshot", name);
				}
			}
		}
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
		for (const auto& [element, affinity] : m.affinities) {
			const std::vector<std::string> known = db.elementIds();
			if (std::find(known.begin(), known.end(), element.id) == known.end() || affinity < 0 || affinity > 10000) {
				return named("bad granted affinity", element.id);
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
		const auto memberSteps = std::count_if(macro.steps.begin(), macro.steps.end(), [&](const rules::MacroStep& s) { return !db.vehicle(s.member); });
		if (memberSteps > rules::kMaxPartySize) {
			return named("too many steps in macro", macro.name);
		}
		for (const rules::MacroStep& step : macro.steps) {
			const bool badTarget = step.target.kind == rules::MacroTargetKind::Enemy && (step.target.enemySlot < 0 || step.target.enemySlot >= rules::kMaxEnemies);
			if (db.vehicle(step.member)) {
				const bool badCommand = step.piece || step.kind == rules::CommandKind::Item || step.kind == rules::CommandKind::Defend;
				const bool badId = step.kind == rules::CommandKind::Ability && !db.ability(step.id);
				if (badCommand || badId || badTarget || step.target.kind == rules::MacroTargetKind::Member) {
					return named("bad step in macro", macro.name);
				}
				continue;
			}
			const bool badMember = step.target.kind == rules::MacroTargetKind::Member && !db.character(step.target.member);
			const rules::EquipmentDef* piece = step.piece ? db.equipmentPiece(step.id) : nullptr;
			const bool badPiece = step.piece && (step.kind != rules::CommandKind::Item || !piece || !db.ability(piece->use));
			const bool badId = (step.kind == rules::CommandKind::Ability && !db.ability(step.id)) || (step.kind == rules::CommandKind::Item && !step.piece && !db.item(step.id)) || badPiece;
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
		if (std::find(data.config.actors.begin(), data.config.actors.end(), id) == data.config.actors.end() || (!place.area.empty() && !cellIn(place.area, place.cell)) ||
			place.animationMs < 0 || place.animationTotalMs < place.animationMs || place.animation.empty() != (place.animationTotalMs == 0) ||
			(!place.lookEnemy.empty() && !db.enemy(place.lookEnemy)) || (!place.lookEnemy.empty() && !place.look.empty()) || place.wanderProgress < 0 ||
			place.wanderPauseMs < 0 || place.patrolStep < 0 || place.patrolWalked < 0 || place.pathTick < 0) {
			return named("unknown actor or cell outside its area for", id);
		}
	}
	for (const auto& [id, place] : w.platforms) {
		const Platform* platform = here.platform(id);
		if (!platform || place.positionUs > platform->length() * platform->stepMs * 1000LL) {
			return named("unknown platform or place outside its run for", id);
		}
	}
	if (!w.ridingPlatform.empty() && !w.platforms.count(w.ridingPlatform)) {
		return named("riding a platform not in the area", w.ridingPlatform);
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
	for (Cell c : w.trail) {
		if (!cellIn(w.area, c)) {
			return "a follower's cell is outside the area";
		}
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
	// A vehicle's step runs in battles aboard it: judged on the vehicle the party has and the skill uses it keeps.
	if (const rules::VehicleDef* vehicle = db.vehicle(step.member)) {
		const auto held = world.vehicles.find(step.member);
		if (held == world.vehicles.end()) {
			return rules::MacroIssue::NotInParty;
		}
		if (step.kind == rules::CommandKind::Ability) {
			if (std::find(vehicle->abilities.begin(), vehicle->abilities.end(), step.id) == vehicle->abilities.end()) {
				return rules::MacroIssue::UnknownAbility;
			}
			const auto uses = held->second.skillUses.find(step.id);
			if (uses != held->second.skillUses.end() && uses->second <= 0) {
				return rules::MacroIssue::NoUsesLeft;
			}
		}
		return rules::MacroIssue::None;
	}
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
	if (step.kind == rules::CommandKind::Item && step.piece) {
		const bool worn = std::any_of(m->equipment.begin(), m->equipment.end(), [&](const auto& slot) { return slot.second == step.id; });
		const rules::EquipmentDef* piece = db.equipmentPiece(step.id);
		auto bagged = world.equipmentBag.find(step.id);
		const bool fromBag = piece && piece->useFromBag && bagged != world.equipmentBag.end() && bagged->second > 0;
		if (!worn && !fromBag) {
			return rules::MacroIssue::NoItem;
		}
	} else if (step.kind == rules::CommandKind::Item) {
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
const Hook* talkPoint(const Area* area, const std::string& actor) {
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
	case MenuKind::Credits: return "credits";
	}
	return "none";
}

const char* toString(InteractKind kind) {
	switch (kind) {
	case InteractKind::Vehicle: return "vehicle";
	case InteractKind::Port: return "port";
	case InteractKind::Helm: return "helm";
	case InteractKind::Talk: return "talk";
	case InteractKind::Actor: return "actor";
	case InteractKind::Chest: return "chest";
	case InteractKind::Shop: return "shop";
	case InteractKind::Rest: return "rest";
	}
	return "talk";
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
	ActionQueue(const PresentationNames& names, const std::vector<Cell>& route, Presentation presentation, std::function<void(const PresentedAction&)> onAction)
		: names_(names), route_(route), queued_(presentation == Presentation::Queued), onAction_(std::move(onAction)) {}

	bool begin(std::uint64_t handle, const events::Action& action) override {
		push({handle, ActionPhase::Begin, action, events::describe(action), events::action_seconds(action, names_.durations, names_.subjectDurations), route_});
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
	const std::vector<Cell>& route_;
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
		"game.bag_full",
		"game.boarded",
		"game.boarded_ship",
		"game.bought",
		"game.chest_empty",
		"game.clear_not_recorded",
		"game.clear_recorded",
		"game.dev_marked",
		"game.discarded",
		"game.drop_left",
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
		"game.machine_stood_up",
		"game.money_given",
		"game.money_taken",
		"game.named",
		"game.no_effect",
		"game.paralysis_worn_off",
		"game.poison_fell",
		"game.port_open",
		"game.reason.after_rest",
		"game.reason.area_entry",
		"game.reason.before_battle",
		"game.reason.event_end",
		"game.reason.vehicle",
		"game.refuse.aboard",
		"game.refuse.blocked",
		"game.refuse.bag_full",
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
		"game.refuse.discard_menu_only",
		"game.refuse.discard_not_held",
		"game.refuse.discard_one",
		"game.refuse.equip_menu_only",
		"game.refuse.erase_failed",
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
		"game.refuse.macro_piece_no_use",
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
		"game.refuse.no_back_option",
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
		"game.refuse.not_discardable",
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
		"game.refuse.unknown_option_level",
		"game.refuse.vehicle_not_owned",
		"game.refuse.walk_field_only",
		"game.rest_no_money",
		"game.rested",
		"game.revived",
		"game.save_erased",
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
		// game.json may give an animation its own length, which stands in for the script's.
		events::Action shown = action;
		const PresentationNames& names = s_.data_.config.presentation;
		const bool timed = action.type == events::ActionType::animate || ((action.type == events::ActionType::show_actor || action.type == events::ActionType::hide_actor) && !action.target.empty());
		if (timed && names.animationSeconds.count(action.target)) {
			shown.seconds = names.animationSeconds.at(action.target);
		}
		if (action.type == events::ActionType::music_play && action.fade_ms < 0) {
			shown.fade_ms = names.music.fadeMs;
		}
		if (action.type == events::ActionType::music_play) {
			s_.scriptTrack_ = action.subject;
		} else if (action.type == events::ActionType::music_stop) {
			s_.scriptTrack_ = std::string();
		}
		if (action.type == events::ActionType::warp) {
			s_.actionRoute_.clear();
			s_.leaveVehicleBehind();
			if (const int index = action.followers.empty() ? -1 : gameEnumIndex("followers_arrive", action.followers); index >= 0) {
				s_.walkedFollowers_ = static_cast<FollowersArrive>(index);
			}
			s_.enterArea(action.subject, action.target);
		} else if (action.type == events::ActionType::place_leader) {
			// Hosts see a script's `place` of the leader's member on that cell, which the world now keeps.
			const std::optional<events::Action> placed = s_.placeLeaderAt(action);
			if (!placed) {
				HandleState state;
				state.kind = HandleKind::Action;
				state.done = true;
				return s_.newHandle(state);
			}
			shown = *placed;
		} else {
			const double seconds = events::action_seconds(shown, names.durations, names.subjectDurations);
			const int ms = action.instant ? 0 : static_cast<int>(std::lround(seconds * 1000));
			s_.applyActorAction(shown, ms);
			s_.moveSceneCamera(shown, ms);
		}
		HandleState state;
		state.kind = HandleKind::Action;
		const events::Handle handle = s_.newHandle(state);
		const bool finished = s_.presenter()->begin(handle, shown);
		// An instant action has its end state on screen once begin returns, whatever the presenter says.
		s_.handles_[handle].done = finished || action.instant;
		s_.output_.push_back({OutputKind::Action, {}, events::describe(shown) + (action.instant ? " (instant)" : "")});
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

	events::Handle play_credits(const std::string& credits) override {
		if (!s_.data_.config.creditsById(credits)) {
			return 0;
		}
		const events::Handle handle = menuHandle();
		s_.creditsId_ = credits;
		s_.creditsElapsedMs_ = 0;
		s_.openScriptMenu(Mode::Menu, MenuKind::Credits, handle);
		return handle;
	}

	void end_game() override { s_.endGamePending_ = true; }

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

	void add_party_member(const std::string& member) override { join(member, true); }
	void add_party_member_silently(const std::string& member) override { join(member, false); }
	void remove_party_member(const std::string& member) override { leave(member, true); }
	void remove_party_member_silently(const std::string& member) override { leave(member, false); }

	void join(const std::string& member, bool notice) {
		World& w = s_.world_;
		if (!w.member(member)) {
			auto level = s_.data_.config.joinLevels.find(member);
			w.roster.push_back(rules::makePartyMember(s_.data_.db, *s_.data_.db.character(member),
				level == s_.data_.config.joinLevels.end() ? s_.data_.config.startLevel : level->second));
		}
		if (std::find(w.lineup.begin(), w.lineup.end(), member) == w.lineup.end() && w.lineup.size() < static_cast<std::size_t>(rules::kMaxPartySize)) {
			w.lineup.push_back(member);
			if (notice) {
				s_.emit(OutputKind::Notice, "game.joins", {member});
			}
		}
	}

	void leave(const std::string& member, bool notice) {
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
		if (notice) {
			s_.emit(OutputKind::Notice, "game.leaves", {member});
		}
		if (s_.data_.config.leaveEquipment == LeaveEquipment::Bag) {
			handBack(member);
		}
	}

	void save_party(const std::string& snapshot) override {
		World& w = s_.world_;
		PartySnapshot taken;
		taken.lineup = w.lineup;
		for (const rules::PartyMember& m : w.roster) {
			if (std::find(w.lineup.begin(), w.lineup.end(), m.characterId) == w.lineup.end()) {
				taken.reserve.push_back(m.characterId);
			}
		}
		w.partySnapshots[snapshot] = std::move(taken);
	}

	// The roster never loses a member, so everyone the snapshot names is still there: a member who left since goes back
	// to its place, and one who joined since goes to the end of the reserve.
	bool restore_party(const std::string& snapshot) override {
		World& w = s_.world_;
		const auto found = w.partySnapshots.find(snapshot);
		if (found == w.partySnapshots.end()) {
			return false;
		}
		std::vector<std::string> order;
		for (const std::vector<std::string>* part : {&found->second.lineup, &found->second.reserve}) {
			for (const std::string& id : *part) {
				if (w.member(id) && std::find(order.begin(), order.end(), id) == order.end()) {
					order.push_back(id);
				}
			}
		}
		std::vector<std::string> lineup;
		for (const std::string& id : found->second.lineup) {
			if (w.member(id) && lineup.size() < static_cast<std::size_t>(rules::kMaxPartySize)) {
				lineup.push_back(id);
			}
		}
		if (lineup.empty()) {
			s_.warn("party snapshot '" + snapshot + "' names no member who has joined; the party stays as it is");
			return true;
		}
		std::vector<rules::PartyMember> roster;
		for (const std::string& id : order) {
			roster.push_back(*w.member(id));
		}
		for (const rules::PartyMember& m : w.roster) {
			if (std::find(order.begin(), order.end(), m.characterId) == order.end()) {
				roster.push_back(m);
			}
		}
		w.roster = std::move(roster);
		w.lineup = std::move(lineup);
		s_.emit(OutputKind::Notice, "game.party_restored");
		return true;
	}

	void swap_party_member(const events::PartySwap& swap) override {
		World& w = s_.world_;
		const std::size_t slot = swap.slot > 0 ? static_cast<std::size_t>(swap.slot - 1) : static_cast<std::size_t>(std::find(w.lineup.begin(), w.lineup.end(), swap.replaced) - w.lineup.begin());
		std::string refused;
		if (slot >= w.lineup.size()) {
			refused = swap.slot > 0 ? "lineup slot " + std::to_string(swap.slot) + " is empty" : "'" + swap.replaced + "' is not in the lineup";
		} else if (!w.member(swap.member)) {
			refused = "'" + swap.member + "' has not joined the party";
		} else if (std::find(w.lineup.begin(), w.lineup.end(), swap.member) != w.lineup.end()) {
			refused = "'" + swap.member + "' is already in the lineup";
		}
		if (!refused.empty()) {
			s_.warn("cannot " + events::describe(swap) + ": " + refused);
			return;
		}
		const std::string replaced = w.lineup[slot];
		w.lineup[slot] = swap.member;
		if (!swap.silent) {
			s_.emit(OutputKind::Notice, "game.party_swapped", {replaced, swap.member});
		}
		if (swap.equipment == events::SwapEquipment::bag || (swap.equipment == events::SwapEquipment::rule && s_.data_.config.leaveEquipment == LeaveEquipment::Bag)) {
			handBack(replaced);
		}
	}

	bool order_party(const std::vector<std::string>& members) override {
		World& w = s_.world_;
		for (const std::string& member : members) {
			if (std::find(w.lineup.begin(), w.lineup.end(), member) == w.lineup.end()) {
				return false;
			}
		}
		std::vector<std::string> lineup = members;
		for (const std::string& member : w.lineup) {
			if (std::find(members.begin(), members.end(), member) == members.end()) {
				lineup.push_back(member);
			}
		}
		w.lineup = std::move(lineup);
		return true;
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
			m->skillUses[ability] = rules::maxSkillUses(s_.data_.db, *m, ability);
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

	void register_town(const std::string& area, bool registered) override {
		if (registered) {
			s_.world_.visitedAreas.insert(area);
		} else {
			s_.world_.visitedAreas.erase(area);
		}
	}

	void show_picture(const std::string& picture) override {
		s_.world_.pictures.push_back(picture);
		s_.output_.push_back({OutputKind::Action, {}, "picture show " + picture});
	}

	void hide_picture(const std::string& picture) override {
		std::vector<std::string>& shown = s_.world_.pictures;
		// A name shown twice: the upper one goes.
		std::size_t at = shown.size();
		for (std::size_t i = shown.size(); i-- > 0;) {
			if (picture.empty() || shown[i] == picture) {
				at = i;
				break;
			}
		}
		if (at == shown.size()) {
			s_.warn(picture.empty() ? std::string("picture hide: no picture is up") : "picture hide: '" + picture + "' is not up");
			return;
		}
		s_.output_.push_back({OutputKind::Action, {}, "picture hide " + shown[at]});
		shown.erase(shown.begin() + static_cast<std::ptrdiff_t>(at));
	}

	void clear_pictures() override {
		s_.world_.pictures.clear();
		s_.output_.push_back({OutputKind::Action, {}, "picture clear"});
	}

	void remove_actor(const std::string& actor, bool untilReload) override { s_.removeActor(actor, untilReload); }

	void spawn_event(const std::string& event) override { s_.spawnBackground(event); }

	void set_actor_look(const std::string& actor, const std::string& enemy, const std::string& look) override { s_.setActorLook(actor, enemy, look); }

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

	bool game_state(const std::string& test, const std::string& value) override { return s_.stateTest(test, value); }

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
	// A member leaving the lineup puts every piece it wears back in the bag.
	void handBack(const std::string& member) {
		rules::PartyMember* leaving = s_.world_.member(member);
		if (!leaving || leaving->equipment.empty()) {
			return;
		}
		while (!leaving->equipment.empty()) {
			rules::unequip(*leaving, s_.data_.db, s_.world_.equipmentBag, leaving->equipment.begin()->first);
		}
		s_.emit(OutputKind::Notice, "game.equipment_returned", {member});
	}

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

// The host of one background event. The compiler lets only actor commands, waits, sounds, flags, variables, calls and spawns
// into a background event, so the calls that would take control are never made; were one made, it warns and is done at once.
class Session::BackgroundHost : public events::Host {
public:
	BackgroundHost(Session& session, std::string event) : s_(session), event_(std::move(event)) {}

	events::Handle start_action(const events::Action& action) override {
		events::Action shown = action;
		const PresentationNames& names = s_.data_.config.presentation;
		const bool timed = action.type == events::ActionType::animate || ((action.type == events::ActionType::show_actor || action.type == events::ActionType::hide_actor) && !action.target.empty());
		if (timed && names.animationSeconds.count(action.target)) {
			shown.seconds = names.animationSeconds.at(action.target);
		}
		const double seconds = events::action_seconds(shown, names.durations, names.subjectDurations);
		s_.applyActorAction(shown, action.instant ? 0 : static_cast<int>(std::lround(seconds * 1000)));
		const events::Handle handle = s_.nextHandle_++;
		live_[handle] = s_.presenter()->begin(handle, shown) || action.instant;
		s_.output_.push_back({OutputKind::Action, {}, event_ + ": " + events::describe(action) + (action.instant ? " (instant)" : "")});
		return handle;
	}

	events::Handle show_line(const events::Line&) override { return refuse("say"); }
	events::Handle present_choice(const events::ChoiceRequest&) override { return refuse("choice"); }
	events::Handle start_battle(const events::BattleRequest&) override { return refuse("battle"); }

	bool is_done(events::Handle handle) override {
		auto it = live_.find(handle);
		if (it == live_.end()) {
			return true;
		}
		if (!it->second) {
			it->second = s_.presenter()->finished(handle);
		}
		return it->second;
	}

	int result(events::Handle) override { return 0; }

	void cancel(events::Handle handle) override {
		auto it = live_.find(handle);
		if (it != live_.end() && !it->second) {
			s_.presenter()->cancel(handle);
		}
		live_.erase(handle);
	}

	void release(events::Handle handle) override { live_.erase(handle); }

	bool get_flag(const std::string& name) override { return s_.world_.flag(name); }

	void set_flag(const std::string& name, bool value) override {
		const bool before = s_.world_.flag(name);
		if (before != value) {
			s_.backgroundChangedFlag_ = true;
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
	void give_item(const std::string&, int) override { refuse("give"); }
	void take_item(const std::string&, int) override { refuse("take"); }
	void add_party_member(const std::string&) override { refuse("party add"); }
	void remove_party_member(const std::string&) override { refuse("party remove"); }
	void set_player_control(bool) override { refuse("lock"); }
	// A background event changes nothing a restart would have to undo, so a save made with other scripts starts it again
	// from its first command over the loaded world.
	std::string snapshot_world() override { return ""; }
	bool restore_world(const std::string&) override { return true; }
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

	bool game_state(const std::string& test, const std::string& value) override { return s_.stateTest(test, value); }

	std::int64_t money() override { return s_.world_.money; }
	void remove_actor(const std::string& actor, bool untilReload) override { s_.removeActor(actor, untilReload); }
	void spawn_event(const std::string& event) override { s_.spawnBackground(event); }
	void set_actor_look(const std::string& actor, const std::string& enemy, const std::string& look) override { s_.setActorLook(actor, enemy, look); }

private:
	events::Handle refuse(const char* command) {
		s_.warn("background event '" + event_ + "' cannot " + command);
		const events::Handle handle = s_.nextHandle_++;
		live_[handle] = true;
		return handle;
	}

	Session& s_;
	std::string event_;
	// Each action in flight and whether it is done.
	std::map<events::Handle, bool> live_;
};

Session::Session(const GameData& data, SessionOptions options) : data_(data), options_(std::move(options)) {
	if (!options_.presenter) {
		actions_ = std::make_unique<ActionQueue>(data_.config.presentation, actionRoute_, options_.presentation, options_.onAction);
	}
	for (const ActorDef& actor : data_.config.actorDefs) {
		if (!actor.area.empty() && actor.solid && (!actor.path || actor.path->footprintWidth > 0)) {
			solidActors_.insert(actor.id);
		}
		actorDefIndex_.emplace(actor.id, static_cast<std::size_t>(&actor - data_.config.actorDefs.data()));
	}
	for (const auto& [id, a] : data_.areas) {
		for (const Hook& h : a.hooks) {
			if (h.actorAt) {
				solidActors_.insert(h.id);
			}
		}
	}
	// A new profile starts at the game's default levels; one read from the file keeps its own.
	const PresentationNames& shownSettings = data_.config.presentation;
	if (shownSettings.textSpeed.declared) {
		profile_.textSpeedLevel = shownSettings.textSpeed.levels[shownSettings.textSpeed.defaultIndex].id;
	}
	if (shownSettings.battleSpeed.declared) {
		profile_.battleSpeedLevel = shownSettings.battleSpeed.levels[shownSettings.battleSpeed.defaultIndex].id;
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

std::optional<Session::ActorHome> Session::actorHome(const Area* area, const std::string& actor) const {
	if (!area) {
		return std::nullopt;
	}
	const AreaIndex& index = areaIndex(area->id);
	const std::map<std::string, std::size_t>& talkPoints = index.talkPoints;
	const ActorDef* def = actorDef(actor);
	// A talk point that answers on its actor leaves the actor at its game.json home, where it has one here.
	const bool homeHere = def && def->area == area->id && (def->at || area->markers.count(def->marker));
	if (const auto point = talkPoints.find(actor); point != talkPoints.end() && point->second < area->hooks.size() &&
		!(homeHere && std::find(index.actorTalk.begin(), index.actorTalk.end(), point->second) != index.actorTalk.end())) {
		const Hook* hook = &area->hooks[point->second];
		return ActorHome{hook->actorAt.value_or(hook->at), Facing::South, !hook->solidWhileActive || hookHolds(*hook, world_)};
	}
	if (!def || def->area != area->id) {
		return std::nullopt;
	}
	if (def->at) {
		return ActorHome{*def->at, def->facing, true};
	}
	const auto marker = area->markers.find(def->marker);
	return marker == area->markers.end() ? std::nullopt : std::optional<ActorHome>(ActorHome{marker->second, def->facing, true});
}

std::optional<Cell> Session::actorCell(const std::string& actor) const {
	auto it = world_.actors.find(actor);
	if (it != world_.actors.end()) {
		return it->second.area == world_.area && !world_.area.empty() ? std::optional<Cell>(it->second.cell) : std::nullopt;
	}
	const std::optional<ActorHome> home = actorHome(area(), actor);
	return home ? std::optional<Cell>(home->cell) : std::nullopt;
}

ActorPlace& Session::touchActor(const std::string& actor) {
	auto it = world_.actors.find(actor);
	if (it == world_.actors.end()) {
		ActorPlace place;
		if (const std::optional<ActorHome> home = actorHome(area(), actor)) {
			place.area = world_.area;
			place.cell = home->cell;
			place.facing = home->facing;
		}
		it = world_.actors.emplace(actor, place).first;
	}
	return it->second;
}

std::set<Cell> Session::actorCells(const std::string& except) const {
	return solidActors_.empty() ? std::set<Cell>() : actorCells(actorsHere(), except);
}

std::set<Cell> Session::actorCells(const std::vector<ActorView>& views, const std::string& except) const {
	std::set<Cell> cells;
	if (solidActors_.empty()) {
		return cells;
	}
	for (const ActorView& a : views) {
		if (!a.solid || a.id == except) {
			continue;
		}
		const ActorDef* def = actorDef(a.id);
		if (def && def->path) {
			const auto place = world_.actors.find(a.id);
			for (const Cell c : pathCells(*def, a.home, place == world_.actors.end() ? 0 : place->second.pathTick)) {
				cells.insert(c);
			}
		} else {
			cells.insert(a.cell);
		}
	}
	return cells;
}

std::vector<Cell> Session::actorRoute(const Area& area, const std::string& actor, Cell from, const std::vector<Cell>& stops, bool pathfind) const {
	std::vector<Cell> route{from};
	const std::set<Cell> blocked = pathfind ? actorCells(actor) : std::set<Cell>();
	for (const Cell stop : stops) {
		const Cell start = route.back();
		std::vector<Cell> leg;
		if (pathfind && start != stop) {
			// Breadth first over floor nothing stands on, never through an exit; the stop itself may be anything.
			std::map<Cell, Cell> came;
			std::deque<Cell> queue{start};
			came[start] = start;
			while (!queue.empty() && !came.count(stop)) {
				const Cell c = queue.front();
				queue.pop_front();
				for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
					const Cell n = stepFrom(c, f);
					const bool open = n == stop || (area.walkable(n) && !area.exitAt(n) && !blocked.count(n));
					if (!came.count(n) && area.inside(n) && open) {
						came[n] = c;
						queue.push_back(n);
					}
				}
			}
			for (Cell c = stop; came.count(stop) && c != start; c = came[c]) {
				leg.insert(leg.begin(), c);
			}
		}
		if (leg.empty()) {
			Cell at = start;
			while (at.x != stop.x) {
				at.x += stop.x > at.x ? 1 : -1;
				leg.push_back(at);
			}
			while (at.y != stop.y) {
				at.y += stop.y > at.y ? 1 : -1;
				leg.push_back(at);
			}
		}
		route.insert(route.end(), leg.begin(), leg.end());
	}
	return route;
}

// The end state of a move, place, face, show, hide or timed `anim` of one of the game's actors. Party members and
// anything else a script names are left to the front end, and so are looping poses and animations of no length that
// do not show or hide. A move walks its route, so the actor ends facing its last step.
void Session::applyActorAction(const events::Action& action, int animationMs) {
	using events::ActionType;
	actionRoute_.clear();
	if (action.type != ActionType::move && action.type != ActionType::place_actor && action.type != ActionType::face &&
		action.type != ActionType::show_actor && action.type != ActionType::hide_actor && action.type != ActionType::animate) {
		return;
	}
	const Area* here = area();
	auto cellOf = [&](const std::string& marker) -> std::optional<Cell> {
		const auto found = here ? here->markers.find(marker) : std::map<std::string, Cell>::const_iterator();
		return here && found != here->markers.end() ? std::optional<Cell>(found->second) : std::nullopt;
	};
	auto goalOf = [&]() {
		return action.has_position ? std::optional<Cell>(Cell{static_cast<int>(std::lround(action.position.x)), static_cast<int>(std::lround(action.position.z))})
								   : cellOf(action.target);
	};
	auto stopsTo = [&](Cell goal) {
		std::vector<Cell> stops;
		for (const std::string& waypoint : action.waypoints) {
			if (const std::optional<Cell> stop = cellOf(waypoint)) {
				stops.push_back(*stop);
			}
		}
		stops.push_back(goal);
		return stops;
	};
	auto lastLeg = [](const std::vector<Cell>& route, Facing& facing) {
		const std::size_t n = route.size();
		if (n >= 2) {
			const Cell last = route[n - 1];
			const Cell before = route[n - 2];
			facing = last.y != before.y ? (last.y > before.y ? Facing::South : Facing::North) : (last.x > before.x ? Facing::East : Facing::West);
		}
	};
	std::string zone;
	int slot = 0;
	if (parseVisibleEnemyActor(action.subject, zone, slot)) {
		// A visible enemy addressed by its reserved name: a move or place puts it there to wander from, a face turns it.
		VisibleEnemy* enemy = visibleEnemyByActor(action.subject);
		if (!enemy || !here) {
			return;
		}
		if (action.type == ActionType::move || action.type == ActionType::place_actor) {
			const std::optional<Cell> goal = goalOf();
			if (!goal) {
				return;
			}
			actionRoute_ = action.type == ActionType::place_actor ? std::vector<Cell>{*goal} : actorRoute(*here, "", enemy->position, stopsTo(*goal), action.pathfind);
			lastLeg(actionRoute_, enemy->facing);
			if (!action.facing.empty()) {
				parseFacing(action.facing, enemy->facing);
			}
			enemy->position = *goal;
			enemy->spawn = *goal;
			enemy->state = EnemyState::Idle;
			enemy->stateMs = 0;
			enemy->moveProgress = 0;
		} else if (action.type == ActionType::face) {
			std::optional<Cell> target;
			if (events::is_direction(action.target)) {
				parseFacing(action.target, enemy->facing);
			} else if (here->markers.count(action.target)) {
				target = here->markers.at(action.target);
			} else if (data_.db.character(action.target)) {
				target = world_.position;
			} else {
				target = actorCell(action.target);
			}
			if (std::optional<Facing> facing = target ? facingToward(enemy->position, *target) : std::nullopt) {
				enemy->facing = *facing;
			}
		}
		return;
	}
	const std::vector<std::string>& actors = data_.config.actors;
	if (std::find(actors.begin(), actors.end(), action.subject) == actors.end()) {
		return;
	}
	const events::StandardAnimation* standard = action.type == ActionType::animate ||
			((action.type == ActionType::show_actor || action.type == ActionType::hide_actor) && !action.target.empty())
		? events::standard_animation(action.target)
		: nullptr;
	auto vanishOf = [](const std::string& name) {
		return name == "fade_out" ? ActorVanish::Faded : name == "dissolve" ? ActorVanish::Dissolved : ActorVanish::BlinkedOut;
	};
	if (action.type == ActionType::animate && (action.loop || ((!standard || standard->end == events::AnimationEnd::none) && animationMs <= 0))) {
		return;
	}
	const auto existing = world_.actors.find(action.subject);
	if (existing != world_.actors.end() && existing->second.removed != ActorRemoval::None) {
		return;
	}
	ActorPlace& place = touchActor(action.subject);
	switch (action.type) {
	case ActionType::move:
	case ActionType::place_actor: {
		const std::optional<Cell> goal = goalOf();
		if (!goal) {
			return;
		}
		if (action.type == ActionType::place_actor || place.area != world_.area) {
			actionRoute_ = {*goal};
		} else {
			actionRoute_ = actorRoute(*here, action.subject, place.cell, stopsTo(*goal), action.pathfind);
			lastLeg(actionRoute_, place.facing);
		}
		if (action.type == ActionType::place_actor && !action.facing.empty()) {
			parseFacing(action.facing, place.facing);
		}
		place.area = world_.area;
		place.cell = *goal;
		break;
	}
	case ActionType::face: {
		if (place.area.empty() || place.area != world_.area) {
			return;
		}
		if (events::is_direction(action.target)) {
			parseFacing(action.target, place.facing);
			break;
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
	case ActionType::show_actor:
	case ActionType::hide_actor: {
		// `with <animation>` plays it as the transition; a standard one that hides records how the actor vanished.
		const bool transition = !action.target.empty() && animationMs > 0;
		place.visible = action.type == ActionType::show_actor;
		place.vanish = !place.visible && standard && standard->end == events::AnimationEnd::hidden ? vanishOf(action.target) : ActorVanish::None;
		place.animation = transition ? action.target : std::string();
		place.animationMs = transition ? animationMs : 0;
		place.animationTotalMs = place.animationMs;
		break;
	}
	case ActionType::animate:
		if (standard && standard->end == events::AnimationEnd::shown) {
			place.visible = true;
			place.vanish = ActorVanish::None;
		} else if (standard && standard->end == events::AnimationEnd::hidden) {
			place.visible = false;
			place.vanish = vanishOf(action.target);
		}
		place.animation = animationMs > 0 ? action.target : std::string();
		place.animationMs = std::max(0, animationMs);
		place.animationTotalMs = place.animationMs;
		break;
	default:
		break;
	}
}

// `place leader at <actor>`: the leader stands on the actor's current cell, facing as the script says, the followers
// stacked on it. Refused aboard, on a platform, with no lineup or with the actor not in the area.
std::optional<events::Action> Session::placeLeaderAt(const events::Action& action) {
	const auto refuse = [&](const std::string& why) -> std::optional<events::Action> {
		warn("cannot place the leader at '" + action.subject + "': " + why);
		return std::nullopt;
	};
	if (world_.lineup.empty()) {
		return refuse("no one is in the lineup");
	}
	if (!world_.aboard.empty() || !world_.ridingPlatform.empty()) {
		return refuse("the party is aboard");
	}
	std::optional<ActorView> found;
	for (const ActorView& view : actorsHere()) {
		if (view.id == action.subject) {
			found = view;
		}
	}
	if (!found) {
		return refuse("not an actor in this area");
	}
	world_.position = found->cell;
	if (action.facing == "same") {
		world_.facing = found->facing;
	} else if (!action.facing.empty()) {
		parseFacing(action.facing, world_.facing);
	}
	world_.trail.clear();
	events::Action shown;
	shown.type = events::ActionType::place_actor;
	shown.subject = world_.lineup.front();
	shown.has_position = true;
	shown.position = {static_cast<double>(found->cell.x), 0, static_cast<double>(found->cell.y)};
	shown.facing = toString(world_.facing);
	shown.instant = action.instant;
	return shown;
}

void Session::removeActor(const std::string& actor, bool untilReload) {
	std::string zone;
	int slot = 0;
	if (parseVisibleEnemyActor(actor, zone, slot)) {
		// Gone from the field without a battle, until the area's visible enemies next spawn.
		if (const VisibleEnemy* enemy = visibleEnemyByActor(actor)) {
			const int id = enemy->id;
			world_.enemies.erase(std::remove_if(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.id == id; }), world_.enemies.end());
		}
		return;
	}
	const std::vector<std::string>& actors = data_.config.actors;
	if (std::find(actors.begin(), actors.end(), actor) == actors.end()) {
		warn("cannot remove '" + actor + "': not an actor of game.json");
		return;
	}
	ActorPlace& place = touchActor(actor);
	if (place.removed != ActorRemoval::ForGood) {
		place.removed = untilReload ? ActorRemoval::UntilReload : ActorRemoval::ForGood;
	}
	place.animation.clear();
	place.animationMs = 0;
	place.animationTotalMs = 0;
}

void Session::moveSceneCamera(const events::Action& action, int ms) {
	World::SceneCamera& camera = world_.sceneCamera;
	if (action.type == events::ActionType::camera_reset) {
		camera = {};
		camera.ms = camera.totalMs = std::max(0, ms);
		return;
	}
	if (action.type != events::ActionType::camera_to) {
		return;
	}
	const Area* here = area();
	std::optional<Cell> cell;
	if (action.has_position) {
		cell = Cell{static_cast<int>(action.position.x), static_cast<int>(action.position.y)};
	} else if (here && here->markers.count(action.target)) {
		cell = here->markers.at(action.target);
	}
	if (!cell || !here || cell->x >= here->width() || cell->y >= here->height()) {
		warn("camera to " + (action.has_position ? std::to_string(static_cast<int>(action.position.x)) + " " + std::to_string(static_cast<int>(action.position.y)) : action.target) +
			": not a cell of the area");
		return;
	}
	camera.held = true;
	camera.cell = *cell;
	camera.ms = camera.totalMs = std::max(0, ms);
}

void Session::advanceActors(int ms) {
	World::SceneCamera& camera = world_.sceneCamera;
	if (camera.ms > 0) {
		camera.ms = std::max(0, camera.ms - ms);
		if (camera.ms == 0) {
			camera.totalMs = 0;
		}
	}
	for (auto& [id, place] : world_.actors) {
		if (place.animationMs > 0) {
			place.animationMs -= ms;
			if (place.animationMs <= 0) {
				place.animation.clear();
				place.animationMs = 0;
				place.animationTotalMs = 0;
			}
		}
	}
}

void Session::setActorLook(const std::string& actor, const std::string& enemy, const std::string& look) {
	const std::vector<std::string>& actors = data_.config.actors;
	if (std::find(actors.begin(), actors.end(), actor) == actors.end()) {
		warn("cannot change the look of '" + actor + "': not an actor of game.json");
		return;
	}
	if (!enemy.empty() && !data_.db.enemy(enemy)) {
		warn("cannot draw '" + actor + "' as '" + enemy + "': not an enemy");
		return;
	}
	ActorPlace& place = touchActor(actor);
	place.lookEnemy = enemy;
	place.look = look;
}

VisibleEnemy* Session::visibleEnemyByActor(const std::string& actor) {
	std::string zone;
	int slot = 0;
	if (!parseVisibleEnemyActor(actor, zone, slot)) {
		return nullptr;
	}
	for (VisibleEnemy& e : world_.enemies) {
		if (e.zone == zone && --slot == 0) {
			return &e;
		}
	}
	return nullptr;
}

std::string Session::enemyActor(int enemyId) const {
	std::map<std::string, int> seen;
	for (const VisibleEnemy& e : world_.enemies) {
		const int n = ++seen[e.zone];
		if (e.id == enemyId) {
			return visibleEnemyActor(e.zone, n);
		}
	}
	return "";
}

const Hook* Session::touchHook(const std::string& actor, bool deliberate) const {
	const Area* here = area();
	if (!here) {
		return nullptr;
	}
	for (const Hook& hook : here->hooks) {
		if (hook.trigger == HookTrigger::Touch && hook.actor == actor && (deliberate || hook.touchFrom == TouchFrom::Any) && hookHolds(hook, world_)) {
			return &hook;
		}
	}
	return nullptr;
}

void Session::turnTowardLeader(const std::string& actor) {
	if (const std::optional<Cell> at = actorCell(actor)) {
		if (const std::optional<Facing> toward = facingToward(*at, world_.position)) {
			touchActor(actor).facing = *toward;
		}
	}
}

const Hook* Session::touchHookAt(Cell cell, bool deliberate) const {
	const Area* here = area();
	if (!here) {
		return nullptr;
	}
	// Of actorsHere(), only the views of actors that touch hooks name are needed.
	ActorView actor;
	for (const std::size_t index : areaIndex(here->id).touchActors) {
		if (actorView(data_.config.actorDefs[index], *here, actor) && actor.visible && actor.cell == cell) {
			if (const Hook* hook = touchHook(actor.id, deliberate)) {
				return hook;
			}
		}
	}
	return nullptr;
}

// `standing` is actorsHere() as it is now, so the solid actors' cells come from it.
bool Session::actorMayEnter(const Area& here, const std::string& terrain, Cell next, const std::vector<ActorView>& standing) const {
	const auto talkPointThere = [&] {
		const Hook* hook = here.hookAt(next, HookTrigger::Interact);
		const std::vector<std::size_t>& actorTalk = areaIndex(here.id).actorTalk;
		return hook && std::find(actorTalk.begin(), actorTalk.end(), static_cast<std::size_t>(hook - here.hooks.data())) == actorTalk.end();
	};
	const bool objectThere = objectsMayStand(here, next) &&
		(talkPointThere() || here.shopAt(next) || here.restAt(next) || here.chestAt(next) || here.portAt(next) || here.helmAt(next) ||
			here.exitAt(next) || here.gangwayAt(next));
	const bool standingThere = std::any_of(standing.begin(), standing.end(), [&](const ActorView& a) { return a.visible && a.cell == next && !a.onPath; }) ||
		std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == next; }) || !vehicleAt(next).empty();
	const bool solidThere = std::any_of(standing.begin(), standing.end(), [&](const ActorView& a) {
		// Only a path actor's view has a footprint.
		if (!a.solid || (a.footprintWidth <= 0 && a.cell != next)) {
			return false;
		}
		const ActorDef* def = actorDef(a.id);
		if (!def || !def->path) {
			return a.cell == next;
		}
		const auto place = world_.actors.find(a.id);
		const std::vector<Cell> cells = pathCells(*def, a.home, place == world_.actors.end() ? 0 : place->second.pathTick);
		return std::find(cells.begin(), cells.end(), next) != cells.end();
	});
	return here.inside(next) && terrain.find(here.rows[next.y][next.x]) != std::string::npos && !here.closed.count(next) && !objectThere && !standingThere &&
		!solidThere;
}

void Session::refreshViews(TickViews& views, const Area& here) const {
	if (!views.stale && views.touched) {
		const std::size_t at = views.slot[static_cast<std::size_t>(views.touched - data_.config.actorDefs.data())];
		views.stale = at == kAway || !actorView(*views.touched, here, views.views[at]);
	}
	views.touched = nullptr;
	if (views.stale) {
		views.views = actorsHere();
		views.slot.assign(data_.config.actorDefs.size(), kAway);
		for (std::size_t i = 0; i < views.views.size(); ++i) {
			if (const auto found = actorDefIndex_.find(views.views[i].id); found != actorDefIndex_.end()) {
				views.slot[found->second] = i;
			}
		}
		views.stale = false;
	}
}

const ActorView* Session::viewOf(const TickViews& views, const ActorDef& def) const {
	const std::size_t at = views.slot[static_cast<std::size_t>(&def - data_.config.actorDefs.data())];
	return at == kAway ? nullptr : &views.views[at];
}

void Session::wanderActors(int ms, TickViews& views) {
	const Area* here = area();
	if (!here) {
		return;
	}
	Timer timer(stats_.wanderUs);
	for (const ActorDef& def : data_.config.actorDefs) {
		if (!def.wander || def.area != world_.area) {
			continue;
		}
		++stats_.wanderTurns;
		refreshViews(views, *here);
		const std::vector<ActorView>& standing = views.views;
		const ActorView* view = viewOf(views, def);
		if (!view || !view->wanders) {
			continue;
		}
		const ActorWander& rules = *def.wander;
		const Cell home = def.at ? *def.at : here->markers.at(def.marker);
		ActorPlace& place = touchActor(def.id);
		views.touched = &def;
		if (place.wanderPauseMs > 0) {
			place.wanderPauseMs = std::max(0, place.wanderPauseMs - ms);
			continue;
		}
		// Progress in milliseconds times percent: a step is the area's step time times 100 (to the hundredth of a millisecond
		// under `exact`, which carries the rest).
		place.wanderProgress += ms * rules.speedPercent;
		const int wanderStep = data_.config.exactSteps ? static_cast<int>(std::max<std::int64_t>(1, here->stepUs / 10)) : here->stepMs * 100;
		if (place.wanderProgress < wanderStep) {
			continue;
		}
		place.wanderProgress = data_.config.exactSteps ? std::min(place.wanderProgress - wanderStep, wanderStep - 1) : 0;
		if (rules.standPercent > 0 && world_.encounterStream.range(1, 100) <= rules.standPercent) {
			place.wanderPauseMs = rules.pauseMaxMs > rules.pauseMinMs ? world_.encounterStream.range(rules.pauseMinMs, rules.pauseMaxMs) : rules.pauseMinMs;
			continue;
		}
		static const Facing kTurns[] = {Facing::North, Facing::East, Facing::South, Facing::West};
		static const Facing kAlongX[] = {Facing::East, Facing::West};
		static const Facing kAlongY[] = {Facing::North, Facing::South};
		const Facing f = rules.axis == WanderAxis::X ? kAlongX[world_.encounterStream.range(0, 1)]
			: rules.axis == WanderAxis::Y            ? kAlongY[world_.encounterStream.range(0, 1)]
													 : kTurns[world_.encounterStream.range(0, 3)];
		const Cell from = place.cell;
		const Cell next = stepFrom(from, f);
		place.facing = f;
		place.wanderPauseMs = rules.pauseMaxMs > rules.pauseMinMs ? world_.encounterStream.range(rules.pauseMinMs, rules.pauseMaxMs) : rules.pauseMinMs;
		if (next == world_.position) {
			if (const Hook* hook = touchHook(def.id, false); hook && world_.playerControl) {
				if (hook->turns) {
					turnTowardLeader(def.id);
				}
				startEvent(hook->event);
				views.stale = true;
				if (mode() != Mode::Field) {
					return;
				}
			}
			continue;
		}
		const auto distance = [&](Cell c) { return std::abs(c.x - home.x) + std::abs(c.y - home.y); };
		if (actorMayEnter(*here, rules.terrain, next, standing) && (rules.covers(home, next) || distance(next) < distance(from))) {
			place.cell = next;
		}
	}
}

std::optional<Cell> Session::settlePatrol(const ActorDef& def, ActorPlace& place) const {
	const std::vector<PatrolStep>& steps = def.patrol->steps;
	const Area* here = nullptr;
	for (std::size_t guard = 0; guard <= steps.size(); ++guard) {
		place.patrolStep %= static_cast<int>(steps.size());
		const PatrolStep& step = steps[static_cast<std::size_t>(place.patrolStep)];
		if (step.walk && place.patrolWalked < step.count) {
			return stepFrom(place.cell, *step.walk);
		}
		std::optional<Cell> goal = step.to;
		if (!step.toMarker.empty() && !here) {
			here = area();
		}
		if (!step.toMarker.empty() && here && here->markers.count(step.toMarker)) {
			goal = here->markers.at(step.toMarker);
		}
		if (goal && place.cell != *goal) {
			return place.cell.x != goal->x ? Cell{place.cell.x + (goal->x > place.cell.x ? 1 : -1), place.cell.y}
										   : Cell{place.cell.x, place.cell.y + (goal->y > place.cell.y ? 1 : -1)};
		}
		if (step.face) {
			place.facing = *step.face;
		}
		place.patrolStep = (place.patrolStep + 1) % static_cast<int>(steps.size());
		place.patrolWalked = 0;
		if (step.pauseMs > 0) {
			place.wanderPauseMs = step.pauseMs;
			return std::nullopt;
		}
	}
	return std::nullopt;
}

void Session::patrolActors(int ms, TickViews& views) {
	const Area* here = area();
	if (!here) {
		return;
	}
	Timer timer(stats_.patrolUs);
	for (const ActorDef& def : data_.config.actorDefs) {
		if (!def.patrol || def.area != world_.area || mode() != Mode::Field) {
			continue;
		}
		++stats_.wanderTurns;
		refreshViews(views, *here);
		const std::vector<ActorView>& standing = views.views;
		const ActorView* view = viewOf(views, def);
		if (!view || !view->patrols) {
			continue;
		}
		ActorPlace& place = touchActor(def.id);
		views.touched = &def;
		if (place.wanderPauseMs > 0) {
			place.wanderPauseMs = std::max(0, place.wanderPauseMs - ms);
			continue;
		}
		const std::optional<Cell> next = settlePatrol(def, place);
		if (!next) {
			continue;
		}
		place.wanderProgress += ms * def.patrol->speedPercent;
		const int stepLength = data_.config.exactSteps ? static_cast<int>(std::max<std::int64_t>(1, here->stepUs / 10)) : here->stepMs * 100;
		if (place.wanderProgress < stepLength) {
			continue;
		}
		place.wanderProgress = data_.config.exactSteps ? std::min(place.wanderProgress - stepLength, stepLength - 1) : 0;
		if (const std::optional<Facing> toward = facingToward(place.cell, *next)) {
			place.facing = *toward;
		}
		if (*next == world_.position) {
			if (const Hook* hook = touchHook(def.id, false); hook && world_.playerControl) {
				if (hook->turns) {
					turnTowardLeader(def.id);
				}
				startEvent(hook->event);
				views.stale = true;
			}
			continue;
		}
		if (!actorMayEnter(*here, def.patrol->terrain, *next, standing)) {
			continue;
		}
		place.cell = *next;
		const PatrolStep& step = def.patrol->steps[static_cast<std::size_t>(place.patrolStep)];
		if (step.walk) {
			++place.patrolWalked;
		}
		settlePatrol(def, place);
	}
}

std::vector<Cell> Session::pathCells(const ActorDef& def, Cell anchor, int tick) const {
	std::vector<Cell> cells;
	const ActorPath& path = *def.path;
	if (path.footprintWidth <= 0) {
		return cells;
	}
	const int size = data_.config.cellPixels;
	const auto& offset = path.offsets[static_cast<std::size_t>(tick / path.ticks) % path.offsets.size()];
	const int left = anchor.x * size + offset.first;
	const int top = anchor.y * size + offset.second;
	const auto cellOf = [size](int pixel) { return pixel >= 0 ? pixel / size : -((-pixel + size - 1) / size); };
	for (int y = cellOf(top); y <= cellOf(top + path.footprintHeight * size - 1); ++y) {
		for (int x = cellOf(left); x <= cellOf(left + path.footprintWidth * size - 1); ++x) {
			cells.push_back({x, y});
		}
	}
	return cells;
}

void Session::pathActors() {
	const Area* here = area();
	if (!here) {
		return;
	}
	// Of actorsHere(), only the path actors' views are needed.
	ActorView view;
	for (const ActorDef& def : data_.config.actorDefs) {
		if (def.path && actorView(def, *here, view) && view.onPath) {
			ActorPlace& place = touchActor(def.id);
			place.pathTick = (place.pathTick + 1) % (def.path->ticks * static_cast<int>(def.path->offsets.size()));
		}
	}
}

void Session::resetPlatforms(bool keep) {
	const Area* here = area();
	std::map<std::string, PlatformPlace> kept;
	if (keep) {
		kept.swap(world_.platforms);
	}
	world_.platforms.clear();
	if (!keep) {
		world_.ridingPlatform.clear();
	}
	if (!here) {
		return;
	}
	for (const Platform& p : here->platforms) {
		if (kept.count(p.id)) {
			world_.platforms[p.id] = kept[p.id];
			continue;
		}
		PlatformPlace place;
		if (p.mode == PlatformMode::OneWay && platformHolds(p, world_)) {
			place.positionUs = p.length() * p.stepMs * 1000LL;
			place.heading = -1;
		}
		place.pauseMs = p.pauseMs;
		world_.platforms[p.id] = place;
	}
}

const Platform* Session::dockedPlatformAt(Cell cell) const {
	// Entering an area puts each of its platforms in World::platforms.
	if (world_.platforms.empty()) {
		return nullptr;
	}
	if (const AreaIndex& index = areaIndex(world_.area); cell.x >= 0 && cell.y >= 0 && cell.x < index.width && cell.y < index.height &&
		!(index.objects[static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(index.width) + static_cast<std::size_t>(cell.x)] & kPlatformEnd)) {
		return nullptr;
	}
	const Area* here = area();
	if (!here) {
		return nullptr;
	}
	for (const Platform& p : here->platforms) {
		const auto place = world_.platforms.find(p.id);
		if (place == world_.platforms.end()) {
			continue;
		}
		const std::int64_t run = p.length() * p.stepMs * 1000LL;
		if ((place->second.positionUs == 0 && p.from == cell) || (place->second.positionUs == run && p.to == cell)) {
			return &p;
		}
	}
	return nullptr;
}

bool Session::platformDocked(const std::string& id) const {
	const Area* here = area();
	const Platform* p = here ? here->platform(id) : nullptr;
	const auto place = world_.platforms.find(id);
	return p && place != world_.platforms.end() && (place->second.positionUs == 0 || place->second.positionUs == p->length() * p->stepMs * 1000LL);
}

void Session::movePlatforms(int ms) {
	const Area* here = area();
	if (!here) {
		return;
	}
	for (const Platform& p : here->platforms) {
		PlatformPlace& place = world_.platforms[p.id];
		const std::int64_t stepUs = p.stepMs * 1000LL;
		const std::int64_t run = p.length() * stepUs;
		const bool holds = platformHolds(p, world_);
		const bool atEnd = place.positionUs == 0 || place.positionUs == run;
		if (p.mode == PlatformMode::OneWay) {
			const std::int64_t goal = holds ? run : 0;
			if (place.positionUs == goal) {
				place.pauseMs = std::max(0, place.pauseMs - ms);
				continue;
			}
			if (atEnd && place.pauseMs > 0) {
				place.pauseMs = std::max(0, place.pauseMs - ms);
				continue;
			}
			place.heading = goal > place.positionUs ? 1 : -1;
		} else if (atEnd) {
			if (place.pauseMs > 0) {
				place.pauseMs = std::max(0, place.pauseMs - ms);
				continue;
			}
			if (!holds || (p.mode == PlatformMode::Triggered && (world_.ridingPlatform != p.id || !place.boarded))) {
				continue;
			}
			place.heading = place.positionUs == 0 ? 1 : -1;
		}
		place.positionUs = std::clamp<std::int64_t>(place.positionUs + place.heading * ms * 1000LL, 0, run);
		if (place.positionUs == 0 || place.positionUs == run) {
			place.pauseMs = p.pauseMs;
			place.boarded = false;
		}
		if (world_.ridingPlatform == p.id) {
			world_.position = p.cellAt(static_cast<int>((place.positionUs + stepUs / 2) / stepUs));
		}
	}
}

std::vector<PlatformView> Session::platformsHere() const {
	std::vector<PlatformView> out;
	const Area* here = area();
	if (!here) {
		return out;
	}
	const int size = data_.config.cellPixels;
	for (const Platform& p : here->platforms) {
		const auto found = world_.platforms.find(p.id);
		const PlatformPlace place = found == world_.platforms.end() ? PlatformPlace{} : found->second;
		const std::int64_t stepUs = p.stepMs * 1000LL;
		PlatformView view;
		view.id = p.id;
		view.mode = p.mode;
		view.from = p.from;
		view.to = p.to;
		view.positionUs = place.positionUs;
		view.runUs = p.length() * stepUs;
		view.cell = p.cellAt(static_cast<int>((place.positionUs + stepUs / 2) / stepUs));
		const std::int64_t travelled = place.positionUs * size / stepUs;
		view.x = p.from.x * size + static_cast<int>(p.to.x > p.from.x ? travelled : p.to.x < p.from.x ? -travelled : 0);
		view.y = p.from.y * size + static_cast<int>(p.to.y > p.from.y ? travelled : p.to.y < p.from.y ? -travelled : 0);
		view.end = place.positionUs == 0 ? 0 : place.positionUs == view.runUs ? 1 : -1;
		view.pauseMs = view.end >= 0 ? place.pauseMs : 0;
		view.carrying = world_.ridingPlatform == p.id;
		out.push_back(view);
	}
	return out;
}

bool Session::backgroundEvent(const std::string& event) const {
	const int index = data_.program.find_event(event);
	return index >= 0 && data_.program.events[index].background;
}

void Session::spawnBackground(const std::string& event) {
	if (std::any_of(background_.begin(), background_.end(), [&](const BackgroundRun& run) { return run.event == event; })) {
		return;
	}
	if (static_cast<int>(background_.size()) >= data_.config.backgroundLimit) {
		warn("background event '" + event + "' not started: " + std::to_string(background_.size()) + " already run (background_limit)");
		return;
	}
	BackgroundRun run;
	run.event = event;
	run.host = std::make_unique<BackgroundHost>(*this, event);
	run.interpreter = std::make_unique<events::Interpreter>(data_.program, *run.host);
	if (!run.interpreter->start(event)) {
		warn("background event '" + event + "' cannot start: " + run.interpreter->error());
		return;
	}
	output_.push_back({OutputKind::Action, {}, "spawn " + event});
	background_.push_back(std::move(run));
}

void Session::stepBackground(int ms) {
	backgroundChangedFlag_ = false;
	// Those a step spawns wait for the next tick.
	const std::size_t count = background_.size();
	std::vector<std::string> ended;
	for (std::size_t i = 0; i < count && i < background_.size(); ++i) {
		events::Interpreter& interpreter = *background_[i].interpreter;
		const events::Status status = interpreter.step(ms / 1000.0);
		if (status == events::Status::error) {
			warn("background event error: " + interpreter.error());
			ended.push_back(background_[i].event);
		} else if (status != events::Status::running) {
			output_.push_back({OutputKind::Action, {}, background_[i].event + ": end"});
			ended.push_back(background_[i].event);
		}
	}
	background_.erase(std::remove_if(background_.begin(), background_.end(),
						  [&](const BackgroundRun& run) { return std::find(ended.begin(), ended.end(), run.event) != ended.end(); }),
		background_.end());
	if (backgroundChangedFlag_) {
		settleField();
	}
}

void Session::stopBackground() {
	for (BackgroundRun& run : background_) {
		run.interpreter->cancel();
		output_.push_back({OutputKind::Action, {}, run.event + ": stopped"});
	}
	background_.clear();
}

bool Session::restoreBackground(const std::string& event, const std::string& text) {
	events::SaveState state;
	std::string error;
	BackgroundRun run;
	run.event = event;
	run.host = std::make_unique<BackgroundHost>(*this, event);
	run.interpreter = std::make_unique<events::Interpreter>(data_.program, *run.host);
	if (!backgroundEvent(event) || !events::save_state_from_text(text, state) || !run.interpreter->restore(state, error)) {
		warn("background event '" + event + "' cannot resume" + (error.empty() ? std::string() : ": " + error));
		return false;
	}
	if (run.interpreter->status() == events::Status::running) {
		background_.push_back(std::move(run));
	}
	return true;
}

std::vector<EventPosition> Session::backgroundEvents() const {
	std::vector<EventPosition> out;
	for (const BackgroundRun& run : background_) {
		const events::Interpreter::Position at = run.interpreter->position();
		out.push_back({run.event, at.file, at.line});
	}
	return out;
}

std::vector<ActorView> Session::actorsHere() const {
	std::vector<ActorView> out;
	const Area* here = area();
	if (!here) {
		return out;
	}
	// Only actors with a home here or a place a script gave them here can be here, in game.json order.
	std::vector<std::size_t> order = areaIndex(here->id).homeActors;
	for (const auto& [id, place] : world_.actors) {
		if (place.area == world_.area) {
			if (const auto def = actorDefIndex_.find(id); def != actorDefIndex_.end()) {
				order.push_back(def->second);
			}
		}
	}
	std::sort(order.begin(), order.end());
	order.erase(std::unique(order.begin(), order.end()), order.end());
	ActorView view;
	for (const std::size_t index : order) {
		if (actorView(data_.config.actorDefs[index], *here, view)) {
			out.push_back(view);
		}
	}
	return out;
}

bool Session::actorView(const ActorDef& def, const Area& here, ActorView& view) const {
	++stats_.actorViews;
	view = ActorView();
	view.id = def.id;
	view.spriteEnemy = def.spriteEnemy;
	view.look = def.look;
	auto it = world_.actors.find(def.id);
	if (it != world_.actors.end()) {
		const ActorPlace& p = it->second;
		if (p.area != world_.area) {
			return false;
		}
		view.cell = p.cell;
		view.facing = p.facing;
		view.visible = p.visible && p.removed == ActorRemoval::None;
		view.scripted = true;
		view.vanish = p.vanish;
		view.animation = p.animation;
		view.animationMs = p.animationMs;
		view.animationTotalMs = p.animationTotalMs;
		view.removed = p.removed;
	} else if (const std::optional<ActorHome> home = actorHome(&here, def.id)) {
		view.cell = home->cell;
		view.facing = home->facing;
		view.visible = home->visible;
	} else {
		return false;
	}
	if ((!def.requireFlag.empty() && !world_.flag(def.requireFlag)) || (!def.blockFlag.empty() && world_.flag(def.blockFlag))) {
		view.visible = false;
	}
	view.solid = view.visible && solidActors_.count(def.id) > 0;
	if (it != world_.actors.end() && (!it->second.lookEnemy.empty() || !it->second.look.empty())) {
		view.spriteEnemy = it->second.lookEnemy;
		view.look = it->second.look;
	}
	const bool moving = view.visible && def.area == world_.area &&
		std::none_of(background_.begin(), background_.end(), [&](const BackgroundRun& run) {
			const std::vector<events::ActorState>& touched = run.interpreter->presentation().actors;
			return std::any_of(touched.begin(), touched.end(), [&](const events::ActorState& a) { return a.actor == def.id; });
		});
	view.wanders = def.wander && moving;
	view.patrols = def.patrol && moving;
	view.onPath = def.path && moving;
	if (def.path) {
		view.home = view.cell;
		const int size = data_.config.cellPixels;
		const int tick = it != world_.actors.end() ? it->second.pathTick : 0;
		const auto& offset = def.path->offsets[static_cast<std::size_t>(tick / def.path->ticks) % def.path->offsets.size()];
		view.pathX = offset.first;
		view.pathY = offset.second;
		const auto cellOf = [size](int pixel) { return pixel >= 0 ? pixel / size : -((-pixel + size - 1) / size); };
		view.cell = {cellOf(view.home.x * size + offset.first), cellOf(view.home.y * size + offset.second)};
		view.footprintWidth = def.path->footprintWidth;
		view.footprintHeight = def.path->footprintHeight;
	}
	return true;
}

const ActorDef* Session::actorDef(const std::string& id) const {
	const auto found = actorDefIndex_.find(id);
	return found == actorDefIndex_.end() ? nullptr : &data_.config.actorDefs[found->second];
}

const Session::AreaIndex& Session::areaIndex(const std::string& id) const {
	if (indexed_ && indexedId_ == id) {
		return *indexed_;
	}
	const auto found = areaIndex_.find(id);
	if (found != areaIndex_.end()) {
		indexedId_ = id;
		indexed_ = &found->second;
		return found->second;
	}
	AreaIndex& index = areaIndex_[id];
	indexedId_ = id;
	indexed_ = &index;
	const Area* a = data_.area(id);
	if (!a) {
		return index;
	}
	index.width = a->width();
	index.height = a->height();
	index.objects.assign(static_cast<std::size_t>(index.width) * static_cast<std::size_t>(index.height), 0);
	const auto mark = [&](Cell c) {
		if (a->inside(c)) {
			index.objects[static_cast<std::size_t>(c.y) * static_cast<std::size_t>(index.width) + static_cast<std::size_t>(c.x)] |= kObject;
		}
	};
	for (std::size_t i = 0; i < a->hooks.size(); ++i) {
		const Hook& h = a->hooks[i];
		mark(h.at);
		for (const Cell c : h.cells) {
			mark(c);
		}
		if (h.trigger == HookTrigger::Interact) {
			index.talkPoints.emplace(h.id, i);
		}
		if (h.solidWhileActive) {
			index.whileActiveHooks.push_back(i);
		}
		index.gatedHooks = index.gatedHooks || h.solidWhileActive || a->exitAt(h.at);
	}
	for (const Exit& e : a->exits) {
		mark(e.at);
		if (a->inside(e.at)) {
			index.objects[static_cast<std::size_t>(e.at.y) * static_cast<std::size_t>(index.width) + static_cast<std::size_t>(e.at.x)] |= kExit;
		}
		index.gatedExits = index.gatedExits || !e.requireFlag.empty() || !e.blockFlag.empty();
	}
	for (const Shop& x : a->shops) {
		if (x.at) {
			mark(*x.at);
		}
	}
	for (const RestPoint& x : a->rests) {
		if (x.at) {
			mark(*x.at);
		}
	}
	for (const Chest& x : a->chests) {
		mark(x.at);
	}
	for (const Port& x : a->ports) {
		mark(x.at);
	}
	for (const Helm& x : a->helms) {
		mark(x.at);
	}
	for (const Cell c : a->gangway) {
		mark(c);
	}
	for (const Platform& p : a->platforms) {
		for (const Cell c : {p.from, p.to}) {
			if (a->inside(c)) {
				index.objects[static_cast<std::size_t>(c.y) * static_cast<std::size_t>(index.width) + static_cast<std::size_t>(c.x)] |= kPlatformEnd;
			}
		}
	}
	std::set<std::string> touching;
	for (const Hook& h : a->hooks) {
		if (h.trigger == HookTrigger::Touch) {
			touching.insert(h.actor);
		}
	}
	for (std::size_t i = 0; i < data_.config.actorDefs.size(); ++i) {
		const ActorDef& def = data_.config.actorDefs[i];
		if (def.area == id || index.talkPoints.count(def.id)) {
			index.homeActors.push_back(i);
		}
		if (touching.count(def.id) && actorDef(def.id) == &def) {
			index.touchActors.push_back(i);
		}
	}
	for (std::size_t i = 0; i < a->hooks.size(); ++i) {
		const Hook& h = a->hooks[i];
		if (h.trigger == HookTrigger::Interact && actorDef(h.id) && !h.actorAt && h.cells.empty() && h.talkAt.value_or(data_.config.talkAt) == TalkAt::Actor) {
			index.actorTalk.push_back(i);
			index.gatedHooks = true;
		}
	}
	return index;
}

bool Session::objectsMayStand(const Area& area, Cell cell) const {
	const AreaIndex& index = areaIndex(area.id);
	if (!area.inside(cell) || index.width != area.width() || index.height != area.height()) {
		return true;
	}
	return (index.objects[static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(index.width) + static_cast<std::size_t>(cell.x)] & kObject) != 0;
}

const Exit* Session::exitOn(const Area& area, Cell cell) const {
	const AreaIndex& index = areaIndex(area.id);
	if (area.inside(cell) && index.width == area.width() && index.height == area.height() &&
		!(index.objects[static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(index.width) + static_cast<std::size_t>(cell.x)] & kExit)) {
		return nullptr;
	}
	return area.exitAt(cell);
}

bool Session::exitTakes(const Area& area, const Exit& exit, Cell from) const {
	return exitFires(exit, data_.config.exitFires) == ExitFires::Always || !exitOn(area, from);
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
	if (!world_.areaMusic.empty() || !here) {
		return world_.areaMusic;
	}
	if (const FieldVehicle* vehicle = riding()) {
		const auto& byVehicle = data_.config.presentation.music.vehiclesField;
		if (auto track = byVehicle.find(vehicle->id); track != byVehicle.end()) {
			return track->second;
		}
	}
	const auto& byArea = data_.config.presentation.music.areas;
	if (auto tracks = byArea.find(here->id); tracks != byArea.end()) {
		for (const AreaTrack& t : tracks->second) {
			if (t.flag.empty() || world_.flag(t.flag)) {
				return t.track;
			}
		}
	}
	return here->music;
}

std::string Session::battleMusic() const {
	const rules::Battle* b = battle_ ? &*battle_ : ended_ ? &ended_->battle : nullptr;
	if (!b) {
		return {};
	}
	const MusicNames& music = data_.config.presentation.music;
	if (auto track = music.encounters.find(battle_ ? battleEncounter_ : ended_->encounter); track != music.encounters.end()) {
		return track->second;
	}
	const BattleSource source = battle_ ? battleSource_ : ended_->source;
	if (source == BattleSource::Random && b->setup().flags.vehicleBattle) {
		if (auto track = music.vehicles.find(b->setup().vehicle); track != music.vehicles.end()) {
			return track->second;
		}
	}
	return {};
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
	const AreaIndex& index = areaIndex(id);
	const bool gatedExits = index.gatedExits;
	const bool gatedHooks = index.gatedHooks;
	if (base->changes.empty() && broken == world_.brokenCells.end() && !here && !gatedExits && !gatedHooks) {
		return base;
	}
	// A hook that is solid only while active stands while its flags hold and, for an actor's talk point, the actor shows.
	auto hookStands = [&](const Hook& h) {
		const auto actor = world_.actors.find(h.id);
		return hookHolds(h, world_) && (actor == world_.actors.end() || (actor->second.visible && actor->second.removed == ActorRemoval::None));
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
	// Hooks solid only while active are all the key needs: every other hook adds the same '1' each time.
	for (const std::size_t h : index.whileActiveHooks) {
		key += hookStands(base->hooks[h]) ? '1' : '0';
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
		std::set<Cell>& closed = view->second.second.closed;
		closed.clear();
		for (const Exit& e : base->exits) {
			if (!exitOpen(e, world_)) {
				if (e.closedTile != '#') {
					closed.insert(e.at);
				}
				if (e.closedTile) {
					rows[e.at.y][e.at.x] = e.closedTile;
				}
			}
		}
		std::set<Cell>& passable = view->second.second.passable;
		passable.clear();
		for (const Hook& h : base->hooks) {
			const Exit* door = h.trigger == HookTrigger::Interact ? base->exitAt(h.at) : nullptr;
			if ((h.solidWhileActive && !hookStands(h)) || (door && exitOpen(*door, world_))) {
				passable.insert(h.at);
			}
		}
		// A talk point that answers on its actor's cell is only where the actor starts; the actor blocks where it stands.
		for (const std::size_t h : index.actorTalk) {
			passable.insert(base->hooks[h].at);
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
		return text(areaNameKey(id));
	}
	return id;
}

const OptionLevels& Session::optionLevels(OptionSetting setting) const {
	return setting == OptionSetting::TextSpeed ? data_.config.presentation.textSpeed : data_.config.presentation.battleSpeed;
}

int Session::optionLevel(OptionSetting setting) const {
	const OptionLevels& levels = optionLevels(setting);
	const bool text = setting == OptionSetting::TextSpeed;
	if (!levels.declared) {
		return text ? static_cast<int>(profile_.textSpeed) : static_cast<int>(profile_.battleSpeed);
	}
	auto find = [&](const std::string& id) {
		const auto at = std::find_if(levels.levels.begin(), levels.levels.end(), [&](const OptionLevel& l) { return l.id == id; });
		return at == levels.levels.end() ? -1 : static_cast<int>(at - levels.levels.begin());
	};
	int at = find(text ? profile_.textSpeedLevel : profile_.battleSpeedLevel);
	if (at < 0) {
		at = find(text ? toString(profile_.textSpeed) : toString(profile_.battleSpeed));
	}
	return at < 0 ? levels.defaultIndex : at;
}

int Session::optionLevelIndex(OptionSetting setting, const std::string& id) const {
	const std::vector<OptionLevel>& levels = optionLevels(setting).levels;
	const auto at = std::find_if(levels.begin(), levels.end(), [&](const OptionLevel& l) { return l.id == id; });
	return at == levels.end() ? -1 : static_cast<int>(at - levels.begin());
}

rules::Message Session::setOptionLevel(OptionSetting setting, int index) {
	const OptionLevels& levels = optionLevels(setting);
	if (index < 0 || index >= static_cast<int>(levels.levels.size())) {
		return refusal("unknown_option_level", {std::to_string(index)});
	}
	Profile p = profile_;
	(setting == OptionSetting::TextSpeed ? p.textSpeedLevel : p.battleSpeedLevel) = levels.levels[static_cast<std::size_t>(index)].id;
	return setProfile(p);
}

rules::Message Session::setProfile(const Profile& given) {
	if (std::none_of(data_.languages.begin(), data_.languages.end(), [&](const LanguageTables& l) { return l.code == given.textLanguage; })) {
		return refusal("unknown_language", {given.textLanguage});
	}
	// A level id names one of the game's levels; without levels of its own a built-in id sets the enum. A host that changed
	// only the enum (the C struct has no level) moves to the level of that name, or leaves the choice to the enum.
	Profile profile = given;
	for (OptionSetting setting : {OptionSetting::TextSpeed, OptionSetting::BattleSpeed}) {
		const bool text = setting == OptionSetting::TextSpeed;
		const OptionLevels& levels = optionLevels(setting);
		std::string& level = text ? profile.textSpeedLevel : profile.battleSpeedLevel;
		const std::string& before = text ? profile_.textSpeedLevel : profile_.battleSpeedLevel;
		const bool enumChanged = text ? profile.textSpeed != profile_.textSpeed : profile.battleSpeed != profile_.battleSpeed;
		if (levels.declared && enumChanged && level == before) {
			level = text ? toString(profile.textSpeed) : toString(profile.battleSpeed);
			if (std::none_of(levels.levels.begin(), levels.levels.end(), [&](const OptionLevel& l) { return l.id == level; })) {
				level.clear();
			}
			continue;
		}
		const bool known = std::any_of(levels.levels.begin(), levels.levels.end(), [&](const OptionLevel& l) { return l.id == level; });
		// An id the profile file brought that this game does not list stays as it was, unused, until a level is chosen.
		if (level.empty() || (!known && level == before)) {
			continue;
		}
		if (!known) {
			return refusal("unknown_option_level", {level});
		}
		if (text) {
			parseTextSpeed(level, profile.textSpeed);
		} else {
			parseBattleSpeed(level, profile.battleSpeed);
		}
		if (!levels.declared) {
			level.clear();
		}
	}
	profile_ = profile;
	if (profile_.flashing != FlashLevel::Unset) {
		profile_.reducedFlash = profile_.flashing == FlashLevel::Reduced;
	}
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
	previousArea_.clear();
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
		const auto own = c.startLevels.find(id);
		world_.roster.push_back(rules::makePartyMember(data_.db, *data_.db.character(id), own == c.startLevels.end() ? c.startLevel : own->second));
		world_.lineup.push_back(id);
	}
	world_.money = c.startMoney;
	world_.visitedAreas.insert(c.startTowns.begin(), c.startTowns.end());
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
	placeFollowers();
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

std::string Session::areaNameKey(const std::string& id) const {
	const Area* a = data_.area(id);
	if (a) {
		for (const Area::NameVariant& v : a->names) {
			const bool from = v.from.empty() || (id == world_.area && v.from == world_.cameFrom);
			if (from && (v.flag.empty() || world_.flag(v.flag))) {
				return v.key;
			}
		}
	}
	return "area." + id;
}

bool Session::stateTest(const std::string& test, const std::string& value) const {
	if (test == "leader") {
		return !world_.lineup.empty() && world_.lineup.front() == value;
	}
	if (test == "music") {
		return (scriptTrack_ ? *scriptTrack_ : areaMusic()) == value;
	}
	if (test == "area") {
		return world_.area == value;
	}
	const Area* here = area();
	if (test != "tile" || !here || !here->inside(world_.position)) {
		return false;
	}
	if (value == "counter" && here->counters.count(world_.position)) {
		return true;
	}
	static const std::map<std::string, char> terrain = {{"floor", '.'}, {"water", '~'}, {"soft", ':'}, {"barrier", '%'}, {"counter", '='}};
	const auto it = terrain.find(value);
	return it != terrain.end() && here->rows[world_.position.y][world_.position.x] == it->second;
}

bool Session::canLoad() const {
	return mode() == Mode::Title;
}

BattleKeys Session::battleKeys() const {
	BattleKeys keys;
	for (const std::string& key : rules::messageKeys(data_.db)) {
		if (key.rfind("battle.", 0) == 0 || key.rfind("status.", 0) == 0) {
			keys.messages.push_back(key);
		}
	}
	keys.notices = {"game.auto_on", "game.battle_start", "game.dropped", "game.fallen", "game.game_over", "game.got_away", "game.learned", "game.level_up",
		"game.scripted_end", "game.victory"};
	for (rules::AutoStop stop : {rules::AutoStop::Cancelled, rules::AutoStop::MemberFell, rules::AutoStop::CannotPay, rules::AutoStop::BattleEnded}) {
		keys.notices.push_back(std::string("auto.stopped.") + rules::toString(stop));
	}
	std::sort(keys.notices.begin(), keys.notices.end());
	for (int i = static_cast<int>(rules::CancelReason::ActorUnable); i <= static_cast<int>(rules::CancelReason::NoTarget); ++i) {
		keys.cancelReasons.push_back(rules::toString(static_cast<rules::CancelReason>(i)));
	}
	return keys;
}

rules::Message Session::eraseSave(const std::string& slot) {
	std::string text;
	if (!store_ || !store_->read(slot, text)) {
		return refusal("slot_unreadable", {slot});
	}
	std::string error;
	if (!store_->erase(slot, error)) {
		return refusal("erase_failed", {slot, error});
	}
	if (retrySlot_ == slot) {
		retrySlot_.clear();
	}
	if (pendingRetrySlot_ == slot) {
		pendingRetrySlot_.clear();
	}
	emit(OutputKind::Notice, "game.save_erased", {slot});
	return {};
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
	Timer timer(stats_.loadUs);
	++stats_.loads;
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
	background_.clear();
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
	stepProgressUs_ = 0;
	conveyorUs_ = 0;
	tickRemainderMs_ = 0;
	if (data_.config.exactSteps) {
		stepProgressUs_ = world_.stepCarryUs;
		conveyorUs_ = world_.conveyorCarryUs;
		tickRemainderMs_ = world_.tickCarryMs;
	}
	world_.stepCarryUs = world_.conveyorCarryUs = 0;
	world_.tickCarryMs = 0;
	resetPlatforms(true);
	previousArea_ = world_.cameFrom;
	modes_.transition(Mode::Field);
	const Area* a = area();
	arrivalAnnounced_ = a->announcesFrom("") && data_.config.arriveNotice == ArriveNotice::Always;
	exitSound_.clear();
	if (pacingOn()) {
		pacing_.enterArea(a->id, a->kind == AreaKind::Dungeon);
	}
	if (slot.empty()) {
		emit(OutputKind::Notice, "game.loaded_text");
	} else {
		emit(OutputKind::Notice, "game.loaded", {slot});
	}
	for (const auto& [event, state] : save.value->background) {
		restoreBackground(event, state);
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
	for (BackgroundRun& run : background_) {
		run.interpreter->cancel();
	}
	background_.clear();
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
				tickInput.held = true;
				tickInput.direction = *latchedDirection_;
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
		advanceActors(ms);
		pumpEvent(ms / 1000.0);
		break;
	case Mode::Menu:
	case Mode::Shop:
		addTime(ms);
		if (menuKind_ == MenuKind::Credits) {
			creditsElapsedMs_ += ms;
		}
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
	Timer timer(stats_.tickUs);
	++stats_.ticks;
	const int tick = data_.config.tickMs;
	addTime(tick);
	if (pacingOn()) {
		pacing_.fieldTime(tick);
	}
	advanceActors(tick);
	world_.graceMs = std::max(0, world_.graceMs - tick);
	const Area& a = *area();
	// Built where the leader's step first needs them; an arrival may change anything.
	TickViews views;
	const FieldVehicle* vehicle = riding();
	const Conveyor* belt = nullptr;
	for (const Conveyor& c : a.conveyors) {
		const bool holds = (c.requireFlag.empty() || world_.flag(c.requireFlag)) && (c.blockFlag.empty() || !world_.flag(c.blockFlag));
		if (holds && std::find(c.cells.begin(), c.cells.end(), world_.position) != c.cells.end()) {
			belt = &c;
		}
	}
	if (!belt) {
		conveyorUs_ = 0;
	}
	if (belt && world_.playerControl) {
		// A belt carries the party one cell per step and takes no input while it does.
		stepProgressUs_ = 0;
		conveyorUs_ += tick * 1000LL;
		const Cell target = stepFrom(world_.position, belt->direction);
		const bool blockedByEnemy = std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; });
		const std::int64_t beltUs = data_.config.exactSteps ? belt->stepUs : belt->stepMs * 1000LL;
		if (conveyorUs_ >= beltUs) {
			conveyorUs_ = data_.config.exactSteps ? std::min(conveyorUs_ - beltUs, beltUs - 1) : 0;
			refreshViews(views, a);
			if (canEnter(a, target, actorCells(views.views, "")) && !blockedByEnemy) {
				views.stale = true;
				arrive(target);
				if (mode() != Mode::Field) {
					return;
				}
			}
		}
	} else if (input.held && world_.playerControl && (world_.ridingPlatform.empty() || platformDocked(world_.ridingPlatform))) {
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
		if (const Hook* touch = vehicle ? nullptr : touchHookAt(target, !pathStep_)) {
			stepProgressUs_ = 0;
			if (touch->turns) {
				turnTowardLeader(touch->actor);
			}
			startEvent(touch->event);
			if (mode() != Mode::Field) {
				return;
			}
		}
		const bool blockedByEnemy = std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; });
		// Under `exact` the time past the step carries into the next (a step shorter than a tick still takes one).
		const std::int64_t stepUs = data_.config.exactSteps ? leaderStepUs(input.running) : leaderStepMs(input.running) * 1000LL;
		refreshViews(views, a);
		if (canEnter(a, target, actorCells(views.views, "")) && !blockedByEnemy) {
			stepProgressUs_ += tick * 1000LL;
			if (stepProgressUs_ >= stepUs) {
				stepProgressUs_ = data_.config.exactSteps ? std::min(stepProgressUs_ - stepUs, stepUs - 1) : 0;
				views.stale = true;
				arrive(target);
				if (mode() != Mode::Field) {
					return;
				}
			}
		} else {
			stepProgressUs_ = 0;
		}
	} else {
		stepProgressUs_ = 0;
	}
	// After the leader's step, in this order: wandering actors, patrols, paths, platforms, visible enemies, background events.
	wanderActors(tick, views);
	if (mode() != Mode::Field) {
		return;
	}
	patrolActors(tick, views);
	if (mode() != Mode::Field) {
		return;
	}
	pathActors();
	movePlatforms(tick);
	if (encounterMode() == EncounterMode::Visible && world_.area == a.id && encountersOn_) {
		Timer enemies(stats_.enemyUs);
		std::vector<Cell> wanderers;
		// The paths' ticks since leave every view's id, solid, cell and home as they were, which is all this reads.
		refreshViews(views, a);
		for (const ActorView& v : views.views) {
			const ActorDef* def = actorDef(v.id);
			if (v.solid && def && (def->wander || def->patrol)) {
				wanderers.push_back(v.cell);
			}
			if (v.solid && def && def->path) {
				for (const Cell c : pathCells(*def, v.home, world_.actors.count(v.id) ? world_.actors.at(v.id).pathTick : 0)) {
					wanderers.push_back(c);
				}
			}
		}
		if (std::optional<EncounterStart> contact = tickVisibleEnemies(world_, a, tick, a.stepMs, !world_.aboard.empty(), data_.config.exactSteps ? a.stepUs : 0, wanderers)) {
			startFieldBattle(*contact);
			return;
		}
	}
	if (!background_.empty()) {
		Timer background(stats_.backgroundUs);
		stepBackground(tick);
	}
}

// P6: a machine member still standing regains hit points with each step.
void Session::healMachinesOnStep() {
	if (data_.config.machineStepHeal <= 0) {
		return;
	}
	settleMachineBank();
	for (const std::string& id : world_.lineup) {
		rules::PartyMember* m = world_.member(id);
		const rules::CharacterDef* character = data_.db.character(id);
		if (m && character && character->kind == rules::CreatureKind::Machine && m->hp == 0 && data_.config.machineStepHealFallen == MachineFallenHeal::Heal) {
			int& banked = world_.machineBank[id];
			banked = std::min(banked + data_.config.machineStepHeal, rules::memberStats(data_.db, *m).maxHp);
			continue;
		}
		if (m && character && character->kind == rules::CreatureKind::Machine && (m->hp > 0 || data_.config.machineStepHealFallen == MachineFallenHeal::Stand)) {
			if (m->hp == 0) {
				m->statuses.clear();
				emit(OutputKind::Notice, "game.machine_stood_up", {id});
			}
			m->hp = std::min(m->hp + data_.config.machineStepHeal, rules::memberStats(data_.db, *m).maxHp);
		}
	}
}

// Banked step heal goes to a machine member once it stands again, whatever stood it up.
void Session::settleMachineBank() {
	for (auto it = world_.machineBank.begin(); it != world_.machineBank.end();) {
		rules::PartyMember* m = world_.member(it->first);
		if (m && m->hp == 0) {
			++it;
			continue;
		}
		if (m) {
			m->hp = std::min(m->hp + it->second, rules::memberStats(data_.db, *m).maxHp);
		}
		it = world_.machineBank.erase(it);
	}
}

// Field poison and paralysis (rules.json; areas with `field_status`, `field_poison`, `field_paralysis`, and every area for
// paralysis under its `everywhere`): each step counts toward them. Returns false after a game over.
bool Session::fieldStatusStep() {
	const rules::BattleRules& r = data_.db.battleRules;
	const Area& here = *area();
	const bool poison = r.fieldPoisonEvery > 0 && (here.fieldStatus || here.fieldPoison);
	const bool paralysisHere = r.fieldParalysisChance > 0 && (here.fieldStatus || here.fieldParalysis || r.fieldParalysisEverywhere);
	if (!poison && !paralysisHere) {
		return true;
	}
	++world_.fieldSteps;
	for (const std::string& id : world_.lineup) {
		rules::PartyMember* m = world_.member(id);
		if (!m || m->hp <= 0) {
			continue;
		}
		const auto has = [&](const std::string& status) {
			return std::find_if(m->statuses.begin(), m->statuses.end(), [&](const rules::ActiveStatus& s) { return s.id == status; });
		};
		if (poison && world_.fieldSteps % r.fieldPoisonEvery == 0 && has(r.fieldPoisonStatus) != m->statuses.end()) {
			--m->hp;
			if (m->hp == 0) {
				m->statuses.clear();
				emit(OutputKind::Notice, "game.poison_fell", {id});
				continue;
			}
		}
		const auto paralysis = has(r.fieldParalysisStatus);
		if (paralysisHere && paralysis != m->statuses.end() && world_.fieldSteps >= r.fieldParalysisFrom &&
			(world_.fieldSteps >= r.fieldParalysisBy || world_.encounterStream.range(1, r.fieldParalysisChance) == 1)) {
			m->statuses.erase(paralysis);
			emit(OutputKind::Notice, "game.paralysis_worn_off", {id});
		}
	}
	if (std::none_of(world_.lineup.begin(), world_.lineup.end(), [&](const std::string& id) { return world_.member(id) && world_.member(id)->hp > 0; })) {
		enterGameOver();
		return false;
	}
	return true;
}

void Session::arrive(Cell cell) {
	const Cell from = world_.position;
	world_.position = cell;
	if (!world_.aboard.empty()) {
		world_.trail.clear();
	} else if (from != cell) {
		world_.trail.insert(world_.trail.begin(), from);
		if (world_.trail.size() > kTrailCells) {
			world_.trail.resize(kTrailCells);
		}
	}
	const Platform* boarded = world_.aboard.empty() ? dockedPlatformAt(cell) : nullptr;
	world_.ridingPlatform = boarded ? boarded->id : "";
	if (boarded) {
		world_.platforms[boarded->id].boarded = true;
	}
	int longestGrace = 0;
	for (const Zone& zone : area()->zones) {
		longestGrace = std::max(longestGrace, zone.random.graceSteps);
	}
	if (world_.areaSteps <= longestGrace) {
		++world_.areaSteps;
	}
	if (!world_.aboard.empty()) {
		world_.vehicles[world_.aboard].cell = cell;
		if (const VehicleSounds* sounds = vehicleSounds(world_.aboard); sounds && !sounds->move.empty()) {
			vehicleMoveMs_ += leaderStepMs(false);
			if (sounds->moveMs <= 0 || vehicleMoveMs_ >= sounds->moveMs) {
				vehicleMoveMs_ = sounds->moveMs <= 0 ? 0 : vehicleMoveMs_ - sounds->moveMs;
				playFieldSound(sounds->move);
			}
		}
	}
	if (pacingOn()) {
		pacing_.step();
	}
	healMachinesOnStep();
	if (!fieldStatusStep()) {
		return;
	}
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
		arrivalWalked_ = true;
		walkedFollowers_ = data_.config.followersArrive;
		enterArea(shore->id, marker, from);
		settleField();
		return;
	}
	if (const Exit* exit = exitOn(a, cell); exit && exitTakes(a, *exit, from)) {
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
		arrivalWalked_ = true;
		walkedFollowers_ = e.followers.value_or(data_.config.followersArrive);
		if (!e.sound.empty()) {
			output_.push_back({OutputKind::Action, {}, "exit sound " + e.sound});
		}
		pendingExitSound_ = e.sound;
		enterArea(e.toArea, e.toMarker, from);
		settleField();
		return;
	}
	if (const Hook* hook = a.stepHookAt(cell)) {
		if (hookHolds(*hook, world_)) {
			startEvent(hook->event);
			if (mode() != Mode::Field || !backgroundEvent(hook->event)) {
				return;
			}
		}
	}
	if (!encountersOn_ || !world_.ridingPlatform.empty()) {
		return;
	}
	if (const FieldVehicle* vehicle = riding()) {
		const rules::VehicleDef* fighter = data_.db.vehicle(vehicle->id);
		const bool roll = vehicle->opening.value_or(data_.config.vehicleOpening) == VehicleOpening::Roll;
		if (std::optional<EncounterStart> start = vehicleStep(world_, a, cell, vehicle->encounters, data_.db, fighter ? fighter->stats.agility : 0, roll)) {
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
	stopBackground();
	const Area* before = area();
	const std::string cameIn = !arrivingFrom_.empty() ? arrivingFrom_ : before ? before->id : "";
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
		previousArea_ = world_.area;
	}
	if (!arrivingFrom_.empty()) {
		previousArea_ = arrivingFrom_;
		arrivingFrom_.clear();
	}
	world_.cameFrom = a->names.empty() ? "" : previousArea_;
	scriptTrack_.reset();
	world_.sceneCamera = {};
	world_.areaSteps = 0;
	if (a->kind != AreaKind::Vessel) {
		world_.shipPort.clear();
	}
	for (auto& [id, place] : world_.actors) {
		if (place.removed == ActorRemoval::UntilReload) {
			place.removed = ActorRemoval::None;
		}
	}
	world_.area = a->id;
	world_.position = cell;
	if (!world_.aboard.empty()) {
		world_.vehicles[world_.aboard].area = a->id;
		world_.vehicles[world_.aboard].cell = cell;
	}
	if (a->registersFrom(cameIn)) {
		world_.visitedAreas.insert(a->id);
	}
	world_.fieldSteps = 0;
	for (const std::string& flag : data_.config.areaFlags) {
		world_.flags.erase(flag);
	}
	resetPlatforms(false);
	stepProgressUs_ = 0;
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
	const ArriveNotice rule = data_.config.arriveNotice;
	arrivalAnnounced_ = a->announcesFrom(cameIn) && (rule == ArriveNotice::Always || (rule == ArriveNotice::Walked && arrivalWalked_));
	arrivalWalked_ = false;
	exitSound_ = pendingExitSound_;
	pendingExitSound_.clear();
	arrivalFollowers_ = walkedFollowers_.value_or(data_.config.followersArriveOther);
	walkedFollowers_.reset();
	placeFollowers();
	if (arrivalAnnounced_) {
		emit(OutputKind::Notice, "game.arrive", {a->id});
	}
}

void Session::placeFollowers() {
	world_.trail.clear();
	const Area* a = area();
	if (arrivalFollowers_ == FollowersArrive::Stacked || !a || !world_.aboard.empty()) {
		return;
	}
	Facing toward = static_cast<Facing>((static_cast<int>(world_.facing) + 2) % 4);
	switch (arrivalFollowers_) {
	case FollowersArrive::Up:
		toward = Facing::North;
		break;
	case FollowersArrive::Down:
		toward = Facing::South;
		break;
	case FollowersArrive::Left:
		toward = Facing::West;
		break;
	case FollowersArrive::Right:
		toward = Facing::East;
		break;
	default:
		break;
	}
	Cell c = world_.position;
	for (std::size_t i = 0; i < kTrailCells; ++i) {
		const Cell n = stepFrom(c, toward);
		if (!a->walkable(n) || (!data_.config.followersOnExits && exitOn(*a, n))) {
			break;
		}
		c = n;
		world_.trail.push_back(c);
	}
}

std::vector<FollowerView> Session::followers() const {
	std::vector<std::string> ids;
	if (world_.lineup.size() > 1) {
		ids.assign(world_.lineup.begin() + 1, world_.lineup.end());
	}
	if (!world_.follower.empty()) {
		ids.push_back(world_.follower);
	}
	const bool together = !world_.aboard.empty() || !world_.ridingPlatform.empty();
	std::vector<FollowerView> out;
	for (std::size_t i = 0; i < ids.size(); ++i) {
		Cell cell = world_.position;
		if (!together && !world_.trail.empty()) {
			cell = world_.trail[std::min(i, world_.trail.size() - 1)];
		}
		out.push_back({ids[i], cell});
	}
	return out;
}

events::Interpreter::NextOutput Session::nextEventOutput() const {
	return interpreter_ && mode() == Mode::Event ? interpreter_->next_output() : events::Interpreter::NextOutput::none;
}

void Session::settleField() {
	if (mode() != Mode::Field || gameOver_) {
		return;
	}
	Timer timer(stats_.settleUs);
	++stats_.settles;
	if (loadHooksPending_) {
		for (const Hook& hook : area()->hooks) {
			if (hook.trigger == HookTrigger::Load && !loadHooksFired_.count(hook.id) && hookHolds(hook, world_)) {
				loadHooksFired_.insert(hook.id);
				startEvent(hook.event);
				if (mode() != Mode::Field || !backgroundEvent(hook.event)) {
					return;
				}
			}
		}
		loadHooksPending_ = false;
	}
	for (const Hook& hook : area()->hooks) {
		if (hook.trigger != HookTrigger::Flag) {
			continue;
		}
		const std::string onceKey = world_.area + "/" + hook.id;
		if (hookHolds(hook, world_) && !(hook.once && world_.onceHooks.count(onceKey))) {
			if (hook.once) {
				world_.onceHooks.insert(onceKey);
			}
			startEvent(hook.event);
			if (mode() != Mode::Field || !backgroundEvent(hook.event)) {
				return;
			}
		}
	}
	if (ambientPending_) {
		ambientPending_ = false;
		const TalkSet* set = profile_.ambientTalk && data_.config.partyTalkAmbient ? talkSet() : nullptr;
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

std::int64_t Session::leaderStepUs(bool running) const {
	const Area* a = area();
	if (!a) {
		return 0;
	}
	if (const FieldVehicle* vehicle = riding()) {
		return std::max<std::int64_t>(1, a->stepUs * 100 / vehicle->speedPercent);
	}
	if (data_.config.runSpeedPercent > 0 && running != profile_.alwaysRun) {
		return std::max<std::int64_t>(1, a->stepUs * 100 / data_.config.runSpeedPercent);
	}
	return a->stepUs;
}

int Session::leaderStepMs(bool running) const {
	const Area* a = area();
	if (!a) {
		return 0;
	}
	if (data_.config.exactSteps) {
		return static_cast<int>(std::max<std::int64_t>(1, (leaderStepUs(running) + 500) / 1000));
	}
	if (const FieldVehicle* vehicle = riding()) {
		return std::max(1, a->stepMs * 100 / vehicle->speedPercent);
	}
	if (data_.config.runSpeedPercent > 0 && running != profile_.alwaysRun) {
		return std::max(1, a->stepMs * 100 / data_.config.runSpeedPercent);
	}
	return a->stepMs;
}

bool Session::canStep(Facing direction) const {
	if (mode() != Mode::Field || !world_.playerControl) {
		return false;
	}
	const Area& a = *area();
	const bool belt = std::any_of(a.conveyors.begin(), a.conveyors.end(), [&](const Conveyor& c) {
		return (c.requireFlag.empty() || world_.flag(c.requireFlag)) && (c.blockFlag.empty() || !world_.flag(c.blockFlag)) &&
			std::find(c.cells.begin(), c.cells.end(), world_.position) != c.cells.end();
	});
	const Cell target = stepFrom(world_.position, direction);
	const bool carried = !world_.ridingPlatform.empty() && !platformDocked(world_.ridingPlatform);
	return !belt && !carried && canEnter(a, target) &&
		std::none_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; });
}

rules::Message Session::walk(Facing direction) {
	return walk(direction, false);
}

rules::Message Session::walk(Facing direction, bool running) {
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
	if (world_.aboard.empty() && touchHookAt(target, !pathStep_)) {
		// Walking into an actor with a touch hook starts it, once.
		update(data_.config.tickMs, {true, direction, running});
		return {};
	}
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
		update(data_.config.tickMs, {true, direction, running});
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
	Cell next = walk.goal;
	{
		Timer timer(stats_.planUs);
		++stats_.walkPlans;
		// Cells by y * width + x: the leader stands inside the area, and no cell outside it can be entered.
		const int width = a.width();
		const auto at = [width](Cell c) { return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(c.x); };
		const std::size_t cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(a.height());
		// A belt carries the party wherever it runs, so the path crosses none but the goal.
		std::vector<char> belts(cells, 0);
		for (const Conveyor& c : a.conveyors) {
			for (const Cell b : c.cells) {
				if (a.inside(b)) {
					belts[at(b)] = 1;
				}
			}
		}
		for (const Platform& p : a.platforms) {
			for (int i = 0; i <= p.length(); ++i) {
				if (a.inside(p.cellAt(i))) {
					belts[at(p.cellAt(i))] = 1;
				}
			}
		}
		const std::set<Cell> standing = actorCells();
		constexpr std::size_t kNone = static_cast<std::size_t>(-1);
		std::vector<std::size_t> from(cells, kNone);
		const bool goalInside = a.inside(walk.goal);
		if (!a.inside(world_.position)) {
			return WalkStatus::NoPath;
		}
		const auto reached = [&](Cell c) { return from[at(c)] != kNone; };
		const auto leaves = [&](Cell c, Cell n) {
			const Exit* exit = exitOn(a, n);
			return exit && exitTakes(a, *exit, c);
		};
		std::deque<Cell> queue{world_.position};
		from[at(world_.position)] = at(world_.position);
		while (!queue.empty() && !(goalInside && reached(walk.goal))) {
			const Cell c = queue.front();
			queue.pop_front();
			for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
				const Cell n = stepFrom(c, f);
				if (!a.inside(n) || reached(n) || !canEnter(a, n, standing) || ((belts[at(n)] || leaves(c, n)) && n != walk.goal)) {
					continue;
				}
				from[at(n)] = at(c);
				queue.push_back(n);
				++stats_.walkCells;
			}
		}
		if (!goalInside || !reached(walk.goal)) {
			return WalkStatus::NoPath;
		}
		const std::size_t start = at(world_.position);
		std::size_t step = at(walk.goal);
		while (from[step] != start) {
			step = from[step];
		}
		next = {static_cast<int>(step % static_cast<std::size_t>(width)), static_cast<int>(step / static_cast<std::size_t>(width))};
	}
	Facing direction = Facing::North;
	for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
		if (stepFrom(world_.position, f) == next) {
			direction = f;
		}
	}
	pathStep_ = true;
	if (!this->walk(direction).empty()) {
		// A visible enemy or an actor in the way: let the moment pass.
		update(data_.config.tickMs);
	}
	pathStep_ = false;
	return WalkStatus::Walking;
}

std::optional<InteractTarget> Session::pointAt(const Area& a, Cell cell, bool& acrossable) const {
	const bool anyAcross = data_.config.interactAcross;
	acrossable = anyAcross;
	InteractTarget t;
	t.cell = cell;
	if (const std::string vehicle = vehicleAt(cell); !vehicle.empty()) {
		t.kind = InteractKind::Vehicle;
		t.id = vehicle;
		return t;
	}
	if (const Port* port = a.portAt(cell)) {
		t.kind = InteractKind::Port;
		t.id = port->id;
		t.ready = portOpen(*port, world_);
		return t;
	}
	if (const Helm* helm = a.helmAt(cell); helm && data_.port(world_.shipPort)) {
		t.kind = InteractKind::Helm;
		t.id = helm->id;
		return t;
	}
	const auto isActor = [&](const std::string& id) { return std::find(data_.config.actors.begin(), data_.config.actors.end(), id) != data_.config.actors.end(); };
	const auto answers = [&](const Hook& hook) {
		const auto actor = world_.actors.find(hook.id);
		return hookHolds(hook, world_) && (actor == world_.actors.end() || actor->second.removed == ActorRemoval::None);
	};
	const std::vector<std::size_t>& actorTalk = areaIndex(a.id).actorTalk;
	const auto onActor = [&](const Hook& hook) {
		return std::find(actorTalk.begin(), actorTalk.end(), static_cast<std::size_t>(&hook - a.hooks.data())) != actorTalk.end();
	};
	// A hook on an exit's cell is a door one talks to while the exit is closed; once it opens the cell is the exit's.
	const Exit* door = a.exitAt(cell);
	if (const Hook* hook = a.hookAt(cell, HookTrigger::Interact); hook && !(door && exitOpen(*door, world_)) && !onActor(*hook)) {
		t.kind = isActor(hook->id) ? InteractKind::Actor : InteractKind::Talk;
		t.id = hook->id;
		t.ready = answers(*hook);
		acrossable = anyAcross || hook->across;
		return t;
	}
	for (const std::size_t index : actorTalk) {
		const Hook& hook = a.hooks[index];
		ActorView actor;
		if (actorView(*actorDef(hook.id), a, actor) && actor.visible && actor.cell == cell) {
			t.kind = InteractKind::Actor;
			t.id = hook.id;
			t.ready = answers(hook);
			acrossable = anyAcross || hook.across;
			return t;
		}
	}
	if (const Chest* chest = a.chestAt(cell)) {
		t.kind = InteractKind::Chest;
		t.id = chest->id;
		return t;
	}
	if (const Shop* shop = a.shopAt(cell)) {
		t.kind = InteractKind::Shop;
		t.id = shop->id;
		t.ready = shopOpen(*shop);
		t.keeper = shop->keeper;
		return t;
	}
	if (const RestPoint* rest = a.restAt(cell)) {
		t.kind = InteractKind::Rest;
		t.id = rest->id;
		t.keeper = rest->keeper;
		return t;
	}
	// An actor with a touch hook (a wanderer) answers where it stands.
	if (const Hook* touch = touchHookAt(cell)) {
		t.kind = InteractKind::Actor;
		t.id = touch->actor;
		return t;
	}
	// An actor away from its talk point (moved by a script, or behind its counter) answers where it stands.
	if (data_.config.interactReach == InteractReach::Nearby) {
		for (const ActorView& actor : actorsHere()) {
			const Hook* hook = actor.visible && actor.cell == cell ? talkPoint(&a, actor.id) : nullptr;
			if (hook) {
				t.kind = InteractKind::Actor;
				t.id = actor.id;
				t.ready = answers(*hook);
				acrossable = anyAcross || hook->across;
				return t;
			}
		}
	}
	return std::nullopt;
}

std::optional<InteractTarget> Session::beyondCounter(const Area& a, Cell cell) const {
	bool acrossable = false;
	std::optional<InteractTarget> t = pointAt(a, cell, acrossable);
	if (!t) {
		const auto keeps = [&](const std::string& keeper) {
			if (keeper.empty()) {
				return false;
			}
			for (const ActorView& actor : actorsHere()) {
				if (actor.id == keeper && actor.visible && actor.cell == cell) {
					return true;
				}
			}
			return false;
		};
		for (const Shop& shop : a.shops) {
			if (shop.at && keeps(shop.keeper)) {
				t = InteractTarget{};
				t->kind = InteractKind::Shop;
				t->id = shop.id;
				t->cell = *shop.at;
				t->ready = shopOpen(shop);
				t->keeper = shop.keeper;
				break;
			}
		}
		for (const RestPoint& rest : a.rests) {
			if (!t && rest.at && keeps(rest.keeper)) {
				t = InteractTarget{};
				t->kind = InteractKind::Rest;
				t->id = rest.id;
				t->cell = *rest.at;
				t->keeper = rest.keeper;
			}
		}
	}
	if (t) {
		t->across = true;
	}
	return t;
}

std::optional<InteractTarget> Session::interactTarget() const {
	if (mode() != Mode::Field || !world_.playerControl || !world_.aboard.empty()) {
		return std::nullopt;
	}
	const Area& a = *area();
	const std::set<Cell> standing = actorCells();
	const auto toward = [&](Facing direction) -> std::optional<InteractTarget> {
		bool acrossable = false;
		const Cell next = stepFrom(world_.position, direction);
		std::optional<InteractTarget> t = pointAt(a, next, acrossable);
		// A marked counter (R109): whatever stands behind it, used as if faced.
		if (!t && a.counters.count(next) && !standing.count(next)) {
			t = beyondCounter(a, stepFrom(next, direction));
		}
		// A counter: a cell straight ahead that is not floor and holds nothing, with a point beyond it.
		if (!t && a.inside(next) && a.rows[next.y][next.x] != '.' && !a.exitAt(next) && !standing.count(next)) {
			t = pointAt(a, stepFrom(next, direction), acrossable);
			if (t && !acrossable) {
				t.reset();
			} else if (t) {
				t->across = true;
			}
		}
		if (t) {
			t->facing = direction;
		}
		return t;
	};
	if (std::optional<InteractTarget> faced = toward(world_.facing)) {
		return faced;
	}
	if (data_.config.interactReach == InteractReach::Nearby) {
		const int f = static_cast<int>(world_.facing);
		for (int turn : {1, 3, 2}) {
			std::optional<InteractTarget> t = toward(static_cast<Facing>((f + turn) % 4));
			if (t && t->ready) {
				return t;
			}
		}
	}
	return std::nullopt;
}

std::optional<InteractTarget> Session::interactAt(Cell cell) const {
	if (mode() != Mode::Field || !world_.playerControl || !world_.aboard.empty()) {
		return std::nullopt;
	}
	const Area& a = *area();
	const std::set<Cell> standing = actorCells();
	for (Facing direction : {Facing::North, Facing::East, Facing::South, Facing::West}) {
		bool acrossable = false;
		const Cell next = stepFrom(world_.position, direction);
		std::optional<InteractTarget> t = next == cell ? pointAt(a, next, acrossable) : std::nullopt;
		if (!t && a.counters.count(next) && !standing.count(next) && !pointAt(a, next, acrossable)) {
			t = beyondCounter(a, stepFrom(next, direction));
			if (t && t->cell != cell && !(t->keeper.size() && stepFrom(next, direction) == cell)) {
				t.reset();
			}
		}
		if (!t && stepFrom(next, direction) == cell && !pointAt(a, next, acrossable) && a.inside(next) && a.rows[next.y][next.x] != '.' && !a.exitAt(next) &&
			!standing.count(next)) {
			t = pointAt(a, cell, acrossable);
			if (t && !acrossable) {
				t.reset();
			} else if (t) {
				t->across = true;
			}
		}
		if (t) {
			t->facing = direction;
			return t;
		}
	}
	return std::nullopt;
}

FieldMotion Session::fieldMotion() const {
	FieldMotion m;
	const Area* a = area();
	if (mode() != Mode::Field || !a) {
		return m;
	}
	m.stepProgressUs = stepProgressUs_;
	m.stepProgressMs = static_cast<int>(stepProgressUs_ / 1000);
	m.stepMs = leaderStepMs(false);
	m.runStepMs = leaderStepMs(true);
	m.stepUs = data_.config.exactSteps ? leaderStepUs(false) : m.stepMs * 1000LL;
	m.runStepUs = data_.config.exactSteps ? leaderStepUs(true) : m.runStepMs * 1000LL;
	const std::int64_t tickUs = data_.config.tickMs * 1000LL;
	m.nextTickUs = tickUs - tickRemainderMs_ * 1000LL;
	auto landsIn = [&](std::int64_t stepUs) {
		const std::int64_t ticks = std::max<std::int64_t>(1, (stepUs - stepProgressUs_ + tickUs - 1) / tickUs);
		return ticks * tickUs - tickRemainderMs_ * 1000LL;
	};
	m.stepLandsInUs = landsIn(m.stepUs);
	m.runStepLandsInUs = landsIn(m.runStepUs);
	for (const Conveyor& c : a->conveyors) {
		const bool holds = (c.requireFlag.empty() || world_.flag(c.requireFlag)) && (c.blockFlag.empty() || !world_.flag(c.blockFlag));
		if (holds && std::find(c.cells.begin(), c.cells.end(), world_.position) != c.cells.end()) {
			m.conveyorStepMs = c.stepMs;
			m.conveyorProgressMs = static_cast<int>(conveyorUs_ / 1000);
			m.conveyorStepUs = data_.config.exactSteps ? c.stepUs : c.stepMs * 1000LL;
			m.conveyorProgressUs = conveyorUs_;
		}
	}
	int bit = 1;
	for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
		if (canStep(f)) {
			m.stepMask |= bit;
		}
		bit <<= 1;
	}
	return m;
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
	const std::optional<InteractTarget> target = interactTarget();
	if (!target) {
		return refusal("nothing_there");
	}
	world_.facing = target->facing;
	const Area& a = *area();
	switch (target->kind) {
	case InteractKind::Vehicle:
		return board();
	case InteractKind::Port: {
		const Port& port = *a.portAt(target->cell);
		if (!target->ready) {
			return refusal("port_closed");
		}
		if (!port.ship.empty()) {
			boardShip(port);
			return {};
		}
		modes_.transition(Mode::Menu);
		menuKind_ = MenuKind::Port;
		portId_ = port.id;
		emit(OutputKind::Notice, "game.port_open");
		return {};
	}
	case InteractKind::Helm:
		modes_.transition(Mode::Menu);
		menuKind_ = MenuKind::Port;
		portId_ = world_.shipPort;
		emit(OutputKind::Notice, "game.helm_open");
		return {};
	case InteractKind::Talk:
	case InteractKind::Actor: {
		const Hook* hook = a.hookAt(target->cell, HookTrigger::Interact);
		// An actor may stand on another's talk point that answers on its actor, which is floor.
		if (!hook || (target->kind == InteractKind::Actor && hook->id != target->id)) {
			hook = talkPoint(&a, target->id);
		}
		if (!hook) {
			hook = touchHook(target->id);
		}
		if (!target->ready || !hook) {
			return refusal("no_answer");
		}
		if (hook->turns) {
			turnTowardLeader(hook->trigger == HookTrigger::Touch ? hook->actor : hook->id);
		}
		startEvent(hook->event);
		return {};
	}
	case InteractKind::Chest:
		openChest(*a.chestAt(target->cell));
		settleField();
		return {};
	case InteractKind::Shop: {
		const Shop& shop = *a.shopAt(target->cell);
		if (!target->ready) {
			return refusal("shop_closed", {shop.id});
		}
		modes_.transition(Mode::Shop);
		shopId_ = shop.id;
		emit(OutputKind::Notice, "game.shop_open");
		return {};
	}
	case InteractKind::Rest: {
		const RestPoint& rest = *a.restAt(target->cell);
		modes_.transition(Mode::Menu);
		menuKind_ = MenuKind::Inn;
		innId_ = rest.id;
		emit(OutputKind::Notice, "game.inn_offer", {std::to_string(restCost(rest))});
		return {};
	}
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
	return canEnter(area, cell, actorCells());
}

bool Session::canEnter(const Area& area, Cell cell, const std::set<Cell>& standing) const {
	if (!vehicleAt(cell).empty() || standing.count(cell)) {
		return false;
	}
	// A cell no object can stand on is walkable when it is floor and no exit is closed there.
	const bool objects = objectsMayStand(area, cell);
	const FieldVehicle* vehicle = riding();
	if (!vehicle) {
		const bool walkable = objects ? area.walkable(cell) : area.inside(cell) && area.rows[cell.y][cell.x] == '.' && !area.closed.count(cell);
		return walkable || dockedPlatformAt(cell);
	}
	return area.inside(cell) && vehicle->canCross(area.rows[cell.y][cell.x]) && !area.closed.count(cell) &&
		(!objects || (!area.hookAt(cell, HookTrigger::Interact) && !area.shopAt(cell) && !area.restAt(cell) && !area.chestAt(cell) && !area.portAt(cell)));
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
	stepProgressUs_ = 0;
	vehicleMoveMs_ = 0;
	emit(OutputKind::Notice, "game.boarded", {id});
	if (const VehicleSounds* sounds = vehicleSounds(id)) {
		playFieldSound(sounds->board);
	}
	return {};
}

const VehicleSounds* Session::vehicleSounds(const std::string& vehicle) const {
	const auto& vehicles = data_.config.presentation.sounds.vehicles;
	const auto it = vehicles.find(vehicle);
	return it == vehicles.end() ? nullptr : &it->second;
}

void Session::playFieldSound(const std::string& cue) {
	if (cue.empty()) {
		return;
	}
	events::Action sound;
	sound.type = events::ActionType::sound;
	sound.subject = cue;
	const events::Handle handle = nextHandle_++;
	presenter()->begin(handle, sound);
	output_.push_back({OutputKind::Action, {}, events::describe(sound)});
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
			stepProgressUs_ = 0;
			emit(OutputKind::Notice, "game.left_vehicle", {id});
			if (const VehicleSounds* sounds = vehicleSounds(id)) {
				playFieldSound(sounds->leave);
			}
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
	stepProgressUs_ = 0;
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
	walkedFollowers_ = ship.arrivalFollowers;
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
			if (a.kind != AreaKind::Town || a.region != here->region || id == here->id || !world_.visitedAreas.count(id)) {
				continue;
			}
			if (const std::optional<DungeonExitTarget>& way = a.returnTo) {
				const Area& to = *data_.area(way->area);
				out.push_back({id, way->area, way->marker.empty() ? way->at : to.markers.at(way->marker), a.region});
			} else if (!a.arrival.empty()) {
				out.push_back({id, id, a.markers.at(a.arrival), a.region});
			}
		}
		break;
	case TravelKind::DungeonExit:
		if (here->kind == AreaKind::Dungeon && here->dungeonExit) {
			const DungeonExitTarget& way = *here->dungeonExit;
			const Area& to = *data_.area(way.area);
			out.push_back({way.area, way.area, way.marker.empty() ? way.at : to.markers.at(way.marker), to.region});
		} else if (here->kind == AreaKind::Dungeon && data_.area(world_.dungeonExitArea)) {
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
	const Area* town = kind == TravelKind::Town ? data_.area(to.id) : nullptr;
	const std::optional<DungeonExitTarget>& way = town ? town->returnTo : area()->dungeonExit;
	const bool turn = (town || kind == TravelKind::DungeonExit) && way && way->facing;
	const Facing arrival = turn ? *way->facing : world_.facing;
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
		const FieldSounds& sounds = data_.config.presentation.sounds;
		const auto own = sounds.travelThings.find(thing);
		playFieldSound(own != sounds.travelThings.end() ? own->second : kind == TravelKind::Town ? sounds.town : sounds.dungeonExit);
	}
	leaveVehicleBehind();
	modes_.transition(Mode::Field);
	menuKind_ = MenuKind::None;
	portId_.clear();
	if (town && town->returnTo) {
		arrivingFrom_ = town->id;
		walkedFollowers_ = town->returnTo->followers;
	} else if (town) {
		walkedFollowers_ = town->arrivalFollowers;
	} else if (kind == TravelKind::Port) {
		walkedFollowers_ = data_.port(to.id)->followers;
	}
	enterAreaAt(to.area, to.cell);
	if (turn) {
		world_.facing = arrival;
		placeFollowers();
	}
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

std::vector<TalkLineView> Session::partyTalkLines() const {
	std::vector<TalkLineView> out;
	const TalkSet* best = mode() == Mode::Title ? nullptr : talkSet();
	if (!best) {
		return out;
	}
	for (const TalkLine& line : best->lines) {
		if (std::find(world_.lineup.begin(), world_.lineup.end(), line.speaker) != world_.lineup.end()) {
			out.push_back({line.speaker, displayName(line.speaker), line.text, stringText(line.text)});
		}
	}
	return out;
}

void Session::openChest(const Chest& chest) {
	if (world_.flag(chest.flag)) {
		emit(OutputKind::Notice, "game.chest_empty");
		return;
	}
	if (data_.config.bagSlots > 0) {
		// The whole chest or nothing: it stays shut, to be opened once the bag has room.
		int slots = 0;
		for (const auto& [id, n] : chest.items) {
			const auto held = world_.items.find(id);
			slots += data_.config.bagSingle ? n : held == world_.items.end() || held->second <= 0 ? 1 : 0;
		}
		if (bagSlotsUsed() + slots > data_.config.bagSlots) {
			emit(OutputKind::Notice, "game.bag_full");
			return;
		}
	}
	if (data_.config.presentation.chestOpenedNotice) {
		const std::string own = "game.chest_opened." + chest.kind;
		emit(OutputKind::Notice, !chest.kind.empty() && language().text.count(own) ? own : "game.chest_opened");
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
	const FieldSounds& sounds = data_.config.presentation.sounds;
	const auto own = sounds.chests.find(chest.id);
	playFieldSound(own != sounds.chests.end() ? own->second : sounds.chest);
	world_.flags[chest.flag] = true;
	refreshJournal(true);
}

int Session::bagSlotsUsed() const {
	int used = 0;
	for (const auto& [id, n] : world_.items) {
		used += n <= 0 ? 0 : data_.config.bagSingle ? n : 1;
	}
	return used;
}

int Session::bagRoom(const std::string& id) const {
	const bool item = data_.db.item(id) != nullptr;
	const std::map<std::string, int>& bag = item ? world_.items : world_.equipmentBag;
	const auto found = bag.find(id);
	const int held = found == bag.end() ? 0 : found->second;
	const int cap = std::max(0, data_.config.itemCap - held);
	if (!item || data_.config.bagSlots <= 0) {
		return cap;
	}
	const int free = data_.config.bagSlots - bagSlotsUsed();
	if (data_.config.bagSingle) {
		return std::max(0, std::min(cap, free));
	}
	return held > 0 || free > 0 ? cap : 0;
}

void Session::giveThing(const std::string& id, int count) {
	std::map<std::string, int>& bag = data_.db.item(id) ? world_.items : world_.equipmentBag;
	bag[id] = std::min(data_.config.itemCap, bag[id] + count);
}

// Events

bool Session::startEvent(const std::string& event) {
	if (backgroundEvent(event)) {
		spawnBackground(event);
		return true;
	}
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
	if (endGamePending_) {
		endGamePending_ = false;
		clearPending_ = false;
		if (world_.devMarked) {
			emit(OutputKind::Notice, "game.clear_not_recorded");
		} else {
			writeClearRecord();
		}
		quitToTitle();
		return;
	}
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

std::optional<ChoiceView> Session::keptChoice() const {
	int picked = -1;
	std::vector<std::string> states;
	const events::ChoiceRequest* request = interpreter_ && mode() == Mode::Event ? interpreter_->kept_choice(picked, states) : nullptr;
	if (!request) {
		return std::nullopt;
	}
	ChoiceView view = choiceViewOf(*request);
	view.states = states;
	view.picked = picked;
	return view;
}

std::optional<ChoiceView> Session::choiceView() const {
	const events::ChoiceRequest* request = choice();
	if (!request) {
		return std::nullopt;
	}
	return choiceViewOf(*request);
}

ChoiceView Session::choiceViewOf(const events::ChoiceRequest& request) const {
	ChoiceView view;
	view.states = request.states;
	view.keep = request.keep;
	view.back = request.back;
	view.prompt = request.prompt.value.empty() ? "" : scriptText(request.prompt);
	view.header = request.header.value.empty() ? "" : scriptText(request.header);
	view.headerId = request.header.is_id ? request.header.value : "";
	view.yesNo = request.yes_no;
	if (request.yes_no) {
		view.options = {stringText(kChoiceYes), stringText(kChoiceNo)};
	} else {
		for (const events::Text& option : request.options) {
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
	// The hidden back branch answers past the options.
	if (index < 0 || index >= count + (request->back ? 1 : 0)) {
		return refusal("pick_option", {std::to_string(count - 1)});
	}
	HandleState& state = handles_[choiceHandle_];
	state.done = true;
	state.result = index;
	pumpEvent(0);
	return {};
}

rules::Message Session::chooseBack() {
	const events::ChoiceRequest* request = choice();
	if (!request) {
		return refusal("no_choice");
	}
	if (!request->back) {
		return refusal("no_back_option");
	}
	return choose(static_cast<int>(request->options.size()));
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
	lastMacroTrouble_ = rules::MacroTroubleReport();
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

rules::Message Session::applyMacro(const std::string& name, std::vector<rules::MacroStepResult>* results, rules::MacroFallback fallback) {
	if (!battle_) {
		return refusal("no_battle");
	}
	for (const rules::Macro& m : world_.macros) {
		if (m.name == name) {
			std::vector<rules::MacroStepResult> r = battle_->applyMacro(m, fallback);
			if (results) {
				*results = std::move(r);
			}
			return {};
		}
	}
	return refusal("no_macro", {name});
}

rules::Message Session::startAuto() {
	return startAuto(rules::MacroTrouble::Default);
}

rules::Message Session::startAuto(rules::MacroTrouble trouble) {
	return startAuto(trouble, rules::MacroPolicy::Default);
}

rules::Message Session::startAuto(rules::MacroTrouble trouble, rules::MacroPolicy policy) {
	if (!battle_) {
		return refusal("no_battle");
	}
	battle_->setAutoPolicy(policy);
	if (world_.autoMacro.empty()) {
		return refusal("no_auto_macro");
	}
	for (const rules::Macro& m : world_.macros) {
		if (m.name == world_.autoMacro) {
			const rules::MacroTrouble mode = trouble != rules::MacroTrouble::Default ? trouble
				: m.onTrouble != rules::MacroTrouble::Default ? m.onTrouble : data_.db.battleRules.autoMacroOnTrouble;
			battle_->startAuto(m, mode);
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
		lastMacroTrouble_ = battle_->lastAutoTrouble();
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

rules::Message Session::setAutoPlayMacro(const std::string& name) {
	return setAutoPlayMacro(name, rules::MacroTrouble::Default);
}

rules::Message Session::setAutoPlayMacro(const std::string& name, rules::MacroTrouble trouble) {
	return setAutoPlayMacro(name, trouble, rules::MacroPolicy::Default);
}

rules::Message Session::setAutoPlayMacro(const std::string& name, rules::MacroTrouble trouble, rules::MacroPolicy policy) {
	if (!name.empty() && std::none_of(world_.macros.begin(), world_.macros.end(), [&](const rules::Macro& m) { return m.name == name; })) {
		return refusal("no_macro", {name});
	}
	autoPlayMacro_ = name;
	autoPlayTrouble_ = trouble;
	autoPlayPolicy_ = policy;
	return {};
}

rules::Message Session::macroTrouble(const std::string& name, rules::MacroTroubleReport& out) const {
	if (!battle_) {
		return refusal("no_battle");
	}
	const auto macro = std::find_if(world_.macros.begin(), world_.macros.end(), [&](const rules::Macro& m) { return m.name == name; });
	if (macro == world_.macros.end()) {
		return refusal("no_macro", {name});
	}
	out = battle_->macroTrouble(*macro);
	return {};
}

rules::Message Session::checkMacro(const std::string& name, std::vector<rules::MacroStepCheck>& out) const {
	if (!battle_) {
		return refusal("no_battle");
	}
	const auto macro = std::find_if(world_.macros.begin(), world_.macros.end(), [&](const rules::Macro& m) { return m.name == name; });
	if (macro == world_.macros.end()) {
		return refusal("no_macro", {name});
	}
	out = battle_->checkMacro(*macro);
	return {};
}

rules::Message Session::fillAutoRound() {
	if (!battle_) {
		return refusal("no_battle");
	}
	lastMacroTrouble_ = rules::MacroTroubleReport();
	battle_->setAutoPolicy(autoPlayPolicy_);
	rules::Message refused;
	const auto named = std::find_if(world_.macros.begin(), world_.macros.end(), [&](const rules::Macro& m) { return m.name == autoPlayMacro_; });
	if (!autoPlayMacro_.empty() && named == world_.macros.end()) {
		refused = refusal("no_macro", {autoPlayMacro_});
	} else if (!autoPlayMacro_.empty()) {
		const rules::MacroTrouble mode = autoPlayTrouble_ != rules::MacroTrouble::Default ? autoPlayTrouble_
			: named->onTrouble != rules::MacroTrouble::Default ? named->onTrouble : data_.db.battleRules.namedMacroOnTrouble;
		if (mode != rules::MacroTrouble::Stop) {
			lastMacroTrouble_ = battle_->applyMacroRound(*named, mode);
			return {};
		}
		if (!battle_->autoActive() && battle_->autoStop() == rules::AutoStop::None && battle_->startAuto(*named, mode)) {
			emit(OutputKind::Notice, "game.auto_on", {named->name});
		}
		if (!fillFromAuto()) {
			fillFromPolicy();
		}
		return {};
	}
	if (!world_.autoMacro.empty() && battle_->round() == 0 && !battle_->autoActive()) {
		const rules::Message notStarted = startAuto(autoPlayMacro_.empty() ? autoPlayTrouble_ : rules::MacroTrouble::Default, autoPlayPolicy_);
		refused = refused.empty() ? notStarted : refused;
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
		r.trouble = lastMacroTrouble_;
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
	settleMachineBank();
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
				const int kept = data_.config.bagSlots > 0 ? std::min(n, bagRoom(id)) : n;
				if (kept > 0) {
					giveThing(id, kept);
					emit(OutputKind::Notice, "game.dropped", {std::to_string(kept), id});
				}
				if (kept < n) {
					emit(OutputKind::Notice, "game.drop_left", {std::to_string(n - kept), id});
				}
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
	if (const Area* here = area()) {
		const Zone* zone = here->zoneAt(world_.position);
		if (data_.config.graceAfterBattle || (zone && zone->random.graceAfterBattle)) {
			world_.areaSteps = 0;
		}
	}

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
	if (data_.config.exactSteps && mode() == Mode::Field) {
		save.world.stepCarryUs = stepProgressUs_;
		save.world.conveyorCarryUs = conveyorUs_;
		save.world.tickCarryMs = tickRemainderMs_;
	}
	save.autosave = autosave;
	save.contentHash = data_.contentHash;
	save.writeTime = options_.clock ? options_.clock() : 0;
	save.sequence = store_ ? store_->nextSequence() : 0;
	for (const std::string& id : world_.lineup) {
		if (const rules::PartyMember* m = world_.member(id)) {
			const rules::Stats stats = rules::memberStats(data_.db, *m);
			save.memberMaxima[id] = {stats.maxHp, stats.maxTp};
		}
	}
	for (const BackgroundRun& run : background_) {
		save.background.emplace_back(run.event, events::save_state_to_text(run.interpreter->save()));
	}
	return save;
}

void Session::autosave(std::string reason) {
	if (!autosaveOn()) {
		autosaveReason_.clear();
		return;
	}
	Timer timer(stats_.autosaveUs);
	++stats_.autosaves;
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
	const std::string text = serializeSave(save);
	stats_.saveBytes += text.size();
	if (!store_->write(slot, text, error)) {
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
	creditsId_.clear();
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
	if (count > bagRoom(id)) {
		return refusal("bag_full", {std::to_string(data_.config.bagSlots)});
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

rules::Message Session::discard(const std::string& id, int count) {
	if (menuKind() != MenuKind::Field) {
		return refusal("discard_menu_only");
	}
	if (count < 1) {
		return refusal("discard_one");
	}
	const rules::ItemDef* item = data_.db.item(id);
	if (item && !item->discardable) {
		return refusal("not_discardable", {id});
	}
	std::map<std::string, int>& bag = item ? world_.items : world_.equipmentBag;
	auto held = bag.find(id);
	if (held == bag.end() || held->second < count) {
		return refusal("discard_not_held", {id});
	}
	held->second -= count;
	if (held->second == 0) {
		bag.erase(held);
	}
	emit(OutputKind::Notice, "game.discarded", {std::to_string(count), id});
	return {};
}

// Rest for the whole roster, active or not (FLD-034). A null point is a free rest.
int Session::restCost(const RestPoint& point) const {
	return point.perMember ? point.cost * static_cast<int>(world_.lineup.size()) : point.cost;
}

rules::Message Session::payRest(const RestPoint* point) {
	const int cost = point ? restCost(*point) : 0;
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
	settleMachineBank();
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
		// Spent under consume_when_no_effect or item_no_reach "consume": gone from the bag, so not "would have".
		const bool spent = std::any_of(use.events.begin(), use.events.end(), [](const rules::Event& e) {
			return e.type == rules::EventType::ResourceSpent && e.resource == rules::ResourceKind::Item;
		});
		return {spent ? "game.no_effect_used" : "game.no_effect", {item}};
	}
	for (std::size_t i = 0; i < party.size(); ++i) {
		*world_.member(world_.lineup[i]) = party[i];
	}
	emit(OutputKind::Notice, "game.used", {item});
	reportFieldUse(use.events, party);
	settleMachineBank();
	return {};
}

std::vector<std::string> Session::alliesOutOfReach(rules::CommandKind kind, const std::string& id, const std::string& user) const {
	std::vector<std::string> out;
	if (kind != rules::CommandKind::Ability && kind != rules::CommandKind::Item) {
		return out;
	}
	if (battle_ && battle_->outcome() == rules::Outcome::Ongoing) {
		for (int actor = 0; actor < battle_->partyCount(); ++actor) {
			if (battle_->combatant(actor).defId != user) {
				continue;
			}
			const rules::Command command = kind == rules::CommandKind::Ability ? rules::Command::ability(id, -1) : rules::Command::item(id, -1);
			for (int ally : battle_->alliesOutOfReach(actor, command)) {
				out.push_back(battle_->combatant(ally).defId);
			}
		}
		return out;
	}
	if (mode() == Mode::Title) {
		return out;
	}
	const auto at = std::find(world_.lineup.begin(), world_.lineup.end(), user);
	const int userIndex = at == world_.lineup.end() ? -1 : static_cast<int>(at - world_.lineup.begin());
	if (kind == rules::CommandKind::Ability && userIndex < 0) {
		return out;
	}
	for (int target : rules::fieldTargetsOutOfReach(data_.db, activeParty(), id, kind == rules::CommandKind::Item, userIndex)) {
		out.push_back(world_.lineup[static_cast<std::size_t>(target)]);
	}
	return out;
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
	settleMachineBank();
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

std::optional<CreditsView> Session::credits() const {
	const Credits* data = menuKind() == MenuKind::Credits ? data_.config.creditsById(creditsId_) : nullptr;
	if (!data) {
		return std::nullopt;
	}
	CreditsView view;
	view.id = data->id;
	view.music = data->music;
	for (const CreditsEntry& entry : data->entries) {
		view.shown += creditsElapsedMs_ >= view.totalMs ? 1 : 0;
		view.totalMs += entry.pauseMs;
		view.entries.push_back({entry.kind, entry.key, text(entry.key), entry.gap, entry.pauseMs});
	}
	view.elapsedMs = std::min(creditsElapsedMs_, view.totalMs);
	return view;
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
		view.onTrouble = macro.onTrouble;
		view.recoverWho = macro.recoverWho;
		view.unnamed = macro.unnamed;
		view.policy = macro.policy;
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

rules::Message Session::setMacroOptions(const std::string& name, rules::MacroTrouble onTrouble, rules::MacroRecoverWho recoverWho, rules::MacroUnnamed unnamed) {
	if (menuKind() != MenuKind::Field) {
		return refusal("macro_menu_only");
	}
	auto macro = findMacro(world_.macros, name);
	if (macro == world_.macros.end()) {
		return refusal("no_macro", {name});
	}
	macro->onTrouble = onTrouble;
	macro->recoverWho = recoverWho;
	macro->unnamed = unnamed;
	return {};
}

rules::Message Session::setMacroPolicy(const std::string& name, rules::MacroPolicy policy) {
	if (menuKind() != MenuKind::Field) {
		return refusal("macro_menu_only");
	}
	auto macro = findMacro(world_.macros, name);
	if (macro == world_.macros.end()) {
		return refusal("no_macro", {name});
	}
	macro->policy = policy;
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
	const rules::Database& db = data_.db;
	// Steps of vehicles, which act only in battles aboard them, do not count against the party's steps.
	const rules::VehicleDef* vehicle = db.vehicle(step.member);
	const int memberSteps = static_cast<int>(std::count_if(steps.begin(), steps.end(), [&](const rules::MacroStep& s) { return !db.vehicle(s.member); }));
	const bool replacesMember = index < count && !db.vehicle(steps[static_cast<std::size_t>(index)].member);
	if (!vehicle && memberSteps - (replacesMember ? 1 : 0) >= rules::kMaxPartySize) {
		return refusal("macro_steps_full", {std::to_string(rules::kMaxPartySize)});
	}
	const rules::PartyMember* member = world_.member(step.member);
	if (!member && !vehicle) {
		return refusal("macro_unknown_member", {step.member});
	}
	for (int i = 0; i < count; ++i) {
		if (i != index && steps[i].member == step.member) {
			return refusal("macro_member_twice", {step.member});
		}
	}
	rules::Targeting targeting = rules::Targeting::OneEnemy;
	if (vehicle && (step.piece || step.kind == rules::CommandKind::Item || step.kind == rules::CommandKind::Defend)) {
		return refusal("macro_bad_command", {rules::toString(step.kind)});
	}
	if (vehicle && step.target.kind == rules::MacroTargetKind::Member) {
		return refusal("macro_target", {rules::toString(step.target.kind)});
	}
	switch (step.kind) {
	case rules::CommandKind::Ability: {
		const std::vector<std::string> known = vehicle ? vehicle->abilities : rules::knownAbilities(db, *member);
		const rules::AbilityDef* ability = db.ability(step.id);
		if (!ability || std::find(known.begin(), known.end(), step.id) == known.end()) {
			return refusal("macro_unknown_ability", {step.member, step.id});
		}
		targeting = ability->targeting;
		break;
	}
	case rules::CommandKind::Item: {
		if (step.piece) {
			const rules::EquipmentDef* piece = db.equipmentPiece(step.id);
			const rules::AbilityDef* use = piece ? db.ability(piece->use) : nullptr;
			if (!use) {
				return refusal("macro_piece_no_use", {step.id});
			}
			targeting = use->targeting;
			break;
		}
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
		if (!step.id.empty() || step.piece) {
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
	detail.profile = data_.db.character(member)->profile;
	const rules::CharacterDef& character = *data_.db.character(member);
	detail.attack = combatant.effective(rules::Stat::Attack);
	detail.baseStats = rules::statsAtLevel(character, m->level);
	detail.experienceForLevel = m->level >= rules::formulas::kMaxLevel ? 0 : rules::experienceToNextLevel(character, m->level);
	detail.totalExperience = m->experience;
	for (int level = 1; level < m->level; ++level) {
		detail.totalExperience += rules::experienceToNextLevel(character, level);
	}
	detail.kind = character.kind;
	detail.vulnerable = character.vulnerable;
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

rules::Message Session::devRest() {
	if (rules::Message refused = devNeedsGame(); !refused.empty()) {
		return refused;
	}
	devMark();
	for (rules::PartyMember& m : world_.roster) {
		rules::rest(m, data_.db);
	}
	for (auto& [id, place] : world_.vehicles) {
		place.skillUses.clear();
	}
	settleMachineBank();
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

rules::Message Session::devCombo(const std::string& recipe) {
	if (!battle_ || battle_->outcome() != rules::Outcome::Ongoing) {
		return refusal("no_battle");
	}
	const auto found = std::find_if(data_.db.combos.begin(), data_.db.combos.end(), [&](const rules::ComboRecipe& r) { return r.id == recipe; });
	if (found == data_.db.combos.end()) {
		return refusal("combo_not_found", {recipe});
	}
	const rules::ComboRecipe& r = *found;
	std::vector<int> chosen(r.abilities.size(), -1);
	std::size_t stuck = 0;
	std::function<bool(std::size_t)> assign = [&](std::size_t component) {
		if (component == r.abilities.size()) {
			return true;
		}
		for (int actor = 0; actor < battle_->partyCount(); ++actor) {
			const rules::Combatant& c = battle_->combatant(actor);
			const bool tiedElsewhere = component < r.characters.size() && !r.characters[component].empty() && r.characters[component] != c.defId;
			if (tiedElsewhere || c.aboard || !c.alive() || !c.knows(r.abilities[component]) ||
				std::find(chosen.begin(), chosen.begin() + static_cast<std::ptrdiff_t>(component), actor) != chosen.begin() + static_cast<std::ptrdiff_t>(component)) {
				continue;
			}
			chosen[component] = actor;
			if (assign(component + 1)) {
				return true;
			}
		}
		stuck = std::max(stuck, component);
		return false;
	};
	if (!assign(0)) {
		return refusal("combo_no_member", {r.abilities[stuck]});
	}
	for (std::size_t i = 0; i < chosen.size(); ++i) {
		if (rules::Message refused = battle_->checkCommand(chosen[i], rules::Command::ability(r.abilities[i], -1)); !refused.empty()) {
			return refused;
		}
	}
	devMark();
	for (std::size_t i = 0; i < chosen.size(); ++i) {
		battle_->setCommand(chosen[i], rules::Command::ability(r.abilities[i], -1));
	}
	battle_->setLeadOrder(chosen);
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
