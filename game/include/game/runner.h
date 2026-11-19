#pragma once

#include "game/session.h"

#include <ostream>
#include <string>

namespace game {

struct RunnerOptions {
	// Resolve every battle with Session::autoRound as soon as it starts.
	bool autoBattle = false;
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
	void flushOutput();
	void printEvents(const std::vector<rules::Event>& events);
	void finishBattleAuto();
	void status();
	bool walkTo(Cell goal);
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
	bool finished = false;
	std::int64_t playTimeMs = 0;
};

BotResult runBot(const GameData& data, std::uint64_t seed, std::optional<EncounterMode> mode, const std::string& route);

}
