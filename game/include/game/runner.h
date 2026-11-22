#pragma once

#include "game/session.h"

#include <ostream>
#include <string>

namespace game {

struct RunnerOptions {
	// Resolve every battle with Session::playBattle as soon as it starts.
	bool autoBattle = false;
	// With autoBattle, only battles the field starts (random and visible encounters, developer battles); a battle an
	// event starts waits for the script's commands. The `autobattle` command sets both.
	bool autoBattleEncountersOnly = false;
	// With autoBattle, the macro those battles play every round (Session::setAutoPlayMacro); empty plays them as
	// Session::playBattle does by default. `autobattle on|random MACRO` sets it.
	std::string autoBattleMacro;
	// With autoBattle, the trouble mode in place of the macro's and the rules' (Session::setAutoPlayMacro);
	// `autobattle on|random [MACRO] continue|recover|stop` sets it.
	rules::MacroTrouble autoBattleTrouble = rules::MacroTrouble::Default;
	// With autoBattle, the automatic choice's policy in place of the macro's and the rules' (`... power|expected`).
	rules::MacroPolicy autoBattlePolicy = rules::MacroPolicy::Default;
	// Print a line for each auto round in which macro trouble holds (`trouble on|off`).
	bool printTrouble = false;
	// Print each command before running it, so a scripted run reads as a transcript.
	bool echo = true;
};

// The terminal command language shared by game_cli (interactive and --script) and the pacing bot. One command
// per line; README.md lists them. Output is plain text with no terminal control codes.
class Runner {
public:
	Runner(Session& session, std::ostream& out, RunnerOptions options = RunnerOptions());

	// Runs one command line. Returns false after `quit`.
	bool execute(const std::string& line);
	// Runs events forward and prints everything pending until the session needs input.
	void settle();
	// Runs every line of a script. Returns false if it stopped on `quit`.
	bool runScript(const std::string& text);

	int errors() const { return errors_; }
	std::string prompt() const;

private:
	void print(const std::string& text);
	void error(const std::string& text);
	void report(const std::string& problem);
	void report(const rules::Message& refusal);
	void printOutput(const std::vector<Output>& outputs);
	void flushOutput();
	void printEvents(const std::vector<rules::Event>& events);
	void finishBattleAuto(const std::string& macro = "", rules::MacroTrouble trouble = rules::MacroTrouble::Default,
		rules::MacroPolicy policy = rules::MacroPolicy::Default);
	void fight(const std::string& macro, rules::MacroFallback fallback);
	void printTrouble(const rules::MacroTroubleReport& trouble);
	void checkMacro(const std::string& name);
	void listMacros();
	void macro(const std::vector<std::string>& w);
	void status();
	void dev(const std::vector<std::string>& w);
	void walkTo(Cell goal);
	void approach(const std::string& actor, bool talk);
	std::string battleName(int index) const;

	Session& session_;
	std::ostream& out_;
	RunnerOptions options_;
	int errors_ = 0;
	std::vector<std::string> battleNames_;
};

// Plays `route` (runner commands) with auto battles from a new game and returns the transcript-free metrics.
struct BotResult {
	std::string table;
	int battles = 0;
	int errors = 0;
	// Every boss flag is set, or the goal flag when one was given.
	bool finished = false;
	std::int64_t playTimeMs = 0;
};

BotResult runBot(const GameData& data, std::uint64_t seed, std::optional<EncounterMode> mode, const std::string& route, const std::string& goalFlag = "");

}
