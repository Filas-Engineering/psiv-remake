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
	// Stops a running event where it stands: the host cancels every action still in flight, nothing more runs, and the
	// status becomes idle. A game uses it to end a background event (FORMAT.md, "Background events").
	void cancel();
	const PresentationState& presentation() const { return presentation_; }

	Status status() const { return status_; }
	const std::string& error() const { return error_; }

	// For tools: the innermost running event, and the source of the instruction its main thread is at, or the first
	// live branch's while the main thread waits on a `together` or `race` block. Empty while nothing runs.
	struct Position {
		std::string event;
		std::string file;
		int line = 0;
	};
	Position position() const;

	// For hosts: what the innermost running event shows next, without running anything. It looks past the line its main
	// thread shows now, through jumps, returns and flag and variable changes (which show nothing); a branch on a
	// condition or a choice's answer, a wait, a block and any other command give `other`.
	enum class NextOutput { none, line, choice, end, other };
	NextOutput next_output() const;

	// For hosts: the last answered `choice ... keep` of the running event (FORMAT.md, "Choices"), which stays on screen
	// with its picked row until `choice clear` or the event ends; nullptr when none. `picked` and `states` (the options'
	// state words as it was presented) come with it.
	const ChoiceRequest* kept_choice(int& picked, std::vector<std::string>& states) const;

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
	// The state words of the choice presented last, kept with it once answered.
	std::vector<std::string> presented_states_;
};

}
