#pragma once

#include <string>
#include <vector>

namespace game {

// The top-level modes of game-flow.md. Event is the spec's Cutscene. Shop is a Menu that sells; it follows
// the Menu rows of the transition table.
enum class Mode { Title, Field, Event, Battle, Menu, Shop };

const char* toString(Mode mode);

// Whether the transition table (FLOW-013) has a row from `from` to `to`.
bool transitionAllowed(Mode from, Mode to);

class ModeMachine {
public:
	Mode mode() const { return mode_; }
	// The mode a Menu or Shop closes back to.
	Mode returnMode() const { return returnTo_; }

	// Applies an allowed transition. Anything else is refused and logged; the mode does not change.
	bool transition(Mode to);
	// Back to Title without a transition: quitting from the pause overlay (FLOW-022) and loading reset the machine.
	void reset(Mode to = Mode::Title);

	const std::vector<std::string>& refused() const { return refused_; }

private:
	Mode mode_ = Mode::Title;
	Mode returnTo_ = Mode::Title;
	std::vector<std::string> refused_;
};

}
