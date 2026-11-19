#pragma once

#include <map>
#include <string>

namespace rules {

enum class Element { None, Heat, Chill, Shock, Gale };

enum class Stat { Strength, Mental, Agility, Dexterity, Defense };

enum class Side { Party, Enemy };

enum class Targeting { OneEnemy, EnemyGroup, AllEnemies, OneAlly, AllAllies, Self };

enum class AbilityKind { Attack, Technique, Skill, Combo };

enum class EffectKind { PhysicalDamage, ElementalDamage, Heal, ApplyStatus, CureStatus, ModifyStat, Revive };

using Inventory = std::map<std::string, int>;

// Percent of incoming elemental damage taken. Missing elements count as 100.
using Affinities = std::map<Element, int>;

}
