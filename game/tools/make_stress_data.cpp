// make_stress_data GAME_DATA CORE_DATA OUT [KEY=VALUE ...]: the placeholder data with a large synthetic world and a script
// that plays it, for `make perf` (see tests/stress_data.h for the settings).
#include "../tests/stress_data.h"

#include <iostream>

int main(int argc, char** argv) {
	if (argc < 4) {
		std::cerr << "usage: make_stress_data GAME_DATA CORE_DATA OUT [KEY=VALUE ...]\n";
		return 2;
	}
	std::string summary;
	const std::string problem = stress_data::make(argv[1], argv[2], argv[3], std::vector<std::string>(argv + 4, argv + argc), summary);
	if (!problem.empty()) {
		std::cerr << problem << "\n";
		return problem.rfind("bad argument", 0) == 0 ? 2 : 1;
	}
	std::cout << summary << "\n";
	return 0;
}
