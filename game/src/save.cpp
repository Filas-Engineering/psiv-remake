#include "game/save.h"

#include "rules/names.h"
#include "rules/version.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace game {

namespace {

constexpr int kHeaderVersion = 1;
constexpr int kMidEventVersion = 1;

std::uint64_t checksum(const std::string& text) {
	std::uint64_t hash = 1469598103934665603ULL;
	for (unsigned char c : text) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	return hash;
}

std::string hex(std::uint64_t value) {
	char buffer[17];
	std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(value));
	return buffer;
}

// Letters, digits, '_' and '.' are kept; every other byte is %XX; an empty value is "-".
std::string esc(const std::string& text) {
	if (text.empty()) {
		return "-";
	}
	static const char* digits = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : text) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.') {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += digits[c >> 4];
			out += digits[c & 15];
		}
	}
	return out;
}

bool unesc(const std::string& token, std::string& out) {
	out.clear();
	if (token == "-") {
		return true;
	}
	for (std::size_t i = 0; i < token.size(); ++i) {
		if (token[i] != '%') {
			out += token[i];
			continue;
		}
		if (i + 2 >= token.size()) {
			return false;
		}
		auto value = [](char c) {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};
		const int hi = value(token[i + 1]);
		const int lo = value(token[i + 2]);
		if (hi < 0 || lo < 0) {
			return false;
		}
		out += static_cast<char>(hi * 16 + lo);
		i += 2;
	}
	return true;
}

struct Chunk {
	std::string type;
	int version = 0;
	std::string payload;
};

class Writer {
public:
	Writer& key(const char* k) {
		line_ = k;
		return *this;
	}
	Writer& text(const std::string& v) {
		line_ += " " + esc(v);
		return *this;
	}
	Writer& num(long long v) {
		line_ += " " + std::to_string(v);
		return *this;
	}
	Writer& unum(std::uint64_t v) {
		line_ += " " + std::to_string(static_cast<unsigned long long>(v));
		return *this;
	}
	void end() {
		out_ += line_ + "\n";
		line_.clear();
	}
	const std::string& str() const { return out_; }

private:
	std::string out_;
	std::string line_;
};

void writeChunk(std::string& out, const std::string& type, int version, const std::string& payload) {
	out += "chunk " + type + " " + std::to_string(version) + " " + std::to_string(payload.size()) + "\n";
	out += payload;
	out += "\n";
}

std::string locationChunk(const World& w) {
	Writer o;
	o.key("area").text(w.area).end();
	o.key("position").num(w.position.x).num(w.position.y).end();
	o.key("facing").text(toString(w.facing)).end();
	o.key("camera").text("").end();
	o.key("vehicle").text("").end();
	return o.str();
}

std::string partyChunk(const World& w) {
	Writer o;
	o.key("lineup");
	for (const std::string& id : w.lineup) {
		o.text(id);
	}
	o.end();
	for (const rules::PartyMember& m : w.roster) {
		o.key("member").text(m.characterId).num(m.level).num(m.experience).num(m.hp).num(m.tp).end();
		for (const auto& [ability, uses] : m.skillUses) {
			o.key("uses").text(ability).num(uses).end();
		}
		for (const auto& [slot, piece] : m.equipment) {
			o.key("wear").text(rules::toString(slot)).text(piece).end();
		}
		for (const rules::ActiveStatus& status : m.statuses) {
			o.key("status").text(status.id).num(status.remaining).end();
		}
	}
	for (const auto& [id, name] : w.memberNames) {
		o.key("named").text(id).text(name).end();
	}
	return o.str();
}

std::string inventoryChunk(const World& w) {
	Writer o;
	o.key("money").num(w.money).end();
	for (const auto& [id, n] : w.items) {
		o.key("item").text(id).num(n).end();
	}
	for (const auto& [id, n] : w.equipmentBag) {
		o.key("bag").text(id).num(n).end();
	}
	return o.str();
}

std::string worldChunk(const World& w) {
	Writer o;
	for (const auto& [name, value] : w.flags) {
		o.key("flag").text(name).num(value ? 1 : 0).end();
	}
	for (const auto& [name, value] : w.vars) {
		o.key("var").text(name).num(value).end();
	}
	for (const std::string& area : w.visitedAreas) {
		o.key("visited").text(area).end();
	}
	o.key("control").num(w.playerControl ? 1 : 0).end();
	for (const VisibleEnemy& e : w.enemies) {
		o.key("enemy").num(e.id).text(e.zone).text(e.encounter).num(e.spawn.x).num(e.spawn.y).num(e.position.x).num(e.position.y)
			.text(toString(e.facing)).text(toString(e.state)).num(e.stateMs).num(e.moveProgress).end();
	}
	o.key("next_enemy").num(w.nextEnemyId).end();
	o.key("grace").num(w.graceMs).end();
	o.key("random").num(w.randomCounter).num(w.randomThreshold).end();
	o.key("play").num(w.playTimeMs).end();
	return o.str();
}

// The auto macro, then core's own macro and combo log forms, each of which ends with an "end" line.
std::string battleChunk(const World& w) {
	Writer o;
	o.key("auto").text(w.autoMacro).end();
	return o.str() + rules::serializeMacros(w.macros) + rules::serializeComboLog(w.comboLog);
}

std::string randomChunk(const World& w) {
	Writer o;
	o.key("stream").unum(w.encounterStream.state()).end();
	return o.str();
}

std::string difficultyChunk(const World& w) {
	Writer o;
	o.key("difficulty").text(w.settings.difficulty).end();
	o.key("reward").text(rules::toString(w.settings.rewardMultiplier)).end();
	return o.str();
}

std::string questsChunk(const World& w) {
	Writer o;
	for (const auto& [id, state] : w.jobs) {
		o.key("job").text(id).text(toString(state)).end();
	}
	return o.str();
}

std::string unlocksChunk(const World& w) {
	Writer o;
	for (const std::string& id : w.unlocked) {
		o.key("entry").text(id).end();
	}
	return o.str();
}

bool toInt(const std::string& text, long long& out) {
	if (text.empty()) {
		return false;
	}
	char* end = nullptr;
	out = std::strtoll(text.c_str(), &end, 10);
	return *end == '\0';
}

bool toUint(const std::string& text, std::uint64_t& out) {
	if (text.empty() || text[0] == '-') {
		return false;
	}
	char* end = nullptr;
	out = std::strtoull(text.c_str(), &end, 10);
	return *end == '\0';
}

// Reads a chunk payload line by line. Every accessor records the first problem and then returns defaults.
class Lines {
public:
	Lines(const std::string& chunk, const std::string& payload) : chunk_(chunk) {
		std::istringstream in(payload);
		std::string line;
		while (std::getline(in, line)) {
			std::vector<std::string> tokens;
			std::istringstream words(line);
			std::string w;
			while (words >> w) {
				tokens.push_back(w);
			}
			if (!tokens.empty()) {
				lines_.push_back(tokens);
			}
		}
	}

	const std::vector<std::vector<std::string>>& lines() const { return lines_; }

	bool need(const std::vector<std::string>& line, std::size_t count) {
		if (line.size() != count + 1) {
			fail("'" + line[0] + "' needs " + std::to_string(count) + " values");
			return false;
		}
		return true;
	}
	std::string text(const std::string& token) {
		std::string out;
		if (!unesc(token, out)) {
			fail("bad text '" + token + "'");
		}
		return out;
	}
	long long num(const std::string& token) {
		long long v = 0;
		if (!toInt(token, v)) {
			fail("bad number '" + token + "'");
		}
		return v;
	}
	void fail(const std::string& message) {
		if (error.empty()) {
			error = chunk_ + ": " + message;
		}
	}

	std::string error;

private:
	std::string chunk_;
	std::vector<std::vector<std::string>> lines_;
};

bool readLocation(const std::string& payload, World& w, std::string& error) {
	Lines in("location", payload);
	for (const auto& l : in.lines()) {
		const std::string& k = l[0];
		if (k == "area" && in.need(l, 1)) {
			w.area = in.text(l[1]);
		} else if (k == "position" && in.need(l, 2)) {
			w.position = {static_cast<int>(in.num(l[1])), static_cast<int>(in.num(l[2]))};
		} else if (k == "facing" && in.need(l, 1)) {
			if (!parseFacing(in.text(l[1]), w.facing)) {
				in.fail("bad facing");
			}
		} else if (k == "camera" || k == "vehicle") {
			// No camera volumes or vehicles exist yet; the fields are kept so the chunk matches SAV-001.
		} else {
			in.fail("unknown line '" + k + "'");
		}
	}
	error = in.error;
	return error.empty();
}

bool readParty(const std::string& payload, World& w, std::string& error) {
	Lines in("party", payload);
	w.roster.clear();
	w.lineup.clear();
	w.memberNames.clear();
	for (const auto& l : in.lines()) {
		const std::string& k = l[0];
		if (k == "lineup") {
			for (std::size_t i = 1; i < l.size(); ++i) {
				w.lineup.push_back(in.text(l[i]));
			}
		} else if (k == "member" && in.need(l, 5)) {
			rules::PartyMember m;
			m.characterId = in.text(l[1]);
			m.level = static_cast<int>(in.num(l[2]));
			m.experience = static_cast<int>(in.num(l[3]));
			m.hp = static_cast<int>(in.num(l[4]));
			m.tp = static_cast<int>(in.num(l[5]));
			w.roster.push_back(m);
		} else if (k == "uses" && in.need(l, 2)) {
			if (w.roster.empty()) {
				in.fail("'uses' before any member");
			} else {
				w.roster.back().skillUses[in.text(l[1])] = static_cast<int>(in.num(l[2]));
			}
		} else if (k == "wear" && in.need(l, 2)) {
			rules::EquipSlot slot;
			if (w.roster.empty() || !rules::parseEquipSlot(in.text(l[1]), slot)) {
				in.fail("bad 'wear' line");
			} else {
				w.roster.back().equipment[slot] = in.text(l[2]);
			}
		} else if (k == "status" && in.need(l, 2)) {
			if (w.roster.empty()) {
				in.fail("'status' before any member");
			} else {
				w.roster.back().statuses.push_back({in.text(l[1]), static_cast<int>(in.num(l[2]))});
			}
		} else if (k == "named" && in.need(l, 2)) {
			w.memberNames[in.text(l[1])] = in.text(l[2]);
		} else if (k != "lineup") {
			in.fail("unknown line '" + k + "'");
		}
	}
	error = in.error;
	return error.empty();
}

bool readInventory(const std::string& payload, World& w, std::string& error) {
	Lines in("inventory", payload);
	w.items.clear();
	w.equipmentBag.clear();
	for (const auto& l : in.lines()) {
		const std::string& k = l[0];
		if (k == "money" && in.need(l, 1)) {
			w.money = static_cast<int>(in.num(l[1]));
		} else if (k == "item" && in.need(l, 2)) {
			w.items[in.text(l[1])] = static_cast<int>(in.num(l[2]));
		} else if (k == "bag" && in.need(l, 2)) {
			w.equipmentBag[in.text(l[1])] = static_cast<int>(in.num(l[2]));
		} else {
			in.fail("unknown line '" + k + "'");
		}
	}
	error = in.error;
	return error.empty();
}

bool readWorld(const std::string& payload, World& w, std::string& error) {
	Lines in("world", payload);
	w.flags.clear();
	w.vars.clear();
	w.visitedAreas.clear();
	w.enemies.clear();
	for (const auto& l : in.lines()) {
		const std::string& k = l[0];
		if (k == "flag" && in.need(l, 2)) {
			w.flags[in.text(l[1])] = in.num(l[2]) != 0;
		} else if (k == "var" && in.need(l, 2)) {
			w.vars[in.text(l[1])] = in.num(l[2]);
		} else if (k == "visited" && in.need(l, 1)) {
			w.visitedAreas.insert(in.text(l[1]));
		} else if (k == "control" && in.need(l, 1)) {
			w.playerControl = in.num(l[1]) != 0;
		} else if (k == "enemy" && in.need(l, 11)) {
			VisibleEnemy e;
			e.id = static_cast<int>(in.num(l[1]));
			e.zone = in.text(l[2]);
			e.encounter = in.text(l[3]);
			e.spawn = {static_cast<int>(in.num(l[4])), static_cast<int>(in.num(l[5]))};
			e.position = {static_cast<int>(in.num(l[6])), static_cast<int>(in.num(l[7]))};
			if (!parseFacing(in.text(l[8]), e.facing) || !parseEnemyState(in.text(l[9]), e.state)) {
				in.fail("bad enemy facing or state");
			}
			e.stateMs = static_cast<int>(in.num(l[10]));
			e.moveProgress = static_cast<int>(in.num(l[11]));
			w.enemies.push_back(e);
		} else if (k == "next_enemy" && in.need(l, 1)) {
			w.nextEnemyId = static_cast<int>(in.num(l[1]));
		} else if (k == "grace" && in.need(l, 1)) {
			w.graceMs = static_cast<int>(in.num(l[1]));
		} else if (k == "random" && in.need(l, 2)) {
			w.randomCounter = static_cast<int>(in.num(l[1]));
			w.randomThreshold = static_cast<int>(in.num(l[2]));
		} else if (k == "play" && in.need(l, 1)) {
			w.playTimeMs = in.num(l[1]);
		} else {
			in.fail("unknown line '" + k + "'");
		}
	}
	error = in.error;
	return error.empty();
}

// Splits a battle chunk payload into its auto line and core's two blocks.
bool splitBattle(const std::string& payload, std::string& autoLine, std::string& macros, std::string& combos) {
	const std::size_t eol = payload.find('\n');
	const std::size_t log = payload.find("\ncombo_log ");
	if (eol == std::string::npos || log == std::string::npos || log < eol) {
		return false;
	}
	autoLine = payload.substr(0, eol);
	macros = payload.substr(eol + 1, log - eol);
	combos = payload.substr(log + 1);
	return true;
}

bool readBattle(const std::string& payload, World& w, std::string& error) {
	std::string autoLine;
	std::string macros;
	std::string combos;
	if (!splitBattle(payload, autoLine, macros, combos)) {
		error = "battle: expected the auto line, the macros and the combo log";
		return false;
	}
	Lines in("battle", autoLine);
	if (in.lines().size() != 1 || in.lines()[0][0] != "auto" || !in.need(in.lines()[0], 1)) {
		error = "battle: bad auto line";
		return false;
	}
	w.autoMacro = in.text(in.lines()[0][1]);
	rules::Result<std::vector<rules::Macro>> parsedMacros = rules::parseMacros(macros);
	rules::Result<rules::ComboLog> parsedCombos = rules::parseComboLog(combos);
	if (!in.error.empty() || !parsedMacros.ok() || !parsedCombos.ok()) {
		error = !in.error.empty() ? in.error : "battle: " + (parsedMacros.ok() ? parsedCombos.error : parsedMacros.error);
		return false;
	}
	w.macros = std::move(*parsedMacros.value);
	w.comboLog = std::move(*parsedCombos.value);
	return true;
}

bool readRandom(const std::string& payload, World& w, std::string& error) {
	Lines in("random", payload);
	for (const auto& l : in.lines()) {
		if (l[0] == "stream" && in.need(l, 1)) {
			std::uint64_t state = 0;
			if (!toUint(l[1], state)) {
				in.fail("bad stream state");
			}
			w.encounterStream.restore(state);
		} else {
			in.fail("unknown line '" + l[0] + "'");
		}
	}
	error = in.error;
	return error.empty();
}

bool readDifficulty(const std::string& payload, World& w, std::string& error) {
	Lines in("difficulty", payload);
	for (const auto& l : in.lines()) {
		if (l[0] == "difficulty" && in.need(l, 1)) {
			w.settings.difficulty = in.text(l[1]);
		} else if (l[0] == "reward" && in.need(l, 1)) {
			if (!rules::parseRewardMultiplier(in.text(l[1]), w.settings.rewardMultiplier)) {
				in.fail("bad reward multiplier");
			}
		} else {
			in.fail("unknown line '" + l[0] + "'");
		}
	}
	error = in.error;
	return error.empty();
}

bool readQuests(const std::string& payload, World& w, std::string& error) {
	Lines in("quests", payload);
	w.jobs.clear();
	for (const auto& l : in.lines()) {
		JobState state = JobState::Available;
		if (l[0] == "job" && in.need(l, 2)) {
			if (!parseJobState(in.text(l[2]), state)) {
				in.fail("bad job state");
			}
			w.jobs[in.text(l[1])] = state;
		} else {
			in.fail("unknown line '" + l[0] + "'");
		}
	}
	error = in.error;
	return error.empty();
}

bool readUnlocks(const std::string& payload, World& w, std::string& error) {
	Lines in("unlocks", payload);
	w.unlocked.clear();
	for (const auto& l : in.lines()) {
		if (l[0] == "entry" && in.need(l, 1)) {
			w.unlocked.insert(in.text(l[1]));
		} else {
			in.fail("unknown line '" + l[0] + "'");
		}
	}
	error = in.error;
	return error.empty();
}

using ChunkReader = bool (*)(const std::string&, World&, std::string&);
using ChunkWriter = std::string (*)(const World&);

struct KnownChunk {
	const char* type;
	int version;
	ChunkWriter write;
	ChunkReader read;
};

// In the order they are written.
const KnownChunk kWorldChunks[] = {
	{"location", 1, locationChunk, readLocation},
	{"party", 2, partyChunk, readParty},
	{"inventory", 1, inventoryChunk, readInventory},
	{"world", 1, worldChunk, readWorld},
	{"battle", 2, battleChunk, readBattle},
	{"random", 1, randomChunk, readRandom},
	{"difficulty", 1, difficultyChunk, readDifficulty},
	{"quests", 1, questsChunk, readQuests},
	{"unlocks", 1, unlocksChunk, readUnlocks},
};

// The clear-data record holds these, from the same writers.
const char* const kClearChunks[] = {"party", "inventory", "world"};

const KnownChunk* knownChunk(const std::string& type) {
	for (const KnownChunk& k : kWorldChunks) {
		if (type == k.type) {
			return &k;
		}
	}
	return nullptr;
}

std::string worldChunks(const World& w) {
	std::string out;
	for (const KnownChunk& k : kWorldChunks) {
		writeChunk(out, k.type, k.version, k.write(w));
	}
	for (const UnknownChunk& c : w.unknownChunks) {
		writeChunk(out, c.type, c.version, c.payload);
	}
	return out;
}

// Splits `text` from `pos` into chunks until the end or an "end" line, which is left at `pos`.
bool splitChunks(const std::string& text, std::size_t& pos, std::vector<Chunk>& chunks, std::string& error) {
	while (pos < text.size()) {
		const std::size_t eol = text.find('\n', pos);
		if (eol == std::string::npos) {
			error = "truncated chunk header";
			return false;
		}
		std::istringstream header(text.substr(pos, eol - pos));
		std::string word;
		header >> word;
		if (word == "end") {
			return true;
		}
		Chunk c;
		long long length = -1;
		if (word != "chunk" || !(header >> c.type >> c.version >> length) || length < 0) {
			error = "bad chunk header";
			return false;
		}
		const std::size_t start = eol + 1;
		if (start + static_cast<std::size_t>(length) + 1 > text.size() || text[start + length] != '\n') {
			error = "chunk '" + c.type + "' is truncated";
			return false;
		}
		c.payload = text.substr(start, static_cast<std::size_t>(length));
		chunks.push_back(c);
		pos = start + static_cast<std::size_t>(length) + 1;
	}
	return true;
}

// `required` lists the known chunks that must be present; every other known chunk is optional.
bool applyChunks(const std::vector<Chunk>& chunks, World& w, const std::vector<std::string>& required, std::string& error) {
	w.unknownChunks.clear();
	std::set<std::string> seen;
	for (const Chunk& c : chunks) {
		if (c.type == "header" || c.type == "midevent") {
			continue;
		}
		const KnownChunk* known = knownChunk(c.type);
		if (!known) {
			w.unknownChunks.push_back({c.type, c.version, c.payload});
			continue;
		}
		if (c.version > known->version) {
			error = "chunk '" + c.type + "' is from a newer build (version " + std::to_string(c.version) + ")";
			return false;
		}
		if (c.version < known->version) {
			error = "chunk '" + c.type + "' has version " + std::to_string(c.version) + ", which no migration produces";
			return false;
		}
		if (!seen.insert(c.type).second) {
			error = "chunk '" + c.type + "' appears twice";
			return false;
		}
		if (!known->read(c.payload, w, error)) {
			return false;
		}
	}
	for (const std::string& type : required) {
		if (!seen.count(type)) {
			error = "the '" + type + "' chunk is missing";
			return false;
		}
	}
	return true;
}

std::vector<std::string> allWorldChunks() {
	std::vector<std::string> out;
	for (const KnownChunk& k : kWorldChunks) {
		out.push_back(k.type);
	}
	return out;
}

// Schema 1 to 2. The battle chunk moves to core's macro and combo log forms; combinations found before have no
// play time or participants, so they get 0 and none. Party lines are unchanged; schema 2 only adds lines. The
// quests and unlocks chunks start empty, and the session fills them from the flags on load. A schema 1 midevent
// chunk holds interpreter state in a format the events library no longer reads, so it is dropped and the save
// continues in the field, as the events library asks for such a save.
bool migrate1to2(std::vector<Chunk>& chunks, std::string& error) {
	std::vector<Chunk> out;
	for (Chunk c : chunks) {
		if (c.type == "midevent") {
			continue;
		}
		if (c.type == "party" && c.version == 1) {
			c.version = 2;
		} else if (c.type == "battle" && c.version == 1) {
			Lines in("battle", c.payload);
			World w;
			for (const auto& l : in.lines()) {
				const std::string& k = l[0];
				if (k == "macro" && in.need(l, 1)) {
					w.macros.push_back({in.text(l[1]), {}});
				} else if (k == "step" && in.need(l, 6)) {
					rules::MacroStep step;
					step.member = in.text(l[1]);
					step.id = in.text(l[3]);
					step.target.member = in.text(l[5]);
					step.target.enemySlot = static_cast<int>(in.num(l[6]));
					if (w.macros.empty() || !rules::parseCommandKind(in.text(l[2]), step.kind) || !rules::parseMacroTargetKind(in.text(l[4]), step.target.kind)) {
						in.fail("bad macro step");
						continue;
					}
					w.macros.back().steps.push_back(step);
				} else if (k == "auto" && in.need(l, 1)) {
					w.autoMacro = in.text(l[1]);
				} else if (k == "combo" && in.need(l, 1)) {
					w.comboLog.discover(in.text(l[1]), 0, {});
				} else {
					in.fail("unknown line '" + k + "'");
				}
			}
			if (!in.error.empty()) {
				error = in.error;
				return false;
			}
			c.version = 2;
			c.payload = battleChunk(w);
		}
		out.push_back(c);
	}
	out.push_back({"quests", 1, ""});
	out.push_back({"unlocks", 1, ""});
	chunks = std::move(out);
	return true;
}

struct Migration {
	int from;
	bool (*apply)(std::vector<Chunk>& chunks, std::string& error);
};

// Ordered: a save of schema N runs every migration from N up.
const Migration kMigrations[] = {
	{1, migrate1to2},
};

std::string headerChunk(const SaveFile& save) {
	const World& w = save.world;
	Writer o;
	o.key("schema").num(kSaveSchemaVersion).end();
	o.key("build").text(kGameBuild).end();
	o.key("rules").num(rules::kRulesVersion).end();
	o.key("content").text(hex(save.contentHash)).end();
	o.key("written").num(save.writeTime).end();
	o.key("sequence").unum(save.sequence).end();
	o.key("autosave").num(save.autosave ? 1 : 0).end();
	o.key("play").num(w.playTimeMs).end();
	o.key("area").text(w.area).end();
	o.key("party");
	for (const std::string& id : w.lineup) {
		const rules::PartyMember* m = w.member(id);
		o.text(id + ":" + std::to_string(m ? m->level : 0));
	}
	o.end();
	o.key("difficulty").text(w.settings.difficulty).end();
	return o.str();
}

struct ParsedFile {
	std::vector<Chunk> chunks;
};

// Checks the magic and the checksum, splits the chunks and returns the version after the magic.
bool splitFile(const std::string& text, const char* fileMagic, ParsedFile& file, long long& version, std::string& error) {
	const std::string magic = std::string(fileMagic) + " ";
	if (text.compare(0, magic.size(), magic) != 0) {
		error = std::string("not a ") + (fileMagic == std::string(kSaveMagic) ? "save" : "clear-data") + " file";
		return false;
	}
	const std::size_t endLine = text.rfind("\nend ");
	if (endLine == std::string::npos) {
		error = "the save is truncated";
		return false;
	}
	const std::string body = text.substr(0, endLine + 1);
	std::string sum = text.substr(endLine + 5);
	while (!sum.empty() && (sum.back() == '\n' || sum.back() == '\r')) {
		sum.pop_back();
	}
	if (sum != hex(checksum(body))) {
		error = "the save is damaged (checksum mismatch)";
		return false;
	}
	const std::size_t firstEol = text.find('\n');
	if (!toInt(text.substr(magic.size(), firstEol - magic.size()), version) || version < 1) {
		error = "bad version";
		return false;
	}
	std::size_t pos = firstEol + 1;
	return splitChunks(body, pos, file.chunks, error);
}

bool splitSave(const std::string& text, ParsedFile& file, std::string& error) {
	long long schema = 0;
	if (!splitFile(text, kSaveMagic, file, schema, error)) {
		return false;
	}
	if (schema > kSaveSchemaVersion) {
		error = "the save is from a newer build (schema " + std::to_string(schema) + ")";
		return false;
	}
	for (const Migration& m : kMigrations) {
		if (m.from >= schema && !m.apply(file.chunks, error)) {
			error = "migrating from schema " + std::to_string(m.from) + ": " + error;
			return false;
		}
	}
	return true;
}

}

std::string serializeWorldChunks(const World& world) {
	return worldChunks(world);
}

bool parseWorldChunks(const std::string& text, World& world, std::string& error) {
	std::vector<Chunk> chunks;
	std::size_t pos = 0;
	if (!splitChunks(text, pos, chunks, error)) {
		return false;
	}
	World w;
	if (!applyChunks(chunks, w, allWorldChunks(), error)) {
		return false;
	}
	world = std::move(w);
	return true;
}

std::string serializeSave(const SaveFile& save) {
	std::string out = std::string(kSaveMagic) + " " + std::to_string(kSaveSchemaVersion) + "\n";
	writeChunk(out, "header", kHeaderVersion, headerChunk(save));
	out += worldChunks(save.world);
	if (!save.midEvent.empty()) {
		Writer o;
		o.key("state").text(save.midEvent).end();
		writeChunk(out, "midevent", kMidEventVersion, o.str());
	}
	out += "end " + hex(checksum(out)) + "\n";
	return out;
}

rules::Result<SaveFile> parseSave(const std::string& text) {
	using R = rules::Result<SaveFile>;
	ParsedFile file;
	std::string error;
	if (!splitSave(text, file, error)) {
		return R::failure(error);
	}
	SaveFile save;
	if (!applyChunks(file.chunks, save.world, allWorldChunks(), error)) {
		return R::failure(error);
	}
	for (const Chunk& c : file.chunks) {
		if (c.type == "header") {
			Lines in("header", c.payload);
			for (const auto& l : in.lines()) {
				if (l[0] == "written" && in.need(l, 1)) {
					save.writeTime = in.num(l[1]);
				} else if (l[0] == "sequence" && in.need(l, 1)) {
					toUint(l[1], save.sequence);
				} else if (l[0] == "autosave" && in.need(l, 1)) {
					save.autosave = in.num(l[1]) != 0;
				} else if (l[0] == "content" && in.need(l, 1)) {
					save.contentHash = std::strtoull(l[1].c_str(), nullptr, 16);
				}
			}
			if (!in.error.empty()) {
				return R::failure(in.error);
			}
		} else if (c.type == "midevent") {
			Lines in("midevent", c.payload);
			for (const auto& l : in.lines()) {
				if (l[0] == "state" && in.need(l, 1)) {
					save.midEvent = in.text(l[1]);
				}
			}
			if (!in.error.empty()) {
				return R::failure(in.error);
			}
		}
	}
	return R::success(std::move(save));
}

rules::Result<SaveSummary> readSummary(const std::string& text) {
	using R = rules::Result<SaveSummary>;
	ParsedFile file;
	std::string error;
	if (!splitSave(text, file, error)) {
		return R::failure(error);
	}
	SaveSummary s;
	for (const Chunk& c : file.chunks) {
		if (c.type != "header") {
			continue;
		}
		Lines in("header", c.payload);
		for (const auto& l : in.lines()) {
			const std::string& k = l[0];
			if (k == "written" && in.need(l, 1)) {
				s.writeTime = in.num(l[1]);
			} else if (k == "sequence" && in.need(l, 1)) {
				toUint(l[1], s.sequence);
			} else if (k == "autosave" && in.need(l, 1)) {
				s.autosave = in.num(l[1]) != 0;
			} else if (k == "play" && in.need(l, 1)) {
				s.playTimeMs = in.num(l[1]);
			} else if (k == "area" && in.need(l, 1)) {
				s.area = in.text(l[1]);
			} else if (k == "difficulty" && in.need(l, 1)) {
				s.difficulty = in.text(l[1]);
			} else if (k == "party") {
				for (std::size_t i = 1; i < l.size(); ++i) {
					s.party.push_back(in.text(l[i]));
				}
			}
		}
		if (!in.error.empty()) {
			return R::failure(in.error);
		}
	}
	return R::success(s);
}

std::string serializeClearRecord(const World& world, std::uint64_t contentHash, std::int64_t writeTime) {
	std::string out = std::string(kClearMagic) + " " + std::to_string(kClearVersion) + "\n";
	Writer o;
	o.key("build").text(kGameBuild).end();
	o.key("rules").num(rules::kRulesVersion).end();
	o.key("content").text(hex(contentHash)).end();
	o.key("written").num(writeTime).end();
	o.key("play").num(world.playTimeMs).end();
	writeChunk(out, "header", kHeaderVersion, o.str());
	for (const char* type : kClearChunks) {
		const KnownChunk* k = knownChunk(type);
		writeChunk(out, k->type, k->version, k->write(world));
	}
	out += "end " + hex(checksum(out)) + "\n";
	return out;
}

rules::Result<World> parseClearRecord(const std::string& text) {
	using R = rules::Result<World>;
	ParsedFile file;
	long long version = 0;
	std::string error;
	if (!splitFile(text, kClearMagic, file, version, error)) {
		return R::failure(error);
	}
	if (version != kClearVersion) {
		return R::failure("clear-data version " + std::to_string(version) + " is not supported");
	}
	World w;
	if (!applyChunks(file.chunks, w, {std::begin(kClearChunks), std::end(kClearChunks)}, error)) {
		return R::failure(error);
	}
	return R::success(std::move(w));
}

bool writeFileAtomic(const std::string& path, const std::string& text, std::string& error) {
	const std::string temp = path + ".tmp";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out) {
			error = "cannot open " + temp;
			return false;
		}
		out.write(text.data(), static_cast<std::streamsize>(text.size()));
		out.flush();
		if (!out) {
			error = "cannot write " + temp;
			return false;
		}
	}
	// std::rename does not replace an existing file on Windows.
	std::error_code failed;
	std::filesystem::rename(temp, path, failed);
	if (failed) {
		error = "cannot replace " + path;
		std::remove(temp.c_str());
		return false;
	}
	return true;
}

bool readFile(const std::string& path, std::string& text) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return false;
	}
	std::ostringstream buffer;
	buffer << in.rdbuf();
	text = buffer.str();
	return true;
}

SaveStore::SaveStore(std::string directory, int manualSlots, int autosaveSlots)
	: directory_(std::move(directory)), manualSlots_(manualSlots), autosaveSlots_(autosaveSlots) {
	std::error_code ignored;
	std::filesystem::create_directories(directory_, ignored);
}

std::string SaveStore::path(const std::string& slot) const {
	return directory_ + "/" + slot + ".sav";
}

bool SaveStore::validSlot(const std::string& slot) const {
	for (int i = 0; i < manualSlots_; ++i) {
		if (slot == "manual_" + std::to_string(i)) {
			return true;
		}
	}
	for (int i = 0; i < autosaveSlots_; ++i) {
		if (slot == "auto_" + std::to_string(i)) {
			return true;
		}
	}
	return false;
}

bool SaveStore::write(const std::string& slot, const std::string& text, std::string& error) const {
	if (!validSlot(slot)) {
		error = "no save slot '" + slot + "'";
		return false;
	}
	return writeFileAtomic(path(slot), text, error);
}

bool SaveStore::read(const std::string& slot, std::string& text) const {
	return validSlot(slot) && readFile(path(slot), text);
}

std::vector<SaveSummary> SaveStore::list() const {
	std::vector<std::string> slots;
	for (int i = 0; i < manualSlots_; ++i) {
		slots.push_back("manual_" + std::to_string(i));
	}
	for (int i = 0; i < autosaveSlots_; ++i) {
		slots.push_back("auto_" + std::to_string(i));
	}
	std::vector<SaveSummary> out;
	for (const std::string& slot : slots) {
		std::string text;
		if (!readFile(path(slot), text)) {
			continue;
		}
		rules::Result<SaveSummary> summary = readSummary(text);
		SaveSummary s;
		if (summary.ok()) {
			s = *summary.value;
		} else {
			s.damaged = true;
			s.error = summary.error;
		}
		s.slot = slot;
		out.push_back(s);
	}
	return out;
}

std::string SaveStore::nextAutosaveSlot() const {
	const std::vector<SaveSummary> saves = list();
	std::string oldest;
	std::uint64_t oldestSequence = 0;
	for (int i = 0; i < autosaveSlots_; ++i) {
		const std::string slot = "auto_" + std::to_string(i);
		auto it = std::find_if(saves.begin(), saves.end(), [&](const SaveSummary& s) { return s.slot == slot; });
		if (it == saves.end() || it->damaged) {
			return slot;
		}
		if (oldest.empty() || it->sequence < oldestSequence) {
			oldest = slot;
			oldestSequence = it->sequence;
		}
	}
	return oldest;
}

std::string SaveStore::latest() const {
	std::string best;
	std::int64_t bestTime = 0;
	std::uint64_t bestSequence = 0;
	for (const SaveSummary& s : list()) {
		if (s.damaged) {
			continue;
		}
		if (best.empty() || s.writeTime > bestTime || (s.writeTime == bestTime && s.sequence > bestSequence)) {
			best = s.slot;
			bestTime = s.writeTime;
			bestSequence = s.sequence;
		}
	}
	return best;
}

std::uint64_t SaveStore::nextSequence() const {
	std::uint64_t next = 1;
	for (const SaveSummary& s : list()) {
		next = std::max(next, s.sequence + 1);
	}
	return next;
}

}
