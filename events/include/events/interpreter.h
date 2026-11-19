#pragma once

#include "events/host.h"
#include "events/program.h"
#include "events/save_state.h"

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
	// True when the innermost running event is skippable and no skip is
	// already in progress.
	bool can_skip() const;
	// Unwinds to the outermost event of the innermost run of skippable
	// events on the call stack, cancels what they started, runs that event's
	// `on skip` block, then returns to its caller as if it had finished.
	bool skip();
	SaveState save() const;
	// With the same program, resumes where the save was made. When the
	// program's fingerprint differs (the scripts changed), puts the world
	// back with Host::restore_world(state.start_world) and starts the saved
	// event again from its first command; restarted() is then true.
	bool restore(const SaveState& state, std::string& error);
	bool restarted() const { return restarted_; }

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
		int pc = -1;
		// Call depth that started it; a skip only cancels its own depths.
		int depth = 0;
	};

	struct Change {
		ChangeKind kind = ChangeKind::item;
		std::string name;
		int depth = 0;
		std::int64_t amount = 0;
	};

	bool run_thread(Thread& thread);
	// Applies a give/take (item) or `+=` (var). Outside `on skip` it adds to
	// the totals for the current depth; inside, it first uses up what the
	// played part already applied in the same direction.
	void apply_change(ChangeKind kind, const std::string& name, std::int64_t amount);
	void merge_changes(int from_depth, int into_depth);
	bool resolve_wait(Thread& thread);
	void execute(Thread& thread);
	bool evaluate(const Condition& condition);
	void prune_background();
	void cancel_from(int depth);
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
	std::vector<Change> applied_;
	std::vector<Change> skip_credit_;
	std::string start_world_;
	bool restarted_ = false;
	long budget_ = 0;
};

}
