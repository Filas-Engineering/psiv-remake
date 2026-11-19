#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace events {

enum class WaitKind : int { none = 0, timer = 1, handle = 2, children = 3, background = 4 };

enum class Status : int { idle = 0, running = 1, finished = 2, error = 3 };

struct SavedThread {
	int pc = -1;
	WaitKind wait = WaitKind::none;
	double timer = 0;
};

struct SavedFrame {
	int event = -1;
	int return_pc = -1;
};

struct SavedBackground {
	int pc = -1;
	int depth = 0;
};

enum class ChangeKind : int { item = 0, var = 1 };

// A running total of the gives and takes (item) or `+=`/`-=` (var) that
// one call depth applied. `on skip` reads these so it never applies twice.
struct SavedChange {
	ChangeKind kind = ChangeKind::item;
	std::string name;
	int depth = 0;
	std::int64_t amount = 0;
};

// Plain data only. Host handles are not saved: whatever was in flight is
// started again by the first step() after restore (see README.md).
struct SaveState {
	std::uint64_t fingerprint = 0;
	// The outermost event and the host's snapshot_world() from when it
	// started. Used when the fingerprint no longer matches the program.
	std::string event;
	std::string start_world;
	Status status = Status::idle;
	// Frame depth whose `on skip` block is running, or -1.
	int skip_depth = -1;
	std::vector<SavedFrame> frames;
	SavedThread main;
	std::vector<SavedThread> children;
	std::vector<SavedBackground> background;
	std::vector<SavedChange> applied;
	// What the played part applied that the running `on skip` has not yet
	// counted against its own changes.
	std::vector<SavedChange> skip_credit;
};

std::string save_state_to_text(const SaveState& state);
bool save_state_from_text(const std::string& text, SaveState& state);

}
