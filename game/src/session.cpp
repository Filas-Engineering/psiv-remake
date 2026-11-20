#include "game/session.h"

#include "game/encounters.h"

#include "events/glossary.h"
#include "events/save_state.h"
#include "events/strings.h"
#include "rules/auto_policy.h"
#include "rules/field.h"
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

constexpr std::size_t kTextLogPages = 500;
constexpr std::size_t kMaxNameLength = 16;
constexpr std::size_t kInstantActionRecords = 256;
constexpr int kMaxWalkSteps = 4000;

bool hookHolds(const Hook& hook, const World& world) {
	return (hook.requireFlag.empty() || world.flag(hook.requireFlag)) && (hook.blockFlag.empty() || !world.flag(hook.blockFlag));
}

bool rangeHolds(const FlagRange& range, const World& world) {
	return (range.after.empty() || world.flag(range.after)) && (range.until.empty() || !world.flag(range.until));
}

rules::Message refusal(const char* key, std::vector<std::string> params = {}) {
	return {std::string("game.refuse.") + key, std::move(params)};
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
	}
	return "none";
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
	ActionQueue(Presentation presentation, std::function<void(const PresentedAction&)> onAction)
		: queued_(presentation == Presentation::Queued), onAction_(std::move(onAction)) {}

	bool begin(std::uint64_t handle, const events::Action& action) override {
		push({handle, ActionPhase::Begin, action, events::describe(action)});
		if (!queued_ || action.instant) {
			return true;
		}
		running_.insert(handle);
		return false;
	}

	bool finished(std::uint64_t handle) override { return !running_.count(handle); }

	void cancel(std::uint64_t handle) override {
		running_.erase(handle);
		push({handle, ActionPhase::Cancel, events::Action(), ""});
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

	bool queued_;
	std::function<void(const PresentedAction&)> onAction_;
	std::deque<PresentedAction> records_;
	std::set<std::uint64_t> running_;
};

const std::vector<std::string>& sessionTextKeys() {
	static const std::vector<std::string> keys = {
		"game.arrive",
		"game.auto_on",
		"game.autosave_failed",
		"game.autosave_point",
		"game.autosaved",
		"game.battle_start",
		"game.bought",
		"game.chest_empty",
		"game.clear_recorded",
		"game.dropped",
		"game.equipped",
		"game.event_cannot_resume",
		"game.event_damaged",
		"game.fallen",
		"game.found",
		"game.found_money",
		"game.game_over",
		"game.got_away",
		"game.healed",
		"game.inn_offer",
		"game.job.accepted",
		"game.job.available",
		"game.job.completed",
		"game.joins",
		"game.learned",
		"game.leaves",
		"game.level_up",
		"game.loaded",
		"game.named",
		"game.no_effect",
		"game.reason.after_rest",
		"game.reason.area_entry",
		"game.reason.before_battle",
		"game.reason.event_end",
		"game.refuse.blocked",
		"game.refuse.buy_one",
		"game.refuse.carry_cap",
		"game.refuse.equip_menu_only",
		"game.refuse.in_the_way",
		"game.refuse.interact_field_only",
		"game.refuse.items_menu_only",
		"game.refuse.load_title",
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
		"game.refuse.no_macro",
		"game.refuse.no_menu",
		"game.refuse.no_money",
		"game.refuse.no_name_entry",
		"game.refuse.no_retry",
		"game.refuse.no_save",
		"game.refuse.no_shop",
		"game.refuse.no_slot",
		"game.refuse.nobody_talks",
		"game.refuse.not_bought",
		"game.refuse.not_enough_held",
		"game.refuse.not_sold",
		"game.refuse.nothing_there",
		"game.refuse.nothing_worn",
		"game.refuse.nowhere_rest",
		"game.refuse.pick_option",
		"game.refuse.profile_not_written",
		"game.refuse.save_damaged",
		"game.refuse.save_failed",
		"game.refuse.save_in_event",
		"game.refuse.save_unknown_area",
		"game.refuse.save_where",
		"game.refuse.saving_disabled",
		"game.refuse.sell_one",
		"game.refuse.settings_in_battle",
		"game.refuse.slot_unreadable",
		"game.refuse.talk_field_only",
		"game.refuse.tech_menu_only",
		"game.refuse.unknown_difficulty",
		"game.refuse.unknown_language",
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
		"game.victory",
	};
	return keys;
}

class Session::GameHost : public events::Host {
public:
	explicit GameHost(Session& session) : s_(session) {}

	events::Handle start_action(const events::Action& action) override {
		if (action.type == events::ActionType::warp) {
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
	}

	void set_player_control(bool enabled) override { s_.world_.playerControl = enabled; }

	std::string snapshot_world() override { return serializeWorldChunks(s_.world_); }

	bool restore_world(const std::string& snapshot) override {
		std::string error;
		if (!parseWorldChunks(snapshot, s_.world_, error)) {
			s_.warn("cannot restore the event-start world: " + error);
			return false;
		}
		return true;
	}

private:
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
		actions_ = std::make_unique<ActionQueue>(options_.presentation, options_.onAction);
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
			out.push_back({id, home->at, Facing::South, true, false});
		}
	}
	return out;
}

const Area* Session::area() const {
	return data_.area(world_.area);
}

EncounterMode Session::encounterMode() const {
	if (options_.encounterMode) {
		return *options_.encounterMode;
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
	if (const rules::CharacterDef* c = db.character(id)) {
		return c->name;
	}
	if (language().strings.find("speaker." + id)) {
		return stringText("speaker." + id);
	}
	if (const rules::AbilityDef* a = db.ability(id)) {
		return a->name;
	}
	if (const rules::ItemDef* i = db.item(id)) {
		return i->name;
	}
	if (const rules::StatusDef* s = db.status(id)) {
		return s->name;
	}
	if (const rules::EnemyDef* e = db.enemy(id)) {
		return e->name;
	}
	if (const rules::EquipmentDef* e = db.equipmentPiece(id)) {
		return e->name;
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
		const std::string line = (e.speaker.empty() ? "" : displayName(e.speaker) + ": ") + scriptText(e.text);
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
	dropEvent();
	battle_.reset();
	ended_.reset();
	latchedDirection_.reset();
	world_ = std::move(save.value->world);
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
	pacing_.enterArea(a->id, a->kind == AreaKind::Dungeon);
	emit(OutputKind::Notice, "game.loaded", {slot});
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
		pacing_.eventTime(ms);
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
	pacing_.fieldTime(tick);
	world_.graceMs = std::max(0, world_.graceMs - tick);
	const Area& a = *area();
	if (input.held && world_.playerControl) {
		world_.facing = input.direction;
		const Cell target = stepFrom(world_.position, input.direction);
		if (std::optional<EncounterStart> contact = leaderBump(world_, target)) {
			startFieldBattle(*contact);
			return;
		}
		const bool blockedByEnemy = std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; });
		if (a.walkable(target) && !blockedByEnemy) {
			stepProgressMs_ += tick;
			if (stepProgressMs_ >= a.stepMs) {
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
	if (encounterMode() == EncounterMode::Visible && world_.area == a.id) {
		if (std::optional<EncounterStart> contact = tickVisibleEnemies(world_, a, tick, a.stepMs)) {
			startFieldBattle(*contact);
		}
	}
}

void Session::arrive(Cell cell) {
	world_.position = cell;
	pacing_.step();
	const Area& a = *area();
	if (const Exit* exit = a.exitAt(cell)) {
		const Exit e = *exit;
		enterArea(e.toArea, e.toMarker);
		settleField();
		return;
	}
	if (const Hook* hook = a.hookAt(cell, HookTrigger::Step)) {
		if (hookHolds(*hook, world_)) {
			startEvent(hook->event);
			return;
		}
	}
	if (encounterMode() == EncounterMode::Random) {
		if (std::optional<EncounterStart> start = randomStep(world_, a, cell, data_.db, activeParty())) {
			startFieldBattle(*start);
		}
	}
}

void Session::enterArea(const std::string& areaId, const std::string& marker) {
	const Area* a = data_.area(areaId);
	if (!a) {
		warn("unknown area '" + areaId + "'");
		return;
	}
	world_.area = a->id;
	world_.position = a->markers.at(marker);
	world_.visitedAreas.insert(a->id);
	stepProgressMs_ = 0;
	if (encounterMode() == EncounterMode::Visible) {
		spawnVisibleEnemies(world_, *a);
	} else {
		world_.enemies.clear();
	}
	pacing_.enterArea(a->id, a->kind == AreaKind::Dungeon);
	loadHooksPending_ = true;
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
		if (hook.trigger == HookTrigger::Flag && hookHolds(hook, world_)) {
			startEvent(hook.event);
			return;
		}
	}
	if (clearPending_) {
		clearPending_ = false;
		writeClearRecord();
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
	if (!a.walkable(target) && !enemyThere) {
		world_.facing = direction;
		return refusal("blocked");
	}
	const std::string startArea = world_.area;
	for (int guard = 0; guard < 1000; ++guard) {
		update(data_.config.tickMs, {true, direction});
		if (mode() != Mode::Field || world_.position != start || world_.area != startArea) {
			return {};
		}
		if (world_.graceMs == 0 && std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; }) && !leaderBump(world_, target)) {
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
	std::map<Cell, Cell> from;
	std::deque<Cell> queue{world_.position};
	from[world_.position] = world_.position;
	while (!queue.empty() && !from.count(walk.goal)) {
		const Cell c = queue.front();
		queue.pop_front();
		for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
			const Cell n = stepFrom(c, f);
			if (from.count(n) || !a.walkable(n) || (a.exitAt(n) && n != walk.goal)) {
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
	const Area& a = *area();
	const Cell target = stepFrom(world_.position, world_.facing);
	if (const Hook* hook = a.hookAt(target, HookTrigger::Interact)) {
		if (!hookHolds(*hook, world_)) {
			return refusal("no_answer");
		}
		startEvent(hook->event);
		return {};
	}
	if (const Chest* chest = a.chestAt(target)) {
		openChest(*chest);
		return {};
	}
	if (const Shop* shop = a.shopAt(target)) {
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

rules::Message Session::partyTalk() {
	if (mode() != Mode::Field || interpreter_) {
		return refusal("talk_field_only");
	}
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
	bool spoke = false;
	for (const TalkLine& line : best ? best->lines : std::vector<TalkLine>()) {
		// A line of someone who is not in the active party is left out.
		if (std::find(world_.lineup.begin(), world_.lineup.end(), line.speaker) == world_.lineup.end()) {
			continue;
		}
		output_.push_back({OutputKind::Line, {line.text, {}}, displayName(line.speaker) + ": " + stringText(line.text)});
		spoke = true;
	}
	return spoke ? rules::Message() : refusal("nobody_talks");
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
	if (eventChangedFlag_) {
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
	view.speakerName = line.speaker.empty() ? "" : displayName(line.speaker);
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
	return view->speakerId.empty() ? view->text : view->speakerName + ": " + view->text;
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

void Session::startFieldBattle(const EncounterStart& start) {
	battleSource_ = start.enemyId ? BattleSource::Visible : BattleSource::Random;
	battleEnemyId_ = start.enemyId;
	battleHandle_ = 0;
	const std::string error = startBattle(start.encounter, start.opening, true, false, rules::ScriptedEnd());
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
	setup.vehicle = vehicle;
	setup.flags.vehicleBattle = !vehicle.empty();
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
	pacing_.battleStarted();
	if (boss) {
		pacing_.bossStarted(boss->flag, encounter, averageLevelX100());
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
		for (const auto& [id, n] : rewards.items) {
			giveThing(id, n);
			emit(OutputKind::Notice, "game.dropped", {std::to_string(n), id});
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
	} else if (outcome == rules::Outcome::Escaped) {
		emit(OutputKind::Notice, "game.got_away");
	} else {
		emit(OutputKind::Notice, "game.fallen");
	}
	pacing_.battleEnded(battleRounds_, data_.config.roundMs, experience, money);
	if (battleIsBoss_ && outcome == rules::Outcome::Victory) {
		pacing_.bossEnded();
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

rules::Message Session::buy(const std::string& id, int count) {
	const Shop* s = shop();
	if (!s) {
		return refusal("no_shop");
	}
	if (std::find(s->stock.begin(), s->stock.end(), id) == s->stock.end()) {
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
	if (menuKind() != MenuKind::Field) {
		return refusal("equip_menu_only");
	}
	rules::PartyMember* m = world_.member(member);
	if (!m) {
		return {"equip.unknown_character", {member}};
	}
	const rules::Message refused = rules::equip(*m, data_.db, world_.equipmentBag, equipment);
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
	const std::size_t first = name.find_first_not_of(' ');
	const std::size_t last = name.find_last_not_of(' ');
	const std::string trimmed = first == std::string::npos ? "" : name.substr(first, last - first + 1);
	if (trimmed.empty() || trimmed.size() > kMaxNameLength ||
		std::any_of(trimmed.begin(), trimmed.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F; })) {
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
	if (!data_.db.difficulty(settings.difficulty)) {
		return refusal("unknown_difficulty", {settings.difficulty});
	}
	world_.settings = settings;
	return {};
}

}
