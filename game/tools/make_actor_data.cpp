// make_actor_data GAME_DATA CORE_DATA FIXTURES OUT [interact|background|scene|motion]: the placeholder data with Wren Hollow and
// Wren Loft added, for data/scripts/actors.txt and party.txt; with `interact`, also Wren Cellar and the interaction rules,
// for data/scripts/interact.txt; with `background`, also Wren Yard, for data/scripts/background.txt; with `scene`, also
// Wren Gallery, for data/scripts/scene.txt; with `motion`, also Wren Mill, for data/scripts/motion.txt (see tests/actor_data.h).
#include "../tests/actor_data.h"

#include <iostream>

int main(int argc, char** argv) {
	const std::string mode = argc == 6 ? argv[5] : "";
	if (argc != 5 && !(argc == 6 && (mode == "interact" || mode == "background" || mode == "scene" || mode == "motion"))) {
		std::cerr << "usage: make_actor_data GAME_DATA CORE_DATA FIXTURES OUT [interact|background|scene|motion]\n";
		return 2;
	}
	const std::string problem = mode == "interact" ? actor_data::makeInteract(argv[1], argv[2], argv[3], argv[4])
		: mode == "background"                     ? actor_data::makeBackground(argv[1], argv[2], argv[3], argv[4])
		: mode == "scene"                          ? actor_data::makeScene(argv[1], argv[2], argv[3], argv[4])
		: mode == "motion"                         ? actor_data::makeMotion(argv[1], argv[2], argv[3], argv[4])
												   : actor_data::make(argv[1], argv[2], argv[3], argv[4]);
	if (!problem.empty()) {
		std::cerr << problem << "\n";
		return 1;
	}
	return 0;
}
