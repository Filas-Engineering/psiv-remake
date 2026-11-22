#include "events/interpreter.h"

#include <algorithm>

namespace events {

namespace {

// Guards against scripts that loop without ever waiting.
const long step_budget = 100000;
const std::size_t max_call_depth = 32;
const double time_epsilon = 1e-9;

bool waits_on_player(Op op) {
	return op == Op::choice || op == Op::battle || op == Op::shop || op == Op::save_prompt || op == Op::name_entry || op == Op::credits;
}

const char* service_method(Op op) {
	switch (op) {
	case Op::shop:
		return "open_shop";
	case Op::rest:
		return "rest_party";
	case Op::save_prompt:
		return "open_save_prompt";
	case Op::credits:
		return "play_credits";
	default:
		return "open_name_entry";
	}
}

}

Interpreter::Interpreter(const Program& program, Host& host) : program_(program), host_(host) {
	for (const Declaration& d : program.declarations) {
		if (d.local) {
			local_initial_[d.name] = d.initial;
		}
	}
}

bool Interpreter::start(const std::string& event) {
	int index = program_.find_event(event);
	if (index < 0) {
		status_ = Status::error;
		error_ = "unknown event '" + event + "'";
		return false;
	}
	status_ = Status::running;
	error_.clear();
	skip_depth_ = -1;
	frames_ = {{index, -1}};
	main_ = Thread();
	main_.pc = program_.events[index].entry;
	children_.clear();
	background_.clear();
	presentation_ = PresentationState();
	dirty_.clear();
	rebuild_pending_ = false;
	locals_ = local_initial_;
	start_world_ = host_.snapshot_world();
	restarted_ = false;
	return true;
}

Status Interpreter::step(double delta_seconds) {
	if (status_ != Status::running) {
		return status_;
	}
	if (main_.wait == WaitKind::timer) {
		main_.timer -= delta_seconds;
	}
	for (Thread& child : children_) {
		if (child.alive && child.wait == WaitKind::timer) {
			child.timer -= delta_seconds;
		}
	}
	if (rebuild_pending_) {
		rebuild_pending_ = false;
		rebuild(true);
	}
	// Entries without a handle come from restore() and are started again.
	for (Background& b : background_) {
		if (b.handle == 0 && b.pc >= 0) {
			b.handle = host_.start_action(program_.code[b.pc].action);
		}
	}
	prune_background();
	budget_ = step_budget;
	bool progress = true;
	while (progress && status_ == Status::running) {
		progress = run_thread(main_);
		for (std::size_t i = 0; i < children_.size() && status_ == Status::running; ++i) {
			if (children_[i].alive && run_thread(children_[i])) {
				progress = true;
			}
		}
	}
	return status_;
}

bool Interpreter::run_thread(Thread& thread) {
	bool progressed = false;
	while (status_ == Status::running && thread.alive) {
		if (thread.wait != WaitKind::none) {
			if (!resolve_wait(thread)) {
				return progressed;
			}
			progressed = true;
			continue;
		}
		if (--budget_ < 0) {
			fail(thread.pc, "the event ran " + std::to_string(step_budget) + " commands in one step without waiting; is there a loop without a wait?");
			return progressed;
		}
		execute(thread);
		progressed = true;
	}
	return progressed;
}

bool Interpreter::resolve_wait(Thread& thread) {
	const Instruction& ins = program_.code[thread.pc];
	switch (thread.wait) {
	case WaitKind::none:
		return true;
	case WaitKind::timer:
		if (thread.timer > time_epsilon) {
			return false;
		}
		thread.wait = WaitKind::none;
		thread.pc++;
		return true;
	case WaitKind::handle: {
		if (!host_.is_done(thread.handle)) {
			return false;
		}
		int next = thread.pc + 1;
		if (ins.op == Op::choice || ins.op == Op::battle) {
			int picked = host_.result(thread.handle);
			std::string refused;
			if (picked < 0 || picked >= static_cast<int>(ins.targets.size())) {
				refused = "the host returned result " + std::to_string(picked) + ", but only " + std::to_string(ins.targets.size()) + " are possible";
			} else if (ins.op == Op::battle && picked == static_cast<int>(BattleResult::lose) && !ins.battle.defeat_handled) {
				refused = "the host reported a defeat in battle '" + ins.battle.encounter + "', which has no 'lose' section; the host must run the game-over flow instead";
			} else if (ins.op == Op::battle && picked == static_cast<int>(BattleResult::flee) && !ins.battle.escape_allowed) {
				refused = "the host reported an escape from battle '" + ins.battle.encounter + "', which has no 'flee' section, so escape was not allowed";
			}
			if (!refused.empty()) {
				host_.release(thread.handle);
				thread.handle = 0;
				thread.wait = WaitKind::none;
				fail(thread.pc, refused);
				return false;
			}
			// Written in the same step that takes the branch, so no save or
			// skip can fall between the answer and its record (EVT-006).
			if (ins.op == Op::choice && !ins.name.empty()) {
				if (ins.choice.yes_no) {
					write_flag(ins.name, picked == 0);
				} else {
					write_var(ins.name, picked + 1);
				}
			}
			next = ins.targets[picked];
		}
		host_.release(thread.handle);
		thread.handle = 0;
		thread.wait = WaitKind::none;
		thread.pc = next;
		return true;
	}
	case WaitKind::children:
		for (const Thread& child : children_) {
			if (child.alive) {
				return false;
			}
		}
		children_.clear();
		thread.wait = WaitKind::none;
		thread.pc = ins.target;
		return true;
	case WaitKind::background:
		prune_background();
		if (!background_.empty()) {
			return false;
		}
		thread.wait = WaitKind::none;
		if (ins.op == Op::ret) {
			finish();
		} else {
			thread.pc++;
		}
		return true;
	}
	return false;
}

void Interpreter::start_action(Thread& thread, const Instruction& ins, const Action& action) {
	Handle handle = host_.start_action(action);
	if (ins.wait) {
		thread.handle = handle;
		thread.wait = WaitKind::handle;
		return;
	}
	background_.push_back({handle, thread.pc, static_cast<int>(frames_.size()) - 1});
	thread.pc++;
}

void Interpreter::execute(Thread& thread) {
	const Instruction& ins = program_.code[thread.pc];
	const bool skipping = skip_depth_ >= 0;
	switch (ins.op) {
	case Op::say:
		if (skipping) {
			host_.log_skipped_line(ins.dialogue);
			break;
		}
		thread.handle = host_.show_line(ins.dialogue);
		thread.wait = WaitKind::handle;
		return;
	case Op::choice:
		stop_skipping();
		thread.handle = host_.present_choice(ins.choice);
		thread.wait = WaitKind::handle;
		return;
	case Op::battle:
		stop_skipping();
		thread.handle = host_.start_battle(ins.battle);
		thread.wait = WaitKind::handle;
		return;
	case Op::shop:
	case Op::rest:
	case Op::save_prompt:
	case Op::name_entry:
	case Op::credits: {
		if (ins.op != Op::rest) {
			stop_skipping();
		}
		Handle handle = 0;
		if (ins.op == Op::shop) {
			handle = host_.open_shop(ins.name);
		} else if (ins.op == Op::rest) {
			handle = host_.rest_party({ins.name, skipping});
		} else if (ins.op == Op::save_prompt) {
			handle = host_.open_save_prompt();
		} else if (ins.op == Op::credits) {
			handle = host_.play_credits(ins.name);
		} else {
			handle = host_.open_name_entry(ins.name);
		}
		if (handle == 0) {
			fail(thread.pc, std::string("the host does not support '") + op_name(ins.op) + "': Host::" + service_method(ins.op) + " returned no handle");
			return;
		}
		thread.handle = handle;
		thread.wait = WaitKind::handle;
		return;
	}
	case Op::action:
		if (ins.action.type == ActionType::warp) {
			// The new map places its own actors and cameras.
			presentation_.actors.clear();
			presentation_.camera.clear();
			presentation_.camera_to = false;
			presentation_.camera_reset = false;
			for (auto it = dirty_.begin(); it != dirty_.end();) {
				it = it->rfind("actor:", 0) == 0 || *it == "camera" ? dirty_.erase(it) : std::next(it);
			}
			Action action = ins.action;
			action.instant = skipping;
			start_action(thread, ins, action);
			return;
		}
		record(ins.action);
		if (skipping) {
			mark_dirty(ins.action);
			break;
		}
		start_action(thread, ins, ins.action);
		return;
	case Op::wait:
		if (ins.seconds > 0 && !skipping) {
			thread.timer = ins.seconds;
			thread.wait = WaitKind::timer;
			return;
		}
		break;
	case Op::sync:
		thread.wait = WaitKind::background;
		return;
	case Op::fork:
		for (int start : ins.targets) {
			Thread child;
			child.pc = start;
			children_.push_back(child);
		}
		thread.wait = WaitKind::children;
		return;
	case Op::end_branch:
		thread.alive = false;
		// EVT-008: the first branch of a race to finish stops the others
		// where they wait, so they apply nothing further.
		if (in_race()) {
			for (Thread& other : children_) {
				if (other.alive) {
					if (other.wait == WaitKind::handle) {
						host_.cancel(other.handle);
					}
					other.alive = false;
				}
			}
		}
		return;
	case Op::jump:
		thread.pc = ins.target;
		return;
	case Op::jump_unless:
		thread.pc = evaluate(ins.condition) ? thread.pc + 1 : ins.target;
		return;
	case Op::call:
		if (frames_.size() >= max_call_depth) {
			fail(thread.pc, "calls are nested more than " + std::to_string(max_call_depth) + " deep; is an event calling itself?");
			return;
		}
		frames_.push_back({ins.target, thread.pc + 1});
		thread.pc = program_.events[ins.target].entry;
		return;
	case Op::ret: {
		Frame top = frames_.back();
		frames_.pop_back();
		if (skip_depth_ >= static_cast<int>(frames_.size())) {
			stop_skipping();
		}
		if (frames_.empty()) {
			// Let actors started with `nowait` arrive before the event ends.
			thread.wait = WaitKind::background;
			return;
		}
		thread.pc = top.return_pc;
		return;
	}
	case Op::set_flag:
		write_flag(ins.name, true);
		break;
	case Op::clear_flag:
		write_flag(ins.name, false);
		break;
	case Op::var_set:
		write_var(ins.name, ins.number);
		break;
	case Op::var_add:
		write_var(ins.name, read_var(ins.name) + ins.number);
		break;
	case Op::give:
		host_.give_item(ins.name, static_cast<int>(ins.number));
		break;
	case Op::vehicle_give:
		host_.give_vehicle(ins.name, ins.action.subject, ins.action.target);
		break;
	case Op::vehicle_place:
		host_.place_vehicle(ins.name, ins.action.subject, ins.action.target);
		break;
	case Op::vehicle_remove:
		host_.remove_vehicle(ins.name);
		break;
	case Op::learn:
		host_.teach_ability(ins.name, ins.action.subject);
		break;
	case Op::follower:
		host_.set_follower(ins.name);
		break;
	case Op::money:
		host_.change_money(ins.number);
		break;
	case Op::equip:
		host_.equip_piece(ins.name, ins.action.subject);
		break;
	case Op::unequip:
		host_.unequip_piece(ins.name, ins.action.subject);
		break;
	case Op::area_music:
		host_.set_area_music(ins.name);
		break;
	case Op::remove_actor:
		host_.remove_actor(ins.name, ins.number == 1);
		break;
	case Op::take:
		host_.take_item(ins.name, static_cast<int>(ins.number));
		break;
	case Op::party_add:
		if (ins.number == 1) {
			host_.add_party_member_silently(ins.name);
		} else {
			host_.add_party_member(ins.name);
		}
		break;
	case Op::party_remove:
		if (ins.number == 1) {
			host_.remove_party_member_silently(ins.name);
		} else {
			host_.remove_party_member(ins.name);
		}
		break;
	case Op::party_order:
		if (!host_.order_party(ins.names)) {
			std::string named;
			for (const std::string& member : ins.names) {
				named += " " + member;
			}
			fail(thread.pc, "'party order" + named + "' names a member who is not in the lineup");
			return;
		}
		break;
	case Op::party_save:
		host_.save_party(ins.name);
		break;
	case Op::party_restore:
		if (!host_.restore_party(ins.name)) {
			fail(thread.pc, "there is no party snapshot '" + ins.name + "'; 'party save " + ins.name + "' makes one");
			return;
		}
		break;
	case Op::party_swap:
		host_.swap_party_member(ins.swap);
		break;
	case Op::spawn:
		host_.spawn_event(ins.name);
		break;
	case Op::end_game:
		host_.end_game();
		cancel_all();
		skip_depth_ = -1;
		status_ = Status::finished;
		return;
	case Op::picture:
		if (ins.number == 0) {
			host_.show_picture(ins.name);
		} else if (ins.number == 1) {
			host_.hide_picture(ins.name);
		} else {
			host_.clear_pictures();
		}
		break;
	case Op::actor_look:
		host_.set_actor_look(ins.name, ins.action.subject == "enemy" ? ins.action.target : std::string(),
			ins.action.subject == "enemy" ? std::string() : ins.action.target);
		break;
	case Op::lock_player:
		host_.set_player_control(false);
		break;
	case Op::unlock_player:
		host_.set_player_control(true);
		break;
	}
	thread.pc++;
}

bool Interpreter::read_flag(const std::string& name) {
	auto local = locals_.find(name);
	return local != locals_.end() ? local->second != 0 : host_.get_flag(name);
}

void Interpreter::write_flag(const std::string& name, bool value) {
	auto local = locals_.find(name);
	if (local != locals_.end()) {
		local->second = value ? 1 : 0;
	} else {
		host_.set_flag(name, value);
	}
}

std::int64_t Interpreter::read_var(const std::string& name) {
	auto local = locals_.find(name);
	return local != locals_.end() ? local->second : host_.get_var(name);
}

void Interpreter::write_var(const std::string& name, std::int64_t value) {
	auto local = locals_.find(name);
	if (local != locals_.end()) {
		local->second = value;
	} else {
		host_.set_var(name, value);
	}
}

bool Interpreter::evaluate(const Condition& condition) {
	for (const Term& term : condition.terms) {
		bool value;
		if (term.test == TermTest::owns) {
			value = host_.has_vehicle(term.name);
		} else if (term.test == TermTest::aboard) {
			value = host_.aboard_vehicle(term.name);
		} else if (term.test == TermTest::member) {
			value = host_.in_party(term.name);
		} else if (term.test == TermTest::holds) {
			value = host_.item_count(term.name) > 0;
		} else if (term.test == TermTest::leader || term.test == TermTest::music || term.test == TermTest::area || term.test == TermTest::tile) {
			static const char* const words[] = {"leader", "music", "area", "tile"};
			value = host_.game_state(words[static_cast<int>(term.test) - static_cast<int>(TermTest::leader)], term.name);
		} else if (term.is_flag) {
			value = read_flag(term.name);
		} else {
			std::int64_t v = term.test == TermTest::money ? host_.money() : read_var(term.name);
			switch (term.compare) {
			case Compare::eq:
				value = v == term.value;
				break;
			case Compare::ne:
				value = v != term.value;
				break;
			case Compare::lt:
				value = v < term.value;
				break;
			case Compare::le:
				value = v <= term.value;
				break;
			case Compare::gt:
				value = v > term.value;
				break;
			default:
				value = v >= term.value;
				break;
			}
		}
		if (term.negate) {
			value = !value;
		}
		if (condition.join == Join::all && !value) {
			return false;
		}
		if (condition.join == Join::any && value) {
			return true;
		}
	}
	return condition.join == Join::all;
}

void Interpreter::prune_background() {
	std::vector<Background> running;
	for (const Background& b : background_) {
		if (b.handle != 0 && host_.is_done(b.handle)) {
			host_.release(b.handle);
		} else {
			running.push_back(b);
		}
	}
	background_.swap(running);
}

bool Interpreter::waiting_on_player() const {
	return main_.wait == WaitKind::handle && waits_on_player(program_.code[main_.pc].op);
}

bool Interpreter::in_race() const {
	return main_.wait == WaitKind::children && program_.code[main_.pc].op == Op::fork && program_.code[main_.pc].number == 1;
}

bool Interpreter::can_skip() const {
	return status_ == Status::running && skip_depth_ < 0 && !frames_.empty() && program_.events[frames_.back().event].skippable && !waiting_on_player();
}

bool Interpreter::skip() {
	if (!can_skip()) {
		return false;
	}
	int depth = static_cast<int>(frames_.size()) - 1;
	while (depth > 0 && program_.events[frames_[depth - 1].event].skippable) {
		--depth;
	}
	skip_depth_ = depth;
	fast_forward(main_);
	for (Thread& child : children_) {
		if (child.alive) {
			fast_forward(child);
		}
	}
	// Background presentation from the skipped events completes at once; a
	// warp keeps loading, and the callers' own actions are not theirs to stop.
	std::vector<Background> kept;
	for (const Background& b : background_) {
		if (b.pc >= 0 && b.depth >= depth && presentation_only(program_.code[b.pc].action.type)) {
			if (b.handle != 0) {
				host_.cancel(b.handle);
			}
			mark_dirty(program_.code[b.pc].action);
		} else {
			kept.push_back(b);
		}
	}
	background_.swap(kept);
	return true;
}

void Interpreter::fast_forward(Thread& thread) {
	if (thread.wait == WaitKind::timer) {
		thread.wait = WaitKind::none;
		thread.timer = 0;
		thread.pc++;
		return;
	}
	if (thread.wait != WaitKind::handle) {
		return;
	}
	const Instruction& ins = program_.code[thread.pc];
	const bool presentation = ins.op == Op::action && presentation_only(ins.action.type);
	if (ins.op != Op::say && !presentation) {
		return;
	}
	host_.cancel(thread.handle);
	thread.handle = 0;
	thread.wait = WaitKind::none;
	thread.pc++;
	if (presentation) {
		mark_dirty(ins.action);
	}
}

void Interpreter::stop_skipping() {
	if (skip_depth_ < 0) {
		return;
	}
	skip_depth_ = -1;
	rebuild(false);
	dirty_.clear();
}

ActorState& Interpreter::actor(const std::string& name) {
	auto it = std::lower_bound(presentation_.actors.begin(), presentation_.actors.end(), name,
		[](const ActorState& a, const std::string& n) { return a.actor < n; });
	if (it == presentation_.actors.end() || it->actor != name) {
		ActorState fresh;
		fresh.actor = name;
		it = presentation_.actors.insert(it, fresh);
	}
	return *it;
}

void Interpreter::record(const Action& action) {
	PresentationState& p = presentation_;
	switch (action.type) {
	case ActionType::move:
	case ActionType::place_actor: {
		ActorState& a = actor(action.subject);
		a.has_position = action.has_position;
		a.marker = action.has_position ? std::string() : action.target;
		a.position = action.position;
		a.via = action.waypoints;
		a.pathfind = action.pathfind;
		a.placed = action.type == ActionType::place_actor;
		a.facing = action.facing;
		break;
	}
	case ActionType::face:
		actor(action.subject).facing = action.target;
		break;
	case ActionType::animate: {
		ActorState& a = actor(action.subject);
		const StandardAnimation* standard = standard_animation(action.target);
		if (standard && standard->end != AnimationEnd::none) {
			// The pose of a hidden actor names the animation that hid it, which the rebuild plays at once.
			a.visible = standard->end == AnimationEnd::shown ? 1 : 0;
			a.pose = standard->end == AnimationEnd::hidden ? action.target : std::string();
		} else {
			a.pose = action.loop ? action.target : std::string();
		}
		break;
	}
	case ActionType::show_actor:
	case ActionType::hide_actor: {
		ActorState& a = actor(action.subject);
		a.visible = action.type == ActionType::show_actor ? 1 : 0;
		const StandardAnimation* standard = standard_animation(a.pose);
		if (standard && standard->end == AnimationEnd::hidden) {
			a.pose.clear();
		}
		// Hidden with a standard animation that hides: rebuilt as that animation, as an `anim` of it is.
		const StandardAnimation* transition = standard_animation(action.target);
		if (action.type == ActionType::hide_actor && transition && transition->end == AnimationEnd::hidden) {
			a.pose = action.target;
		}
		break;
	}
	case ActionType::panel_show: {
		auto it = std::find_if(p.panels.begin(), p.panels.end(), [&](const PanelState& s) { return s.panel == action.subject; });
		if (it == p.panels.end()) {
			p.panels.push_back({action.subject, action.target, action.slot});
		} else {
			it->camera = action.target;
			it->slot = action.slot;
		}
		break;
	}
	case ActionType::panel_hide:
		p.panels.erase(std::remove_if(p.panels.begin(), p.panels.end(), [&](const PanelState& s) { return s.panel == action.subject; }), p.panels.end());
		break;
	case ActionType::panel_clear:
		p.panels.clear();
		break;
	case ActionType::camera_cut:
	case ActionType::camera_move:
		p.camera = action.subject;
		p.camera_to = false;
		p.camera_reset = false;
		break;
	case ActionType::camera_to:
	case ActionType::camera_reset:
		p.camera.clear();
		p.camera_to = action.type == ActionType::camera_to;
		p.camera_reset = !p.camera_to;
		p.camera_marker = p.camera_to && !action.has_position ? action.target : std::string();
		p.camera_has_position = p.camera_to && action.has_position;
		p.camera_position = p.camera_has_position ? action.position : Vec3();
		break;
	case ActionType::fade_in:
	case ActionType::fade_out:
		p.fade_set = true;
		p.faded_out = action.type == ActionType::fade_out;
		break;
	case ActionType::music_play:
	case ActionType::music_stop:
	case ActionType::music_fade:
		p.music_set = true;
		p.music = action.type == ActionType::music_play ? action.subject : std::string();
		break;
	case ActionType::ambient_play:
	case ActionType::ambient_stop:
		p.ambient_set = true;
		p.ambient = action.type == ActionType::ambient_play ? action.subject : std::string();
		break;
	case ActionType::shake:
	case ActionType::sound:
	case ActionType::warp:
		break;
	}
}

void Interpreter::mark_dirty(const Action& action) {
	switch (action.type) {
	case ActionType::move:
	case ActionType::place_actor:
	case ActionType::face:
	case ActionType::animate:
	case ActionType::show_actor:
	case ActionType::hide_actor:
		dirty_.insert("actor:" + action.subject);
		break;
	case ActionType::panel_show:
	case ActionType::panel_hide:
	case ActionType::panel_clear:
		dirty_.insert("panels");
		break;
	case ActionType::camera_cut:
	case ActionType::camera_move:
	case ActionType::camera_to:
	case ActionType::camera_reset:
		dirty_.insert("camera");
		break;
	case ActionType::fade_in:
	case ActionType::fade_out:
		dirty_.insert("fade");
		break;
	case ActionType::music_play:
	case ActionType::music_stop:
	case ActionType::music_fade:
		dirty_.insert("music");
		break;
	case ActionType::ambient_play:
	case ActionType::ambient_stop:
		dirty_.insert("ambient");
		break;
	case ActionType::shake:
	case ActionType::sound:
	case ActionType::warp:
		break;
	}
}

void Interpreter::issue_instant(Action action) {
	action.instant = true;
	background_.push_back({host_.start_action(action), -1, 0});
}

void Interpreter::rebuild(bool everything) {
	const PresentationState& p = presentation_;
	auto wanted = [&](const std::string& key) { return everything || dirty_.count(key) > 0; };
	auto make = [](ActionType type, const std::string& subject = std::string(), const std::string& target = std::string()) {
		Action a;
		a.type = type;
		a.subject = subject;
		a.target = target;
		return a;
	};
	if (p.music_set && wanted("music") && (!p.music.empty() || !everything)) {
		issue_instant(p.music.empty() ? make(ActionType::music_stop) : make(ActionType::music_play, p.music));
	}
	if (p.ambient_set && wanted("ambient") && (!p.ambient.empty() || !everything)) {
		issue_instant(p.ambient.empty() ? make(ActionType::ambient_stop) : make(ActionType::ambient_play, p.ambient));
	}
	if (!p.camera.empty() && wanted("camera")) {
		issue_instant(make(ActionType::camera_cut, p.camera));
	} else if (p.camera_to && wanted("camera")) {
		Action to = make(ActionType::camera_to, std::string(), p.camera_marker);
		to.has_position = p.camera_has_position;
		to.position = p.camera_position;
		issue_instant(to);
	} else if (p.camera_reset && wanted("camera")) {
		issue_instant(make(ActionType::camera_reset));
	}
	for (const ActorState& a : p.actors) {
		if (!wanted("actor:" + a.actor)) {
			continue;
		}
		const StandardAnimation* vanished = a.visible == 0 ? standard_animation(a.pose) : nullptr;
		if (a.visible >= 0 && !vanished) {
			issue_instant(make(a.visible ? ActionType::show_actor : ActionType::hide_actor, a.actor));
		}
		if (a.has_position || !a.marker.empty()) {
			Action move = make(a.placed ? ActionType::place_actor : ActionType::move, a.actor, a.marker);
			move.has_position = a.has_position;
			move.position = a.position;
			move.waypoints = a.via;
			move.pathfind = a.pathfind;
			issue_instant(move);
		}
		if (!a.facing.empty()) {
			issue_instant(make(ActionType::face, a.actor, a.facing));
		}
		if (vanished) {
			Action gone = make(ActionType::animate, a.actor, a.pose);
			gone.seconds = vanished->seconds;
			issue_instant(gone);
		} else if (!a.pose.empty()) {
			Action pose = make(ActionType::animate, a.actor, a.pose);
			pose.loop = true;
			issue_instant(pose);
		}
	}
	if (wanted("panels")) {
		if (!everything) {
			issue_instant(make(ActionType::panel_clear));
		}
		for (const PanelState& panel : p.panels) {
			Action show = make(ActionType::panel_show, panel.panel, panel.camera);
			show.slot = panel.slot;
			issue_instant(show);
		}
	}
	if (p.fade_set && wanted("fade") && (p.faded_out || !everything)) {
		issue_instant(make(p.faded_out ? ActionType::fade_out : ActionType::fade_in));
	}
}

void Interpreter::cancel_all() {
	if (main_.wait == WaitKind::handle) {
		host_.cancel(main_.handle);
	}
	main_.wait = WaitKind::none;
	for (const Thread& child : children_) {
		if (child.alive && child.wait == WaitKind::handle) {
			host_.cancel(child.handle);
		}
	}
	children_.clear();
	for (const Background& b : background_) {
		if (b.handle != 0) {
			host_.cancel(b.handle);
		}
	}
	background_.clear();
}

void Interpreter::cancel() {
	if (status_ != Status::running) {
		return;
	}
	cancel_all();
	skip_depth_ = -1;
	status_ = Status::idle;
}

void Interpreter::finish() {
	status_ = Status::finished;
}

void Interpreter::fail(int pc, const std::string& message) {
	const Instruction& ins = program_.code[pc];
	error_ = program_.files[ins.file] + ":" + std::to_string(ins.source_line) + ": " + message;
	cancel_all();
	skip_depth_ = -1;
	status_ = Status::error;
}

Interpreter::Position Interpreter::position() const {
	Position out;
	if (status_ != Status::running || frames_.empty()) {
		return out;
	}
	out.event = program_.events[frames_.back().event].name;
	const Thread* thread = &main_;
	if (main_.wait == WaitKind::children) {
		for (const Thread& child : children_) {
			if (child.alive) {
				thread = &child;
				break;
			}
		}
	}
	if (thread->pc >= 0 && thread->pc < static_cast<int>(program_.code.size())) {
		const Instruction& ins = program_.code[thread->pc];
		if (ins.file >= 0 && ins.file < static_cast<int>(program_.files.size())) {
			out.file = program_.files[ins.file];
		}
		out.line = ins.source_line;
	}
	return out;
}

SaveState Interpreter::save() const {
	SaveState state;
	state.fingerprint = program_.fingerprint;
	if (!frames_.empty()) {
		state.event = program_.events[frames_.front().event].name;
	}
	state.start_world = start_world_;
	state.status = status_;
	state.skip_depth = skip_depth_;
	for (const Frame& f : frames_) {
		state.frames.push_back({f.event, f.return_pc});
	}
	state.main = {main_.pc, main_.wait, main_.timer};
	for (const Thread& child : children_) {
		if (child.alive) {
			state.children.push_back({child.pc, child.wait, child.timer});
		}
	}
	for (const Background& b : background_) {
		if (b.pc >= 0) {
			state.background.push_back({b.pc, b.depth});
		}
	}
	state.presentation = presentation_;
	state.locals = locals_;
	return state;
}

bool Interpreter::restore(const SaveState& state, std::string& error) {
	restarted_ = false;
	if (state.fingerprint != program_.fingerprint && state.status == Status::running) {
		// The saved positions mean nothing in the new program (EVT-052).
		if (program_.find_event(state.event) < 0) {
			error = "the save was made with different event scripts, and its event '" + state.event + "' no longer exists";
			return false;
		}
		if (!host_.restore_world(state.start_world)) {
			error = "the save was made with different event scripts, and the host could not restore the world from the start of '" + state.event + "'";
			return false;
		}
		start(state.event);
		restarted_ = true;
		return true;
	}
	int code_size = static_cast<int>(program_.code.size());
	int event_count = static_cast<int>(program_.events.size());
	auto valid_pc = [code_size](int pc) { return pc >= 0 && pc < code_size; };
	if (state.status == Status::running) {
		bool ok = valid_pc(state.main.pc) && state.skip_depth < static_cast<int>(state.frames.size()) && !state.frames.empty();
		for (const SavedFrame& f : state.frames) {
			ok = ok && f.event >= 0 && f.event < event_count && (f.return_pc == -1 || valid_pc(f.return_pc));
		}
		for (const SavedThread& t : state.children) {
			ok = ok && valid_pc(t.pc);
		}
		for (const SavedBackground& b : state.background) {
			ok = ok && valid_pc(b.pc) && program_.code[b.pc].op == Op::action;
		}
		if (!ok) {
			error = "the save refers to positions outside the program";
			return false;
		}
	}

	// Whatever was in flight is started again: a thread that waited on a
	// handle re-runs its instruction, and background warps restart in step().
	auto thread_from = [](const SavedThread& saved) {
		Thread t;
		t.pc = saved.pc;
		t.wait = saved.wait == WaitKind::handle ? WaitKind::none : saved.wait;
		t.timer = saved.timer;
		return t;
	};
	status_ = state.status;
	error_.clear();
	skip_depth_ = state.skip_depth;
	frames_.clear();
	for (const SavedFrame& f : state.frames) {
		frames_.push_back({f.event, f.return_pc});
	}
	main_ = thread_from(state.main);
	children_.clear();
	for (const SavedThread& t : state.children) {
		children_.push_back(thread_from(t));
	}
	// The rebuild puts background presentation at its end state; only a warp
	// has to run again.
	background_.clear();
	for (const SavedBackground& b : state.background) {
		if (!presentation_only(program_.code[b.pc].action.type)) {
			background_.push_back({0, b.pc, b.depth});
		}
	}
	presentation_ = state.presentation;
	dirty_.clear();
	rebuild_pending_ = status_ == Status::running;
	locals_ = local_initial_;
	for (const auto& saved : state.locals) {
		auto local = locals_.find(saved.first);
		if (local != locals_.end()) {
			local->second = saved.second;
		}
	}
	start_world_ = state.start_world;
	return true;
}

}
