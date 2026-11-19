#pragma once

#include "events/host.h"

#include <cstdint>
#include <map>
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

// What the running event last told the host to show for one actor. Only
// what the event commanded is known; the rest is up to the game.
struct ActorState {
	std::string actor;
	// -1 when the event never showed or hid the actor, 0 hidden, 1 shown.
	int visible = -1;
	// The last move's target: a marker, or a position when has_position.
	std::string marker;
	bool has_position = false;
	Vec3 position;
	// The last `face` target since the last move.
	std::string facing;
	// The last looping animation; a one-shot animation ends it.
	std::string pose;

	bool operator==(const ActorState& other) const;
};

struct PanelState {
	std::string panel;
	std::string camera;
	std::string slot;

	bool operator==(const PanelState& other) const;
};

// What the running event put on screen and in the speakers (EVT-022). A
// restore, and the end of a skip, re-issue host calls to rebuild it
// (EVT-051). Fields the event never set stay as the game has them.
struct PresentationState {
	bool music_set = false;
	// Empty when the event stopped the music.
	std::string music;
	bool ambient_set = false;
	std::string ambient;
	bool fade_set = false;
	bool faded_out = false;
	// The last camera cut or move; empty when unset.
	std::string camera;
	// The panels on screen, in the order they were first shown: the page.
	std::vector<PanelState> panels;
	// Sorted by actor.
	std::vector<ActorState> actors;

	bool operator==(const PresentationState& other) const;
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
	// Frame depth of the outermost event being skipped, or -1.
	int skip_depth = -1;
	std::vector<SavedFrame> frames;
	SavedThread main;
	std::vector<SavedThread> children;
	std::vector<SavedBackground> background;
	PresentationState presentation;
	// Values of the registry's local flags (0 or 1) and variables.
	std::map<std::string, std::int64_t> locals;
};

// The text form ends with a checksum of everything before it, so damage is
// detected (SAV-014): save_state_from_text rejects a text whose checksum
// does not match, and texts from other format versions.
std::string save_state_to_text(const SaveState& state);
bool save_state_from_text(const std::string& text, SaveState& state);

}
