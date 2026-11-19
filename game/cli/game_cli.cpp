#include "game/data.h"
#include "game/runner.h"
#include "game/save.h"
#include "game/session.h"

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

namespace {

void usage() {
	std::cout << "game_cli: play the placeholder adventure in a terminal\n"
				 "\n"
				 "  game_cli [--data DIR] [--seed N] [--saves DIR] [--auto-battle] [--encounters visible|random]\n"
				 "  game_cli --script FILE ...      run the commands in FILE, print the transcript, then exit\n"
				 "  game_cli --bot [--route FILE]   walk the route in both encounter modes and print pacing tables\n"
				 "\n"
				 "Defaults: --data data, --seed 1, --saves saves, --route DATA/routes/main.route.\n"
				 "Type 'help' at the prompt for the command list.\n"
				 "Exit codes: 0 done, 1 data error, 2 bad arguments, 3 a scripted command failed or the bot did not finish.\n";
}

void commands() {
	std::cout << "title:  new | continue | load SLOT | retry | quit\n"
				 "field:  n|e|s|w [COUNT] | walk MARKER | walk X Y | face DIR | talk | menu | look | status | wait MS\n"
				 "event:  choose N | skip\n"
				 "battle: attack M [eN] | use M ABILITY [eN|pN] | item M ITEM [eN|pN] | defend M | escape M | macro NAME | round | auto\n"
				 "menu:   item ITEM MEMBER | equip MEMBER PIECE | unequip MEMBER SLOT | save manual_N | saves | close\n"
				 "shop:   buy ID [N] | sell ID [N] | leave      inn: rest | close\n"
				 "any:    set difficulty|reward|text|battle|shipped|auto VALUE | pacing | title\n";
}

bool parseSeed(const std::string& text, std::uint64_t& out) {
	char* end = nullptr;
	out = std::strtoull(text.c_str(), &end, 10);
	return !text.empty() && *end == '\0';
}

}

int main(int argc, char** argv) {
	std::string dataDir = "data";
	std::string saves = "saves";
	std::string script;
	std::string route;
	std::uint64_t seed = 1;
	bool bot = false;
	bool autoBattle = false;
	std::optional<game::EncounterMode> mode;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		auto value = [&](std::string& out) {
			if (i + 1 >= argc) {
				return false;
			}
			out = argv[++i];
			return true;
		};
		std::string text;
		if (arg == "--help" || arg == "-h") {
			usage();
			return 0;
		} else if (arg == "--data" && value(dataDir)) {
		} else if (arg == "--saves" && value(saves)) {
		} else if (arg == "--script" && value(script)) {
		} else if (arg == "--route" && value(route)) {
		} else if (arg == "--seed" && value(text) && parseSeed(text, seed)) {
		} else if (arg == "--encounters" && value(text)) {
			game::EncounterMode m;
			if (!game::parseEncounterMode(text, m)) {
				std::cerr << "--encounters takes visible or random\n";
				return 2;
			}
			mode = m;
		} else if (arg == "--auto-battle") {
			autoBattle = true;
		} else if (arg == "--bot") {
			bot = true;
		} else {
			std::cerr << "bad argument '" << arg << "' (try --help)\n";
			return 2;
		}
	}

	rules::Result<game::GameData> data = game::loadGameData(dataDir);
	if (!data.ok()) {
		std::cerr << data.error << "\n";
		return 1;
	}

	if (bot) {
		if (route.empty()) {
			route = dataDir + "/routes/main.route";
		}
		std::string text;
		if (!game::readFile(route, text)) {
			std::cerr << "cannot read " << route << "\n";
			return 1;
		}
		bool finished = true;
		std::vector<game::EncounterMode> modes;
		if (mode) {
			modes.push_back(*mode);
		} else {
			modes = {game::EncounterMode::Visible, game::EncounterMode::Random};
		}
		for (game::EncounterMode m : modes) {
			const game::BotResult result = game::runBot(*data.value, seed, m, text);
			std::cout << "== " << game::toString(m) << " encounters, seed " << seed << ": " << result.battles << " battles, "
					  << result.playTimeMs / 1000 << " s, " << (result.finished ? "finished" : "did not finish") << " ==\n"
					  << result.table;
			finished = finished && result.finished && result.errors == 0;
		}
		return finished ? 0 : 3;
	}

	game::SessionOptions options;
	options.seed = seed;
	options.saveDirectory = saves;
	options.encounterMode = mode;
	if (script.empty()) {
		options.clock = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
	}
	game::Session session(*data.value, options);
	game::RunnerOptions runnerOptions;
	runnerOptions.autoBattle = autoBattle;
	runnerOptions.echo = !script.empty();
	game::Runner runner(session, std::cout, runnerOptions);

	if (!script.empty()) {
		std::string text;
		if (!game::readFile(script, text)) {
			std::cerr << "cannot read " << script << "\n";
			return 1;
		}
		std::cout << data.value->config.title << " (seed " << seed << ")\n";
		runner.runScript(text);
		std::cout << "End of script.\n";
		return runner.errors() == 0 ? 0 : 3;
	}

	std::cout << data.value->config.title << "\n";
	commands();
	std::string line;
	while (true) {
		std::cout << runner.prompt() << std::flush;
		if (!std::getline(std::cin, line)) {
			break;
		}
		if (line == "help") {
			commands();
			continue;
		}
		if (!runner.execute(line)) {
			break;
		}
	}
	return 0;
}
