#include "events/interpreter.h"

namespace events {

namespace {

// Guards against scripts that loop without ever waiting.
const long step_budget = 100000;
const std::size_t max_call_depth = 32;
const double time_epsilon = 1e-9;

}

Interpreter::Interpreter(const Program& program, Host& host) : program_(program), host_(host) {}

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
	// Entries without a handle come from restore() and are started again.
	for (Background& b : background_) {
		if (b.handle == 0) {
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
			if (picked < 0 || picked >= static_cast<int>(ins.targets.size())) {
				host_.release(thread.handle);
				thread.handle = 0;
				thread.wait = WaitKind::none;
				fail(thread.pc, "the host returned result " + std::to_string(picked) + ", but only " + std::to_string(ins.targets.size()) + " are possible");
				return false;
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

void Interpreter::execute(Thread& thread) {
	const Instruction& ins = program_.code[thread.pc];
	switch (ins.op) {
	case Op::say:
		thread.handle = host_.show_line(ins.dialogue);
		thread.wait = WaitKind::handle;
		return;
	case Op::choice:
		thread.handle = host_.present_choice(ins.choice);
		thread.wait = WaitKind::handle;
		return;
	case Op::action: {
		Handle handle = host_.start_action(ins.action);
		if (ins.wait) {
			thread.handle = handle;
			thread.wait = WaitKind::handle;
			return;
		}
		background_.push_back({handle, thread.pc, static_cast<int>(frames_.size()) - 1});
		break;
	}
	case Op::battle:
		thread.handle = host_.start_action(ins.action);
		thread.wait = WaitKind::handle;
		return;
	case Op::wait:
		if (ins.seconds > 0) {
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
			skip_depth_ = -1;
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
		host_.set_flag(ins.name, true);
		break;
	case Op::clear_flag:
		host_.set_flag(ins.name, false);
		break;
	case Op::var_set:
		host_.set_var(ins.name, ins.number);
		break;
	case Op::var_add:
		host_.set_var(ins.name, host_.get_var(ins.name) + ins.number);
		break;
	case Op::give:
		host_.give_item(ins.name, static_cast<int>(ins.number));
		break;
	case Op::take:
		host_.take_item(ins.name, static_cast<int>(ins.number));
		break;
	case Op::party_add:
		host_.add_party_member(ins.name);
		break;
	case Op::party_remove:
		host_.remove_party_member(ins.name);
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

bool Interpreter::evaluate(const Condition& condition) {
	for (const Term& term : condition.terms) {
		bool value;
		if (term.is_flag) {
			value = host_.get_flag(term.name);
		} else {
			std::int64_t v = host_.get_var(term.name);
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

bool Interpreter::can_skip() const {
	return status_ == Status::running && skip_depth_ < 0 && !frames_.empty() && program_.events[frames_.back().event].skippable;
}

bool Interpreter::skip() {
	if (!can_skip()) {
		return false;
	}
	int depth = static_cast<int>(frames_.size()) - 1;
	while (depth > 0 && program_.events[frames_[depth - 1].event].skippable) {
		--depth;
	}
	cancel_from(depth);
	frames_.resize(depth + 1);
	skip_depth_ = depth;
	main_ = Thread();
	main_.pc = program_.events[frames_.back().event].skip_entry;
	return true;
}

void Interpreter::cancel_from(int depth) {
	if (main_.wait == WaitKind::handle) {
		host_.cancel(main_.handle);
	}
	for (const Thread& child : children_) {
		if (child.alive && child.wait == WaitKind::handle) {
			host_.cancel(child.handle);
		}
	}
	children_.clear();
	std::vector<Background> kept;
	for (const Background& b : background_) {
		if (b.depth < depth) {
			kept.push_back(b);
		} else if (b.handle != 0) {
			host_.cancel(b.handle);
		}
	}
	background_.swap(kept);
}

void Interpreter::finish() {
	status_ = Status::finished;
}

void Interpreter::fail(int pc, const std::string& message) {
	const Instruction& ins = program_.code[pc];
	error_ = program_.files[ins.file] + ":" + std::to_string(ins.source_line) + ": " + message;
	cancel_from(0);
	main_.wait = WaitKind::none;
	status_ = Status::error;
}

SaveState Interpreter::save() const {
	SaveState state;
	state.fingerprint = program_.fingerprint;
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
		state.background.push_back({b.pc, b.depth});
	}
	return state;
}

bool Interpreter::restore(const SaveState& state, std::string& error) {
	if (state.fingerprint != program_.fingerprint) {
		error = "the save was made with different event scripts";
		return false;
	}
	int code_size = static_cast<int>(program_.code.size());
	int event_count = static_cast<int>(program_.events.size());
	auto valid_pc = [code_size](int pc) { return pc >= 0 && pc < code_size; };
	if (state.status == Status::running) {
		bool ok = valid_pc(state.main.pc) && state.skip_depth < static_cast<int>(state.frames.size());
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
	// handle re-runs its instruction, background actions restart in step().
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
	background_.clear();
	for (const SavedBackground& b : state.background) {
		background_.push_back({0, b.pc, b.depth});
	}
	return true;
}

}
