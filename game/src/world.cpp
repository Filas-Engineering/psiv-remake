#include "game/world.h"

namespace game {

Cell facingVector(Facing facing) {
	switch (facing) {
	case Facing::North: return {0, -1};
	case Facing::East: return {1, 0};
	case Facing::South: return {0, 1};
	case Facing::West: return {-1, 0};
	}
	return {0, 0};
}

Cell stepFrom(Cell cell, Facing facing) {
	const Cell d = facingVector(facing);
	return {cell.x + d.x, cell.y + d.y};
}

const char* toString(Facing facing) {
	switch (facing) {
	case Facing::North: return "north";
	case Facing::East: return "east";
	case Facing::South: return "south";
	case Facing::West: return "west";
	}
	return "south";
}

bool parseFacing(const std::string& text, Facing& out) {
	for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
		if (text == toString(f) || (text.size() == 1 && text[0] == toString(f)[0])) {
			out = f;
			return true;
		}
	}
	return false;
}

std::uint64_t EncounterStream::next() {
	state += 0x9E3779B97F4A7C15ULL;
	std::uint64_t z = state;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

int EncounterStream::range(int low, int high) {
	if (high <= low) {
		return low;
	}
	const std::uint64_t span = static_cast<std::uint64_t>(high - low) + 1;
	return low + static_cast<int>(next() % span);
}

const char* toString(TextSpeed speed) {
	switch (speed) {
	case TextSpeed::Slow: return "slow";
	case TextSpeed::Normal: return "normal";
	case TextSpeed::Fast: return "fast";
	case TextSpeed::Instant: return "instant";
	}
	return "normal";
}

const char* toString(BattleSpeed speed) {
	switch (speed) {
	case BattleSpeed::X1: return "1x";
	case BattleSpeed::X1_5: return "1.5x";
	case BattleSpeed::X2: return "2x";
	case BattleSpeed::X3: return "3x";
	}
	return "1x";
}

bool parseTextSpeed(const std::string& text, TextSpeed& out) {
	for (TextSpeed s : {TextSpeed::Slow, TextSpeed::Normal, TextSpeed::Fast, TextSpeed::Instant}) {
		if (text == toString(s)) {
			out = s;
			return true;
		}
	}
	return false;
}

bool parseBattleSpeed(const std::string& text, BattleSpeed& out) {
	for (BattleSpeed s : {BattleSpeed::X1, BattleSpeed::X1_5, BattleSpeed::X2, BattleSpeed::X3}) {
		if (text == toString(s)) {
			out = s;
			return true;
		}
	}
	return false;
}

const char* toString(EnemyState state) {
	switch (state) {
	case EnemyState::Idle: return "idle";
	case EnemyState::Chasing: return "chasing";
	case EnemyState::Returning: return "returning";
	}
	return "idle";
}

bool parseEnemyState(const std::string& text, EnemyState& out) {
	for (EnemyState s : {EnemyState::Idle, EnemyState::Chasing, EnemyState::Returning}) {
		if (text == toString(s)) {
			out = s;
			return true;
		}
	}
	return false;
}

rules::PartyMember* World::member(const std::string& characterId) {
	for (rules::PartyMember& m : roster) {
		if (m.characterId == characterId) {
			return &m;
		}
	}
	return nullptr;
}

const rules::PartyMember* World::member(const std::string& characterId) const {
	return const_cast<World*>(this)->member(characterId);
}

bool World::flag(const std::string& name) const {
	auto it = flags.find(name);
	return it != flags.end() && it->second;
}

namespace {

bool sameMember(const rules::PartyMember& a, const rules::PartyMember& b) {
	return a.characterId == b.characterId && a.level == b.level && a.experience == b.experience && a.hp == b.hp &&
		a.tp == b.tp && a.skillUses == b.skillUses && a.equipment == b.equipment;
}

bool sameMacro(const rules::Macro& a, const rules::Macro& b) {
	if (a.name != b.name || a.steps.size() != b.steps.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.steps.size(); ++i) {
		const rules::MacroStep& x = a.steps[i];
		const rules::MacroStep& y = b.steps[i];
		if (x.member != y.member || x.kind != y.kind || x.id != y.id || x.target.kind != y.target.kind ||
			x.target.member != y.target.member || x.target.enemySlot != y.target.enemySlot) {
			return false;
		}
	}
	return true;
}

bool sameEnemy(const VisibleEnemy& a, const VisibleEnemy& b) {
	return a.id == b.id && a.zone == b.zone && a.encounter == b.encounter && a.spawn == b.spawn &&
		a.position == b.position && a.facing == b.facing && a.state == b.state && a.stateMs == b.stateMs &&
		a.moveProgress == b.moveProgress;
}

template <class T, class Same>
bool sameList(const std::vector<T>& a, const std::vector<T>& b, Same same) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (!same(a[i], b[i])) {
			return false;
		}
	}
	return true;
}

}

bool sameWorld(const World& a, const World& b, std::string* diff) {
	auto differs = [&](const char* field) {
		if (diff) {
			*diff = field;
		}
		return false;
	};
	if (a.flags != b.flags) return differs("flags");
	if (a.vars != b.vars) return differs("vars");
	if (!sameList(a.roster, b.roster, sameMember)) return differs("roster");
	if (a.lineup != b.lineup) return differs("lineup");
	if (a.items != b.items) return differs("items");
	if (a.equipmentBag != b.equipmentBag) return differs("equipmentBag");
	if (a.money != b.money) return differs("money");
	if (!sameList(a.macros, b.macros, sameMacro)) return differs("macros");
	if (a.autoMacro != b.autoMacro) return differs("autoMacro");
	if (a.comboLog.discovered() != b.comboLog.discovered()) return differs("comboLog");
	if (a.area != b.area) return differs("area");
	if (a.position != b.position) return differs("position");
	if (a.facing != b.facing) return differs("facing");
	if (a.visitedAreas != b.visitedAreas) return differs("visitedAreas");
	if (a.playerControl != b.playerControl) return differs("playerControl");
	if (!sameList(a.enemies, b.enemies, sameEnemy)) return differs("enemies");
	if (a.nextEnemyId != b.nextEnemyId) return differs("nextEnemyId");
	if (a.graceMs != b.graceMs) return differs("graceMs");
	if (a.randomCounter != b.randomCounter) return differs("randomCounter");
	if (a.randomThreshold != b.randomThreshold) return differs("randomThreshold");
	if (a.encounterStream.state != b.encounterStream.state) return differs("encounterStream");
	if (a.playTimeMs != b.playTimeMs) return differs("playTimeMs");
	if (a.settings.difficulty != b.settings.difficulty) return differs("settings.difficulty");
	if (a.settings.rewardMultiplier != b.settings.rewardMultiplier) return differs("settings.rewardMultiplier");
	auto sameChunk = [](const UnknownChunk& x, const UnknownChunk& y) {
		return x.type == y.type && x.version == y.version && x.payload == y.payload;
	};
	if (!sameList(a.unknownChunks, b.unknownChunks, sameChunk)) return differs("unknownChunks");
	return true;
}

}
