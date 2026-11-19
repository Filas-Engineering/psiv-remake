#pragma once

#include "events/host.h"
#include "events/program.h"
#include "events/save_state.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace events {

class Interpreter {
public:
	Interpreter(const Program& program, Host& host);

	bool start(const std::string& event);
	// Runs until every thread is blocked, then returns. Time only counts
	// against waits that were already in progress when the step began.
	Status step(double delta_seconds);
	// True when the innermost running event is skippable, no skip is in
	// progress, and the event is not waiting on the player's answer to a
	// choice, a battle or a menu.
	bool can_skip() const;
	// Fast-forwards the rest of the innermost run of skippable events on the
	// call stack (EVT-041): lines and presentation in flight are cancelled,
	// later lines are not shown, waits take no time, presentation commands
	// only update presentation(), and every state change applies in order.
	// The skip stops at the next choice, battle, shop, save prompt or name
	// entry, or when those events return; the screen is then rebuilt with
	// instant host calls. The player can skip again after the stop.
	bool skip();
	bool skipping() const { return skip_depth_ >= 0; }
	// Safe to call from inside any Host call that returns a handle, such as
	// start_battle: the save holds the state from just before that command,
	// so a restore issues the command again.
	SaveState save() const;
	// With the same program, resumes where the save was made, and the first
	// step() rebuilds presentation() with instant host calls before anything
	// else. When the program's fingerprint differs (the scripts changed),
	// puts the world back with Host::restore_world(state.start_world) and
	// starts the saved event again from its first command; restarted() is
	// then true.
	bool restore(const SaveState& state, std::string& error);
	bool restarted() const { return restarted_; }
	const PresentationState& presentation() const { return presentation_; }

	Status status() const { return status_; }
	const std::string& error() const { return error_; }

private:
	struct Thread {
		int pc = -1;
		WaitKind wait = WaitKind::none;
		double timer = 0;
		Handle handle = 0;
		bool alive = true;
	};

	struct Frame {
		int event = -1;
		int return_pc = -1;
	};

	struct Background {
		Handle handle = 0;
		// -1 for the instant calls that rebuild the screen; they are not saved.
		int pc = -1;
		int depth = 0;
	};

	bool run_thread(Thread& thread);
	bool resolve_wait(Thread& thread);
	void execute(Thread& thread);
	void start_action(Thread& thread, const Instruction& ins, const Action& action);
	bool evaluate(const Condition& condition);
	bool read_flag(const std::string& name);
	void write_flag(const std::string& name, bool value);
	std::int64_t read_var(const std::string& name);
	void write_var(const std::string& name, std::int64_t value);
	bool waiting_on_player() const;
	bool in_race() const;
	void fast_forward(Thread& thread);
	void stop_skipping();
	void record(const Action& action);
	void mark_dirty(const Action& action);
	void rebuild(bool everything);
	void issue_instant(Action action);
	ActorState& actor(const std::string& name);
	void prune_background();
	void cancel_all();
	void finish();
	void fail(int pc, const std::string& message);

	const Program& program_;
	Host& host_;
	Status status_ = Status::idle;
	std::string error_;
	int skip_depth_ = -1;
	std::vector<Frame> frames_;
	Thread main_;
	std::vector<Thread> children_;
	std::vector<Background> background_;
	PresentationState presentation_;
	// Parts of presentation_ changed while skipping: "music", "ambient",
	// "fade", "camera", "panels" or "actor:<name>".
	std::set<std::string> dirty_;
	bool rebuild_pending_ = false;
	std::map<std::string, std::int64_t> local_initial_;
	std::map<std::string, std::int64_t> locals_;
	std::string start_world_;
	bool restarted_ = false;
	long budget_ = 0;
};

}
