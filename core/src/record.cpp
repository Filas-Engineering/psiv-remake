#include "rules/record.h"

#include "rules/battle.h"
#include "rules/names.h"
#include "text_encoding.h"

#include <cstdlib>
#include <sstream>

namespace rules {

namespace {

using text::parseInt;
using text::parseInt64;

std::string hex64(std::uint64_t value) {
	static const char digits[] = "0123456789abcdef";
	std::string out(16, '0');
	for (int i = 15; i >= 0; --i) {
		out[static_cast<std::size_t>(i)] = digits[value & 15];
		value >>= 4;
	}
	return out;
}

bool parseU64(const std::string& text, std::uint64_t& out, int base) {
	if (text.empty() || text[0] == '-') {
		return false;
	}
	char* end = nullptr;
	out = std::strtoull(text.c_str(), &end, base);
	return *end == '\0';
}

bool parseBool(const std::string& text, bool& out) {
	if (text != "0" && text != "1") {
		return false;
	}
	out = text == "1";
	return true;
}

// Splits "key=value" and checks the key.
bool keyed(const std::string& token, const char* key, std::string& value) {
	const std::string prefix = std::string(key) + "=";
	if (token.compare(0, prefix.size(), prefix) != 0) {
		return false;
	}
	value = token.substr(prefix.size());
	return true;
}

constexpr const char* kChecksumKey = "checksum ";

}

std::string serializeRecord(const BattleRecord& record) {
	const BattleSetup& setup = record.setup;
	std::ostringstream s;
	s << "battle_record " << kRecordFormatVersion << '\n';
	s << "rules_version " << record.rulesVersion << '\n';
	s << "event_schema " << record.eventSchemaVersion << '\n';
	s << "content_hash " << hex64(record.contentHash) << '\n';
	s << "seed " << setup.seed << '\n';
	s << "difficulty " << text::encode(setup.difficulty) << '\n';
	s << "reward_multiplier " << toString(setup.rewardMultiplier) << '\n';
	s << "opening " << toString(setup.opening) << '\n';
	s << "flags escape=" << setup.flags.escapeAllowed << " defeat_branch=" << setup.flags.defeatHasBranch
		<< " vehicle=" << setup.flags.vehicleBattle << '\n';
	s << "vehicle " << text::encode(setup.vehicle) << '\n';
	for (const auto& [id, uses] : setup.vehicleSkillUses) {
		s << "vehicle_uses " << text::encode(id) << ' ' << uses << '\n';
	}
	s << "escape_rating " << setup.escapeRating << '\n';
	s << "scripted_end " << toString(setup.scriptedEnd.kind) << ' ' << setup.scriptedEnd.rounds << ' ' << setup.scriptedEnd.enemySlot << '\n';
	s << "enemy_group " << text::encode(setup.enemyGroup) << '\n';
	for (const FormationSlot& slot : setup.enemies) {
		s << "enemy " << text::encode(slot.enemy) << ' ' << text::encode(slot.group) << '\n';
	}
	for (const auto& [id, count] : setup.inventory) {
		s << "inventory " << text::encode(id) << ' ' << count << '\n';
	}
	for (const PartyMember& member : setup.party) {
		s << "member " << text::encode(member.characterId) << ' ' << member.level << ' ' << member.experience << ' '
			<< member.hp << ' ' << member.tp << '\n';
		if (!member.name.empty()) {
			s << "member_name " << text::encode(member.name) << '\n';
		}
		for (const auto& [id, uses] : member.skillUses) {
			s << "skill_uses " << text::encode(id) << ' ' << uses << '\n';
		}
		for (const auto& [slot, id] : member.equipment) {
			s << "equip " << toString(slot) << ' ' << text::encode(id) << '\n';
		}
		for (const ActiveStatus& status : member.statuses) {
			s << "member_status " << text::encode(status.id) << ' ' << status.remaining << '\n';
		}
		for (const std::string& id : member.taught) {
			s << "member_taught " << text::encode(id) << '\n';
		}
	}
	for (const ComboLogEntry& entry : record.knownCombos) {
		s << "known_combo " << text::encode(entry.recipe) << ' ' << entry.playTimeMs << ' ' << text::encodeList(entry.characters) << '\n';
	}
	for (const RoundInput& round : record.rounds) {
		s << "round " << round.playTimeMs << ' ' << text::encode(round.macroName) << '\n';
		for (const RecordedCommand& c : round.commands) {
			s << "command " << c.actor << ' ' << toString(c.command.kind) << ' ' << text::encode(c.command.id) << ' '
				<< c.command.target << '\n';
		}
		if (!round.macroBlock.empty()) {
			s << "macro_block";
			for (int actor : round.macroBlock) {
				s << ' ' << actor;
			}
			s << '\n';
		}
	}
	s << "end\n";
	const std::string body = s.str();
	return body + kChecksumKey + text::checksum(body) + '\n';
}

Result<BattleRecord> parseRecord(const std::string& text) {
	BattleRecord record;
	BattleSetup& setup = record.setup;
	std::istringstream in(text);
	std::string line;
	int lineNumber = 0;
	bool sawHeader = false;
	bool ended = false;
	std::size_t bodyLength = 0;
	std::string checksum;
	auto fail = [&](const std::string& message) {
		return Result<BattleRecord>::failure("record line " + std::to_string(lineNumber) + ": " + message);
	};

	while (std::getline(in, line)) {
		++lineNumber;
		const std::vector<std::string> t = text::split(line);
		if (t.empty()) {
			continue;
		}
		const std::string& key = t[0];
		const std::size_t n = t.size();
		if (ended) {
			if (key != "checksum" || n != 2 || !checksum.empty()) {
				return fail("content after 'end'");
			}
			checksum = t[1];
			continue;
		}
		if (!sawHeader) {
			int version = 0;
			if (key != "battle_record" || n != 2 || !parseInt(t[1], version)) {
				return fail("expected 'battle_record <version>'");
			}
			// Format 4 differs only in having no escape rating, and format 3 also has no member names.
			if (version != kRecordFormatVersion && version != 5 && version != 4 && version != 3) {
				return fail("record format " + t[1] + " is not supported (this build reads " + std::to_string(kRecordFormatVersion) + ")");
			}
			sawHeader = true;
			continue;
		}
		bool ok = true;
		if (key == "rules_version" && n == 2) {
			ok = parseInt(t[1], record.rulesVersion);
		} else if (key == "event_schema" && n == 2) {
			ok = parseInt(t[1], record.eventSchemaVersion);
		} else if (key == "content_hash" && n == 2) {
			ok = parseU64(t[1], record.contentHash, 16);
		} else if (key == "seed" && n == 2) {
			ok = parseU64(t[1], setup.seed, 10);
		} else if (key == "difficulty" && n == 2) {
			ok = text::decode(t[1], setup.difficulty);
		} else if (key == "reward_multiplier" && n == 2) {
			ok = parseRewardMultiplier(t[1], setup.rewardMultiplier);
		} else if (key == "opening" && n == 2) {
			ok = parseOpening(t[1], setup.opening);
		} else if (key == "flags" && n == 4) {
			std::string a;
			std::string b;
			std::string c;
			ok = keyed(t[1], "escape", a) && keyed(t[2], "defeat_branch", b) && keyed(t[3], "vehicle", c)
				&& parseBool(a, setup.flags.escapeAllowed) && parseBool(b, setup.flags.defeatHasBranch) && parseBool(c, setup.flags.vehicleBattle);
		} else if (key == "vehicle" && n == 2) {
			ok = text::decode(t[1], setup.vehicle);
		} else if (key == "vehicle_uses" && n == 3) {
			std::string id;
			int uses = 0;
			ok = text::decode(t[1], id) && parseInt(t[2], uses) && !id.empty();
			setup.vehicleSkillUses[id] = uses;
		} else if (key == "member_taught" && n == 2) {
			if (setup.party.empty()) {
				return fail("'member_taught' before any 'member'");
			}
			std::string id;
			ok = text::decode(t[1], id) && !id.empty();
			setup.party.back().taught.push_back(id);
		} else if (key == "escape_rating" && n == 2) {
			ok = parseInt(t[1], setup.escapeRating);
		} else if (key == "scripted_end" && n == 4) {
			ok = parseScriptedEndKind(t[1], setup.scriptedEnd.kind) && parseInt(t[2], setup.scriptedEnd.rounds) && parseInt(t[3], setup.scriptedEnd.enemySlot);
		} else if (key == "enemy_group" && n == 2) {
			ok = text::decode(t[1], setup.enemyGroup);
		} else if (key == "enemy" && n == 3) {
			FormationSlot slot;
			ok = text::decode(t[1], slot.enemy) && text::decode(t[2], slot.group) && !slot.enemy.empty();
			setup.enemies.push_back(slot);
		} else if (key == "inventory" && n == 3) {
			std::string id;
			int count = 0;
			ok = text::decode(t[1], id) && parseInt(t[2], count) && !id.empty();
			setup.inventory[id] = count;
		} else if (key == "member" && n == 6) {
			PartyMember member;
			ok = text::decode(t[1], member.characterId) && parseInt(t[2], member.level) && parseInt(t[3], member.experience)
				&& parseInt(t[4], member.hp) && parseInt(t[5], member.tp);
			setup.party.push_back(member);
		} else if (key == "member_name" && n == 2) {
			if (setup.party.empty()) {
				return fail("'member_name' before any 'member'");
			}
			ok = text::decode(t[1], setup.party.back().name) && !setup.party.back().name.empty();
		} else if ((key == "skill_uses" || key == "equip" || key == "member_status") && n == 3) {
			if (setup.party.empty()) {
				return fail("'" + key + "' before any 'member'");
			}
			PartyMember& member = setup.party.back();
			std::string id;
			if (key == "skill_uses") {
				int uses = 0;
				ok = text::decode(t[1], id) && parseInt(t[2], uses);
				member.skillUses[id] = uses;
			} else if (key == "equip") {
				EquipSlot slot = EquipSlot::Weapon;
				ok = parseEquipSlot(t[1], slot) && text::decode(t[2], id);
				member.equipment[slot] = id;
			} else {
				ActiveStatus status;
				ok = text::decode(t[1], status.id) && parseInt(t[2], status.remaining);
				member.statuses.push_back(status);
			}
		} else if (key == "known_combo" && n == 4) {
			ComboLogEntry entry;
			ok = text::decode(t[1], entry.recipe) && parseInt64(t[2], entry.playTimeMs) && text::decodeList(t[3], entry.characters);
			record.knownCombos.push_back(entry);
		} else if (key == "round" && n == 3) {
			RoundInput round;
			ok = parseInt64(t[1], round.playTimeMs) && text::decode(t[2], round.macroName);
			record.rounds.push_back(round);
		} else if ((key == "command" && n == 5) || key == "macro_block") {
			if (record.rounds.empty()) {
				return fail("'" + key + "' before any 'round'");
			}
			RoundInput& round = record.rounds.back();
			if (key == "command") {
				RecordedCommand c;
				ok = parseInt(t[1], c.actor) && parseCommandKind(t[2], c.command.kind) && parseInt(t[4], c.command.target) && text::decode(t[3], c.command.id);
				round.commands.push_back(c);
			} else {
				for (std::size_t i = 1; i < n && ok; ++i) {
					int actor = 0;
					ok = parseInt(t[i], actor);
					round.macroBlock.push_back(actor);
				}
			}
		} else if (key == "end" && n == 1) {
			ended = true;
			bodyLength = static_cast<std::size_t>(in.tellg() == std::streampos(-1) ? text.size() : static_cast<std::size_t>(in.tellg()));
		} else {
			return fail("unknown or malformed line '" + line + "'");
		}
		if (!ok) {
			return fail("bad value in '" + line + "'");
		}
	}
	if (!sawHeader) {
		return Result<BattleRecord>::failure("record line 1: expected 'battle_record <version>'");
	}
	if (!ended) {
		return Result<BattleRecord>::failure("record ends without 'end'");
	}
	if (checksum.empty()) {
		return Result<BattleRecord>::failure("record has no checksum line");
	}
	if (checksum != text::checksum(text.substr(0, bodyLength))) {
		return Result<BattleRecord>::failure("record checksum does not match: the record is damaged");
	}
	return Result<BattleRecord>::success(std::move(record));
}

Result<Battle> replayBattle(const Database& db, const BattleRecord& record, ComboLog& log, std::vector<Event>& events) {
	if (record.rulesVersion != kRulesVersion) {
		return Result<Battle>::failure("the record uses rules version " + std::to_string(record.rulesVersion) + ", this build has " + std::to_string(kRulesVersion));
	}
	if (record.eventSchemaVersion != kEventSchemaVersion) {
		return Result<Battle>::failure("the record uses event schema " + std::to_string(record.eventSchemaVersion) + ", this build has " + std::to_string(kEventSchemaVersion));
	}
	if (record.contentHash != db.contentHash) {
		return Result<Battle>::failure("the record was made with different data (content hash " + hex64(record.contentHash) + ", loaded " + hex64(db.contentHash) + ")");
	}
	for (const ComboLogEntry& entry : record.knownCombos) {
		log.discover(entry.recipe, entry.playTimeMs, entry.characters);
	}
	Result<Battle> created = Battle::create(db, record.setup, &log);
	if (!created.ok()) {
		return created;
	}
	Battle& battle = *created.value;
	for (std::size_t r = 0; r < record.rounds.size(); ++r) {
		const RoundInput& round = record.rounds[r];
		// Enemies that joined in earlier rounds can be named from then on.
		const int count = static_cast<int>(battle.combatants_.size());
		const std::string where = "round " + std::to_string(r + 1) + ": ";
		if (battle.outcome() != Outcome::Ongoing) {
			return Result<Battle>::failure(where + "the battle had already ended");
		}
		for (const RecordedCommand& c : round.commands) {
			if (c.actor < 0 || c.actor >= count || c.command.target < -1 || c.command.target >= count) {
				return Result<Battle>::failure(where + "command for combatant " + std::to_string(c.actor) + " is out of range");
			}
			battle.commands_[static_cast<std::size_t>(c.actor)] = c.command;
		}
		for (int actor : round.macroBlock) {
			if (actor < 0 || actor >= battle.partyCount_) {
				return Result<Battle>::failure(where + "macro block names combatant " + std::to_string(actor));
			}
		}
		battle.forcedOrder_ = round.macroBlock;
		battle.macroName_ = round.macroName;
		battle.playTimeMs_ = round.playTimeMs;
		const std::vector<Event> roundEvents = battle.resolveRound();
		events.insert(events.end(), roundEvents.begin(), roundEvents.end());
	}
	return created;
}

}
