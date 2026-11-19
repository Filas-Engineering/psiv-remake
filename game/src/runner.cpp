#include "game/runner.h"

#include "rules/names.h"
#include "rules/progression.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>

namespace game {

namespace {

std::vector<std::string> words(const std::string& line) {
	std::vector<std::string> out;
	std::istringstream in(line);
	std::string w;
	while (in >> w) {
		out.push_back(w);
	}
	return out;
}

bool toInt(const std::string& text, int& out) {
	if (text.empty()) {
		return false;
	}
	char* end = nullptr;
	const long v = std::strtol(text.c_str(), &end, 10);
	if (*end != '\0') {
		return false;
	}
	out = static_cast<int>(v);
	return true;
}

std::string clock(std::int64_t ms) {
	const std::int64_t s = ms / 1000;
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%lld:%02lld:%02lld", static_cast<long long>(s / 3600), static_cast<long long>(s / 60 % 60), static_cast<long long>(s % 60));
	return buffer;
}

}

Runner::Runner(Session& session, std::ostream& out, RunnerOptions options) : session_(session), out_(out), options_(options) {}

void Runner::print(const std::string& text) {
	out_ << text << "\n";
}

void Runner::error(const std::string& text) {
	++errors_;
	print("! " + text);
}

void Runner::report(const std::string& problem) {
	if (!problem.empty()) {
		error(problem);
	}
}

void Runner::report(const rules::Message& refusal) {
	if (!refusal.empty()) {
		error(session_.render(refusal));
	}
}

std::string Runner::prompt() const {
	return std::string(toString(session_.mode())) + "> ";
}

void Runner::printOutput(const std::vector<Output>& outputs) {
	for (const Output& o : outputs) {
		switch (o.kind) {
		case OutputKind::Action:
			print("  (" + o.text + ")");
			break;
		case OutputKind::Warning:
			print("! " + o.text);
			break;
		case OutputKind::Autosave:
			print("[" + o.text + "]");
			break;
		default:
			print(o.text);
			break;
		}
	}
}

void Runner::flushOutput() {
	printOutput(session_.drainOutput());
}

std::string Runner::battleName(int index) const {
	return index >= 0 && index < static_cast<int>(battleNames_.size()) ? battleNames_[index] : "?";
}

void Runner::printEvents(const std::vector<rules::Event>& events) {
	const rules::TextTable& text = session_.language().text;
	auto name = [&](const std::string& id) { return session_.displayName(id); };
	for (const rules::Event& e : events) {
		switch (e.type) {
		case rules::EventType::RoundStarted:
			print("-- round " + std::to_string(e.round) + " --");
			break;
		case rules::EventType::Retargeted:
			print(battleName(e.actor) + " turns from " + battleName(e.previousTarget) + " to " + battleName(e.target) + ".");
			break;
		case rules::EventType::ActionStarted:
			if (e.command == rules::CommandKind::Defend) {
				print(battleName(e.actor) + " defends.");
			} else if (e.command == rules::CommandKind::Escape) {
				print(battleName(e.actor) + " tries to escape.");
			} else {
				print(battleName(e.actor) + " uses " + name(e.ability) + (e.target >= 0 ? " on " + battleName(e.target) : "") + ".");
			}
			break;
		case rules::EventType::ActionCancelled:
			print(rules::formatText(text, std::string("cancel.") + rules::toString(e.cancel),
				{battleName(e.actor), e.cancel == rules::CancelReason::ActorUnable ? name(e.status) : name(e.ability)}));
			break;
		case rules::EventType::ComboTriggered:
			print("Combo: " + name(e.ability) + ".");
			break;
		case rules::EventType::ComboDiscovered:
			print("  A new combination joins the combo log.");
			break;
		case rules::EventType::ItemHadNoEffect:
			print("  " + name(e.ability) + " has no effect and is kept.");
			break;
		case rules::EventType::Missed:
			print("  " + battleName(e.target) + " evades.");
			break;
		case rules::EventType::Critical:
			print("  Critical hit!");
			break;
		case rules::EventType::DamageDealt:
			print("  " + battleName(e.target) + " takes " + std::to_string(e.amount) + " damage.");
			break;
		case rules::EventType::Healed:
			print("  " + battleName(e.target) + " recovers " + std::to_string(e.amount) + ".");
			break;
		case rules::EventType::Revived:
			print("  " + battleName(e.target) + " is revived.");
			break;
		case rules::EventType::StatusApplied:
		case rules::EventType::StatusResisted:
		case rules::EventType::StatusRemoved:
			print("  " + rules::formatText(text, e.message, {battleName(e.target), name(e.status)}));
			break;
		case rules::EventType::StatusTicked:
			print("  " + battleName(e.target) + " suffers " + std::to_string(e.amount) + " from " + name(e.status) + ".");
			break;
		case rules::EventType::StatModified:
			print("  " + battleName(e.target) + "'s " + rules::toString(e.stat) + (e.amount > 0 ? " rises." : " falls."));
			break;
		case rules::EventType::StatModifierExpired:
			print("  " + battleName(e.target) + "'s " + rules::toString(e.stat) + " returns to normal.");
			break;
		case rules::EventType::CombatantDefeated:
			print("  " + battleName(e.target) + " falls.");
			break;
		case rules::EventType::EscapeAttempted:
			print(e.succeeded ? "  The party escapes!" : "  No escape!");
			break;
		case rules::EventType::Message:
			print(session_.render({e.message, e.params}));
			break;
		case rules::EventType::BattleEnded:
			print("Outcome: " + std::string(rules::toString(e.outcome)));
			break;
		default:
			break;
		}
	}
}

void Runner::settle() {
	for (int guard = 0; guard < 100000; ++guard) {
		flushOutput();
		const Mode mode = session_.mode();
		if (mode == Mode::Battle) {
			if (battleNames_.empty()) {
				const rules::Battle* b = session_.battle();
				std::map<std::string, int> seen;
				std::map<std::string, int> total;
				for (const rules::Combatant& c : b->combatants()) {
					total[c.name]++;
				}
				std::string enemies;
				for (int i = 0; i < static_cast<int>(b->combatants().size()); ++i) {
					const rules::Combatant& c = b->combatant(i);
					std::string name = c.name;
					if (!b->isParty(i) && total[c.name] > 1) {
						name += " " + std::string(1, static_cast<char>('A' + seen[c.name]++));
					}
					battleNames_.push_back(name);
					if (!b->isParty(i)) {
						enemies += (enemies.empty() ? "" : ", ") + name;
					}
				}
				print("Enemies: " + enemies + ".");
			}
			if (!options_.autoBattle) {
				return;
			}
			finishBattleAuto();
			continue;
		}
		battleNames_.clear();
		if (mode != Mode::Event) {
			return;
		}
		if (session_.linePending()) {
			print(session_.lineText());
			session_.confirmLine();
			continue;
		}
		if (const std::optional<ChoiceView> c = session_.choiceView()) {
			std::string text = c->prompt.empty() ? "Choose:" : c->prompt;
			for (std::size_t i = 0; i < c->options.size(); ++i) {
				text += " [" + std::to_string(i) + "] " + c->options[i];
			}
			print(text);
			return;
		}
		session_.update(100);
	}
	error("the game did not settle");
}

void Runner::finishBattleAuto() {
	const AutoBattle played = session_.playBattle();
	report(played.refusal);
	for (const AutoRound& round : played.rounds) {
		printOutput(round.output);
		printEvents(round.events);
	}
	if (!played.ended) {
		error("the battle did not end in 60 rounds");
	}
	flushOutput();
}

void Runner::status() {
	const World& w = session_.world();
	const Area* a = session_.area();
	print("Mode " + std::string(toString(session_.mode())) + ", " + (a ? session_.displayName(a->id) : std::string("-")) + " (" + std::to_string(w.position.x) + "," +
		std::to_string(w.position.y) + ") facing " + toString(w.facing) + ", play time " + clock(w.playTimeMs) + ".");
	for (const std::string& id : w.lineup) {
		const rules::PartyMember* m = w.member(id);
		const rules::Stats s = rules::memberStats(session_.data().db, *m);
		std::string statuses;
		for (const rules::ActiveStatus& status : m->statuses) {
			statuses += " [" + session_.displayName(status.id) + (status.remaining > 0 ? " " + std::to_string(status.remaining) : "") + "]";
		}
		print("  " + session_.displayName(id) + " Lv" + std::to_string(m->level) + " HP " + std::to_string(m->hp) + "/" +
			std::to_string(s.maxHp) + " TP " + std::to_string(m->tp) + "/" + std::to_string(s.maxTp) + " EXP " + std::to_string(m->experience) + statuses);
	}
	std::string items;
	for (const auto& [id, n] : w.items) {
		items += (items.empty() ? "" : ", ") + id + " x" + std::to_string(n);
	}
	for (const auto& [id, n] : w.equipmentBag) {
		items += (items.empty() ? "" : ", ") + id + " x" + std::to_string(n);
	}
	print("  Money " + std::to_string(w.money) + ". Items: " + (items.empty() ? "none" : items) + ".");
}

void Runner::walkTo(Cell goal) {
	PathWalk walk = session_.beginWalk(goal);
	for (;;) {
		const WalkStatus status = session_.walkStep(walk);
		if (status == WalkStatus::Walking) {
			settle();
			continue;
		}
		if (status == WalkStatus::NoPath) {
			error("no path to (" + std::to_string(goal.x) + "," + std::to_string(goal.y) + ")");
		} else if (status == WalkStatus::TooLong) {
			error("walking took too long");
		}
		return;
	}
}

bool Runner::execute(const std::string& rawLine) {
	std::string line = rawLine;
	const std::size_t hash = line.find('#');
	if (hash != std::string::npos) {
		line = line.substr(0, hash);
	}
	const std::vector<std::string> w = words(line);
	if (w.empty()) {
		return true;
	}
	if (options_.echo) {
		print(prompt() + line.substr(line.find_first_not_of(" \t")));
	}
	const std::string& cmd = w[0];
	const Mode mode = session_.mode();
	auto rest = [&](std::size_t from) {
		std::string out;
		for (std::size_t i = from; i < w.size(); ++i) {
			out += (out.empty() ? "" : " ") + w[i];
		}
		return out;
	};
	auto actorIndex = [&](const std::string& text, int& out) {
		if (toInt(text, out)) {
			return true;
		}
		const std::vector<std::string>& lineup = session_.world().lineup;
		auto it = std::find(lineup.begin(), lineup.end(), text);
		if (it == lineup.end()) {
			return false;
		}
		out = static_cast<int>(it - lineup.begin());
		return true;
	};
	auto targetIndex = [&](const std::string& text, int& out) {
		const rules::Battle* b = session_.battle();
		if (text.size() > 1 && (text[0] == 'e' || text[0] == 'p') && toInt(text.substr(1), out)) {
			if (text[0] == 'e') {
				out += b->partyCount();
			}
			return true;
		}
		return toInt(text, out);
	};

	Facing facing;
	int count = 1;
	if (cmd == "quit") {
		return false;
	} else if (cmd == "new") {
		report(session_.newGame());
	} else if (cmd == "continue") {
		report(session_.continueGame());
	} else if (cmd == "load" && w.size() == 2) {
		report(session_.load(w[1]));
	} else if (cmd == "retry") {
		report(session_.retryBattle());
	} else if (cmd == "title") {
		session_.quitToTitle();
	} else if (parseFacing(cmd, facing) && (w.size() == 1 || (w.size() == 2 && toInt(w[1], count)))) {
		for (int i = 0; i < count; ++i) {
			const rules::Message problem = session_.walk(facing);
			if (!problem.empty()) {
				report(problem);
				break;
			}
			settle();
			if (session_.mode() != Mode::Field) {
				break;
			}
		}
	} else if (cmd == "face" && w.size() == 2 && parseFacing(w[1], facing)) {
		report(session_.face(facing));
	} else if (cmd == "walk" && (w.size() == 2 || w.size() == 3)) {
		const Area* a = session_.area();
		Cell goal;
		int x = 0;
		int y = 0;
		if (w.size() == 3 && toInt(w[1], x) && toInt(w[2], y)) {
			goal = {x, y};
		} else if (w.size() == 2 && a && a->markers.count(w[1])) {
			goal = a->markers.at(w[1]);
		} else {
			error("walk takes a marker of this area or x y");
			return true;
		}
		walkTo(goal);
	} else if ((cmd == "talk" || cmd == "x" || cmd == "interact") && w.size() == 1) {
		report(session_.interact());
	} else if (cmd == "choose" && w.size() == 2 && toInt(w[1], count)) {
		report(session_.choose(count));
	} else if (cmd == "skip") {
		if (!session_.skip()) {
			error("this scene cannot be skipped");
		}
	} else if (cmd == "wait" && w.size() == 2 && toInt(w[1], count)) {
		session_.update(count);
	} else if (mode == Mode::Battle && cmd == "attack" && w.size() >= 2) {
		int actor = 0;
		int target = -1;
		if (!actorIndex(w[1], actor) || (w.size() == 3 && !targetIndex(w[2], target))) {
			error("attack <member> [eN|pN]");
		} else {
			report(session_.setCommand(actor, rules::Command::attack(target)));
		}
	} else if (mode == Mode::Battle && (cmd == "use" || cmd == "item") && w.size() >= 3) {
		int actor = 0;
		int target = -1;
		if (!actorIndex(w[1], actor) || (w.size() == 4 && !targetIndex(w[3], target))) {
			error(cmd + " <member> <id> [eN|pN]");
		} else {
			report(session_.setCommand(actor, cmd == "use" ? rules::Command::ability(w[2], target) : rules::Command::item(w[2], target)));
		}
	} else if (mode == Mode::Battle && (cmd == "defend" || cmd == "escape") && w.size() == 2) {
		int actor = 0;
		if (!actorIndex(w[1], actor)) {
			error(cmd + " <member>");
		} else {
			report(session_.setCommand(actor, cmd == "defend" ? rules::Command::defend() : rules::Command::escape()));
		}
	} else if (mode == Mode::Battle && cmd == "macro" && w.size() == 2) {
		std::vector<rules::MacroStepResult> results;
		report(session_.applyMacro(w[1], &results));
	} else if (mode == Mode::Battle && cmd == "round") {
		printEvents(session_.resolveRound());
	} else if (mode == Mode::Battle && cmd == "auto") {
		finishBattleAuto();
	} else if (mode == Mode::Battle && cmd == "cancel") {
		session_.cancelAuto();
	} else if (cmd == "menu") {
		report(session_.openMenu());
	} else if (cmd == "close" || cmd == "leave") {
		report(session_.closeMenu());
	} else if (cmd == "item" && (w.size() == 2 || w.size() == 3)) {
		report(session_.useItem(w[1], w.size() == 3 ? w[2] : ""));
	} else if (cmd == "tech" && (w.size() == 3 || w.size() == 4)) {
		report(session_.useAbility(w[1], w[2], w.size() == 4 ? w[3] : ""));
	} else if (cmd == "name" && w.size() >= 2) {
		report(session_.enterName(rest(1)));
	} else if (cmd == "chat") {
		report(session_.partyTalk());
	} else if (cmd == "quests") {
		const QuestLog log = session_.questLog();
		print("  Objective: " + (log.objective.empty() ? std::string("-") : log.objective));
		for (const JobView& job : log.jobs) {
			print("  Job: " + job.title + " (" + toString(job.state) + ")");
		}
	} else if (cmd == "codex") {
		for (const EncyclopediaView& entry : session_.encyclopedia()) {
			print("  " + entry.title + ": " + entry.text);
		}
		for (const events::GlossaryEntry& g : session_.data().glossary.entries) {
			print("  Glossary: " + g.restored + " (shipped as " + g.shipped + ")");
		}
	} else if (cmd == "log") {
		for (const std::string& page : session_.textLog()) {
			print("  " + page);
		}
	} else if (cmd == "equip" && w.size() == 3) {
		report(session_.equip(w[1], w[2]));
	} else if (cmd == "unequip" && w.size() == 3) {
		rules::EquipSlot slot;
		if (!rules::parseEquipSlot(w[2], slot)) {
			error("unknown slot '" + w[2] + "'");
		} else {
			report(session_.unequip(w[1], slot));
		}
	} else if ((cmd == "buy" || cmd == "sell") && (w.size() == 2 || (w.size() == 3 && toInt(w[2], count)))) {
		report(cmd == "buy" ? session_.buy(w[1], count) : session_.sell(w[1], count));
	} else if (cmd == "rest") {
		report(session_.rest());
	} else if (cmd == "save" && w.size() == 2) {
		report(session_.saveManual(w[1]));
	} else if (cmd == "saves") {
		if (!session_.store()) {
			error("saving is disabled");
		} else {
			for (const SaveSummary& s : session_.store()->list()) {
				std::string party;
				for (const std::string& p : s.party) {
					party += (party.empty() ? "" : " ") + p;
				}
				print("  " + s.slot + (s.damaged ? " DAMAGED: " + s.error : " " + s.area + " " + clock(s.playTimeMs) + " [" + party + "] " + s.difficulty + (s.autosave ? " (auto)" : "")));
			}
		}
	} else if (cmd == "set" && w.size() == 3) {
		Settings s = session_.world().settings;
		if (w[1] == "difficulty" || w[1] == "reward") {
			if (w[1] == "difficulty") {
				s.difficulty = w[2];
			} else if (!rules::parseRewardMultiplier(w[2], s.rewardMultiplier)) {
				error("bad reward multiplier '" + w[2] + "'");
				return true;
			}
			report(session_.setSettings(s));
		} else if (w[1] == "auto") {
			report(session_.setAutoMacro(w[2] == "-" ? "" : w[2]));
		} else {
			static const std::map<std::string, std::string> shortNames = {{"text", "text_speed"}, {"battle", "battle_speed"}, {"shipped", "show_shipped_names"}};
			auto shortName = shortNames.find(w[1]);
			Profile profile = session_.profile();
			const std::string problem = setProfileValue(profile, shortName == shortNames.end() ? w[1] : shortName->second, w[2]);
			if (problem.empty()) {
				report(session_.setProfile(profile));
			} else {
				report(problem);
			}
		}
	} else if (cmd == "status") {
		status();
	} else if (cmd == "look") {
		const Area* a = session_.area();
		if (!a) {
			error("there is no area");
		} else {
			std::vector<std::string> rows = a->rows;
			auto put = [&](Cell c, char ch) {
				if (a->inside(c)) {
					rows[c.y][c.x] = ch;
				}
			};
			for (const Exit& e : a->exits) put(e.at, '>');
			for (const Hook& h : a->hooks) if (h.trigger == HookTrigger::Interact) put(h.at, 'N');
			for (const Chest& c : a->chests) put(c.at, session_.world().flag(c.flag) ? 'c' : 'C');
			for (const Shop& s : a->shops) if (s.at) put(*s.at, '$');
			for (const RestPoint& r : a->rests) if (r.at) put(*r.at, 'R');
			for (const VisibleEnemy& e : session_.world().enemies) put(e.position, e.state == EnemyState::Chasing ? '!' : 'E');
			put(session_.world().position, '@');
			for (const std::string& r : rows) {
				print("  " + r);
			}
		}
	} else if (cmd == "pacing") {
		out_ << session_.pacing().table();
	} else {
		error("unknown command '" + line + "' in " + toString(mode) + " mode");
	}
	settle();
	return true;
}

bool Runner::runScript(const std::string& text) {
	std::istringstream in(text);
	std::string line;
	settle();
	while (std::getline(in, line)) {
		if (!execute(line)) {
			return false;
		}
	}
	return true;
}

BotResult runBot(const GameData& data, std::uint64_t seed, std::optional<EncounterMode> mode, const std::string& route) {
	SessionOptions options;
	options.seed = seed;
	options.encounterMode = mode;
	Session session(data, options);
	std::ostringstream transcript;
	RunnerOptions runnerOptions;
	runnerOptions.autoBattle = true;
	runnerOptions.echo = false;
	Runner runner(session, transcript, runnerOptions);
	runner.runScript(route);
	BotResult result;
	result.table = session.pacing().table();
	result.battles = session.pacing().totalBattles();
	result.errors = runner.errors();
	result.playTimeMs = session.world().playTimeMs;
	result.finished = !data.config.bosses.empty();
	for (const Boss& b : data.config.bosses) {
		result.finished = result.finished && session.world().flag(b.flag);
	}
	return result;
}

}
