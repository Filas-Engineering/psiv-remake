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
		case OutputKind::Ambient:
			print("~ " + o.text);
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
		if (e.type == rules::EventType::EnemyJoined && e.target == static_cast<int>(battleNames_.size()) && !e.ids.empty()) {
			const rules::EnemyDef* def = session_.data().db.enemy(e.ids.front());
			const std::string base = def ? def->name : e.ids.front();
			int same = 0;
			for (const std::string& shown : battleNames_) {
				same += shown == base || shown.rfind(base + " ", 0) == 0 ? 1 : 0;
			}
			battleNames_.push_back(same == 0 ? base : base + " " + std::string(1, static_cast<char>('A' + same)));
		}
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
		case rules::EventType::StatModified:
			print("  " + battleName(e.target) + "'s " + rules::toString(e.stat) + (e.amount > 0 ? " rises." : " falls."));
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
		case rules::EventType::EnemyJoined:
			print("  " + battleName(e.target) + " joins the battle.");
			break;
		// Self-destruct and remove-beside announce the leaving enemy with a message; a merge does not.
		case rules::EventType::EnemyLeft:
			if (e.actor >= 0) {
				print("  " + battleName(e.target) + " merges into " + battleName(e.actor) + ".");
			}
			break;
		case rules::EventType::EnemyTransformed:
			print("  " + battleName(e.target) + " becomes " + name(e.ids.empty() ? "" : e.ids.front()) + ".");
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
			if (!options_.autoBattle || (options_.autoBattleEncountersOnly && session_.battleSource() == BattleSource::Event)) {
				return;
			}
			finishBattleAuto(options_.autoBattleMacro);
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

void Runner::finishBattleAuto(const std::string& macro) {
	report(session_.setAutoPlayMacro(macro));
	const AutoBattle played = session_.playBattle();
	session_.setAutoPlayMacro("");
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

// Plays the battle round by round with the macro, each member whose step cannot run taking core's automatic choice.
void Runner::fight(const std::string& macro) {
	for (int round = 0; round < 60 && session_.battle(); ++round) {
		flushOutput();
		const rules::Message refused = session_.applyMacro(macro, nullptr, rules::MacroFallback::Automatic);
		if (!refused.empty()) {
			report(refused);
			return;
		}
		printEvents(session_.resolveRound());
	}
	if (session_.battle()) {
		error("the battle did not end in 60 rounds");
	}
}

void Runner::listMacros() {
	for (const MacroView& view : session_.macroViews()) {
		print("  " + view.name + (view.name == session_.world().autoMacro ? " (auto)" : ""));
		for (std::size_t i = 0; i < view.steps.size(); ++i) {
			const rules::MacroStep& step = view.steps[i].step;
			std::string text = "    " + std::to_string(i) + " " + step.member + " " + (step.piece ? "piece" : rules::toString(step.kind));
			text += step.id.empty() ? "" : " " + step.id;
			switch (step.target.kind) {
			case rules::MacroTargetKind::Default:
				break;
			case rules::MacroTargetKind::Self:
			case rules::MacroTargetKind::All:
				text += std::string(" ") + rules::toString(step.target.kind);
				break;
			case rules::MacroTargetKind::Member:
				text += " " + step.target.member;
				break;
			case rules::MacroTargetKind::Enemy:
				text += " e" + std::to_string(step.target.enemySlot);
				break;
			}
			if (view.steps[i].issue != rules::MacroIssue::None) {
				text += std::string(" (") + rules::toString(view.steps[i].issue) + ")";
			}
			print(text);
		}
	}
}

// The macro editor's commands: create, rename, delete, move, step, removestep, movestep and combo.
void Runner::macro(const std::vector<std::string>& w) {
	int a = 0;
	int b = 0;
	if (w[1] == "create" && w.size() == 3) {
		report(session_.createMacro(w[2]));
	} else if (w[1] == "rename" && w.size() == 4) {
		report(session_.renameMacro(w[2], w[3]));
	} else if (w[1] == "delete" && w.size() == 3) {
		report(session_.deleteMacro(w[2]));
	} else if (w[1] == "move" && w.size() == 4 && toInt(w[3], a)) {
		report(session_.moveMacro(w[2], a));
	} else if (w[1] == "removestep" && w.size() == 4 && toInt(w[3], a)) {
		report(session_.removeMacroStep(w[2], a));
	} else if (w[1] == "movestep" && w.size() == 5 && toInt(w[3], a) && toInt(w[4], b)) {
		report(session_.moveMacroStep(w[2], a, b));
	} else if (w[1] == "combo" && w.size() == 4) {
		report(session_.macroFromCombo(w[2], w[3]));
	} else if (w[1] == "step" && w.size() >= 6 && w.size() <= 8 && toInt(w[3], a)) {
		rules::MacroStep step;
		step.member = w[4];
		step.piece = w[5] == "piece";
		if (!step.piece && !rules::parseCommandKind(w[5], step.kind)) {
			error("unknown command '" + w[5] + "'");
			return;
		}
		step.kind = step.piece ? rules::CommandKind::Item : step.kind;
		const bool takesId = step.kind == rules::CommandKind::Ability || step.kind == rules::CommandKind::Item;
		std::size_t next = 6;
		if (takesId && w.size() > next) {
			step.id = w[next++];
		}
		if (w.size() > next + 1) {
			error("macro step NAME INDEX MEMBER COMMAND [ID] [TARGET]");
			return;
		}
		const std::string target = w.size() > next ? w[next] : "default";
		int slot = 0;
		if (target.size() > 1 && target[0] == 'e' && toInt(target.substr(1), slot)) {
			step.target.kind = rules::MacroTargetKind::Enemy;
			step.target.enemySlot = slot;
		} else if (!rules::parseMacroTargetKind(target, step.target.kind) || step.target.kind == rules::MacroTargetKind::Member || step.target.kind == rules::MacroTargetKind::Enemy) {
			step.target.kind = rules::MacroTargetKind::Member;
			step.target.member = target;
		}
		report(session_.setMacroStep(w[2], a, step));
	} else {
		error("unknown macro command");
	}
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
	if (!w.aboard.empty()) {
		print("  Aboard the " + session_.displayName(w.aboard) + ".");
	}
}

// The developer commands a script uses to reach a part of the adventure directly; each marks the game.
void Runner::dev(const std::vector<std::string>& w) {
	int n = 0;
	if (w[1] == "warp" && w.size() == 4) {
		report(session_.devWarp(w[2], w[3]));
	} else if (w[1] == "flag" && w.size() == 4 && (w[3] == "on" || w[3] == "off")) {
		report(session_.devSetFlag(w[2], w[3] == "on"));
	} else if (w[1] == "give" && w.size() == 4 && toInt(w[3], n)) {
		report(session_.devGive(w[2], n));
	} else if (w[1] == "money" && w.size() == 3 && toInt(w[2], n)) {
		report(session_.devMoney(n));
	} else if (w[1] == "level" && w.size() == 4 && toInt(w[3], n)) {
		report(session_.devSetLevel(w[2], n));
	} else if (w[1] == "vehicle" && w.size() == 5) {
		report(session_.devVehicle(w[2], w[3], w[4]));
	} else {
		error("unknown dev command");
	}
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
	// `refused COMMAND` runs the command and passes only when the game refuses it, so a script can show a refusal.
	if (w[0] == "refused" && w.size() > 1) {
		const int before = errors_;
		const bool echo = options_.echo;
		options_.echo = false;
		const bool more = execute(line.substr(line.find("refused") + 7));
		options_.echo = echo;
		if (errors_ == before) {
			error("'" + line.substr(line.find("refused") + 8) + "' was not refused");
		} else {
			errors_ = before;
		}
		return more;
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
		// In a battle aboard, the vehicle acts for the party under its own id.
		if (const rules::Battle* b = session_.battle()) {
			for (int i = 0; i < b->partyCount(); ++i) {
				if (b->combatant(i).defId == text && !b->combatant(i).aboard) {
					out = i;
					return true;
				}
			}
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
	} else if (cmd == "erase" && w.size() == 2) {
		report(session_.eraseSave(w[1]));
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
	} else if (cmd == "board" && w.size() == 1) {
		report(session_.board());
	} else if (cmd == "disembark" && w.size() == 1) {
		report(session_.leaveVehicle());
	} else if (cmd == "destinations" && w.size() == 2) {
		TravelKind kind = TravelKind::Port;
		if (!parseTravelKind(w[1], kind)) {
			error("destinations port|town|dungeon_exit");
		} else {
			for (const TravelDestination& d : session_.travelDestinations(kind)) {
				print("  " + d.id + ": " + session_.displayName(d.area) + " (" + std::to_string(d.cell.x) + "," + std::to_string(d.cell.y) + ")");
			}
		}
	} else if (cmd == "travel" && w.size() >= 3 && w.size() <= 5) {
		TravelKind kind = TravelKind::Port;
		if (!parseTravelKind(w[1], kind)) {
			error("travel port DEST | travel town DEST THING [USER] | travel dungeon_exit THING [USER]");
		} else if (kind == TravelKind::Port) {
			report(session_.travel(kind, w[2]));
		} else if (kind == TravelKind::Town && w.size() >= 4) {
			report(session_.travel(kind, w[2], w.size() == 5 ? w[4] : "", w[3]));
		} else if (kind == TravelKind::DungeonExit && w.size() <= 4) {
			report(session_.travel(kind, "", w.size() == 4 ? w[3] : "", w[2]));
		} else {
			error("travel port DEST | travel town DEST THING [USER] | travel dungeon_exit THING [USER]");
		}
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
	} else if (mode == Mode::Battle && cmd == "fight" && w.size() == 2) {
		fight(w[1]);
	} else if (cmd == "macros" && w.size() == 1) {
		listMacros();
	} else if (cmd == "macro" && w.size() >= 3) {
		macro(w);
	} else if (cmd == "autobattle" && (w.size() == 2 || (w.size() == 3 && w[1] != "off")) && (w[1] == "on" || w[1] == "off" || w[1] == "random")) {
		options_.autoBattle = w[1] != "off";
		options_.autoBattleEncountersOnly = w[1] == "random";
		options_.autoBattleMacro = w.size() == 3 ? w[2] : "";
		if (!options_.autoBattleMacro.empty() && std::none_of(session_.world().macros.begin(), session_.world().macros.end(), [&](const rules::Macro& m) { return m.name == options_.autoBattleMacro; })) {
			error("no macro named '" + options_.autoBattleMacro + "'");
			options_.autoBattleMacro.clear();
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
	} else if (cmd == "equip" && w.size() == 4) {
		rules::EquipSlot position;
		if (!rules::parseEquipSlot(w[3], position)) {
			error("unknown slot '" + w[3] + "'");
		} else {
			report(session_.equip(w[1], w[2], position));
		}
	} else if (cmd == "gear" && w.size() == 2) {
		const rules::PartyMember* m = session_.world().member(w[1]);
		if (!m) {
			error("'" + w[1] + "' has not joined");
		} else {
			std::string worn;
			for (const auto& [slot, id] : m->equipment) {
				worn += std::string(worn.empty() ? "" : ", ") + rules::toString(slot) + " " + id;
			}
			const rules::Combatant c = rules::memberCombatant(session_.data().db, *m);
			print("  " + session_.displayName(w[1]) + ": " + (worn.empty() ? "nothing" : worn) + "; strength " + std::to_string(c.stats.strength) + ", defense " +
				std::to_string(c.stats.defense) + ", attack " + c.basicAttack + ".");
		}
	} else if (cmd == "stock" && mode == Mode::Shop) {
		const Shop* shop = session_.shop();
		std::string sold;
		for (const std::string& id : session_.stock(*shop)) {
			sold += (sold.empty() ? "" : ", ") + id + " " + std::to_string(session_.data().price(id));
		}
		print("  " + std::string(toString(shop->kind)) + " shop " + shop->id + ": " + (sold.empty() ? "nothing" : sold) + ".");
	} else if (cmd == "dev" && w.size() >= 2) {
		dev(w);
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
	} else if (cmd == "party" && w.size() == 1) {
		const World& world = session_.world();
		std::string lineup;
		for (const std::string& id : world.lineup) {
			lineup += (lineup.empty() ? "" : ", ") + session_.displayName(id);
		}
		std::string reserve;
		for (const rules::PartyMember& m : world.roster) {
			if (std::find(world.lineup.begin(), world.lineup.end(), m.characterId) == world.lineup.end()) {
				reserve += (reserve.empty() ? "" : ", ") + session_.displayName(m.characterId);
			}
		}
		print("  Lineup: " + lineup + ". Reserve: " + (reserve.empty() ? "nobody" : reserve) + ".");
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
			for (const Port& p : a->ports) put(p.at, 'P');
			for (const Helm& h : a->helms) put(h.at, 'H');
			for (Cell g : a->gangway) put(g, 'G');
			for (const VehicleView& v : session_.vehiclesHere()) put(v.cell, 'V');
			for (const VisibleEnemy& e : session_.world().enemies) put(e.position, e.state == EnemyState::Chasing ? '!' : 'E');
			put(session_.world().position, '@');
			for (const std::string& r : rows) {
				print("  " + r);
			}
		}
	} else if (cmd == "actors" && w.size() == 1) {
		if (!session_.area()) {
			error("there is no area");
		} else {
			const std::vector<ActorView> actors = session_.actorsHere();
			if (actors.empty()) {
				print("  (no actors)");
			}
			for (const ActorView& a : actors) {
				std::string text = "  " + a.id + " at " + std::to_string(a.cell.x) + " " + std::to_string(a.cell.y) + " facing " + toString(a.facing) +
					(a.visible ? " shown" : " hidden");
				if (a.solid) {
					text += " solid";
				}
				if (a.vanish != ActorVanish::None) {
					text += std::string(" ") + toString(a.vanish);
				}
				if (a.removed != ActorRemoval::None) {
					text += std::string(" removed ") + toString(a.removed);
				}
				if (!a.animation.empty()) {
					text += " anim " + a.animation + " " + std::to_string(a.animationMs) + "/" + std::to_string(a.animationTotalMs) + "ms";
				}
				if (!a.spriteEnemy.empty()) {
					text += " enemy " + a.spriteEnemy;
				}
				if (!a.look.empty()) {
					text += " look " + a.look;
				}
				if (a.wanders) {
					text += " wanders";
				}
				if (a.patrols) {
					text += " patrols";
				}
				if (a.onPath) {
					text += " path " + std::to_string(a.pathX) + " " + std::to_string(a.pathY) + " from " + std::to_string(a.home.x) + " " + std::to_string(a.home.y);
				}
				print(text);
			}
		}
	} else if (cmd == "credits" && w.size() == 1) {
		const std::optional<CreditsView> credits = session_.credits();
		if (!credits) {
			error("no credits are playing");
			return true;
		}
		print("Credits " + credits->id + (credits->music.empty() ? "" : ", music " + credits->music) + ", " + std::to_string(credits->shown) + " of " +
			std::to_string(credits->entries.size()) + " shown, " + std::to_string(credits->elapsedMs) + " of " + std::to_string(credits->totalMs) + " ms.");
		for (std::size_t i = 0; i < credits->entries.size(); ++i) {
			const CreditsEntryView& e = credits->entries[i];
			print(std::string("  ") + (static_cast<int>(i) < credits->shown ? "+ " : "- ") + (e.kind == CreditsEntryKind::Heading ? "heading " : "line ") + e.text + " (" +
				std::to_string(e.pauseMs) + " ms" + (e.gap > 0 ? ", gap " + std::to_string(e.gap) : "") + ")");
		}
	} else if (cmd == "background" && w.size() == 1) {
		const std::vector<EventPosition> running = session_.backgroundEvents();
		if (running.empty()) {
			print("  background: none");
		}
		for (const EventPosition& at : running) {
			print("  background: " + at.event + " at " + at.file + ":" + std::to_string(at.line));
		}
	} else if (cmd == "platforms" && w.size() == 1) {
		const std::vector<PlatformView> platforms = session_.platformsHere();
		if (platforms.empty()) {
			print("  platforms: none");
		}
		for (const PlatformView& p : platforms) {
			const std::string where = p.end == 0 ? "docked at from" : p.end == 1 ? "docked at to" : "moving";
			print("  platform " + p.id + " at " + std::to_string(p.cell.x) + " " + std::to_string(p.cell.y) + " (" + std::to_string(p.x) + ", " + std::to_string(p.y) +
				" px) " + where + (p.pauseMs > 0 ? ", resting " + std::to_string(p.pauseMs) + " ms" : "") + (p.carrying ? ", carrying the party" : ""));
		}
	} else if (cmd == "pictures" && w.size() == 1) {
		std::string text;
		for (const std::string& p : session_.pictures()) {
			text += (text.empty() ? "" : ", ") + p;
		}
		print("  pictures: " + (text.empty() ? std::string("none") : text));
	} else if (cmd == "camera" && w.size() == 1) {
		const World::SceneCamera& c = session_.sceneCamera();
		const std::string moving = c.ms > 0 ? ", " + std::to_string(c.ms) + "/" + std::to_string(c.totalMs) + " ms" : "";
		print(c.held ? "  camera: at " + std::to_string(c.cell.x) + " " + std::to_string(c.cell.y) + moving
					 : "  camera: leader" + (c.ms > 0 ? " (returning" + moving + ")" : std::string()));
	} else if (cmd == "motion" && w.size() == 1) {
		const FieldMotion m = session_.fieldMotion();
		print("  motion: next tick in " + std::to_string(m.nextTickUs) + " us, a step lands in " + std::to_string(m.stepLandsInUs) + " us, " +
			std::to_string(m.runStepLandsInUs) + " us running; last exit sound " + (session_.exitSound().empty() ? std::string("none") : session_.exitSound()));
	} else if (cmd == "rows" && w.size() == 1) {
		if (const Area* a = session_.area()) {
			for (const std::string& r : a->rows) {
				print("  " + r);
			}
		} else {
			error("there is no area");
		}
	} else if (cmd == "prompt" && w.size() == 1) {
		if (const std::optional<InteractTarget> t = session_.interactTarget()) {
			print(std::string("  prompt: ") + toString(t->kind) + " " + t->id + " at " + std::to_string(t->cell.x) + " " + std::to_string(t->cell.y) + " facing " +
				toString(t->facing) + (t->across ? " across" : "") + (t->keeper.empty() ? "" : " kept by " + t->keeper) + (t->ready ? "" : " (refused)"));
		} else {
			print("  prompt: nothing");
		}
	} else if (cmd == "steps" && w.size() == 1) {
		std::string text;
		for (Facing f : {Facing::North, Facing::East, Facing::South, Facing::West}) {
			text += std::string(text.empty() ? "" : ", ") + toString(f) + (session_.canStep(f) ? " yes" : " no");
		}
		print("  steps: " + text);
	} else if (cmd == "time" && w.size() == 1) {
		print("  play time " + std::to_string(session_.world().playTimeMs) + " ms; a step takes " + std::to_string(session_.leaderStepMs()) + " ms, " +
			std::to_string(session_.leaderStepMs(true)) + " ms with the run input");
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

BotResult runBot(const GameData& data, std::uint64_t seed, std::optional<EncounterMode> mode, const std::string& route, const std::string& goalFlag) {
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
	if (!goalFlag.empty()) {
		result.finished = session.world().flag(goalFlag);
		return result;
	}
	result.finished = !data.config.bosses.empty();
	for (const Boss& b : data.config.bosses) {
		result.finished = result.finished && session.world().flag(b.flag);
	}
	return result;
}

}
