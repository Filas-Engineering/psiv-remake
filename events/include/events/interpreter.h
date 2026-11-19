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
	bool restore(const SaveState& state, std::string& error);

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

	bool run_thread(Thread& thread);
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
	long budget_ = 0;
};

}
