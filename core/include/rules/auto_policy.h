#pragma once

#include "rules/battle.h"
#include "rules/command.h"

namespace rules {

// A simple deterministic policy for auto-battle: revive, then heal the weakest ally, then the strongest
// affordable damaging technique, then the basic attack. Always aims at the weakest living foe. A revive or heal passes
// by allies it would not change (Battle::wouldChange): kinds its effects do not reach, nothing to heal.
Command autoCommand(const Battle& battle, int actor);

}
