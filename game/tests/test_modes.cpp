#include "test.h"

#include "game/modes.h"
#include "game/session.h"

using namespace game;

TEST(modeTableMatchesGameFlow) {
	CHECK(transitionAllowed(Mode::Title, Mode::Field));
	CHECK(transitionAllowed(Mode::Field, Mode::Battle));
	CHECK(transitionAllowed(Mode::Event, Mode::Battle));
	CHECK(transitionAllowed(Mode::Battle, Mode::Event));
	CHECK(transitionAllowed(Mode::Battle, Mode::Title));
	CHECK(transitionAllowed(Mode::Event, Mode::Shop));
	CHECK(!transitionAllowed(Mode::Title, Mode::Battle));
	CHECK(!transitionAllowed(Mode::Title, Mode::Event));
	CHECK(!transitionAllowed(Mode::Field, Mode::Title));
	CHECK(!transitionAllowed(Mode::Battle, Mode::Menu));
	CHECK(!transitionAllowed(Mode::Battle, Mode::Battle));
	CHECK(!transitionAllowed(Mode::Menu, Mode::Battle));
	CHECK(!transitionAllowed(Mode::Shop, Mode::Menu));
}

TEST(illegalTransitionIsRefusedAndLogged) {
	ModeMachine m;
	CHECK(!m.transition(Mode::Battle));
	CHECK(m.mode() == Mode::Title);
	CHECK_EQ(m.refused().size(), 1u);
	CHECK_EQ(m.refused()[0], std::string("title -> battle"));
	CHECK(m.transition(Mode::Field));
	CHECK(m.transition(Mode::Battle));
	CHECK(!m.transition(Mode::Menu));
	CHECK(m.mode() == Mode::Battle);
}

TEST(menuClosesOnlyToItsOpener) {
	ModeMachine m;
	m.transition(Mode::Field);
	m.transition(Mode::Event);
	CHECK(m.transition(Mode::Shop));
	CHECK(m.returnMode() == Mode::Event);
	CHECK(!m.transition(Mode::Field));
	CHECK(m.transition(Mode::Event));

	ModeMachine title;
	CHECK(title.transition(Mode::Menu));
	CHECK(!title.transition(Mode::Field));
	CHECK(title.transition(Mode::Title));
}

TEST(sessionRefusesOperationsOutsideTheirMode) {
	Session s(test::data(), SessionOptions());
	CHECK(!s.walk(Facing::East).empty());
	CHECK(!s.openMenu().empty());
	CHECK(s.newGame().empty());
	test::confirmLines(s);
	CHECK(s.mode() == Mode::Field);
	CHECK(!s.newGame().empty());
	CHECK(!s.buy("tonic", 1).empty());
	CHECK(!s.rest().empty());
	CHECK(s.openMenu().empty());
	CHECK(s.mode() == Mode::Menu);
	CHECK(!s.walk(Facing::East).empty());
	CHECK(s.closeMenu().empty());
	CHECK(s.mode() == Mode::Field);
	CHECK(s.modes().refused().empty());
}
