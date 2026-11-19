#pragma once

#include "rules/character.h"
#include "rules/database.h"
#include "rules/stats.h"

namespace rules {

Stats statsAtLevel(const CharacterDef& character, int level);

// Full hit points, technique points and skill uses.
PartyMember makePartyMember(const Database& db, const CharacterDef& character, int level);

void rest(PartyMember& member, const Database& db);

// Returns the number of levels gained. Gained max hit points and technique points are added to the current values.
int grantExperience(PartyMember& member, const CharacterDef& character, int experience);

}
