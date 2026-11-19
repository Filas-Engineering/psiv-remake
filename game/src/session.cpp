#include "game/session.h"

#include "game/encounters.h"

#include "events/glossary.h"
#include "events/save_state.h"
#include "rules/auto_policy.h"
#include "rules/formulas.h"
#include "rules/names.h"
#include "rules/progression.h"
#include "rules/setup.h"

#include <algorithm>

namespace game {

namespace {

const char* kBeforeBattle = "before an event battle";

bool hookHolds(const Hook& hook, const World& world) {
	return (hook.requireFlag.empty() || world.flag(hook.requireFlag)) && (hook.blockFlag.empty() || !world.flag(hook.blockFlag));
}

}

class Session::GameHost : public events::Host {
public:
	explicit GameHost(Session& session) : s_(session) {}

	events::Handle start_action(const events::Action& action) override {
		if (action.type == events::ActionType::warp) {
			s_.enterArea(action.subject, action.target);
		}
		HandleState state;
		state.kind = HandleKind::Action;
		const events::Handle handle = s_.newHandle(state);
		s_.handles_[handle].done = s_.options_.presenter ? s_.options_.presenter->begin(handle, action) : true;
		s_.emit(OutputKind::Action, events::describe(action));
		return handle;
	}

	events::Handle show_line(const events::Line& line) override {
		HandleState state;
		state.kind = HandleKind::Line;
		state.line = line;
		s_.lineHandle_ = s_.newHandle(state);
		return s_.lineHandle_;
	}

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
		const std::string error = s_.startBattle(request.encounter, rules::Opening::Normal, request.escape_allowed, request.defeat_handled);
		if (!error.empty()) {
			// Encounters are checked when the data loads, so this only happens if the party is empty.
			s_.emit(OutputKind::Warning, "battle '" + request.encounter + "' could not start: " + error);
			s_.handles_[handle].done = true;
			s_.handles_[handle].result = static_cast<int>(events::BattleResult::win);
		} else {
			s_.battleAutosavePending_ = true;
		}
		return handle;
	}

	bool is_done(events::Handle handle) override {
		auto it = s_.handles_.find(handle);
		if (it == s_.handles_.end()) {
			return true;
		}
		if (!it->second.done && it->second.kind == HandleKind::Action && s_.options_.presenter) {
			it->second.done = s_.options_.presenter->finished(handle);
		}
		return it->second.done;
	}

	int result(events::Handle handle) override {
		auto it = s_.handles_.find(handle);
		return it == s_.handles_.end() ? 0 : it->second.result;
	}

	void cancel(events::Handle handle) override {
		auto it = s_.handles_.find(handle);
		if (it != s_.handles_.end() && it->second.kind == HandleKind::Action && s_.options_.presenter) {
			s_.options_.presenter->cancel(handle);
		}
		forget(handle);
	}

	void release(events::Handle handle) override { forget(handle); }

	bool get_flag(const std::string& name) override { return s_.world_.flag(name); }

	void set_flag(const std::string& name, bool value) override {
		if (s_.world_.flag(name) != value) {
			s_.eventChangedFlag_ = true;
		}
		s_.world_.flags[name] = value;
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
			s_.emit(OutputKind::Notice, s_.memberName(member) + " joins the party.");
		}
	}

	void remove_party_member(const std::string& member) override {
		World& w = s_.world_;
		auto it = std::find(w.lineup.begin(), w.lineup.end(), member);
		if (it == w.lineup.end()) {
			return;
		}
		if (w.lineup.size() == 1) {
			s_.emit(OutputKind::Warning, "the last active member cannot leave the party");
			return;
		}
		w.lineup.erase(it);
		s_.emit(OutputKind::Notice, s_.memberName(member) + " leaves the party.");
	}

	void set_player_control(bool enabled) override { s_.world_.playerControl = enabled; }

	std::string snapshot_world() override { return serializeWorldChunks(s_.world_); }

	bool restore_world(const std::string& snapshot) override {
		std::string error;
		if (!parseWorldChunks(snapshot, s_.world_, error)) {
			s_.emit(OutputKind::Warning, "cannot restore the event-start world: " + error);
			return false;
		}
		return true;
	}

private:
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
	}

	Session& s_;
};

Session::Session(const GameData& data, SessionOptions options) : data_(data), options_(std::move(options)) {
	if (!options_.saveDirectory.empty()) {
		store_.emplace(options_.saveDirectory, data_.config.manualSlots, data_.config.autosaveSlots);
	}
	host_ = std::make_unique<GameHost>(*this);
}

Session::~Session() = default;

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

void Session::emit(OutputKind kind, const std::string& text) {
	output_.push_back({kind, text});
}

events::Handle Session::newHandle(HandleState state) {
	const events::Handle handle = nextHandle_++;
	handles_[handle] = std::move(state);
	return handle;
}

std::string Session::memberName(const std::string& characterId) const {
	const rules::CharacterDef* def = data_.db.character(characterId);
	return def ? def->name : characterId;
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

// Title

std::string Session::newGame() {
	if (mode() != Mode::Title) {
		return "a new game starts from the title screen";
	}
	const GameConfig& c = data_.config;
	const Settings profile = world_.settings;
	world_ = World();
	world_.settings = profile;
	world_.encounterStream.state = options_.seed;
	for (const std::string& id : c.startLineup) {
		world_.roster.push_back(rules::makePartyMember(data_.db, *data_.db.character(id), c.startLevel));
		world_.lineup.push_back(id);
	}
	world_.money = c.startMoney;
	for (const auto& [id, n] : c.startItems) {
		giveThing(id, n);
	}
	world_.macros = data_.db.macros;
	dropEvent();
	battle_.reset();
	gameOver_ = false;
	retrySlot_.clear();
	pendingRetrySlot_.clear();
	retryCount_ = 0;
	areasSinceAutosave_.clear();
	autosaveReason_.clear();
	modes_.transition(Mode::Field);
	enterArea(c.startArea, c.startMarker);
	world_.facing = c.startFacing;
	settleField();
	return "";
}

std::string Session::continueGame() {
	if (!store_) {
		return "saving is disabled";
	}
	const std::string slot = store_->latest();
	if (slot.empty()) {
		return "there is no save to continue";
	}
	return load(slot);
}

std::string Session::load(const std::string& slot) {
	retryCount_ = 0;
	retrySlot_.clear();
	std::string text;
	if (!store_ || !store_->read(slot, text)) {
		return "cannot read save slot '" + slot + "'";
	}
	return loadText(text, slot);
}

std::string Session::retryBattle() {
	if (!canRetry()) {
		return "there is no battle to retry";
	}
	const std::string slot = retrySlot_;
	std::string text;
	if (!store_->read(slot, text)) {
		return "cannot read save slot '" + slot + "'";
	}
	++retryCount_;
	return loadText(text, slot);
}

std::string Session::loadText(const std::string& text, const std::string& slot) {
	if (mode() != Mode::Title) {
		return "saves load from the title screen";
	}
	rules::Result<SaveFile> save = parseSave(text, world_.settings);
	if (!save.ok()) {
		return "save slot '" + slot + "' cannot be loaded: " + save.error;
	}
	if (!data_.area(save.value->world.area)) {
		return "save slot '" + slot + "' is in unknown area '" + save.value->world.area + "'";
	}
	dropEvent();
	battle_.reset();
	world_ = std::move(save.value->world);
	gameOver_ = false;
	pendingRetrySlot_.clear();
	areasSinceAutosave_ = {world_.area};
	autosaveReason_.clear();
	loadHooksPending_ = false;
	loadHooksFired_.clear();
	stepProgressMs_ = 0;
	tickRemainderMs_ = 0;
	modes_.transition(Mode::Field);
	const Area* a = area();
	pacing_.enterArea(a->id, a->kind == AreaKind::Dungeon);
	emit(OutputKind::Notice, "Loaded " + slot + ".");
	if (save.value->midEvent.empty()) {
		return "";
	}
	events::SaveState state;
	std::string error;
	if (!events::save_state_from_text(save.value->midEvent, state)) {
		emit(OutputKind::Warning, "the saved event state is damaged; continuing in the field");
		return "";
	}
	modes_.transition(Mode::Event);
	interpreter_ = std::make_unique<events::Interpreter>(data_.program, *host_);
	eventChangedFlag_ = false;
	if (!interpreter_->restore(state, error)) {
		emit(OutputKind::Warning, "the saved event cannot resume: " + error);
		dropEvent();
		modes_.transition(Mode::Field);
		return "";
	}
	pumpEvent(0);
	return "";
}

void Session::quitToTitle() {
	dropEvent();
	battle_.reset();
	gameOver_ = false;
	modes_.reset(Mode::Title);
}

// Field

void Session::update(int ms, const FieldInput& input) {
	switch (mode()) {
	case Mode::Field:
		tickRemainderMs_ += ms;
		while (tickRemainderMs_ >= data_.config.tickMs && mode() == Mode::Field) {
			tickRemainderMs_ -= data_.config.tickMs;
			tickField(input);
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
		if (std::optional<EncounterStart> start = randomStep(world_, a, cell)) {
			startFieldBattle(*start);
		}
	}
}

void Session::enterArea(const std::string& areaId, const std::string& marker) {
	const Area* a = data_.area(areaId);
	if (!a) {
		emit(OutputKind::Warning, "unknown area '" + areaId + "'");
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
			autosaveReason_ = "area entry";
		}
	}
	emit(OutputKind::Notice, "You arrive at " + a->name + ".");
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
	if (!autosaveReason_.empty()) {
		autosave(autosaveReason_);
	}
}

std::string Session::walk(Facing direction) {
	if (mode() != Mode::Field) {
		return "you can only walk in the field";
	}
	if (!world_.playerControl) {
		return "you cannot move right now";
	}
	const Area& a = *area();
	const Cell start = world_.position;
	const Cell target = stepFrom(start, direction);
	const bool enemyThere = std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; });
	if (!a.walkable(target) && !enemyThere) {
		world_.facing = direction;
		return "the way is blocked";
	}
	const std::string startArea = world_.area;
	for (int guard = 0; guard < 1000; ++guard) {
		update(data_.config.tickMs, {true, direction});
		if (mode() != Mode::Field || world_.position != start || world_.area != startArea) {
			return "";
		}
		if (world_.graceMs == 0 && std::any_of(world_.enemies.begin(), world_.enemies.end(), [&](const VisibleEnemy& e) { return e.position == target; }) && !leaderBump(world_, target)) {
			return "something is in the way";
		}
	}
	return "the way is blocked";
}

std::string Session::interact() {
	if (mode() != Mode::Field) {
		return "there is nothing to interact with here";
	}
	if (!world_.playerControl) {
		return "you cannot act right now";
	}
	const Area& a = *area();
	const Cell target = stepFrom(world_.position, world_.facing);
	if (const Hook* hook = a.hookAt(target, HookTrigger::Interact)) {
		if (!hookHolds(*hook, world_)) {
			return "there is no answer";
		}
		startEvent(hook->event);
		return "";
	}
	if (const Chest* chest = a.chestAt(target)) {
		openChest(*chest);
		return "";
	}
	if (const Shop* shop = a.shopAt(target)) {
		modes_.transition(Mode::Shop);
		shopId_ = shop->id;
		emit(OutputKind::Notice, "The shop is open.");
		return "";
	}
	if (const RestPoint* rest = a.restAt(target)) {
		modes_.transition(Mode::Menu);
		innId_ = rest->id;
		emit(OutputKind::Notice, "A night's rest costs " + std::to_string(rest->cost) + ".");
		return "";
	}
	return "there is nothing there";
}

void Session::openChest(const Chest& chest) {
	if (world_.flag(chest.flag)) {
		emit(OutputKind::Notice, "The chest is empty.");
		return;
	}
	std::string found;
	for (const auto& [id, n] : chest.items) {
		giveThing(id, n);
		found += (found.empty() ? "" : ", ") + std::to_string(n) + " " + data_.db.item(id)->name;
	}
	for (const auto& [id, n] : chest.equipment) {
		giveThing(id, n);
		found += (found.empty() ? "" : ", ") + std::to_string(n) + " " + data_.db.equipmentPiece(id)->name;
	}
	if (chest.money > 0) {
		world_.money = std::min(data_.config.moneyCap, world_.money + chest.money);
		found += (found.empty() ? "" : ", ") + std::to_string(chest.money) + " money";
	}
	world_.flags[chest.flag] = true;
	emit(OutputKind::Notice, "Found " + (found.empty() ? std::string("nothing") : found) + ".");
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
		emit(OutputKind::Warning, "event '" + event + "' cannot start: " + interpreter_->error());
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
	if (battleAutosavePending_) {
		battleAutosavePending_ = false;
		autosave(kBeforeBattle);
	}
	if (status == events::Status::error) {
		emit(OutputKind::Warning, "event error: " + interpreter_->error());
		endEvent();
	} else if (status == events::Status::finished) {
		endEvent();
	}
}

void Session::endEvent() {
	dropEvent();
	if (eventChangedFlag_) {
		autosaveReason_ = "event end";
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
	battleAutosavePending_ = false;
}

bool Session::linePending() const {
	auto it = handles_.find(lineHandle_);
	return lineHandle_ && it != handles_.end() && !it->second.done;
}

std::string Session::lineText() const {
	auto it = handles_.find(lineHandle_);
	if (!linePending()) {
		return "";
	}
	const events::Line& line = it->second.line;
	std::string text = line.text.is_id ? "@" + line.text.value : line.text.value;
	text = events::render_names(text, data_.glossary, events::name_set_for(world_.settings.showShippedNames, "en"));
	if (line.speaker.empty()) {
		return text;
	}
	auto name = data_.config.speakers.find(line.speaker);
	return (name == data_.config.speakers.end() ? line.speaker : name->second) + ": " + text;
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

std::string Session::choose(int index) {
	const events::ChoiceRequest* request = choice();
	if (!request) {
		return "there is no choice to make";
	}
	const int count = request->yes_no ? 2 : static_cast<int>(request->options.size());
	if (index < 0 || index >= count) {
		return "pick an option from 0 to " + std::to_string(count - 1);
	}
	HandleState& state = handles_[choiceHandle_];
	state.done = true;
	state.result = index;
	pumpEvent(0);
	return "";
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

// Battles

void Session::startFieldBattle(const EncounterStart& start) {
	battleSource_ = start.enemyId ? BattleSource::Visible : BattleSource::Random;
	battleEnemyId_ = start.enemyId;
	battleHandle_ = 0;
	const std::string error = startBattle(start.encounter, start.opening, true, false);
	if (!error.empty()) {
		emit(OutputKind::Warning, "battle '" + start.encounter + "' could not start: " + error);
	}
}

std::string Session::startBattle(const std::string& encounter, rules::Opening opening, bool escapeAllowed, bool defeatHasBranch) {
	rules::BattleSetup setup;
	setup.party = activeParty();
	setup.inventory = world_.items;
	const std::string error = rules::useEncounter(setup, data_.db, encounter);
	if (!error.empty()) {
		return error;
	}
	setup.flags.escapeAllowed = setup.flags.escapeAllowed && escapeAllowed;
	setup.flags.defeatHasBranch = defeatHasBranch;
	setup.opening = opening;
	setup.difficulty = world_.settings.difficulty;
	setup.rewardMultiplier = world_.settings.rewardMultiplier;
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
	battleEncounter_ = encounter;
	battleRounds_ = 0;
	battleDefeatHandled_ = defeatHasBranch;
	const Boss* boss = data_.boss(encounter);
	battleIsBoss_ = boss != nullptr;
	pacing_.battleStarted();
	if (boss) {
		pacing_.bossStarted(boss->flag, encounter, averageLevelX100());
	}
	emit(OutputKind::Notice, std::string("Battle! (") + rules::toString(opening) + ")");
	return "";
}

std::string Session::setCommand(int actor, const rules::Command& command) {
	if (!battle_) {
		return "there is no battle";
	}
	return battle_->setCommand(actor, command);
}

std::string Session::applyMacro(const std::string& name, std::vector<rules::MacroStepResult>* results) {
	if (!battle_) {
		return "there is no battle";
	}
	for (const rules::Macro& m : world_.macros) {
		if (m.name == name) {
			std::vector<rules::MacroStepResult> r = battle_->applyMacro(m);
			if (results) {
				*results = std::move(r);
			}
			return "";
		}
	}
	return "no macro named '" + name + "'";
}

std::vector<rules::Event> Session::resolveRound() {
	if (!battle_) {
		return {};
	}
	std::vector<rules::Event> events = battle_->resolveRound();
	++battleRounds_;
	addTime(data_.config.roundMs);
	if (battle_->outcome() != rules::Outcome::Ongoing) {
		finishBattle();
	}
	return events;
}

std::vector<rules::Event> Session::autoRound() {
	if (!battle_) {
		return {};
	}
	bool usedMacro = false;
	if (!world_.autoMacro.empty()) {
		usedMacro = applyMacro(world_.autoMacro).empty();
	}
	if (!usedMacro && battle_->sideActsNextRound(rules::Side::Party)) {
		for (int i = 0; i < battle_->partyCount(); ++i) {
			if (battle_->combatant(i).alive()) {
				battle_->setCommand(i, rules::autoCommand(*battle_, i));
			}
		}
	}
	return resolveRound();
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
	battle_.reset();
	for (const rules::PartyMember& m : state) {
		if (rules::PartyMember* member = world_.member(m.characterId)) {
			*member = m;
		}
	}
	int experience = 0;
	int money = 0;
	if (outcome == rules::Outcome::Victory) {
		experience = rewards.experience;
		money = rewards.currency;
		world_.money = std::min(data_.config.moneyCap, world_.money + money);
		emit(OutputKind::Notice, "Victory. " + std::to_string(experience) + " experience, " + std::to_string(money) + " money.");
		for (std::size_t i = 0; i < state.size() && i < rewards.memberExperience.size(); ++i) {
			rules::PartyMember* member = world_.member(state[i].characterId);
			const rules::LevelUp up = rules::grantExperience(*member, data_.db, rewards.memberExperience[i]);
			if (up.levelsGained > 0) {
				std::string line = memberName(member->characterId) + " reached level " + std::to_string(member->level);
				for (std::size_t k = 0; k < up.learned.size(); ++k) {
					const rules::AbilityDef* ability = data_.db.ability(up.learned[k]);
					line += (k == 0 ? " and learned " : ", ") + (ability ? ability->name : up.learned[k]);
				}
				emit(OutputKind::Notice, line + ".");
			}
		}
	} else if (outcome == rules::Outcome::Escaped) {
		emit(OutputKind::Notice, "The party got away.");
	} else {
		emit(OutputKind::Notice, "The party has fallen.");
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
	emit(OutputKind::Notice, "Game over.");
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
	}
	if (!store_) {
		emit(OutputKind::Autosave, "Autosave point (" + reason + ").");
		return;
	}
	const std::string slot = store_->nextAutosaveSlot();
	std::string error;
	if (!store_->write(slot, serializeSave(save), error)) {
		emit(OutputKind::Warning, "autosave failed, will retry at the next autosave point: " + error);
		return;
	}
	if (reason == kBeforeBattle) {
		pendingRetrySlot_ = slot;
	}
	emit(OutputKind::Autosave, "Autosaved to " + slot + " (" + reason + ").");
}

std::string Session::saveManual(const std::string& slot) {
	if (mode() != Mode::Menu || modes_.returnMode() != Mode::Field || !innId_.empty()) {
		return "saving is only allowed from the field menu";
	}
	if (interpreter_) {
		return "you cannot save while an event runs";
	}
	if (!store_) {
		return "saving is disabled";
	}
	if (slot.compare(0, 7, "manual_") != 0 || !store_->validSlot(slot)) {
		return "no manual save slot '" + slot + "'";
	}
	SaveFile save = makeSave(false);
	if (!options_.clock) {
		save.writeTime = ++clockCounter_;
	}
	std::string error;
	if (!store_->write(slot, serializeSave(save), error)) {
		return error;
	}
	emit(OutputKind::Notice, "Saved to " + slot + ".");
	return "";
}

// Menus

std::string Session::openMenu() {
	if (mode() != Mode::Field) {
		return "the menu opens from the field";
	}
	if (!modes_.transition(Mode::Menu)) {
		return "the menu cannot open now";
	}
	innId_.clear();
	shopId_.clear();
	return "";
}

std::string Session::closeMenu() {
	if (mode() != Mode::Menu && mode() != Mode::Shop) {
		return "no menu is open";
	}
	modes_.transition(modes_.returnMode());
	innId_.clear();
	shopId_.clear();
	settleField();
	if (mode() == Mode::Event) {
		pumpEvent(0);
	}
	return "";
}

const Shop* Session::shop() const {
	return mode() == Mode::Shop ? area()->shop(shopId_) : nullptr;
}

const RestPoint* Session::inn() const {
	return mode() == Mode::Menu && !innId_.empty() ? area()->rest(innId_) : nullptr;
}

std::string Session::buy(const std::string& id, int count) {
	const Shop* s = shop();
	if (!s) {
		return "no shop is open";
	}
	if (std::find(s->stock.begin(), s->stock.end(), id) == s->stock.end()) {
		return "this shop does not sell '" + id + "'";
	}
	if (count < 1) {
		return "buy at least one";
	}
	const std::int64_t total = static_cast<std::int64_t>(data_.price(id)) * count;
	if (total > world_.money) {
		return "not enough money";
	}
	const std::map<std::string, int>& bag = data_.db.item(id) ? world_.items : world_.equipmentBag;
	auto held = bag.find(id);
	if ((held == bag.end() ? 0 : held->second) + count > data_.config.itemCap) {
		return "you can carry at most " + std::to_string(data_.config.itemCap);
	}
	world_.money -= static_cast<int>(total);
	giveThing(id, count);
	emit(OutputKind::Notice, "Bought " + std::to_string(count) + " " + id + " for " + std::to_string(total) + ".");
	return "";
}

std::string Session::sell(const std::string& id, int count) {
	const Shop* s = shop();
	if (!s) {
		return "no shop is open";
	}
	if (data_.price(id) < 0) {
		return "the shop will not buy '" + id + "'";
	}
	if (count < 1) {
		return "sell at least one";
	}
	std::map<std::string, int>& bag = data_.db.item(id) ? world_.items : world_.equipmentBag;
	auto held = bag.find(id);
	if (held == bag.end() || held->second < count) {
		return "you do not have that many (worn equipment cannot be sold)";
	}
	const std::int64_t value = static_cast<std::int64_t>(data_.price(id)) * s->sellPercent / 100 * count;
	held->second -= count;
	if (held->second == 0) {
		bag.erase(held);
	}
	world_.money = static_cast<int>(std::min<std::int64_t>(data_.config.moneyCap, world_.money + value));
	emit(OutputKind::Notice, "Sold " + std::to_string(count) + " " + id + " for " + std::to_string(value) + ".");
	return "";
}

std::string Session::rest() {
	const RestPoint* r = inn();
	if (!r) {
		return "there is nowhere to rest";
	}
	if (world_.money < r->cost) {
		return "not enough money";
	}
	world_.money -= r->cost;
	for (rules::PartyMember& m : world_.roster) {
		rules::rest(m, data_.db);
	}
	emit(OutputKind::Notice, "The party rests and recovers.");
	modes_.transition(modes_.returnMode());
	innId_.clear();
	if (mode() == Mode::Field) {
		autosaveReason_ = "after rest";
		settleField();
	}
	return "";
}

std::string Session::useItem(const std::string& item, const std::string& member) {
	if (mode() != Mode::Menu || !innId_.empty()) {
		return "items are used from the field menu";
	}
	const rules::ItemDef* def = data_.db.item(item);
	auto held = world_.items.find(item);
	if (!def || held == world_.items.end()) {
		return "you have no '" + item + "'";
	}
	std::vector<rules::PartyMember*> targets;
	if (def->targeting == rules::Targeting::OneAlly || def->targeting == rules::Targeting::Self) {
		if (std::find(world_.lineup.begin(), world_.lineup.end(), member) == world_.lineup.end()) {
			return "'" + member + "' is not in the party";
		}
		targets.push_back(world_.member(member));
	} else if (def->targeting == rules::Targeting::AllAllies) {
		for (const std::string& id : world_.lineup) {
			targets.push_back(world_.member(id));
		}
	} else {
		return def->name + " can only be used in battle";
	}
	// Core has no out-of-battle item rule, so the battle formulas are applied here with flat item power.
	bool changed = false;
	for (rules::PartyMember* m : targets) {
		const int maxHp = rules::memberStats(data_.db, *m).maxHp;
		for (const rules::Effect& e : def->effects) {
			if (e.kind == rules::EffectKind::Heal && m->hp > 0 && m->hp < maxHp) {
				m->hp = std::min(maxHp, m->hp + rules::formulas::healAmount(e.power, 0));
				changed = true;
			} else if (e.kind == rules::EffectKind::Revive && m->hp <= 0) {
				m->hp = rules::formulas::reviveHp(maxHp, e.power);
				changed = true;
			}
		}
	}
	if (!changed) {
		return def->name + " would have no effect";
	}
	if (--held->second == 0) {
		world_.items.erase(held);
	}
	emit(OutputKind::Notice, "Used " + def->name + ".");
	return "";
}

std::string Session::equip(const std::string& member, const std::string& equipment) {
	if (mode() != Mode::Menu || !innId_.empty()) {
		return "equipment is changed from the field menu";
	}
	rules::PartyMember* m = world_.member(member);
	if (!m) {
		return "'" + member + "' is not in the roster";
	}
	auto held = world_.equipmentBag.find(equipment);
	if (held == world_.equipmentBag.end()) {
		return "you have no '" + equipment + "'";
	}
	const rules::EquipmentDef* piece = data_.db.equipmentPiece(equipment);
	auto worn = m->equipment.find(piece->slot);
	const std::string previous = worn == m->equipment.end() ? "" : worn->second;
	const std::string error = rules::equip(*m, data_.db, equipment);
	if (!error.empty()) {
		return error;
	}
	if (--held->second == 0) {
		world_.equipmentBag.erase(held);
	}
	if (!previous.empty()) {
		++world_.equipmentBag[previous];
	}
	emit(OutputKind::Notice, memberName(member) + " equips " + piece->name + ".");
	return "";
}

std::string Session::unequip(const std::string& member, rules::EquipSlot slot) {
	if (mode() != Mode::Menu || !innId_.empty()) {
		return "equipment is changed from the field menu";
	}
	rules::PartyMember* m = world_.member(member);
	if (!m) {
		return "'" + member + "' is not in the roster";
	}
	auto worn = m->equipment.find(slot);
	if (worn == m->equipment.end()) {
		return "nothing is worn there";
	}
	const std::string piece = worn->second;
	rules::unequip(*m, data_.db, slot);
	++world_.equipmentBag[piece];
	return "";
}

std::string Session::setSettings(const Settings& settings) {
	if (mode() == Mode::Battle) {
		return "settings change outside battle";
	}
	if (!data_.db.difficulty(settings.difficulty)) {
		return "unknown difficulty '" + settings.difficulty + "'";
	}
	world_.settings = settings;
	return "";
}

}
