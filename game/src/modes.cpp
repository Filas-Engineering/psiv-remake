#include "game/modes.h"

namespace game {

const char* toString(Mode mode) {
	switch (mode) {
	case Mode::Title: return "title";
	case Mode::Field: return "field";
	case Mode::Event: return "event";
	case Mode::Battle: return "battle";
	case Mode::Menu: return "menu";
	case Mode::Shop: return "shop";
	}
	return "title";
}

bool transitionAllowed(Mode from, Mode to) {
	switch (from) {
	case Mode::Title: return to == Mode::Field || to == Mode::Menu;
	case Mode::Field: return to == Mode::Battle || to == Mode::Event || to == Mode::Menu || to == Mode::Shop;
	case Mode::Battle: return to == Mode::Field || to == Mode::Event || to == Mode::Title;
	case Mode::Event: return to == Mode::Field || to == Mode::Battle || to == Mode::Menu || to == Mode::Shop;
	case Mode::Menu:
	case Mode::Shop: return to == Mode::Field || to == Mode::Title || to == Mode::Event;
	}
	return false;
}

bool ModeMachine::transition(Mode to) {
	bool allowed = transitionAllowed(mode_, to);
	// A menu only closes back to the mode that opened it.
	if (allowed && (mode_ == Mode::Menu || mode_ == Mode::Shop) && to != returnTo_) {
		allowed = false;
	}
	if (!allowed) {
		refused_.push_back(std::string(toString(mode_)) + " -> " + toString(to));
		return false;
	}
	if (to == Mode::Menu || to == Mode::Shop) {
		returnTo_ = mode_;
	}
	mode_ = to;
	return true;
}

void ModeMachine::reset(Mode to) {
	mode_ = to;
	returnTo_ = Mode::Title;
}

}
